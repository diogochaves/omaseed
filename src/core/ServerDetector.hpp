#pragma once

// ServerDetector — first-run detection of a torrent server on this machine (T4.6, spec v2 §5
// Fig. 15; DECISIONS C14 E8, C25 K1 and K2).
//
// Three kinds of probe run in parallel, purely to answer "is there something on this machine
// the user probably means to talk to", for each engine omaseed knows:
//   * a TCP connect-and-close against 127.0.0.1 on each engine's usual port: qBittorrent's
//     8080, Transmission's 9091, deluge-web's 8112. Never an HTTP request, never a login
//     (CLAUDE.md: the connect is the only permitted contact with the real library's port,
//     and C14 E8 keeps every engine to it until the row is picked);
//   * `docker ps` for a container whose name or image names an engine;
//   * the running processes (pgrep): qbittorrent-nox/qbittorrent, transmission-daemon/-gtk/
//     -qt, deluged/deluge-web/deluge-gtk.
// Each engine's own config file (read, never written) gives a running process its port:
// qBittorrent's Web UI port and switch, Transmission's `rpc-port` and `rpc-enabled`,
// deluge-web's `port`. None of this ever probes the dev servers on :8081/:8082 in production;
// the ports are only injectable so the tests do not have to bind the real ones.
//
// finished() fires once, after every probe has answered (or timed out: the commands too,
// after setCommandTimeoutMs), with at most one candidate per engine (K2), in the order
// qBittorrent, Transmission, Deluge: for each, the docker container, else the open port,
// else the running process.
//
// `runningProcesses()` counts every qbittorrent/qbittorrent-nox process pgrep matched, so
// first run can say when there is more than the one it offers (Q26: a second instance with
// its own `--profile` is never a candidate; the user adds it in Settings › Servers). Q26
// holds within each engine (`runningProcesses(engine)`).

#include "core/engines/EngineKind.hpp"

#include <QHash>
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

/// One thing ServerDetector found. `kind` is "port", "docker" or "process"; `engine` the
/// engine it points to ("qbittorrent", "transmission", "deluge"); `url` is filled only when a
/// usable one is known; `label`/`detail` are the strings the first-run row prints (the detail
/// grows a version only after a caller's own test, which is App's job, not the detector's).
struct DetectedServer
{
    QString kind;
    QString url;
    QString label;
    QString detail;
    QString engine = QStringLiteral("qbittorrent");

    bool operator==(const DetectedServer &other) const = default;
};

class ServerDetector : public QObject
{
    Q_OBJECT

public:
    explicit ServerDetector(QObject *parent = nullptr);
    ~ServerDetector() override;

    /// Starts every probe. Safe to call again once `finished` has fired.
    void start();

    /// How many qbittorrent/qbittorrent-nox processes the process probe saw; read it once
    /// `finished` has fired (0 before, and when pgrep found none or could not run).
    int runningProcesses() const { return runningProcesses(EngineKind::Qbittorrent); }
    /// The same for `engine`'s processes (a daemon and its web front count apart).
    int runningProcesses(EngineKind engine) const;

    /// The usual port of `engine`: 8080, 9091, 8112.
    static quint16 usualPort(EngineKind engine);

    /// Test-only: the TCP port to probe for qBittorrent instead of 8080 (setProbePort(port)),
    /// or for `engine` instead of its usual one, so a test points it at a fake `QTcpServer`
    /// on a random port rather than the real library.
    void setProbePort(quint16 port);
    void setProbePort(EngineKind engine, quint16 port);
    quint16 probePort() const { return probePort(EngineKind::Qbittorrent); }
    quint16 probePort(EngineKind engine) const;

    /// Test-only: the command names used for the docker and pgrep probes, so the test can
    /// point them at a fake script rather than relying on `docker`/`pgrep` being installed
    /// (and rather than ever matching this machine's real processes).
    void setDockerCommand(const QString &command);
    void setPgrepCommand(const QString &command);

    /// Test-only: the config files to read for a running process's port, first existing one
    /// wins. qBittorrent's (setConfigPaths(paths)) default to `~/.config/qBittorrent/
    /// qBittorrent.conf` and the Flatpak's copy; Transmission's to its daemon's and its
    /// desktop clients' `settings.json`; Deluge's to `~/.config/deluge/web.conf`. A test
    /// points them at its own files (or none).
    void setConfigPaths(const QStringList &paths);
    void setConfigPaths(EngineKind engine, const QStringList &paths);

    /// Test-only: the connect timeout, default 1000 ms.
    void setProbeTimeoutMs(int ms);
    /// Test-only: how long `docker ps` and `pgrep` may run before they are killed and
    /// counted as "nothing found", default 3000 ms.
    void setCommandTimeoutMs(int ms);

Q_SIGNALS:
    void finished(QList<omaseed::core::DetectedServer> candidates);

private:
    /// One engine's findings.
    struct Found
    {
        quint16 port = 0;
        QTcpSocket *socket = nullptr;
        bool portDone = false;
        bool portFound = false;
        QString dockerName;
        QString dockerHostPort;
        int processCount = 0;
        QString processName;
        QStringList configPaths;
    };

    void startPortProbe(EngineKind engine);
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
    /// The candidate for `engine`, if anything pointed to it.
    bool candidateFor(EngineKind engine, DetectedServer *candidate) const;

    QHash<EngineKind, Found> m_found;
    QProcess *m_dockerProcess = nullptr;
    QProcess *m_pgrepProcess = nullptr;

    QString m_dockerCommand = QStringLiteral("docker");
    QString m_pgrepCommand = QStringLiteral("pgrep");
    int m_probeTimeoutMs = 1000;
    int m_commandTimeoutMs = 3000;

    bool m_dockerDone = false;
    bool m_processDone = false;
    bool m_finished = false;
};

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::DetectedServer)
