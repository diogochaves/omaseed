#pragma once

// ServerDetector — first-run detection of a local qBittorrent (T4.6, spec v2 §5 Fig. 15).
//
// Three probes run in parallel, purely to answer "is there something on this machine the
// user probably means to talk to": a TCP connect-and-close against 127.0.0.1:8080 (CLAUDE.md
// is explicit that this is the only permitted contact with the real library port — never an
// HTTP request, never a login), `docker ps` for a container that looks like qBittorrent, and
// a running qbittorrent-nox/qbittorrent process. None of this ever probes the dev servers on
// :8081/:8082 in production; the port is only injectable so the test does not have to bind
// the real 8080.
//
// finished() fires once, after every probe has answered (or timed out: the commands too,
// after setCommandTimeoutMs), with at most one candidate: the docker container, else the
// open port, else the running process.

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

QT_BEGIN_NAMESPACE
class QProcess;
class QTcpSocket;
QT_END_NAMESPACE

namespace omaseed::core {

/// One thing ServerDetector found. `kind` is "port", "docker" or "process"; `url` is filled
/// only when a usable one is known (the port probe and a docker port mapping); `label`/
/// `detail` are the exact strings Fig. 15 prints for the "use the server on this machine"
/// row (the detail grows a `qBittorrent 5.2.3` clause only after a caller's own test, which
/// is App's job, not the detector's).
struct DetectedServer
{
    QString kind;
    QString url;
    QString label;
    QString detail;

    bool operator==(const DetectedServer &other) const = default;
};

class ServerDetector : public QObject
{
    Q_OBJECT

public:
    explicit ServerDetector(QObject *parent = nullptr);
    ~ServerDetector() override;

    /// Starts all three probes. Safe to call again once `finished` has fired.
    void start();

    /// Test-only: the TCP port to probe instead of 8080, so the test can point this at a
    /// fake `QTcpServer` on a random port rather than the real library.
    void setProbePort(quint16 port);
    quint16 probePort() const { return m_probePort; }

    /// Test-only: the command names used for the docker and pgrep probes, so the test can
    /// point them at a fake script rather than relying on `docker`/`pgrep` being installed
    /// (and rather than ever matching this machine's real processes).
    void setDockerCommand(const QString &command);
    void setPgrepCommand(const QString &command);

    /// Test-only: the connect timeout, default 1000 ms.
    void setProbeTimeoutMs(int ms);
    /// Test-only: how long `docker ps` and `pgrep` may run before they are killed and
    /// counted as "nothing found", default 3000 ms.
    void setCommandTimeoutMs(int ms);

Q_SIGNALS:
    void finished(QList<omaseed::core::DetectedServer> candidates);

private:
    void startPortProbe();
    void startDockerProbe();
    void startProcessProbe();
    /// Starts `process` and marks `*done` exactly once, on finish, error or timeout;
    /// `onFinished` reads its output only after a clean exit 0.
    void runCommand(QProcess *process,
                    const QString &program,
                    const QStringList &arguments,
                    std::function<void(QProcess *)> onFinished,
                    bool *done);
    void maybeFinish();

    QTcpSocket *m_socket = nullptr;
    QProcess *m_dockerProcess = nullptr;
    QProcess *m_pgrepProcess = nullptr;

    quint16 m_probePort = 8080;
    QString m_dockerCommand = QStringLiteral("docker");
    QString m_pgrepCommand = QStringLiteral("pgrep");
    int m_probeTimeoutMs = 1000;
    int m_commandTimeoutMs = 3000;

    bool m_portDone = false;
    bool m_dockerDone = false;
    bool m_processDone = false;
    bool m_finished = false;

    bool m_portFound = false;
    QString m_dockerName;
    QString m_dockerImage;
    QString m_dockerHostPort;
    bool m_processFound = false;
    QString m_processName;
};

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::DetectedServer)
