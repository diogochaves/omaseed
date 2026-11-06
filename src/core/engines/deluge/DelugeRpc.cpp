#include "core/engines/deluge/DelugeRpc.hpp"

#include "core/CertificatePin.hpp"
#include "core/Version.hpp"
#include "core/engines/deluge/DelugeFingerprint.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QTimer>

namespace omaseed::core {

Q_LOGGING_CATEGORY(lcDeluge, "omaseed.deluge")

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

ApiError notJsonRpc(const QString &what)
{
    return ApiError(ApiError::Kind::Parse, QStringLiteral("%1: not a deluge-web answer").arg(what));
}

} // namespace

/// One call. It outlives its QNetworkReply: a retry, or a sign-in in between, sends it again.
struct DelugeRpc::Job
{
    QString method;
    QJsonArray params;
    Retry retry = Retry::No;
    std::chrono::milliseconds timeout{0}; ///< 0: the client's
    bool isLogin = false;                 ///< auth.login itself: never waits for a sign-in
    int attempt = 0;                      ///< attempts already finished
    bool signedInAgain = false;           ///< the one sign-in after "Not authenticated" spent
    bool cancelled = false;
    bool finished = false;
    QPointer<QNetworkReply> networkReply;
    QString refusedCertificate;
    QString refusedCertificateReason;
    std::function<void(const Result<QJsonValue> &)> deliver;
};

DelugeRpc::DelugeRpc(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_nam->setAutoDeleteReplies(false);
    m_nam->setCookieJar(new QNetworkCookieJar(m_nam)); // the session cookie lives here
}

DelugeRpc::~DelugeRpc()
{
    cancelAll();
}

QUrl DelugeRpc::baseUrl() const
{
    return m_baseUrl;
}

void DelugeRpc::setBaseUrl(const QUrl &baseUrl)
{
    if (m_baseUrl == baseUrl)
        return;
    m_baseUrl = baseUrl;
    // Work queued for another server must not reach this one, and its session was that one's.
    cancelAll();
    forgetSession();
}

QUrl DelugeRpc::endpoint() const
{
    return DelugeFingerprint::jsonUrl(m_baseUrl);
}

void DelugeRpc::setPassword(const QString &password)
{
    const bool wasRefused = m_loginRejected;
    m_loginRejected = false; // whoever sets a password (again) wants it tried
    if (password != m_password) {
        m_password = password;
        m_signedIn = false;
    }
    if (wasRefused)
        Q_EMIT loginRefusedChanged();
}

void DelugeRpc::clearAuth()
{
    m_password.clear();
    const bool wasRefused = m_loginRejected;
    m_loginRejected = false;
    cancelAll();
    forgetSession();
    if (wasRefused)
        Q_EMIT loginRefusedChanged();
}

void DelugeRpc::allowLoginAgain()
{
    if (!m_loginRejected)
        return;
    m_loginRejected = false;
    Q_EMIT loginRefusedChanged();
}

void DelugeRpc::forgetSession()
{
    m_signedIn = false;
    m_nam->setCookieJar(new QNetworkCookieJar(m_nam)); // the old jar goes with its parent
}

void DelugeRpc::setPinnedCertificate(const QString &sha256)
{
    const QString pin = CertificatePin::normalized(sha256);
    if (pin == m_pinnedCertificate)
        return;
    m_pinnedCertificate = pin;
    m_nam->clearConnectionCache(); // a pooled TLS session was allowed under the old pin
}

void DelugeRpc::setTimeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
}

std::chrono::milliseconds DelugeRpc::timeout() const
{
    return m_timeout;
}

int DelugeRpc::maxRetries() const
{
    return m_maxRetries;
}

void DelugeRpc::setMaxRetries(int retries)
{
    m_maxRetries = qMax(0, retries);
}

void DelugeRpc::setRetryBackoff(std::chrono::milliseconds backoff)
{
    m_retryBackoff = backoff;
}

// --- the answers ----------------------------------------------------------------------------

QByteArray DelugeRpc::request(const QString &method, const QJsonArray &params, int id)
{
    return DelugeFingerprint::call(method, params, id);
}

QString DelugeRpc::failureWords(const QString &message)
{
    // A daemon's exception comes wrapped: "Failure: [Failure instance: Traceback (failure with
    // no frames): <class 'deluge.error.AddTorrentError'>: Torrent already in session (H).\n]",
    // or a WrappedException whose first line is the words and the traceback follows.
    static const QRegularExpression wrapped(QStringLiteral("<class '[^']*'>: ([^\\n]*)"),
                                            QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = wrapped.match(message);
    QString words = match.hasMatch() ? match.captured(1) : message;
    words = words.trimmed();
    if (words.endsWith(QLatin1Char(']')) && !words.contains(QLatin1Char('[')))
        words.chop(1);
    // A KeyError's words are only the key, quoted: say what it was.
    if (match.hasMatch() && words.startsWith(QLatin1Char('\'')) &&
        words.endsWith(QLatin1Char('\'')))
        words = QStringLiteral("missing %1").arg(words);
    return words.trimmed();
}

ApiError DelugeRpc::errorOf(const QString &method, const QJsonValue &error)
{
    const QJsonObject object = error.toObject();
    const int code = object.value(QStringLiteral("code")).toInt();
    const QString message = object.value(QStringLiteral("message")).toString();
    switch (code) {
    case NotAuthenticated:
        return ApiError(ApiError::Kind::Unauthorized,
                        QStringLiteral("%1: deluge-web wants its password").arg(method),
                        401);
    case UnknownMethod:
        return ApiError(ApiError::Kind::NotFound, QStringLiteral("%1: %2").arg(method, message));
    default:
        break;
    }
    return ApiError(ApiError::Kind::Conflict,
                    QStringLiteral("%1: %2").arg(method, failureWords(message)));
}

Result<QJsonValue> DelugeRpc::answerOf(const QString &method, const QByteArray &body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject())
        return Result<QJsonValue>(notJsonRpc(method));
    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("result")) || !root.contains(QStringLiteral("error")))
        return Result<QJsonValue>(notJsonRpc(method));
    const QJsonValue error = root.value(QStringLiteral("error"));
    if (!error.isNull() && !error.isUndefined())
        return Result<QJsonValue>(errorOf(method, error));
    return Result<QJsonValue>(root.value(QStringLiteral("result")));
}

// --- calls ----------------------------------------------------------------------------------

Reply<QJsonValue> *DelugeRpc::call(const QString &method,
                                   const QJsonArray &params,
                                   Retry retry,
                                   std::chrono::milliseconds timeout)
{
    auto *reply = new Reply<QJsonValue>(this);
    auto job = std::make_shared<Job>();
    job->method = method;
    job->params = params;
    job->retry = retry;
    job->timeout = timeout;
    QPointer<Reply<QJsonValue>> to(reply);
    job->deliver = [to](const Result<QJsonValue> &result) {
        if (!to.isNull())
            to->complete(result);
    };
    connect(reply, &ReplyBase::abortRequested, this, [this, job]() { cancelJob(job); });
    start(job);
    return reply;
}

void DelugeRpc::start(const JobPtr &job)
{
    if (!m_baseUrl.isValid() || m_baseUrl.isEmpty()) {
        finishLater(job,
                    ApiError(ApiError::Kind::Network,
                             QStringLiteral("%1: no server URL is configured").arg(job->method)));
        return;
    }
    if (m_loginRejected) {
        // deluge-web refused this password already: asking again changes nothing.
        finishLater(
            job,
            ApiError(
                ApiError::Kind::Unauthorized,
                QStringLiteral("%1: the password was refused, not tried again").arg(job->method),
                401));
        return;
    }
    if (!m_signedIn && hasPassword()) {
        m_waitingForLogin.append(job);
        login();
        return;
    }
    send(job);
}

void DelugeRpc::login()
{
    if (m_loggingIn)
        return;
    m_loggingIn = true;
    auto job = std::make_shared<Job>();
    job->method = QStringLiteral("auth.login");
    job->params = QJsonArray{m_password};
    job->retry = Retry::OnTransient; // signing in changes nothing on the server
    job->isLogin = true;
    QPointer<DelugeRpc> self(this);
    job->deliver = [self](const Result<QJsonValue> &result) {
        if (self.isNull())
            return;
        if (result.isError())
            self->loginFinished(false, result.error());
        else
            self->loginFinished(result.value().toBool(), ApiError());
    };
    send(job);
}

void DelugeRpc::loginFinished(bool ok, const ApiError &error)
{
    m_loggingIn = false;
    const QList<JobPtr> waiting = std::exchange(m_waitingForLogin, {});
    if (error.isError()) {
        for (const JobPtr &job : waiting)
            finish(job,
                   Result<QJsonValue>(ApiError(
                       error.kind(),
                       QStringLiteral("%1: signing in: %2").arg(job->method, error.message()),
                       error.httpStatus())));
        return;
    }
    if (!ok) {
        m_signedIn = false;
        m_loginRejected = true;
        Q_EMIT loginRefusedChanged();
        for (const JobPtr &job : waiting)
            finish(job,
                   Result<QJsonValue>(ApiError(ApiError::Kind::Unauthorized,
                                               QStringLiteral("%1: deluge-web refused the "
                                                              "password")
                                                   .arg(job->method),
                                               401)));
        return;
    }
    m_signedIn = true;
    for (const JobPtr &job : waiting) {
        if (!job->cancelled && !job->finished)
            send(job);
    }
}

// --- the wire -------------------------------------------------------------------------------

void DelugeRpc::send(const JobPtr &job)
{
    QNetworkRequest request(endpoint());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("omaseed/%1").arg(version()));
    request.setTransferTimeout(job->timeout.count() > 0 ? job->timeout : m_timeout);

    QNetworkReply *networkReply =
        m_nam->post(request, this->request(job->method, job->params, nextId()));
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

void DelugeRpc::onSslErrors(const JobPtr &job,
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

void DelugeRpc::onFinished(const JobPtr &job)
{
    QNetworkReply *networkReply = job->networkReply;
    if (networkReply == nullptr)
        return;
    job->networkReply = nullptr;
    m_active.removeOne(job);
    ++job->attempt;

    const int status = networkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = networkReply->isReadable() ? networkReply->readAll() : QByteArray();
    const QNetworkReply::NetworkError networkError = networkReply->error();
    const QString networkText = networkReply->errorString();
    networkReply->deleteLater();

    const QString where = job->method;
    ApiError error;
    if (job->cancelled) {
        error = ApiError(ApiError::Kind::Cancelled, QStringLiteral("%1: cancelled").arg(where));
    } else if (status == 200) {
        const Result<QJsonValue> answer = answerOf(where, body);
        if (answer.isError() && answer.error().kind() == ApiError::Kind::Unauthorized &&
            !job->isLogin) {
            // The session ran out (or deluge-web restarted): sign in again and send this call
            // once more, once. Without a password there is nothing to sign in with.
            if (hasPassword() && !job->signedInAgain && !m_loginRejected) {
                job->signedInAgain = true;
                m_signedIn = false;
                m_waitingForLogin.append(job);
                login();
                return;
            }
            finish(
                job,
                Result<QJsonValue>(ApiError(
                    ApiError::Kind::Unauthorized,
                    hasPassword() ? QStringLiteral("%1: signed in, then refused again").arg(where)
                                  : QStringLiteral("%1: deluge-web wants its password").arg(where),
                    401)));
            return;
        }
        finish(job, answer);
        return;
    } else if (status != 0) {
        error =
            ApiError::fromHttpStatus(status, QStringLiteral("%1: HTTP %2").arg(where).arg(status));
        if (!error.isError())
            error = ApiError(ApiError::Kind::Unknown,
                             QStringLiteral("%1: HTTP %2").arg(where).arg(status),
                             status);
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
    } else {
        error = notJsonRpc(where);
    }

    if (error.isTransient() && job->retry == Retry::OnTransient && job->attempt <= m_maxRetries) {
        const auto delay = m_retryBackoff * (1 << (job->attempt - 1));
        qCDebug(lcDeluge) << "retrying" << where << "in" << delay.count() << "ms";
        m_backingOff.append(job);
        QTimer::singleShot(delay, this, [this, job]() {
            if (m_backingOff.removeOne(job))
                send(job);
        });
        return;
    }
    finish(job, Result<QJsonValue>(error));
}

void DelugeRpc::finish(const JobPtr &job, const Result<QJsonValue> &result)
{
    if (job->finished)
        return;
    job->finished = true;
    if (result.isError())
        qCDebug(lcDeluge) << "failed:" << result.error().toString();
    if (job->deliver)
        job->deliver(result);
}

void DelugeRpc::finishLater(const JobPtr &job, const ApiError &error)
{
    QTimer::singleShot(0, this, [this, job, error]() { finish(job, Result<QJsonValue>(error)); });
}

void DelugeRpc::cancelJob(const JobPtr &job)
{
    const ApiError cancelled(ApiError::Kind::Cancelled,
                             QStringLiteral("%1: cancelled").arg(job->method));
    if (m_backingOff.removeOne(job) || m_waitingForLogin.removeOne(job)) {
        finish(job, Result<QJsonValue>(cancelled));
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

void DelugeRpc::cancelAll()
{
    const QList<JobPtr> jobs = m_waitingForLogin + m_backingOff + m_active;
    for (const JobPtr &job : jobs)
        cancelJob(job);
    m_loggingIn = false;
}

} // namespace omaseed::core
