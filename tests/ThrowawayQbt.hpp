#pragma once

// A qbittorrent-nox of the test's own: a fresh profile in a temporary directory, Web UI and
// peer ports picked free at start, a login made up for the run. For the tests that change
// what the :8081 playground and the :8082 strict twin must keep (the Web UI port, its login,
// local auth, the API key): whatever they do dies with this server. Never on 8080–8083, 8090
// or 8091, whatever the kernel hands out.
//
//   ThrowawayQbt qbt;
//   if (!qbt.start())
//       QSKIP(qPrintable(qbt.skipReason()));   // no qbittorrent-nox here
//   QbtClient client; client.setBaseUrl(qbt.url());
//   client.setCredentials(qbt.username(), qbt.password());
//
// The destructor stops the process (SIGTERM, then SIGKILL) and the profile goes with the
// temporary directory, also when a QVERIFY returns early.

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QPasswordDigestor>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>

class ThrowawayQbt
{
public:
    ThrowawayQbt() = default;
    ThrowawayQbt(const ThrowawayQbt &) = delete;
    ThrowawayQbt &operator=(const ThrowawayQbt &) = delete;
    ~ThrowawayQbt() { stop(); }

    /// Writes the profile and starts the server; false (with `skipReason()` or `error()`)
    /// when it cannot. `localHostAuth` false lets 127.0.0.1 in without a login.
    bool start(bool localHostAuth = true)
    {
        const QString binary = QStandardPaths::findExecutable(QStringLiteral("qbittorrent-nox"));
        if (binary.isEmpty()) {
            m_skip = QStringLiteral("qbittorrent-nox is not installed");
            return false;
        }
        if (!m_profile.isValid()) {
            m_error = QStringLiteral("no temporary directory");
            return false;
        }
        m_binary = binary;
        m_webPort = freePort();
        m_btPort = freePort();
        m_username = QStringLiteral("omaseed-%1").arg(randomText(4));
        m_password = randomText(12);
        if (m_webPort == 0 || m_btPort == 0 || !writeConf(localHostAuth))
            return false;
        return launch();
    }

    /// Stops the server and starts it again on the same profile (what it saved stays).
    bool restart()
    {
        stop();
        return launch();
    }

    void stop()
    {
        if (!m_process)
            return;
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(10000)) {
                m_process->kill();
                m_process->waitForFinished(3000);
            }
        }
        m_process.reset();
    }

    QUrl url() const { return urlOn(m_webPort); }
    static QUrl urlOn(int port) { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port)); }
    int webPort() const { return m_webPort; }
    QString username() const { return m_username; }
    QString password() const { return m_password; }
    QString profilePath() const { return m_profile.path(); }
    QString downloadsPath() const { return m_profile.path() + QStringLiteral("/downloads"); }
    QString skipReason() const { return m_skip; }
    QString error() const { return m_error; }
    QString log() const
    {
        return QString::fromUtf8(m_process ? m_process->readAll() : QByteArray());
    }

    /// A port nothing listens on now and that is none of the machine's known servers.
    static int freePort()
    {
        for (int attempt = 0; attempt < 50; ++attempt) {
            QTcpServer probe;
            if (!probe.listen(QHostAddress::LocalHost, 0))
                continue;
            const int port = probe.serverPort();
            if (!reserved(port))
                return port;
        }
        return 0;
    }

    /// True when something accepts a connection on 127.0.0.1:`port`.
    static bool answers(int port)
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, quint16(port));
        const bool connected = socket.waitForConnected(1000);
        socket.abort();
        return connected;
    }

    /// Waits until 127.0.0.1:`port` answers (or stops answering, with `up` false).
    static bool waitFor(int port, bool up, int timeoutMs = 15000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (answers(port) == up)
                return true;
            QTest::qWait(100); // the event loop runs: requests on their way still go out
        }
        return false;
    }

private:
    static bool reserved(int port)
    {
        return (port >= 8080 && port <= 8083) || port == 8090 || port == 8091;
    }

    static QString randomText(int bytes)
    {
        QByteArray raw(bytes, Qt::Uninitialized);
        for (char &c : raw)
            c = char(QRandomGenerator::global()->bounded(256));
        return QString::fromLatin1(raw.toHex());
    }

    bool writeConf(bool localHostAuth)
    {
        const QString dir = m_profile.path() + QStringLiteral("/qBittorrent/config");
        if (!QDir().mkpath(dir) || !QDir().mkpath(downloadsPath())) {
            m_error = QStringLiteral("cannot make the profile in %1").arg(m_profile.path());
            return false;
        }
        // qBittorrent's own format: base64(salt):base64(PBKDF2-SHA512, 100000 rounds, 64 bytes).
        QByteArray salt(16, Qt::Uninitialized);
        for (char &c : salt)
            c = char(QRandomGenerator::global()->bounded(256));
        const QByteArray key = QPasswordDigestor::deriveKeyPbkdf2(
            QCryptographicHash::Sha512, m_password.toUtf8(), salt, 100000, 64);
        const QByteArray hash = salt.toBase64() + ':' + key.toBase64();

        QFile conf(dir + QStringLiteral("/qBittorrent.conf"));
        if (!conf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_error = QStringLiteral("cannot write %1").arg(conf.fileName());
            return false;
        }
        // Everything else at qBittorrent's defaults (CSRF and host header checks on), as on a
        // user's own server. Peers on loopback only, no DHT, PeX, LSD or port forwarding: the
        // server is never reachable from the LAN and never talks to the internet.
        const QString text =
            QStringLiteral("[LegalNotice]\nAccepted=true\n\n"
                           "[Preferences]\n"
                           "WebUI\\Enabled=true\n"
                           "WebUI\\Address=127.0.0.1\n"
                           "WebUI\\Port=%1\n"
                           "WebUI\\Username=%2\n"
                           "WebUI\\Password_PBKDF2=\"@ByteArray(%3)\"\n"
                           "WebUI\\LocalHostAuth=%4\n"
                           "WebUI\\UseUPnP=false\n"
                           "General\\Locale=en\n\n"
                           "[BitTorrent]\n"
                           "Session\\Port=%5\n"
                           "Session\\Interface=lo\n"
                           "Session\\InterfaceName=lo\n"
                           "Session\\InterfaceAddress=127.0.0.1\n"
                           "Session\\DHTEnabled=false\n"
                           "Session\\PeXEnabled=false\n"
                           "Session\\LSDEnabled=false\n"
                           "Session\\DefaultSavePath=%6\n\n"
                           "[Network]\nPortForwardingEnabled=false\n")
                .arg(m_webPort)
                .arg(m_username, QString::fromLatin1(hash))
                .arg(localHostAuth ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(m_btPort)
                .arg(downloadsPath());
        conf.write(text.toUtf8());
        return true;
    }

    bool launch()
    {
        m_process = std::make_unique<QProcess>();
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        m_process->start(m_binary,
                         {QStringLiteral("--profile=%1").arg(m_profile.path()),
                          QStringLiteral("--confirm-legal-notice")});
        if (!m_process->waitForStarted(5000)) {
            m_error =
                QStringLiteral("qbittorrent-nox did not start: %1").arg(m_process->errorString());
            return false;
        }
        if (!waitFor(m_webPort, true)) {
            m_error = QStringLiteral("qbittorrent-nox did not answer on :%1: %2")
                          .arg(m_webPort)
                          .arg(log());
            return false;
        }
        return true;
    }

    QTemporaryDir m_profile;
    std::unique_ptr<QProcess> m_process;
    QString m_binary;
    int m_webPort = 0;
    int m_btPort = 0;
    QString m_username;
    QString m_password;
    QString m_skip;
    QString m_error;
};
