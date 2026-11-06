#include "core/engines/transmission/TransmissionRpc.hpp"

#include "core/CertificatePin.hpp"
#include "core/Version.hpp"
#include "core/engines/transmission/TransmissionFingerprint.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QTimer>

namespace omaseed::core {

Q_LOGGING_CATEGORY(lcTransmission, "omaseed.transmission")

namespace {

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

/// The JSON-RPC error object as an ApiError (rpc-spec §2.1: `data.error_string` says more).
ApiError rpcError(const QString &method, const QJsonObject &error)
{
    const int code = error.value(QStringLiteral("code")).toInt();
    const QString message = error.value(QStringLiteral("message")).toString();
    const QString detail = error.value(QStringLiteral("data"))
                               .toObject()
                               .value(QStringLiteral("error_string"))
                               .toString();
    const QString text = QStringLiteral("%1: %2").arg(method, detail.isEmpty() ? message : detail);
    switch (code) {
    case -32601: // Method not found
        return ApiError(ApiError::Kind::NotFound, text);
    case -32700: // Parse error
    case -32600: // Invalid Request
        return ApiError(ApiError::Kind::Parse, text);
    default:
        break;
    }
    return ApiError(ApiError::Kind::Conflict, text);
}

ApiError notJsonRpc(const QString &what)
{
    return ApiError(ApiError::Kind::Parse,
                    QStringLiteral("%1: not a JSON-RPC 2.0 answer (a Transmission older than "
                                   "4.1 answers in its own dialect)")
                        .arg(what));
}

} // namespace

/// One POST. It outlives its QNetworkReply: the 409 resend and the retries send it again.
struct TransmissionRpc::Job
{
    QByteArray body;
    QString what; ///< the method(s), for errors and logs
    Retry retry = Retry::No;
    std::chrono::milliseconds timeout{0}; ///< 0: the client's
    int attempt = 0;                      ///< attempts already finished
    bool sessionRetried = false;          ///< the one resend with a new session id was spent
    bool cancelled = false;
    bool finished = false;
    QPointer<QNetworkReply> networkReply;
    QString refusedCertificate;
    QString refusedCertificateReason;
    std::function<void(const HttpAnswer &, const ApiError &)> deliver;
};

TransmissionRpc::TransmissionRpc(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_nam->setAutoDeleteReplies(false);
}

TransmissionRpc::~TransmissionRpc()
{
    cancelAll();
}

QUrl TransmissionRpc::baseUrl() const
{
    return m_baseUrl;
}

void TransmissionRpc::setBaseUrl(const QUrl &baseUrl)
{
    if (m_baseUrl == baseUrl)
        return;
    m_baseUrl = baseUrl;
    // Work queued for another server must not reach this one (QbtClient::setBaseUrl), and
    // the session id was that server's.
    m_sessionId.clear();
    m_rpcVersionHeader.clear();
    cancelAll();
}

QUrl TransmissionRpc::endpoint() const
{
    return TransmissionFingerprint::rpcUrl(m_baseUrl);
}

void TransmissionRpc::setCredentials(const QString &username, const QString &password)
{
    m_loginRejected = false; // whoever sets a login (again) wants it tried
    m_username = username;
    m_password = password;
}

bool TransmissionRpc::hasCredentials() const
{
    return !m_username.isEmpty() || !m_password.isEmpty();
}

void TransmissionRpc::clearAuth()
{
    m_username.clear();
    m_password.clear();
    m_loginRejected = false;
    m_sessionId.clear();
    cancelAll();
}

void TransmissionRpc::allowLoginAgain()
{
    m_loginRejected = false;
}

void TransmissionRpc::setPinnedCertificate(const QString &sha256)
{
    const QString pin = CertificatePin::normalized(sha256);
    if (pin == m_pinnedCertificate)
        return;
    m_pinnedCertificate = pin;
    m_nam->clearConnectionCache(); // a pooled TLS session was allowed under the old pin
}

void TransmissionRpc::setTimeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
}

std::chrono::milliseconds TransmissionRpc::timeout() const
{
    return m_timeout;
}

int TransmissionRpc::maxRetries() const
{
    return m_maxRetries;
}

void TransmissionRpc::setMaxRetries(int retries)
{
    m_maxRetries = qMax(0, retries);
}

void TransmissionRpc::setRetryBackoff(std::chrono::milliseconds backoff)
{
    m_retryBackoff = backoff;
}

QJsonObject TransmissionRpc::request(const QString &method, const QJsonObject &params, int id)
{
    QJsonObject object{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                       {QStringLiteral("method"), method},
                       {QStringLiteral("id"), id}};
    if (!params.isEmpty())
        object.insert(QStringLiteral("params"), params);
    return object;
}

Result<QJsonObject> TransmissionRpc::answerOf(const QString &method, const QJsonObject &answer)
{
    if (answer.value(QStringLiteral("jsonrpc")).toString() != QLatin1String("2.0"))
        return Result<QJsonObject>(notJsonRpc(method));
    if (answer.contains(QStringLiteral("error")))
        return Result<QJsonObject>(
            rpcError(method, answer.value(QStringLiteral("error")).toObject()));
    return Result<QJsonObject>(answer.value(QStringLiteral("result")).toObject());
}

// --- requests -------------------------------------------------------------------------------

Reply<HttpAnswer> *TransmissionRpc::post(const QByteArray &body,
                                         const QString &what,
                                         Retry retry,
                                         std::chrono::milliseconds timeout)
{
    auto *reply = new Reply<HttpAnswer>(this);
    auto job = std::make_shared<Job>();
    job->body = body;
    job->what = what;
    job->retry = retry;
    job->timeout = timeout;
    QPointer<Reply<HttpAnswer>> to(reply);
    job->deliver = [to](const HttpAnswer &answer, const ApiError &error) {
        if (to.isNull())
            return;
        to->complete(error.isError() ? Result<HttpAnswer>(error) : Result<HttpAnswer>(answer));
    };
    connect(reply, &ReplyBase::abortRequested, this, [this, job]() { cancelJob(job); });

    if (!m_baseUrl.isValid() || m_baseUrl.isEmpty()) {
        finishLater(job,
                    ApiError(ApiError::Kind::Network,
                             QStringLiteral("%1: no server URL is configured").arg(what)));
    } else if (m_loginRejected && hasCredentials()) {
        // The daemon refused this login already: asking again only counts toward its ban.
        finishLater(job,
                    ApiError(ApiError::Kind::Unauthorized,
                             QStringLiteral("%1: sign-in was refused, not retried").arg(what),
                             401));
    } else {
        send(job);
    }
    return reply;
}

Reply<QJsonObject> *TransmissionRpc::call(const QString &method,
                                          const QJsonObject &params,
                                          Retry retry,
                                          std::chrono::milliseconds timeout)
{
    const QByteArray body =
        QJsonDocument(request(method, params, nextId())).toJson(QJsonDocument::Compact);
    return mapReply<QJsonObject>(
        post(body, method, retry, timeout), this, [method](const HttpAnswer &a) {
            const QJsonDocument document = QJsonDocument::fromJson(a.body);
            if (!document.isObject())
                return Result<QJsonObject>(notJsonRpc(method));
            return answerOf(method, document.object());
        });
}

Reply<QList<Result<QJsonObject>>> *TransmissionRpc::batch(const QList<Call> &calls, Retry retry)
{
    QJsonArray body;
    QList<int> ids;
    QStringList methods;
    for (const Call &call : calls) {
        const int id = nextId();
        ids.append(id);
        methods.append(call.method);
        body.append(request(call.method, call.params, id));
    }
    const QString what = methods.join(QLatin1Char('+'));
    return mapReply<QList<Result<QJsonObject>>>(
        post(QJsonDocument(body).toJson(QJsonDocument::Compact), what, retry),
        this,
        [ids, methods, what](const HttpAnswer &a) {
            using Results = QList<Result<QJsonObject>>;
            const QJsonDocument document = QJsonDocument::fromJson(a.body);
            if (document.isObject()) { // the batch as a whole refused
                const Result<QJsonObject> whole = answerOf(what, document.object());
                return Result<Results>(whole.isError() ? whole.error() : notJsonRpc(what));
            }
            if (!document.isArray())
                return Result<Results>(notJsonRpc(what));
            QHash<int, QJsonObject> byId;
            for (const QJsonValue &value : document.array()) {
                const QJsonObject answer = value.toObject();
                byId.insert(answer.value(QStringLiteral("id")).toInt(), answer);
            }
            Results results;
            results.reserve(ids.size());
            for (qsizetype i = 0; i < ids.size(); ++i) {
                const auto it = byId.constFind(ids.at(i));
                if (it == byId.constEnd())
                    results.append(Result<QJsonObject>(
                        ApiError(ApiError::Kind::Parse,
                                 QStringLiteral("%1: no answer in the batch").arg(methods.at(i)))));
                else
                    results.append(answerOf(methods.at(i), *it));
            }
            return Result<Results>(results);
        });
}

// --- the wire -------------------------------------------------------------------------------

void TransmissionRpc::send(const JobPtr &job)
{
    QNetworkRequest request(endpoint());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("omaseed/%1").arg(version()));
    if (!m_sessionId.isEmpty())
        request.setRawHeader("X-Transmission-Session-Id", m_sessionId);
    if (hasCredentials()) {
        const QByteArray login = (m_username + QLatin1Char(':') + m_password).toUtf8();
        request.setRawHeader("Authorization", QByteArrayLiteral("Basic ") + login.toBase64());
    }
    request.setTransferTimeout(job->timeout.count() > 0 ? job->timeout : m_timeout);

    QNetworkReply *networkReply = m_nam->post(request, job->body);
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
    connect(networkReply, &QNetworkReply::finished, this, [this, job]() { onFinished(job); });
}

void TransmissionRpc::onSslErrors(const JobPtr &job,
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
        networkReply->ignoreSslErrors(errors); // exactly the one the person trusted (Q27)
        return;
    }
    job->refusedCertificate = seen;
    job->refusedCertificateReason =
        errors.isEmpty() ? QString() : errors.constFirst().errorString();
}

void TransmissionRpc::onFinished(const JobPtr &job)
{
    QNetworkReply *networkReply = job->networkReply;
    if (networkReply == nullptr)
        return;
    job->networkReply = nullptr;
    m_active.removeOne(job);
    ++job->attempt;

    HttpAnswer answer;
    answer.status = networkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    answer.body = networkReply->isReadable() ? networkReply->readAll() : QByteArray();
    answer.headers = networkReply->rawHeaderPairs();
    const QNetworkReply::NetworkError networkError = networkReply->error();
    const QString networkText = networkReply->errorString();
    networkReply->deleteLater();

    const QString where = job->what;
    ApiError error;
    if (job->cancelled) {
        error = ApiError(ApiError::Kind::Cancelled, QStringLiteral("%1: cancelled").arg(where));
    } else if (answer.status == 409) {
        const QByteArray id = answer.header("X-Transmission-Session-Id");
        const QByteArray rpcVersion = answer.header("X-Transmission-Rpc-Version");
        if (!rpcVersion.isEmpty())
            m_rpcVersionHeader = QString::fromLatin1(rpcVersion);
        if (!id.isEmpty() && !job->sessionRetried) {
            // rpc-spec §2.2.1: keep the new id and send the same request again, once.
            m_sessionId = id;
            job->sessionRetried = true;
            send(job);
            return;
        }
        error = ApiError(ApiError::Kind::Conflict,
                         id.isEmpty()
                             ? QStringLiteral("%1: 409 without a session id").arg(where)
                             : QStringLiteral("%1: the session id was refused twice").arg(where),
                         409);
    } else if (answer.status == 401) {
        if (hasCredentials())
            m_loginRejected = true;
        error = ApiError(ApiError::Kind::Unauthorized,
                         QStringLiteral("%1: %2").arg(where,
                                                      hasCredentials()
                                                          ? QStringLiteral("the login was refused")
                                                          : QStringLiteral("a login is needed")),
                         401);
    } else if (answer.status != 0) {
        error = ApiError::fromHttpStatus(
            answer.status, QStringLiteral("%1: HTTP %2").arg(where).arg(answer.status));
    } else if (networkError == QNetworkReply::TimeoutError ||
               networkError == QNetworkReply::OperationCanceledError) {
        error = ApiError(ApiError::Kind::Timeout,
                         QStringLiteral("%1: no reply within %2 ms")
                             .arg(where)
                             .arg((job->timeout.count() > 0 ? job->timeout : m_timeout).count()));
    } else if (!job->refusedCertificate.isEmpty()) {
        const QString why =
            job->refusedCertificateReason.isEmpty() ? networkText : job->refusedCertificateReason;
        error = ApiError::certificate(QStringLiteral("%1: %2").arg(where, why),
                                      job->refusedCertificate);
    } else if (networkError != QNetworkReply::NoError) {
        error = ApiError(isNetworkFailure(networkError) ? ApiError::Kind::Network
                                                        : ApiError::Kind::Unknown,
                         QStringLiteral("%1: %2").arg(where, networkText));
    }

    if (error.isError() && error.isTransient() && job->retry == Retry::OnTransient &&
        job->attempt <= m_maxRetries) {
        const auto delay = m_retryBackoff * (1 << (job->attempt - 1));
        qCDebug(lcTransmission) << "retrying" << where << "in" << delay.count() << "ms";
        m_backingOff.append(job);
        QTimer::singleShot(delay, this, [this, job]() {
            if (m_backingOff.removeOne(job))
                send(job);
        });
        return;
    }
    finish(job, answer, error);
}

void TransmissionRpc::finish(const JobPtr &job, const HttpAnswer &answer, const ApiError &error)
{
    if (job->finished)
        return;
    job->finished = true;
    if (error.isError())
        qCDebug(lcTransmission) << "failed:" << error.toString();
    if (job->deliver)
        job->deliver(answer, error);
}

void TransmissionRpc::finishLater(const JobPtr &job, const ApiError &error)
{
    QTimer::singleShot(0, this, [this, job, error]() { finish(job, HttpAnswer(), error); });
}

void TransmissionRpc::cancelJob(const JobPtr &job)
{
    if (m_backingOff.removeOne(job)) {
        finish(job,
               HttpAnswer(),
               ApiError(ApiError::Kind::Cancelled, QStringLiteral("%1: cancelled").arg(job->what)));
        return;
    }
    if (m_active.contains(job)) {
        job->cancelled = true;
        if (job->networkReply)
            job->networkReply->abort(); // finishes through onFinished
        return;
    }
    // Not sent yet (a refusal waiting for the next turn): it settles as it was going to.
}

void TransmissionRpc::cancelAll()
{
    const QList<JobPtr> jobs = m_backingOff + m_active;
    for (const JobPtr &job : jobs)
        cancelJob(job);
}

} // namespace omaseed::core
