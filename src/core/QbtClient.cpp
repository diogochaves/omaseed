#include "core/QbtClient.hpp"

#include "core/CertificatePin.hpp"
#include "core/Version.hpp"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QTimer>
#include <QVersionNumber>

#include <functional>
#include <type_traits>
#include <utility>

namespace omaseed::core {
namespace {

Q_LOGGING_CATEGORY(lcQbt, "omaseed.qbt")

constexpr auto kApiPrefix = "/api/v2/";
constexpr int kMaxRetriesCap = 10;

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

QString contentLayoutName(QbtClient::ContentLayout layout)
{
    switch (layout) {
    case QbtClient::ContentLayout::Original:
        return QStringLiteral("Original");
    case QbtClient::ContentLayout::Subfolder:
        return QStringLiteral("Subfolder");
    case QbtClient::ContentLayout::NoSubfolder:
        return QStringLiteral("NoSubfolder");
    }
    return QStringLiteral("Original");
}

/// A ratio limit as plain decimal text. QString::number's default ('g', 6 significant
/// digits) turns 1.2345678 into 1.23457 and 1234567.5 into 1.23457e+06.
QString ratioText(double ratio)
{
    QString text = QString::number(ratio, 'f', 4);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }
    return text;
}

/// A multipart filename qBittorrent can read: it ignores the name anyway, and quotes, CR/LF
/// or non-Latin-1 characters would break the header (Qt sends it as Latin-1).
QString safeUploadName(const QString &fileName)
{
    QString out;
    out.reserve(fileName.size());
    for (const QChar c : fileName) {
        const char16_t u = c.unicode();
        if (u < 0x20 || u >= 0x7f || u == u'"' || u == u'\\')
            out += QLatin1Char('_');
        else
            out += c;
    }
    return out.isEmpty() ? QStringLiteral("upload.torrent") : out;
}

void addBoolItem(QUrlQuery &query, const QString &key, bool value)
{
    addItem(query, key, value ? QStringLiteral("true") : QStringLiteral("false"));
}

/// Shared between torrentsAddUrls and torrentsAddFile.
void applyAddOptions(QUrlQuery &form, const QbtClient::AddOptions &options)
{
    if (options.savepath)
        addItem(form, QStringLiteral("savepath"), *options.savepath);
    if (options.downloadPath)
        addItem(form, QStringLiteral("downloadPath"), *options.downloadPath);
    if (options.useDownloadPath)
        addBoolItem(form, QStringLiteral("useDownloadPath"), *options.useDownloadPath);
    if (options.category)
        addItem(form, QStringLiteral("category"), *options.category);
    if (!options.tags.isEmpty())
        addItem(form, QStringLiteral("tags"), options.tags.join(QLatin1Char(',')));
    if (options.stopped) {
        // 5.x renamed `paused` to `stopped`; sending both keeps this working on either.
        addBoolItem(form, QStringLiteral("stopped"), *options.stopped);
        addBoolItem(form, QStringLiteral("paused"), *options.stopped);
    }
    if (options.skipChecking)
        addBoolItem(form, QStringLiteral("skip_checking"), *options.skipChecking);
    if (options.contentLayout)
        addItem(form, QStringLiteral("contentLayout"), contentLayoutName(*options.contentLayout));
    if (options.rename)
        addItem(form, QStringLiteral("rename"), *options.rename);
    if (options.upLimit)
        addItem(form, QStringLiteral("upLimit"), QString::number(*options.upLimit));
    if (options.dlLimit)
        addItem(form, QStringLiteral("dlLimit"), QString::number(*options.dlLimit));
    if (options.ratioLimit)
        addItem(form, QStringLiteral("ratioLimit"), ratioText(*options.ratioLimit));
    if (options.seedingTimeLimit)
        addItem(
            form, QStringLiteral("seedingTimeLimit"), QString::number(*options.seedingTimeLimit));
    if (options.autoTMM)
        addBoolItem(form, QStringLiteral("autoTMM"), *options.autoTMM);
    if (options.sequentialDownload)
        addBoolItem(form, QStringLiteral("sequentialDownload"), *options.sequentialDownload);
    if (options.firstLastPiecePrio)
        addBoolItem(form, QStringLiteral("firstLastPiecePrio"), *options.firstLastPiecePrio);
    if (options.cookie)
        addItem(form, QStringLiteral("cookie"), *options.cookie);
    if (!options.filePriorities.isEmpty()) {
        QStringList priorities;
        priorities.reserve(options.filePriorities.size());
        for (const int priority : options.filePriorities)
            priorities.append(QString::number(priority));
        addItem(form, QStringLiteral("filePriorities"), priorities.join(QLatin1Char(',')));
    }
    if (options.stopCondition)
        addItem(form, QStringLiteral("stopCondition"), *options.stopCondition);
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
    /// The certificate TLS refused on the last attempt (CertificatePin form), or empty.
    QString refusedCertificate;
    QString refusedCertificateReason;
    /// The HTTP status of the last attempt (0 without one), for the calls whose 2xx statuses
    /// mean different things (torrentsFetchMetadata: 202 still looking, 200 done).
    int httpStatus = 0;
    std::function<void(const QByteArray &, const ApiError &, int)> deliver;
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
    if (m_baseUrl == baseUrl)
        return;
    m_baseUrl = baseUrl;
    // Every job builds its URL when it is sent, so anything still queued, waiting for a
    // login or backing off would otherwise go to the new server with the old server's
    // intent (a delete, a setLocation, a torrent id). Drop it all instead.
    cancelAll();
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
    // Whoever sets credentials (again) wants them tried: lift the "server said no" latch.
    m_loginRejected = false;
    if (m_username == username && m_password == password)
        return;
    m_username = username;
    m_password = password;
    // A different credential means any session cookie we hold is the wrong one.
    m_nam->setCookieJar(new QNetworkCookieJar());
}

void QbtClient::allowLoginAgain()
{
    m_loginRejected = false;
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
    m_loginRejected = false;
    m_nam->setCookieJar(new QNetworkCookieJar());
    // Queued work was built for the identity just forgotten; see setBaseUrl().
    cancelAll();
    setAuthState(AuthState::Unknown);
}

QbtClient::AuthState QbtClient::authState() const
{
    return m_authState;
}

void QbtClient::setPinnedCertificate(const QString &sha256)
{
    const QString pin = CertificatePin::normalized(sha256);
    if (pin == m_pinnedCertificate)
        return;
    m_pinnedCertificate = pin;
    // A pooled connection keeps the TLS session it was allowed under: one accepted with the
    // old pin must not carry on under a new one (or none).
    m_nam->clearConnectionCache();
}

QString QbtClient::pinnedCertificate() const
{
    return m_pinnedCertificate;
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
    // Capped so the doubling back-off (1 << attempt) can never overflow.
    m_maxRetries = qBound(0, retries, kMaxRetriesCap);
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
    job->deliver = [guard, parse](const QByteArray &body, const ApiError &error, int status) {
        if (guard.isNull())
            return;
        if (error.isError())
            guard->complete(QbtResult<T>(error));
        else if constexpr (std::is_invocable_v<Fn, const QByteArray &, int>)
            guard->complete(parse(body, status));
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
    // RemoveUserInfo: a URL typed as http://user:pass@host must not leak the password
    // into every Origin/Referer header.
    const QByteArray origin = m_baseUrl
                                  .toString(QUrl::RemoveUserInfo | QUrl::RemovePath |
                                            QUrl::RemoveQuery | QUrl::RemoveFragment)
                                  .toUtf8();
    request.setRawHeader("Referer", origin);
    request.setRawHeader("Origin", origin);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("omaseed/%1").arg(version()));
    if (!m_apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());
    request.setTransferTimeout(m_timeout);

    QNetworkReply *networkReply = nullptr;
    if (job->spec.verb == QLatin1String("POST")) {
        if (job->spec.buildMultiPart) {
            // A fresh body per attempt, owned by the reply that sends it, so the one-shot
            // re-login retry can resend it. QNetworkAccessManager sets its own Content-Type
            // (with the boundary) for us.
            QHttpMultiPart *multiPart = job->spec.buildMultiPart();
            networkReply = m_nam->post(request, multiPart);
            multiPart->setParent(networkReply);
        } else {
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/x-www-form-urlencoded"));
            networkReply = m_nam->post(request, job->spec.form.query(QUrl::FullyEncoded).toUtf8());
        }
    } else {
        networkReply = m_nam->get(request);
    }

    job->networkReply = networkReply;
    job->refusedCertificate.clear();
    job->refusedCertificateReason.clear();
    m_active.append(job);
    connect(networkReply,
            &QNetworkReply::sslErrors,
            this,
            [this, job, networkReply](const QList<QSslError> &errors) {
                onSslErrors(job, networkReply, errors);
            });
    connect(
        networkReply, &QNetworkReply::finished, this, [this, job]() { onJobReplyFinished(job); });
}

void QbtClient::onSslErrors(const JobPtr &job,
                            QNetworkReply *networkReply,
                            const QList<QSslError> &errors)
{
    QSslCertificate certificate = networkReply->sslConfiguration().peerCertificate();
    for (const QSslError &error : errors) {
        if (!certificate.isNull())
            break;
        certificate = error.certificate();
    }
    const QString seen = CertificatePin::fingerprint(certificate);
    if (!seen.isEmpty() && seen == m_pinnedCertificate) {
        // Exactly the certificate the person chose to trust for this server: whatever the
        // system holds against it (self-signed, a name that does not match) is accepted.
        networkReply->ignoreSslErrors(errors);
        return;
    }
    job->refusedCertificate = seen;
    job->refusedCertificateReason =
        errors.isEmpty() ? QString() : errors.constFirst().errorString();
}

void QbtClient::onJobReplyFinished(const JobPtr &job)
{
    QNetworkReply *networkReply = job->networkReply;
    if (networkReply == nullptr)
        return;
    job->networkReply = nullptr;
    m_active.removeOne(job);

    const int status = networkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    job->httpStatus = status;
    // An aborted reply is closed already; reading it would only warn.
    const QByteArray body = networkReply->isReadable() ? networkReply->readAll() : QByteArray();
    job->networkError = networkReply->error();
    const QString reason =
        job->networkError == QNetworkReply::NoError ? firstLine(body) : networkReply->errorString();
    networkReply->deleteLater();
    ++job->attempt;

    ApiError error = mapError(job, status, reason);
    if (job->spec.isLogin) {
        if (!error.isError()) {
            // 5.x answers 204; older builds answer 200 with "Ok." or "Fails.".
            const QByteArray trimmed = body.trimmed();
            if (!trimmed.isEmpty() && trimmed != QByteArrayLiteral("Ok.")) {
                error = ApiError(ApiError::Kind::Unauthorized,
                                 QStringLiteral("auth/login: %1").arg(firstLine(body)),
                                 status);
            }
        }
        // qBittorrent bans the client IP after a handful of failed logins (5 by default,
        // for an hour), so once the server has said no we stop logging in by ourselves
        // until someone hands us credentials again.
        if (!error.isError())
            m_loginRejected = false;
        else if (error.kind() == ApiError::Kind::Unauthorized ||
                 error.kind() == ApiError::Kind::Forbidden) {
            m_loginRejected = true;
            m_loginRejectedKind = error.kind();
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
    if (authFailure && !job->spec.isLogin && !job->relogged && hasCredentials() &&
        !m_loginRejected && job->spec.allowReloginRetry) {
        job->relogged = true;
        m_waitingForLogin.append(job);
        qCDebug(lcQbt) << "auth failure on" << job->spec.path << "- logging in and retrying";
        startLogin();
        pump();
        return;
    }
    if (authFailure && !job->spec.isLogin && !job->relogged && hasCredentials() &&
        m_loginRejected && job->spec.allowReloginRetry) {
        // The re-login this call would have made is held back because the server already
        // refused the credential. Answer as that login did (a wrong password stays
        // Unauthorized / NeedsCredential), not with the bare 403 an unauthenticated call gets.
        error = ApiError(m_loginRejectedKind,
                         QStringLiteral("%1 %2: sign-in was refused, not retried")
                             .arg(job->spec.verb, job->spec.path),
                         error.httpStatus());
    }

    if (error.isTransient() && job->spec.retryOnTransient && job->attempt <= m_maxRetries) {
        const auto delay = m_retryBackoff * (1 << (job->attempt - 1));
        qCDebug(lcQbt) << "retrying" << job->spec.path << "attempt" << job->attempt + 1 << "in"
                       << delay.count() << "ms";
        // Parked in m_backingOff so abort()/cancelAll() can still find it; a job cancelled
        // meanwhile is gone from the list and the timer does nothing.
        m_backingOff.append(job);
        QTimer::singleShot(delay, this, [this, job]() {
            if (!m_backingOff.removeOne(job))
                return;
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

    if (!job->refusedCertificate.isEmpty()) {
        const QString why =
            job->refusedCertificateReason.isEmpty() ? reason : job->refusedCertificateReason;
        return ApiError::certificate(why.isEmpty() ? where
                                                   : QStringLiteral("%1: %2").arg(where, why),
                                     job->refusedCertificate);
    }

    if (isNetworkFailure(job->networkError))
        return ApiError(ApiError::Kind::Network, describe());

    return ApiError(ApiError::Kind::Unknown, describe());
}

void QbtClient::finishJob(const JobPtr &job, const QByteArray &body, const ApiError &error)
{
    if (error.isError())
        qCDebug(lcQbt) << "failed:" << error.toString();
    if (job->deliver)
        job->deliver(body, error, job->httpStatus);
}

void QbtClient::cancelJob(const JobPtr &job)
{
    const ApiError cancelled(
        ApiError::Kind::Cancelled,
        QStringLiteral("%1 %2: cancelled").arg(job->spec.verb, job->spec.path));
    if (m_queued.removeOne(job) || m_waitingForLogin.removeOne(job) ||
        m_backingOff.removeOne(job)) {
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
    const QList<JobPtr> jobs = m_queued + m_waitingForLogin + m_backingOff + m_active;
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
    job->deliver = [this](const QByteArray &, const ApiError &error, int) {
        onLoginFinished(error);
    };
    m_queued.prepend(job);
    pump();
}

void QbtClient::onLoginFinished(const ApiError &error)
{
    m_loginInFlight = false;
    const QList<JobPtr> waiting = m_waitingForLogin;
    m_waitingForLogin.clear();

    if (error.kind() == ApiError::Kind::Cancelled) {
        // cancelAll() or the destructor dropped the login: that says nothing about the
        // credentials, so the auth state stays as it was.
        for (const JobPtr &job : waiting)
            finishJob(job, QByteArray(), error);
        pump();
        return;
    }

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

QbtReply<QByteArray> *QbtClient::appSetPreferences(const QJsonObject &changes)
{
    QUrlQuery form;
    addItem(form,
            QStringLiteral("json"),
            QString::fromUtf8(QJsonDocument(changes).toJson(QJsonDocument::Compact)));
    return post(QStringLiteral("app/setPreferences"), form);
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

QbtReply<QJsonArray> *QbtClient::torrentsPieceStates(const QString &hash)
{
    RequestSpec spec;
    spec.path = QStringLiteral("torrents/pieceStates");
    addItem(spec.query, QStringLiteral("hash"), hash);
    const QString path = spec.path;
    return submit<QJsonArray>(std::move(spec),
                              [path](const QByteArray &body) { return parseArray(body, path); });
}

QbtReply<QJsonArray> *QbtClient::torrentsWebSeeds(const QString &hash)
{
    RequestSpec spec;
    spec.path = QStringLiteral("torrents/webseeds");
    addItem(spec.query, QStringLiteral("hash"), hash);
    const QString path = spec.path;
    return submit<QJsonArray>(std::move(spec),
                              [path](const QByteArray &body) { return parseArray(body, path); });
}

QbtReply<QJsonArray> *QbtClient::logMainProblems(int lastKnownId)
{
    RequestSpec spec;
    spec.path = QStringLiteral("log/main");
    addItem(spec.query, QStringLiteral("normal"), QStringLiteral("false"));
    addItem(spec.query, QStringLiteral("info"), QStringLiteral("false"));
    addItem(spec.query, QStringLiteral("warning"), QStringLiteral("true"));
    addItem(spec.query, QStringLiteral("critical"), QStringLiteral("true"));
    addItem(spec.query, QStringLiteral("last_known_id"), QString::number(lastKnownId));
    const QString path = spec.path;
    return submit<QJsonArray>(std::move(spec),
                              [path](const QByteArray &body) { return parseArray(body, path); });
}

QbtReply<QJsonArray> *QbtClient::logMain(int lastKnownId)
{
    RequestSpec spec;
    spec.path = QStringLiteral("log/main");
    for (const char *level : {"normal", "info", "warning", "critical"})
        addItem(spec.query, QString::fromLatin1(level), QStringLiteral("true"));
    addItem(spec.query, QStringLiteral("last_known_id"), QString::number(lastKnownId));
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

QbtReply<QByteArray> *QbtClient::transferSetSpeedLimitsMode(bool on)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("mode"), on ? QStringLiteral("1") : QStringLiteral("0"));
    return post(QStringLiteral("transfer/setSpeedLimitsMode"), form);
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

    QPointer<QbtReplyBase> versionReply =
        appVersion()->then(this, [info, step](const QbtResult<QString> &result) {
            if (result.isValid())
                info->version = result.value();
            step(result.error());
        });
    QPointer<QbtReplyBase> apiReply =
        webApiVersion()->then(this, [info, step](const QbtResult<QString> &result) {
            if (result.isValid())
                info->apiVersion = result.value();
            step(result.error());
        });
    // abort() on the probe drops both halves; the first Cancelled finishes the probe.
    connect(reply, &QbtReplyBase::abortRequested, this, [versionReply, apiReply]() {
        if (!versionReply.isNull())
            versionReply->abort();
        if (!apiReply.isNull())
            apiReply->abort();
    });
    return reply;
}

// --- torrent actions (T2.5) ---------------------------------------------------------------

namespace {

QUrlQuery hashesForm(const QStringList &hashes)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("hashes"), hashes.join(QLatin1Char('|')));
    return form;
}

} // namespace

QbtReply<QByteArray> *QbtClient::torrentsStart(const QStringList &hashes)
{
    return post(QStringLiteral("torrents/start"), hashesForm(hashes));
}

QbtReply<QByteArray> *QbtClient::torrentsStop(const QStringList &hashes)
{
    return post(QStringLiteral("torrents/stop"), hashesForm(hashes));
}

QbtReply<QByteArray> *QbtClient::torrentsSetForceStart(const QStringList &hashes, bool value)
{
    QUrlQuery form = hashesForm(hashes);
    addBoolItem(form, QStringLiteral("value"), value);
    return post(QStringLiteral("torrents/setForceStart"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsRecheck(const QStringList &hashes)
{
    return post(QStringLiteral("torrents/recheck"), hashesForm(hashes));
}

QbtReply<QByteArray> *QbtClient::torrentsReannounce(const QStringList &hashes)
{
    return post(QStringLiteral("torrents/reannounce"), hashesForm(hashes));
}

QbtReply<QByteArray> *QbtClient::torrentsRemoveTrackers(const QString &hash,
                                                        const QStringList &urls)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("hash"), hash);
    addItem(form, QStringLiteral("urls"), urls.join(QLatin1Char('|')));
    return post(QStringLiteral("torrents/removeTrackers"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsAddTrackers(const QString &hash, const QStringList &urls)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("hash"), hash);
    addItem(form, QStringLiteral("urls"), urls.join(QLatin1Char('\n')));
    return post(QStringLiteral("torrents/addTrackers"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsAddWebSeeds(const QString &hash, const QStringList &urls)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("hash"), hash);
    addItem(form, QStringLiteral("urls"), urls.join(QLatin1Char('|')));
    return post(QStringLiteral("torrents/addWebSeeds"), form);
}

QbtReply<QByteArray> *
QbtClient::torrentsEditWebSeed(const QString &hash, const QString &origUrl, const QString &newUrl)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("hash"), hash);
    addItem(form, QStringLiteral("origUrl"), origUrl);
    addItem(form, QStringLiteral("newUrl"), newUrl);
    return post(QStringLiteral("torrents/editWebSeed"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsRemoveWebSeeds(const QString &hash,
                                                        const QStringList &urls)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("hash"), hash);
    addItem(form, QStringLiteral("urls"), urls.join(QLatin1Char('|')));
    return post(QStringLiteral("torrents/removeWebSeeds"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsAddPeers(const QStringList &hashes,
                                                  const QStringList &peers)
{
    QUrlQuery form = hashesForm(hashes);
    addItem(form, QStringLiteral("peers"), peers.join(QLatin1Char('|')));
    return post(QStringLiteral("torrents/addPeers"), form);
}

QbtReply<QByteArray> *QbtClient::transferBanPeers(const QStringList &peers)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("peers"), peers.join(QLatin1Char('|')));
    return post(QStringLiteral("transfer/banPeers"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsSetCategory(const QStringList &hashes,
                                                     const QString &category)
{
    QUrlQuery form = hashesForm(hashes);
    addItem(form, QStringLiteral("category"), category);
    return post(QStringLiteral("torrents/setCategory"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsCreateCategory(const QString &name,
                                                        const QString &savePath)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("category"), name);
    addItem(form, QStringLiteral("savePath"), savePath);
    return post(QStringLiteral("torrents/createCategory"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsEditCategory(const QString &name, const QString &savePath)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("category"), name);
    addItem(form, QStringLiteral("savePath"), savePath);
    return post(QStringLiteral("torrents/editCategory"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsRemoveCategories(const QStringList &names)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("categories"), names.join(QLatin1Char('\n')));
    return post(QStringLiteral("torrents/removeCategories"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsAddTags(const QStringList &hashes, const QStringList &tags)
{
    QUrlQuery form = hashesForm(hashes);
    addItem(form, QStringLiteral("tags"), tags.join(QLatin1Char(',')));
    return post(QStringLiteral("torrents/addTags"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsRemoveTags(const QStringList &hashes,
                                                    const QStringList &tags)
{
    QUrlQuery form = hashesForm(hashes);
    addItem(form, QStringLiteral("tags"), tags.join(QLatin1Char(',')));
    return post(QStringLiteral("torrents/removeTags"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsCreateTags(const QStringList &tags)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("tags"), tags.join(QLatin1Char(',')));
    return post(QStringLiteral("torrents/createTags"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsDeleteTags(const QStringList &tags)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("tags"), tags.join(QLatin1Char(',')));
    return post(QStringLiteral("torrents/deleteTags"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsSetLocation(const QStringList &hashes,
                                                     const QString &location)
{
    QUrlQuery form = hashesForm(hashes);
    addItem(form, QStringLiteral("location"), location);
    return post(QStringLiteral("torrents/setLocation"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsSetDownloadLimit(const QStringList &hashes,
                                                          qint64 bytesPerSec)
{
    QUrlQuery form = hashesForm(hashes);
    addItem(form, QStringLiteral("limit"), QString::number(bytesPerSec));
    return post(QStringLiteral("torrents/setDownloadLimit"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsSetUploadLimit(const QStringList &hashes,
                                                        qint64 bytesPerSec)
{
    QUrlQuery form = hashesForm(hashes);
    addItem(form, QStringLiteral("limit"), QString::number(bytesPerSec));
    return post(QStringLiteral("torrents/setUploadLimit"), form);
}

QString QbtClient::shareLimitActionName(ShareLimitAction action)
{
    switch (action) {
    case ShareLimitAction::Default:
        return QStringLiteral("Default");
    case ShareLimitAction::Stop:
        return QStringLiteral("Stop");
    case ShareLimitAction::Remove:
        return QStringLiteral("Remove");
    case ShareLimitAction::RemoveWithContent:
        return QStringLiteral("RemoveWithContent");
    case ShareLimitAction::EnableSuperSeeding:
        return QStringLiteral("EnableSuperSeeding");
    }
    return QStringLiteral("Default");
}

QbtClient::ShareLimitAction QbtClient::shareLimitActionFromName(QStringView name)
{
    static const ShareLimitAction all[] = {
        ShareLimitAction::Stop,
        ShareLimitAction::Remove,
        ShareLimitAction::RemoveWithContent,
        ShareLimitAction::EnableSuperSeeding,
    };
    for (ShareLimitAction action : all) {
        if (name == shareLimitActionName(action))
            return action;
    }
    return ShareLimitAction::Default;
}

bool QbtClient::supportsShareLimitAction(const QString &apiVersion)
{
    const QVersionNumber version = QVersionNumber::fromString(apiVersion);
    return !version.isNull() && version >= QVersionNumber(2, 15, 1);
}

QbtReply<QByteArray> *
QbtClient::torrentsSetShareLimits(const QStringList &hashes,
                                  double ratioLimit,
                                  qint64 seedingTimeLimit,
                                  qint64 inactiveSeedingTimeLimit,
                                  std::optional<ShareLimitAction> shareLimitAction)
{
    QUrlQuery form = hashesForm(hashes);
    addItem(form, QStringLiteral("ratioLimit"), ratioText(ratioLimit));
    addItem(form, QStringLiteral("seedingTimeLimit"), QString::number(seedingTimeLimit));
    addItem(form,
            QStringLiteral("inactiveSeedingTimeLimit"),
            QString::number(inactiveSeedingTimeLimit));
    if (shareLimitAction)
        addItem(form, QStringLiteral("shareLimitAction"), shareLimitActionName(*shareLimitAction));
    return post(QStringLiteral("torrents/setShareLimits"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsRename(const QString &hash, const QString &name)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("hash"), hash);
    addItem(form, QStringLiteral("name"), name);
    return post(QStringLiteral("torrents/rename"), form);
}

QbtReply<QByteArray> *
QbtClient::torrentsRenameFile(const QString &hash, const QString &oldPath, const QString &newPath)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("hash"), hash);
    addItem(form, QStringLiteral("oldPath"), oldPath);
    addItem(form, QStringLiteral("newPath"), newPath);
    return post(QStringLiteral("torrents/renameFile"), form);
}

QbtReply<QByteArray> *
QbtClient::torrentsRenameFolder(const QString &hash, const QString &oldPath, const QString &newPath)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("hash"), hash);
    addItem(form, QStringLiteral("oldPath"), oldPath);
    addItem(form, QStringLiteral("newPath"), newPath);
    return post(QStringLiteral("torrents/renameFolder"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsIncreasePrio(const QStringList &hashes)
{
    return post(QStringLiteral("torrents/increasePrio"), hashesForm(hashes));
}

QbtReply<QByteArray> *QbtClient::torrentsDecreasePrio(const QStringList &hashes)
{
    return post(QStringLiteral("torrents/decreasePrio"), hashesForm(hashes));
}

QbtReply<QByteArray> *QbtClient::torrentsTopPrio(const QStringList &hashes)
{
    return post(QStringLiteral("torrents/topPrio"), hashesForm(hashes));
}

QbtReply<QByteArray> *QbtClient::torrentsBottomPrio(const QStringList &hashes)
{
    return post(QStringLiteral("torrents/bottomPrio"), hashesForm(hashes));
}

QbtReply<QByteArray> *
QbtClient::torrentsSetFilePrio(const QString &hash, const QList<int> &fileIds, int priority)
{
    QStringList ids;
    ids.reserve(fileIds.size());
    for (int id : fileIds)
        ids.append(QString::number(id));

    QUrlQuery form;
    addItem(form, QStringLiteral("hash"), hash);
    addItem(form, QStringLiteral("id"), ids.join(QLatin1Char('|')));
    addItem(form, QStringLiteral("priority"), QString::number(priority));
    return post(QStringLiteral("torrents/filePrio"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsToggleSequentialDownload(const QStringList &hashes)
{
    return post(QStringLiteral("torrents/toggleSequentialDownload"), hashesForm(hashes));
}

QbtReply<QByteArray> *QbtClient::torrentsToggleFirstLastPiecePrio(const QStringList &hashes)
{
    return post(QStringLiteral("torrents/toggleFirstLastPiecePrio"), hashesForm(hashes));
}

QbtReply<QByteArray> *QbtClient::torrentsSetAutoManagement(const QStringList &hashes, bool enable)
{
    QUrlQuery form = hashesForm(hashes);
    addBoolItem(form, QStringLiteral("enable"), enable);
    return post(QStringLiteral("torrents/setAutoManagement"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsSetSuperSeeding(const QStringList &hashes, bool value)
{
    QUrlQuery form = hashesForm(hashes);
    addBoolItem(form, QStringLiteral("value"), value);
    return post(QStringLiteral("torrents/setSuperSeeding"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsDelete(const QStringList &hashes, bool deleteFiles)
{
    QUrlQuery form = hashesForm(hashes);
    addBoolItem(form, QStringLiteral("deleteFiles"), deleteFiles);
    return post(QStringLiteral("torrents/delete"), form);
}

bool QbtClient::supportsFetchMetadata(const QString &apiVersion)
{
    const QVersionNumber version = QVersionNumber::fromString(apiVersion);
    return !version.isNull() && version >= QVersionNumber(2, 11, 9);
}

QbtReply<QbtClient::FetchedMetadata> *QbtClient::torrentsFetchMetadata(const QString &source)
{
    RequestSpec spec;
    spec.verb = QStringLiteral("POST");
    spec.path = QStringLiteral("torrents/fetchMetadata");
    addItem(spec.form, QStringLiteral("source"), source);
    // Asking again is how this call is used, so a lost answer is simply the next poll's job.
    spec.retryOnTransient = false;
    return submit<FetchedMetadata>(
        std::move(spec), [](const QByteArray &body, int status) -> QbtResult<FetchedMetadata> {
            // 202 carries the hashes (or `{}` for a link still downloading); 200 the metadata.
            FetchedMetadata fetched;
            fetched.ready = status == 200;
            if (!body.trimmed().isEmpty()) {
                ApiError error;
                const QJsonDocument document =
                    parseJson(body, QStringLiteral("torrents/fetchMetadata"), &error);
                if (error.isError())
                    return error;
                if (!document.isObject())
                    return ApiError(ApiError::Kind::Parse,
                                    QStringLiteral("torrents/fetchMetadata: not an object"));
                fetched.body = document.object();
            }
            if (fetched.ready && !fetched.body.value(QStringLiteral("info")).isObject())
                return ApiError(ApiError::Kind::Parse,
                                QStringLiteral("torrents/fetchMetadata: 200 without info"));
            return fetched;
        });
}

QbtReply<QByteArray> *QbtClient::torrentsAddUrls(const QStringList &urls, const AddOptions &options)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("urls"), urls.join(QLatin1Char('\n')));
    applyAddOptions(form, options);
    return post(QStringLiteral("torrents/add"), form);
}

QbtReply<QByteArray> *QbtClient::torrentsAddFile(const QString &fileName,
                                                 const QByteArray &bytes,
                                                 const AddOptions &options)
{
    return torrentsAddMany({AddFile{fileName, bytes}}, {}, options);
}

QbtReply<QByteArray> *QbtClient::torrentsAddMany(const QList<AddFile> &files,
                                                 const QStringList &urls,
                                                 const AddOptions &options)
{
    QUrlQuery optionsForm;
    applyAddOptions(optionsForm, options);
    const QList<QPair<QString, QString>> items = optionsForm.queryItems(QUrl::FullyDecoded);
    QList<AddFile> uploads;
    uploads.reserve(files.size());
    for (const AddFile &file : files)
        uploads.append(AddFile{safeUploadName(file.fileName), file.bytes});
    const QString links = urls.join(QLatin1Char('\n'));

    // Built again for every attempt (see postMultipart), from values captured by copy.
    auto build = [uploads, links, items]() {
        auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

        for (const AddFile &upload : uploads) {
            QHttpPart filePart;
            filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                               QStringLiteral("application/x-bittorrent"));
            filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                               QStringLiteral("form-data; name=\"torrents\"; filename=\"%1\"")
                                   .arg(upload.fileName));
            filePart.setBody(upload.bytes);
            multiPart->append(filePart);
        }

        if (!links.isEmpty()) {
            QHttpPart part;
            part.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QStringLiteral("form-data; name=\"urls\""));
            part.setBody(links.toUtf8());
            multiPart->append(part);
        }

        for (const auto &[key, value] : items) {
            QHttpPart part;
            part.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QStringLiteral("form-data; name=\"%1\"").arg(key));
            part.setBody(value.toUtf8());
            multiPart->append(part);
        }
        return multiPart;
    };

    return postMultipart(QStringLiteral("torrents/add"), std::move(build));
}

QbtReply<QByteArray> *QbtClient::transferSetDownloadLimit(qint64 bytesPerSec)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("limit"), QString::number(bytesPerSec));
    return post(QStringLiteral("transfer/setDownloadLimit"), form);
}

QbtReply<QByteArray> *QbtClient::transferSetUploadLimit(qint64 bytesPerSec)
{
    QUrlQuery form;
    addItem(form, QStringLiteral("limit"), QString::number(bytesPerSec));
    return post(QStringLiteral("transfer/setUploadLimit"), form);
}

QbtReply<QByteArray> *QbtClient::postMultipart(const QString &path,
                                               std::function<QHttpMultiPart *()> buildMultiPart)
{
    RequestSpec spec;
    spec.verb = QStringLiteral("POST");
    spec.path = path;
    spec.retryOnTransient = false;
    spec.buildMultiPart = std::move(buildMultiPart);
    return submit<QByteArray>(std::move(spec),
                              [](const QByteArray &body) { return QbtResult<QByteArray>(body); });
}

} // namespace omaseed::core
