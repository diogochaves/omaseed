#include "core/engines/EngineRecognizer.hpp"

#include "core/CertificatePin.hpp"
#include "core/engines/deluge/DelugeFingerprint.hpp"
#include "core/engines/qbt/QbtFingerprint.hpp"
#include "core/engines/transmission/TransmissionFingerprint.hpp"

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslError>

#include <memory>

namespace omaseed::core {

namespace {

/// The first line of a body, trimmed and short, for an error's text.
QString firstLine(const QByteArray &body)
{
    const QString text = QString::fromUtf8(body).trimmed();
    return text.section(QLatin1Char('\n'), 0, 0).left(120);
}

} // namespace

EngineRecognizer::EngineRecognizer(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_nam->setCookieJar(new QNetworkCookieJar(m_nam));
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

EngineRecognizer::~EngineRecognizer()
{
    ++m_run;
    for (const QPointer<QNetworkReply> &reply : std::as_const(m_replies)) {
        if (reply)
            reply->abort();
    }
}

QList<EngineKind> EngineRecognizer::order(const QUrl &address)
{
    const int port = address.port();
    const QString path = address.path();
    EngineKind first = EngineKind::Qbittorrent;
    if (port == 9091 || path.contains(QLatin1String("/transmission")))
        first = EngineKind::Transmission;
    else if (port == 8112 || path.endsWith(QLatin1String("/json")) ||
             path.endsWith(QLatin1String("/json/")))
        first = EngineKind::Deluge;
    QList<EngineKind> engines{first};
    for (const EngineKind kind : kKnownEngines) {
        if (kind != first)
            engines.append(kind);
    }
    return engines;
}

void EngineRecognizer::setTimeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
}

void EngineRecognizer::setPinnedCertificate(const QString &sha256)
{
    m_pinned = sha256;
}

void EngineRecognizer::setCredentials(const QString &username, const QString &password)
{
    m_username = username;
    m_password = password;
}

void EngineRecognizer::start(const QUrl &address, EngineKind only)
{
    ++m_run;
    for (const QPointer<QNetworkReply> &reply : std::as_const(m_replies)) {
        if (reply)
            reply->abort();
    }
    m_replies.clear();
    m_nam->setCookieJar(new QNetworkCookieJar(m_nam)); // the old jar goes with the manager's child
    m_address = address;
    m_result = Recognition();
    m_queue = only == EngineKind::Unknown ? order(address) : QList<EngineKind>{only};
    m_done = false;
    askNext();
}

void EngineRecognizer::send(const QString &label,
                            const QUrl &url,
                            const QByteArray &body,
                            Answered answered)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(int(m_timeout.count()));
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    QNetworkReply *reply = nullptr;
    if (body.isNull()) {
        reply = m_nam->get(request);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = m_nam->post(request, body);
    }
    m_replies.append(reply);

    const quint64 run = m_run;
    auto refused = std::make_shared<QString>();
    connect(reply,
            &QNetworkReply::sslErrors,
            this,
            [this, reply, refused](const QList<QSslError> &errors) {
                QSslCertificate certificate = reply->sslConfiguration().peerCertificate();
                for (const QSslError &error : errors) {
                    if (!certificate.isNull())
                        break;
                    certificate = error.certificate();
                }
                const QString seen = CertificatePin::fingerprint(certificate);
                if (!seen.isEmpty() && seen == m_pinned) {
                    reply->ignoreSslErrors(errors);
                    return;
                }
                *refused = seen;
            });
    connect(reply, &QNetworkReply::finished, this, [this, run, reply, label, refused, answered]() {
        reply->deleteLater();
        m_replies.removeAll(reply);
        if (run != m_run || m_done)
            return;
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QNetworkReply::NetworkError error = reply->error();
        if (status.isValid() && status.toInt() > 0) {
            HttpAnswer answer;
            answer.status = status.toInt();
            answer.body = reply->readAll();
            answer.headers = reply->rawHeaderPairs();
            answered(answer);
            return;
        }
        // The transport failed: another engine would meet the same.
        m_result.outcome = Recognition::Outcome::Failed;
        if (error == QNetworkReply::TimeoutError ||
            error == QNetworkReply::OperationCanceledError) {
            m_result.error = ApiError(
                ApiError::Kind::Timeout,
                QStringLiteral("%1: no reply within %2 ms").arg(label).arg(m_timeout.count()));
        } else if (!refused->isEmpty()) {
            m_result.error = ApiError::certificate(
                QStringLiteral("%1: %2").arg(label, reply->errorString()), *refused);
        } else {
            m_result.error = ApiError(ApiError::Kind::Network,
                                      QStringLiteral("%1: %2").arg(label, reply->errorString()));
        }
        finish();
    });
}

void EngineRecognizer::askNext()
{
    if (m_queue.isEmpty()) {
        m_result.outcome = Recognition::Outcome::Unrecognised;
        finish();
        return;
    }
    const EngineKind next = m_queue.takeFirst();
    m_result.asked.append(next);
    switch (next) {
    case EngineKind::Qbittorrent:
        askQbittorrent();
        return;
    case EngineKind::Transmission:
        askTransmission();
        return;
    case EngineKind::Deluge:
        askDeluge();
        return;
    case EngineKind::Unknown:
        break;
    }
    askNext();
}

void EngineRecognizer::notItself(const HttpAnswer &answer, const QString &label)
{
    const QString line = firstLine(answer.body);
    m_result.error = ApiError::fromHttpStatus(
        answer.status, line.isEmpty() ? label : QStringLiteral("%1: %2").arg(label, line));
    if (!m_result.error.isError()) // a 200 that wasn't the engine's
        m_result.error = ApiError(ApiError::Kind::Parse,
                                  QStringLiteral("%1: not the answer it gives").arg(label));
    askNext();
}

void EngineRecognizer::askQbittorrent()
{
    const QString label = QStringLiteral("GET app/version");
    send(label,
         QbtFingerprint::versionUrl(m_address),
         QByteArray(),
         [this, label](const HttpAnswer &answer) {
             const QbtFingerprint::Reading reading = QbtFingerprint::read(answer);
             if (reading.fingerprint != Fingerprint::Itself) {
                 notItself(answer, label);
                 return;
             }
             m_result.outcome = Recognition::Outcome::Recognised;
             m_result.engine = EngineKind::Qbittorrent;
             m_result.version = reading.version;
             m_result.wantsSignIn = reading.version.isEmpty();
             m_result.endpoint = m_address;
             m_result.error = ApiError();
             finish();
         });
}

void EngineRecognizer::askTransmission()
{
    const QUrl rpc = TransmissionFingerprint::rpcUrl(m_address);
    const QString label = QStringLiteral("POST %1").arg(rpc.path().mid(1));
    send(label,
         rpc,
         TransmissionFingerprint::sessionGetBody(),
         [this, rpc, label](const HttpAnswer &answer) {
             const TransmissionFingerprint::Reading reading = TransmissionFingerprint::read(answer);
             if (reading.fingerprint != Fingerprint::Itself) {
                 notItself(answer, label);
                 return;
             }
             m_result.outcome = Recognition::Outcome::Recognised;
             m_result.engine = EngineKind::Transmission;
             m_result.endpoint = rpc;
             m_result.error = ApiError();
             if (!reading.rpcVersion.isEmpty())
                 m_result.apiVersion = reading.rpcVersion;
             if (reading.wantsLogin && m_password.isEmpty() && m_username.isEmpty()) {
                 m_result.wantsSignIn = true;
                 finish();
                 return;
             }
             // Sent again with the session id (and the login, once one was asked for or typed):
             // session_get names the version.
             askTransmissionAgain(reading.sessionId);
         });
}

void EngineRecognizer::askTransmissionAgain(const QByteArray &sessionId)
{
    const QUrl rpc = m_result.endpoint;
    QNetworkRequest request(rpc);
    request.setTransferTimeout(int(m_timeout.count()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!sessionId.isEmpty())
        request.setRawHeader("X-Transmission-Session-Id", sessionId);
    if (!m_username.isEmpty() || !m_password.isEmpty()) {
        request.setRawHeader(
            "Authorization",
            "Basic " + QStringLiteral("%1:%2").arg(m_username, m_password).toUtf8().toBase64());
    }
    QNetworkReply *reply = m_nam->post(request, TransmissionFingerprint::sessionGetBody());
    m_replies.append(reply);
    const quint64 run = m_run;
    const bool sentLogin = !m_username.isEmpty() || !m_password.isEmpty();
    const bool hadSession = !sessionId.isEmpty();
    connect(reply, &QNetworkReply::finished, this, [this, run, reply, sentLogin, hadSession]() {
        reply->deleteLater();
        m_replies.removeAll(reply);
        if (run != m_run || m_done)
            return;
        HttpAnswer answer;
        answer.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        answer.body = reply->readAll();
        answer.headers = reply->rawHeaderPairs();
        const TransmissionFingerprint::Reading reading = TransmissionFingerprint::read(answer);
        if (answer.status == 409 && !hadSession && !reading.sessionId.isEmpty()) {
            // Signed in at the first try, then the session dance.
            if (!reading.rpcVersion.isEmpty())
                m_result.apiVersion = reading.rpcVersion;
            askTransmissionAgain(reading.sessionId);
            return;
        }
        if (answer.status == 401) {
            m_result.signInRefused = sentLogin;
            m_result.wantsSignIn = !sentLogin;
        } else if (answer.status == 200) {
            m_result.version = reading.version;
            if (!reading.rpcVersion.isEmpty())
                m_result.apiVersion = reading.rpcVersion;
        }
        // Anything else leaves what the first answer said: it is Transmission all the same.
        finish();
    });
}

void EngineRecognizer::askDeluge()
{
    const QUrl json = DelugeFingerprint::jsonUrl(m_address);
    const QString label = QStringLiteral("POST %1").arg(json.path().mid(1));
    m_delugeId = 0;
    send(label,
         json,
         DelugeFingerprint::call(QStringLiteral("auth.check_session"), {}, ++m_delugeId),
         [this, json, label](const HttpAnswer &answer) {
             const DelugeFingerprint::Reading reading = DelugeFingerprint::read(answer);
             if (reading.fingerprint != Fingerprint::Itself) {
                 notItself(answer, label);
                 return;
             }
             m_result.outcome = Recognition::Outcome::Recognised;
             m_result.engine = EngineKind::Deluge;
             m_result.endpoint = json;
             m_result.error = ApiError();
             if (m_password.isEmpty()) {
                 m_result.wantsSignIn = true; // K7: deluge-web always wants its password
                 finish();
                 return;
             }
             delugeCall(QStringLiteral("auth.login"),
                        QJsonArray{m_password},
                        [this](const QJsonValue &result, const QString &) {
                            if (!result.toBool()) {
                                m_result.signInRefused = true;
                                finish();
                                return;
                            }
                            delugeSignedIn();
                        });
         });
}

void EngineRecognizer::delugeCall(const QString &method,
                                  const QJsonArray &params,
                                  std::function<void(const QJsonValue &, const QString &)> done)
{
    const QString label = QStringLiteral("POST json %1").arg(method);
    send(label,
         m_result.endpoint,
         DelugeFingerprint::call(method, params, ++m_delugeId),
         [this, done](const HttpAnswer &answer) {
             const DelugeFingerprint::Reading reading = DelugeFingerprint::read(answer);
             if (reading.fingerprint != Fingerprint::Itself) {
                 finish(); // recognised already; what follows only adds to it
                 return;
             }
             done(reading.result, reading.errorMessage);
         });
}

void EngineRecognizer::delugeSignedIn()
{
    delugeCall(
        QStringLiteral("web.connected"), {}, [this](const QJsonValue &connected, const QString &) {
            m_result.daemonConnected = connected.toBool();
            // K10: the daemons it knows, for the step that picks one.
            delugeCall(QStringLiteral("web.get_hosts"),
                       {},
                       [this](const QJsonValue &hosts, const QString &) {
                           m_result.daemons = DelugeFingerprint::hostNames(hosts);
                           if (!m_result.daemonConnected) {
                               finish();
                               return;
                           }
                           delugeCall(QStringLiteral("daemon.info"),
                                      {},
                                      [this](const QJsonValue &info, const QString &) {
                                          m_result.version = info.toString();
                                          finish();
                                      });
                       });
        });
}

void EngineRecognizer::finish()
{
    if (m_done)
        return;
    m_done = true;
    for (const QPointer<QNetworkReply> &reply : std::as_const(m_replies)) {
        if (reply)
            reply->abort();
    }
    m_replies.clear();
    const Recognition result = m_result;
    Q_EMIT finished(result);
}

} // namespace omaseed::core
