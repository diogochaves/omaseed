#pragma once

// EngineRecognizer — which engine answers at an address (C25 K4, K5, K6, K8, K10; C14 E8).
//
// A server's test recognises its engine before it signs in. The address picks which engine
// is asked first (order(), K5): port 9091 or a /transmission path, Transmission; port 8112 or
// a /json path, Deluge; anything else, qBittorrent; the other two follow in K2's order. Each
// is asked once, without credentials, in its own way (EngineFingerprint): qBittorrent's
// app/version, Transmission's 409 and session id, deluge-web's auth.check_session. The first
// that answers as itself is the engine; the rest are not asked. With one engine picked (K4's
// chips) only that one is asked.
//
// After that, for the two engines this build doesn't drive yet, just enough to name them:
// Transmission's session_get (its version; the login, Basic, when one was given), deluge-web's
// auth.login with the password, web.connected, daemon.get_version and, when it has no daemon,
// web.get_hosts. qBittorrent's signed-in probe is its engine's (QbtEngine::probe), as before.
//
// A transport failure (nothing listening, no answer in time, a certificate the system refuses
// and the server's pin doesn't name) ends it at once: another engine would meet the same.
// Every request leaves through one QNetworkAccessManager of its own, with a cookie jar for
// deluge-web's session; nothing is kept after `finished`.

#include "core/ApiError.hpp"
#include "core/engines/EngineFingerprint.hpp"
#include "core/engines/EngineKind.hpp"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>

#include <chrono>
#include <functional>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

namespace omaseed::core {

/// What a recognition found.
struct Recognition
{
    enum class Outcome {
        Recognised,   ///< `engine` answered as itself
        Unrecognised, ///< something answered, as none of the engines asked (K8: say so)
        Failed,       ///< the transport failed first: `error`
    };
    Outcome outcome = Outcome::Unrecognised;
    EngineKind engine = EngineKind::Unknown;
    QString version;            ///< the engine's own ("v5.3.0", "4.1.3", "2.2.0"); empty unknown
    QString apiVersion;         ///< Transmission's RPC version ("6.0.0"); empty otherwise
    QUrl endpoint;              ///< where it answered (K6: Transmission's RPC, deluge-web's /json)
    bool wantsSignIn = false;   ///< it asks for a login and none was given
    bool signInRefused = false; ///< it turned down the login that was given
    /// Deluge (K10): whether deluge-web has a daemon, and the daemons it knows ("host:port").
    bool daemonConnected = false;
    QStringList daemons;
    QList<EngineKind> asked; ///< in the order they were asked
    ApiError error;          ///< Failed: why; Unrecognised: the last answer's status

    bool recognised() const { return outcome == Outcome::Recognised; }
};

class EngineRecognizer : public QObject
{
    Q_OBJECT

public:
    explicit EngineRecognizer(QObject *parent = nullptr);
    ~EngineRecognizer() override;

    /// K5: the engines to ask about `address`, first the one it suggests.
    static QList<EngineKind> order(const QUrl &address);

    void setTimeout(std::chrono::milliseconds timeout);
    /// The certificate to accept although the system doesn't trust it (CertificatePin form).
    void setPinnedCertificate(const QString &sha256);
    /// The login for the steps after recognition: Transmission's Basic, deluge-web's password
    /// (`username` ignored). Never sent to qBittorrent, nor before an engine is recognised.
    void setCredentials(const QString &username, const QString &password);

    /// Asks the engines about `address`; `only` other than Unknown asks that one alone.
    /// Answers with `finished`, exactly once. Calling it again starts over.
    void start(const QUrl &address, EngineKind only = EngineKind::Unknown);

Q_SIGNALS:
    void finished(const omaseed::core::Recognition &recognition);

private:
    using Answered = std::function<void(const HttpAnswer &)>;
    /// One request; `answered` runs for any HTTP answer, a transport failure ends the run.
    void send(const QString &label, const QUrl &url, const QByteArray &body, Answered answered);
    void askNext();
    void askQbittorrent();
    void askTransmission();
    void askTransmissionAgain(const QByteArray &sessionId);
    void askDeluge();
    void delugeCall(const QString &method,
                    const QJsonArray &params,
                    std::function<void(const QJsonValue &result, const QString &error)> done);
    void delugeSignedIn();
    void notItself(const HttpAnswer &answer, const QString &label);
    void finish();

    QNetworkAccessManager *m_nam = nullptr;
    std::chrono::milliseconds m_timeout{8000};
    QString m_pinned;
    QString m_username;
    QString m_password;
    QUrl m_address;
    QList<EngineKind> m_queue;
    Recognition m_result;
    QList<QPointer<QNetworkReply>> m_replies;
    quint64 m_run = 0;
    int m_delugeId = 0;
    bool m_done = true;
};

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::Recognition)
