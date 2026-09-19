#include "core/QbtClient.hpp"

#include "core/Version.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <utility>

namespace omaseed::core {
namespace {

Q_LOGGING_CATEGORY(lcQbt, "omaseed.qbt")

constexpr auto kApiPrefix = "/api/v2/";

/// QUrlQuery stores items already percent-encoded, so every value goes through this.
void addItem(QUrlQuery &query, const QString &key, const QString &value)
{
    query.addQueryItem(QString::fromLatin1(QUrl::toPercentEncoding(key)),
                       QString::fromLatin1(QUrl::toPercentEncoding(value)));
}

QString firstLine(const QByteArray &body)
{
    QString text = QString::fromUtf8(body).trimmed();
    const qsizetype newline = text.indexOf(QLatin1Char('\n'));
    if (newline >= 0)
        text.truncate(newline);
    if (text.size() > 120)
        text = text.left(117) + QStringLiteral("...");
    return text;
}

QbtResult<QString> parseText(const QByteArray &body)
{
    return QString::fromUtf8(body).trimmed();
}

QJsonDocument parseJson(const QByteArray &body, const QString &path, ApiError *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = ApiError(ApiError::Kind::Parse,
                          QStringLiteral("%1: %2").arg(path, parseError.errorString()));
    }
    return document;
}

QbtResult<QJsonObject> parseObject(const QByteArray &body, const QString &path)
{
    ApiError error;
    const QJsonDocument document = parseJson(body, path, &error);
    if (error.isError())
        return error;
    if (!document.isObject())
        return ApiError(ApiError::Kind::Parse,
                        QStringLiteral("%1: expected a JSON object").arg(path));
    return document.object();
}

QbtResult<QJsonArray> parseArray(const QByteArray &body, const QString &path)
{
    ApiError error;
    const QJsonDocument document = parseJson(body, path, &error);
    if (error.isError())
        return error;
    if (!document.isArray())
        return ApiError(ApiError::Kind::Parse,
                        QStringLiteral("%1: expected a JSON array").arg(path));
    return document.array();
}

bool isNetworkFailure(QNetworkReply::NetworkError error)
{
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::SslHandshakeFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::UnknownProxyError:
        return true;
    default:
        return false;
    }
}

} // namespace

/// One request in the queue. It outlives its QNetworkReply: a retry sends it again.
struct QbtClient::Job
{
    RequestSpec spec;
    int attempt = 0;       ///< attempts already finished
    bool relogged = false; ///< an auth retry was already spent on this job
    bool cancelled = false;
    QPointer<QNetworkReply> networkReply;
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    std::function<void(const QByteArray &, const ApiError &)> deliver;
};

QbtClient::QbtClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_nam->setAutoDeleteReplies(false);
}

QbtClient::~QbtClient()
{
    cancelAll();
}

QUrl QbtClient::baseUrl() const
{
    return m_baseUrl;
}

void QbtClient::setBaseUrl(const QUrl &baseUrl)
{
    m_baseUrl = baseUrl;
}

void QbtClient::setApiKey(const QString &apiKey)
{
    m_apiKey = apiKey;
}

bool QbtClient::hasApiKey() const
{
    return !m_apiKey.isEmpty();
}

void QbtClient::setCredentials(const QString &username, const QString &password)
{
    if (m_username == username && m_password == password)
        return;
    m_username = username;
    m_password = password;
    // A different credential means any session cookie we hold is the wrong one.
    m_nam->setCookieJar(new QNetworkCookieJar());
}

bool QbtClient::hasCredentials() const
{
    return !m_username.isEmpty();
}

void QbtClient::clearAuth()
{
    m_apiKey.clear();
    m_username.clear();
    m_password.clear();
    m_nam->setCookieJar(new QNetworkCookieJar());
    setAuthState(AuthState::Unknown);
}

QbtClient::AuthState QbtClient::authState() const
{
    return m_authState;
}

std::chrono::milliseconds QbtClient::timeout() const
{
    return m_timeout;
}

void QbtClient::setTimeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
}

int QbtClient::maxRetries() const
{
    return m_maxRetries;
}

void QbtClient::setMaxRetries(int retries)
{
    m_maxRetries = qMax(0, retries);
}

std::chrono::milliseconds QbtClient::retryBackoff() const
{
    return m_retryBackoff;
}

void QbtClient::setRetryBackoff(std::chrono::milliseconds backoff)
{
    m_retryBackoff = backoff;
}

int QbtClient::maxConcurrentRequests() const
{
    return m_maxConcurrent;
}

void QbtClient::setMaxConcurrentRequests(int count)
{
    m_maxConcurrent = qMax(1, count);
    pump();
}

// --- plumbing ---------------------------------------------------------------------------

QUrl QbtClient::endpointUrl(const QString &path, const QUrlQuery &query) const
{
    QUrl url = m_baseUrl;
    QString base = url.path();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QString tail = path;
    while (tail.startsWith(QLatin1Char('/')))
        tail.remove(0, 1);
    url.setPath(base + QString::fromLatin1(kApiPrefix) + tail);
    if (!query.isEmpty())
        url.setQuery(query);
    return url;
}

template<class T, class Fn>
QbtReply<T> *QbtClient::submit(RequestSpec spec, Fn parse)
{
    auto *reply = new QbtReply<T>(this);
    auto job = std::make_shared<Job>();
    job->spec = std::move(spec);

    QPointer<QbtReply<T>> guard(reply);
    job->deliver = [guard, parse](const QByteArray &body, const ApiError &error) {
        if (guard.isNull())
            return;
        if (error.isError())
            guard->complete(QbtResult<T>(error));
        else
            guard->complete(parse(body));
    };

    connect(reply, &QbtReplyBase::abortRequested, this, [this, job]() { cancelJob(job); });
    enqueue(job);
    return reply;
}

void QbtClient::enqueue(const JobPtr &job)
{
    if (m_baseUrl.isEmpty() || !m_baseUrl.isValid()) {
        const ApiError error(ApiError::Kind::Network,
                             QStringLiteral("%1: no server URL is configured").arg(job->spec.path));
        QTimer::singleShot(0, this, [this, job, error]() { finishJob(job, QByteArray(), error); });
        return;
    }
    m_queued.append(job);
    pump();
}

void QbtClient::pump()
{
    while (!m_queued.isEmpty() && m_active.size() < m_maxConcurrent) {
        const JobPtr job = m_queued.takeFirst();
        send(job);
    }
}

void QbtClient::send(const JobPtr &job)
{
    QNetworkRequest request(endpointUrl(job->spec.path, job->spec.query));
    const QByteArray origin =
        m_baseUrl.toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toUtf8();
    request.setRawHeader("Referer", origin);
    request.setRawHeader("Origin", origin);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("omaseed/%1").arg(version()));
    if (!m_apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());
    request.setTransferTimeout(m_timeout);

    QNetworkReply *networkReply = nullptr;
    if (job->spec.verb == QLatin1String("POST")) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded"));
        networkReply = m_nam->post(request, job->spec.form.query(QUrl::FullyEncoded).toUtf8());
    } else {
        networkReply = m_nam->get(request);
    }

    job->networkReply = networkReply;
    m_active.append(job);
    connect(
        networkReply, &QNetworkReply::finished, this, [this, job]() { onJobReplyFinished(job); });
}

void QbtClient::onJobReplyFinished(const JobPtr &job)
{
    QNetworkReply *networkReply = job->networkReply;
    if (networkReply == nullptr)
        return;
    job->networkReply = nullptr;
    m_active.removeOne(job);

    const int status = networkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // An aborted reply is closed already; reading it would only warn.
    const QByteArray body = networkReply->isReadable() ? networkReply->readAll() : QByteArray();
    job->networkError = networkReply->error();
    const QString reason =
        job->networkError == QNetworkReply::NoError ? firstLine(body) : networkReply->errorString();
    networkReply->deleteLater();
    ++job->attempt;

    ApiError error = mapError(job, status, reason);
    if (!error.isError() && job->spec.isLogin) {
        // 5.x answers 204; older builds answer 200 with "Ok." or "Fails.".
        const QByteArray trimmed = body.trimmed();
        if (!trimmed.isEmpty() && trimmed != QByteArrayLiteral("Ok.")) {
            error = ApiError(ApiError::Kind::Unauthorized,
                             QStringLiteral("auth/login: %1").arg(firstLine(body)),
                             status);
        }
    }

    if (!error.isError()) {
        setAuthState(AuthState::Ok);
        finishJob(job, body, ApiError());
        pump();
        return;
    }

    const bool authFailure =
        error.kind() == ApiError::Kind::Unauthorized || error.kind() == ApiError::Kind::Forbidden;
    if (authFailure && !job->spec.isLogin && !job->relogged && hasCredentials()) {
        job->relogged = true;
        m_waitingForLogin.append(job);
        qCDebug(lcQbt) << "auth failure on" << job->spec.path << "- logging in and retrying";
        startLogin();
        pump();
        return;
    }

    if (error.isTransient() && job->spec.retryOnTransient && job->attempt <= m_maxRetries) {
        const auto delay = m_retryBackoff * (1 << (job->attempt - 1));
        qCDebug(lcQbt) << "retrying" << job->spec.path << "attempt" << job->attempt + 1 << "in"
                       << delay.count() << "ms";
        QTimer::singleShot(delay, this, [this, job]() {
            m_queued.prepend(job);
            pump();
        });
        pump();
        return;
    }

    if (error.kind() == ApiError::Kind::Unauthorized)
        setAuthState(AuthState::NeedsCredential);
    else if (error.kind() == ApiError::Kind::Forbidden)
        setAuthState(AuthState::Forbidden);

    finishJob(job, body, error);
    pump();
}

ApiError QbtClient::mapError(const JobPtr &job, int status, const QString &reason) const
{
    const QString where = QStringLiteral("%1 %2").arg(job->spec.verb, job->spec.path);
    const auto describe = [&where, &reason]() {
        return reason.isEmpty() ? where : QStringLiteral("%1: %2").arg(where, reason);
    };

    if (job->cancelled)
        return ApiError(ApiError::Kind::Cancelled, QStringLiteral("%1: cancelled").arg(where));

    if (job->networkError == QNetworkReply::NoError)
        return ApiError::fromHttpStatus(status, describe());

    if (job->networkError == QNetworkReply::TimeoutError ||
        job->networkError == QNetworkReply::OperationCanceledError) {
        return ApiError(
            ApiError::Kind::Timeout,
            QStringLiteral("%1: no reply within %2 ms").arg(where).arg(m_timeout.count()));
    }

    if (status != 0)
        return ApiError::fromHttpStatus(status, describe());

    if (isNetworkFailure(job->networkError))
        return ApiError(ApiError::Kind::Network, describe());

    return ApiError(ApiError::Kind::Unknown, describe());
}

void QbtClient::finishJob(const JobPtr &job, const QByteArray &body, const ApiError &error)
{
    if (error.isError())
        qCDebug(lcQbt) << "failed:" << error.toString();
    if (job->deliver)
        job->deliver(body, error);
}

void QbtClient::cancelJob(const JobPtr &job)
{
    const ApiError cancelled(
        ApiError::Kind::Cancelled,
        QStringLiteral("%1 %2: cancelled").arg(job->spec.verb, job->spec.path));
    if (m_queued.removeOne(job) || m_waitingForLogin.removeOne(job)) {
        finishJob(job, QByteArray(), cancelled);
        pump();
        return;
    }
    if (m_active.contains(job)) {
        job->cancelled = true;
        if (job->networkReply)
            job->networkReply->abort(); // finishes through onJobReplyFinished
        return;
    }
}

void QbtClient::cancelAll()
{
    const QList<JobPtr> jobs = m_queued + m_waitingForLogin + m_active;
    for (const JobPtr &job : jobs)
        cancelJob(job);
}

void QbtClient::startLogin()
{
    if (m_loginInFlight)
        return;
    if (!hasCredentials()) {
        onLoginFinished(
            ApiError(ApiError::Kind::Unauthorized, QStringLiteral("auth/login: no credentials")));
        return;
    }
    m_loginInFlight = true;

    RequestSpec spec;
    spec.verb = QStringLiteral("POST");
    spec.path = QStringLiteral("auth/login");
    addItem(spec.form, QStringLiteral("username"), m_username);
    addItem(spec.form, QStringLiteral("password"), m_password);
    spec.retryOnTransient = false;
    spec.isLogin = true;

    auto job = std::make_shared<Job>();
    job->spec = std::move(spec);
    job->deliver = [this](const QByteArray &, const ApiError &error) { onLoginFinished(error); };
    m_queued.prepend(job);
    pump();
}

void QbtClient::onLoginFinished(const ApiError &error)
{
    m_loginInFlight = false;
    const QList<JobPtr> waiting = m_waitingForLogin;
    m_waitingForLogin.clear();

    if (error.isError()) {
        setAuthState(error.kind() == ApiError::Kind::Forbidden ? AuthState::Forbidden
                                                               : AuthState::NeedsCredential);
        for (const JobPtr &job : waiting)
            finishJob(job, QByteArray(), error);
        pump();
        return;
    }

    setAuthState(AuthState::Ok);
    for (const JobPtr &job : waiting)
        m_queued.append(job);
    pump();
}

void QbtClient::setAuthState(AuthState state)
{
    if (m_authState == state)
        return;
    m_authState = state;
    Q_EMIT authStateChanged(state);
}

// --- generic calls ------------------------------------------------------------------------

QbtReply<QByteArray> *QbtClient::get(const QString &path, const QUrlQuery &query)
{
    RequestSpec spec;
    spec.path = path;
    spec.query = query;
    return submit<QByteArray>(std::move(spec),
                              [](const QByteArray &body) { return QbtResult<QByteArray>(body); });
}

QbtReply<QByteArray> *QbtClient::post(const QString &path, const QUrlQuery &form)
{
    RequestSpec spec;
    spec.verb = QStringLiteral("POST");
    spec.path = path;
    spec.form = form;
    spec.retryOnTransient = false;
    return submit<QByteArray>(std::move(spec),
                              [](const QByteArray &body) { return QbtResult<QByteArray>(body); });
}

// --- typed calls --------------------------------------------------------------------------

QbtReply<QString> *QbtClient::appVersion()
{
    RequestSpec spec;
    spec.path = QStringLiteral("app/version");
    return submit<QString>(std::move(spec), parseText);
}

QbtReply<QString> *QbtClient::webApiVersion()
{
    RequestSpec spec;
    spec.path = QStringLiteral("app/webapiVersion");
    return submit<QString>(std::move(spec), parseText);
}

QbtReply<QJsonObject> *QbtClient::appPreferences()
{
    RequestSpec spec;
    spec.path = QStringLiteral("app/preferences");
    const QString path = spec.path;
    return submit<QJsonObject>(std::move(spec),
                               [path](const QByteArray &body) { return parseObject(body, path); });
}

QbtReply<QJsonObject> *QbtClient::appBuildInfo()
{
    RequestSpec spec;
    spec.path = QStringLiteral("app/buildInfo");
    const QString path = spec.path;
    return submit<QJsonObject>(std::move(spec),
                               [path](const QByteArray &body) { return parseObject(body, path); });
}

QbtReply<QJsonObject> *QbtClient::syncMainData(int rid)
{
    RequestSpec spec;
    spec.path = QStringLiteral("sync/maindata");
    addItem(spec.query, QStringLiteral("rid"), QString::number(rid));
    const QString path = spec.path;
    return submit<QJsonObject>(std::move(spec),
                               [path](const QByteArray &body) { return parseObject(body, path); });
}

QbtReply<QJsonArray> *QbtClient::torrentsInfo(const TorrentsInfoQuery &query)
{
    RequestSpec spec;
    spec.path = QStringLiteral("torrents/info");
    if (query.filter)
        addItem(spec.query, QStringLiteral("filter"), *query.filter);
    if (query.category)
        addItem(spec.query, QStringLiteral("category"), *query.category);
    if (query.tag)
        addItem(spec.query, QStringLiteral("tag"), *query.tag);
    if (!query.hashes.isEmpty())
        addItem(spec.query, QStringLiteral("hashes"), query.hashes.join(QLatin1Char('|')));
    if (query.sort) {
        addItem(spec.query, QStringLiteral("sort"), *query.sort);
        addItem(spec.query,
                QStringLiteral("reverse"),
                query.reverse ? QStringLiteral("true") : QStringLiteral("false"));
    }
    if (query.limit > 0)
        addItem(spec.query, QStringLiteral("limit"), QString::number(query.limit));
    if (query.offset != 0)
        addItem(spec.query, QStringLiteral("offset"), QString::number(query.offset));
    const QString path = spec.path;
    return submit<QJsonArray>(std::move(spec),
                              [path](const QByteArray &body) { return parseArray(body, path); });
}

QbtReply<QJsonObject> *QbtClient::torrentsProperties(const QString &hash)
{
    RequestSpec spec;
    spec.path = QStringLiteral("torrents/properties");
    addItem(spec.query, QStringLiteral("hash"), hash);
    const QString path = spec.path;
    return submit<QJsonObject>(std::move(spec),
                               [path](const QByteArray &body) { return parseObject(body, path); });
}

QbtReply<QJsonArray> *QbtClient::torrentsFiles(const QString &hash)
{
    RequestSpec spec;
    spec.path = QStringLiteral("torrents/files");
    addItem(spec.query, QStringLiteral("hash"), hash);
    const QString path = spec.path;
    return submit<QJsonArray>(std::move(spec),
                              [path](const QByteArray &body) { return parseArray(body, path); });
}

QbtReply<QJsonArray> *QbtClient::torrentsTrackers(const QString &hash)
{
    RequestSpec spec;
    spec.path = QStringLiteral("torrents/trackers");
    addItem(spec.query, QStringLiteral("hash"), hash);
    const QString path = spec.path;
    return submit<QJsonArray>(std::move(spec),
                              [path](const QByteArray &body) { return parseArray(body, path); });
}

QbtReply<QJsonObject> *QbtClient::torrentsPeers(const QString &hash, int rid)
{
    RequestSpec spec;
    spec.path = QStringLiteral("sync/torrentPeers");
    addItem(spec.query, QStringLiteral("hash"), hash);
    addItem(spec.query, QStringLiteral("rid"), QString::number(rid));
    const QString path = spec.path;
    return submit<QJsonObject>(std::move(spec),
                               [path](const QByteArray &body) { return parseObject(body, path); });
}

QbtReply<QJsonObject> *QbtClient::torrentsCategories()
{
    RequestSpec spec;
    spec.path = QStringLiteral("torrents/categories");
    const QString path = spec.path;
    return submit<QJsonObject>(std::move(spec),
                               [path](const QByteArray &body) { return parseObject(body, path); });
}

QbtReply<QJsonArray> *QbtClient::torrentsTags()
{
    RequestSpec spec;
    spec.path = QStringLiteral("torrents/tags");
    const QString path = spec.path;
    return submit<QJsonArray>(std::move(spec),
                              [path](const QByteArray &body) { return parseArray(body, path); });
}

QbtReply<QJsonObject> *QbtClient::transferInfo()
{
    RequestSpec spec;
    spec.path = QStringLiteral("transfer/info");
    const QString path = spec.path;
    return submit<QJsonObject>(std::move(spec),
                               [path](const QByteArray &body) { return parseObject(body, path); });
}

QbtReply<bool> *QbtClient::transferSpeedLimitsMode()
{
    RequestSpec spec;
    spec.path = QStringLiteral("transfer/speedLimitsMode");
    const QString path = spec.path;
    return submit<bool>(std::move(spec), [path](const QByteArray &body) -> QbtResult<bool> {
        const QByteArray trimmed = body.trimmed();
        if (trimmed == QByteArrayLiteral("0"))
            return false;
        if (trimmed == QByteArrayLiteral("1"))
            return true;
        return ApiError(
            ApiError::Kind::Parse,
            QStringLiteral("%1: expected 0 or 1, got \"%2\"").arg(path, firstLine(body)));
    });
}

QbtReply<QByteArray> *QbtClient::transferToggleSpeedLimitsMode()
{
    return post(QStringLiteral("transfer/toggleSpeedLimitsMode"));
}

QbtReply<bool> *QbtClient::login()
{
    RequestSpec spec;
    spec.verb = QStringLiteral("POST");
    spec.path = QStringLiteral("auth/login");
    addItem(spec.form, QStringLiteral("username"), m_username);
    addItem(spec.form, QStringLiteral("password"), m_password);
    spec.retryOnTransient = false;
    spec.isLogin = true;
    return submit<bool>(std::move(spec), [](const QByteArray &) { return QbtResult<bool>(true); });
}

QbtReply<ServerInfo> *QbtClient::probe()
{
    auto *reply = new QbtReply<ServerInfo>(this);
    QPointer<QbtReply<ServerInfo>> guard(reply);
    auto info = std::make_shared<ServerInfo>();
    auto pending = std::make_shared<int>(2);
    auto failed = std::make_shared<bool>(false);

    auto step = [this, info, pending, failed, guard](const ApiError &error) {
        if (*failed)
            return;
        if (error.isError()) {
            *failed = true;
            if (!guard.isNull())
                guard->complete(QbtResult<ServerInfo>(error));
            return;
        }
        if (--(*pending) > 0)
            return;
        Q_EMIT serverInfoChanged(info->version, info->apiVersion);
        if (!guard.isNull())
            guard->complete(QbtResult<ServerInfo>(*info));
    };

    appVersion()->then(this, [info, step](const QbtResult<QString> &result) {
        if (result.isValid())
            info->version = result.value();
        step(result.error());
    });
    webApiVersion()->then(this, [info, step](const QbtResult<QString> &result) {
        if (result.isValid())
            info->apiVersion = result.value();
        step(result.error());
    });
    return reply;
}

} // namespace omaseed::core
