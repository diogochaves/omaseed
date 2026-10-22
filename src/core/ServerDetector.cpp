#include "core/ServerDetector.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>

#include <memory>

namespace omaseed::core {

namespace {

/// What an engine's config says about the address omaseed would use: the port (0 when it
/// does not say), whether remote control is on, and Transmission's RPC path.
struct RemoteConfig
{
    int port = 0;
    bool enabled = true;
    QString path; ///< Transmission's `rpc-url` when it isn't the default; empty otherwise
};

RemoteConfig readQbittorrentConfig(const QString &path)
{
    // qBittorrent writes Qt's own INI. Up to 5.2: `[Preferences]` `WebUI\Port=8080`. 5.3 moves
    // every Web UI key to a `[WebUI]` section (`Port=8080`) on its first start and never reads
    // the old ones again, so the new key wins and the old is the fallback.
    RemoteConfig config;
    const QSettings settings(path, QSettings::IniFormat);
    const auto value = [&settings](const QString &key, const QVariant &fallback) {
        return settings.value(QStringLiteral("WebUI/") + key,
                              settings.value(QStringLiteral("Preferences/WebUI/") + key, fallback));
    };
    config.port = value(QStringLiteral("Port"), 0).toInt();
    config.enabled = value(QStringLiteral("Enabled"), true).toBool();
    return config;
}

RemoteConfig readTransmissionConfig(const QString &path)
{
    // settings.json: "rpc-port": 9091, "rpc-enabled": true, "rpc-url": "/transmission/".
    RemoteConfig config;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return config;
    const QJsonObject settings = QJsonDocument::fromJson(file.readAll()).object();
    config.port = settings.value(QStringLiteral("rpc-port")).toInt(0);
    config.enabled = settings.value(QStringLiteral("rpc-enabled")).toBool(true);
    const QString url = settings.value(QStringLiteral("rpc-url")).toString();
    if (!url.isEmpty() && url != QLatin1String("/transmission/"))
        config.path =
            (url.endsWith(QLatin1Char('/')) ? url : url + QLatin1Char('/')) + QStringLiteral("rpc");
    return config;
}

RemoteConfig readDelugeConfig(const QString &path)
{
    // Deluge's config files hold two JSON objects one after the other: a header
    // ({"file": 2, "format": 1}) and the settings, here web.conf's "port": 8112.
    RemoteConfig config;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return config;
    const QString text = QString::fromUtf8(file.readAll());
    static const QRegularExpression twoObjects(
        QStringLiteral("^\\s*\\{[^{}]*\\}\\s*(\\{.*\\})\\s*$"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = twoObjects.match(text);
    const QJsonObject settings =
        QJsonDocument::fromJson((match.hasMatch() ? match.captured(1) : text).toUtf8()).object();
    config.port = settings.value(QStringLiteral("port")).toInt(0);
    return config;
}

RemoteConfig readConfig(EngineKind engine, const QStringList &paths)
{
    for (const QString &path : paths) {
        if (!QFile::exists(path))
            continue;
        switch (engine) {
        case EngineKind::Qbittorrent:
            return readQbittorrentConfig(path);
        case EngineKind::Transmission:
            return readTransmissionConfig(path);
        case EngineKind::Deluge:
            return readDelugeConfig(path);
        case EngineKind::Unknown:
            break;
        }
    }
    return {};
}

/// Which engine a process name (pgrep -l, cut at 15 characters by the kernel) belongs to.
EngineKind engineOfProcess(const QString &name)
{
    if (name.startsWith(QLatin1String("qbittorrent")))
        return EngineKind::Qbittorrent;
    if (name.startsWith(QLatin1String("transmission")))
        return EngineKind::Transmission;
    if (name.startsWith(QLatin1String("deluge")))
        return EngineKind::Deluge;
    return EngineKind::Unknown;
}

/// The name as people know it: the kernel keeps 15 characters of it.
QString fullProcessName(const QString &name)
{
    if (name == QLatin1String("transmission-da"))
        return QStringLiteral("transmission-daemon");
    if (name == QLatin1String("transmission-gt"))
        return QStringLiteral("transmission-gtk");
    return name;
}

} // namespace

ServerDetector::ServerDetector(QObject *parent)
    : QObject(parent)
{
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    for (const EngineKind engine : kKnownEngines)
        m_found[engine].port = usualPort(engine);
    m_found[EngineKind::Qbittorrent].configPaths = {
        config + QStringLiteral("/qBittorrent/qBittorrent.conf"),
        QDir::homePath() + QStringLiteral("/.var/app/org.qbittorrent.qBittorrent/config/"
                                          "qBittorrent/qBittorrent.conf"),
    };
    m_found[EngineKind::Transmission].configPaths = {
        config + QStringLiteral("/transmission-daemon/settings.json"),
        config + QStringLiteral("/transmission/settings.json"),
        QDir::homePath() + QStringLiteral("/.var/app/com.transmissionbt.Transmission/config/"
                                          "transmission/settings.json"),
    };
    m_found[EngineKind::Deluge].configPaths = {
        config + QStringLiteral("/deluge/web.conf"),
    };
}

ServerDetector::~ServerDetector() = default;

quint16 ServerDetector::usualPort(EngineKind engine)
{
    switch (engine) {
    case EngineKind::Transmission:
        return 9091;
    case EngineKind::Deluge:
        return 8112;
    case EngineKind::Qbittorrent:
    case EngineKind::Unknown:
        break;
    }
    return 8080;
}

int ServerDetector::runningProcesses(EngineKind engine) const
{
    return m_found.value(engine).processCount;
}

void ServerDetector::setConfigPaths(const QStringList &paths)
{
    setConfigPaths(EngineKind::Qbittorrent, paths);
}

void ServerDetector::setConfigPaths(EngineKind engine, const QStringList &paths)
{
    m_found[engine].configPaths = paths;
}

void ServerDetector::setProbePort(quint16 port)
{
    setProbePort(EngineKind::Qbittorrent, port);
}

void ServerDetector::setProbePort(EngineKind engine, quint16 port)
{
    m_found[engine].port = port;
}

quint16 ServerDetector::probePort(EngineKind engine) const
{
    return m_found.value(engine).port;
}

void ServerDetector::setDockerCommand(const QString &command)
{
    m_dockerCommand = command;
}

void ServerDetector::setPgrepCommand(const QString &command)
{
    m_pgrepCommand = command;
}

void ServerDetector::setProbeTimeoutMs(int ms)
{
    m_probeTimeoutMs = ms;
}

void ServerDetector::setCommandTimeoutMs(int ms)
{
    m_commandTimeoutMs = ms;
}

void ServerDetector::runCommand(QProcess *process,
                                const QString &program,
                                const QStringList &arguments,
                                std::function<void(QProcess *)> onFinished,
                                bool *done)
{
    // One way out per run, whichever comes first: finished, an error, or the timeout (a
    // `docker ps` against a wedged daemon never returns). Settling disconnects the process
    // from us, so a later signal from it (finished after errorOccurred, say) cannot count
    // twice, and hands it to deleteLater so repeated start() calls do not pile them up.
    QPointer<QProcess> guard(process);
    auto settled = std::make_shared<bool>(false);
    auto settle = [this, guard, done, settled](bool kill) {
        if (*settled || guard.isNull())
            return;
        *settled = true;
        disconnect(guard, nullptr, this, nullptr);
        if (kill && guard->state() != QProcess::NotRunning)
            guard->kill();
        guard->deleteLater();
        *done = true;
        maybeFinish();
    };

    connect(process,
            &QProcess::finished,
            this,
            [process, onFinished, settle](int exitCode, QProcess::ExitStatus status) {
                if (status == QProcess::NormalExit && exitCode == 0)
                    onFinished(process);
                settle(false);
            });
    connect(process, &QProcess::errorOccurred, this, [settle](QProcess::ProcessError) {
        settle(true);
    });
    QTimer::singleShot(m_commandTimeoutMs, this, [settle]() { settle(true); });

    process->start(program, arguments);
}

void ServerDetector::start()
{
    m_finished = false;
    m_dockerDone = m_processDone = false;
    for (Found &found : m_found) {
        found.portDone = false;
        found.portFound = false;
        found.dockerName.clear();
        found.dockerHostPort.clear();
        found.processCount = 0;
        found.processName.clear();
    }

    for (const EngineKind engine : kKnownEngines)
        startPortProbe(engine);
    startDockerProbe();
    startProcessProbe();
}

void ServerDetector::startPortProbe(EngineKind engine)
{
    auto *socket = new QTcpSocket(this);
    m_found[engine].socket = socket;
    const auto settle = [this, engine, socket](bool found) {
        Found &state = m_found[engine];
        if (state.portDone || state.socket != socket)
            return;
        state.portFound = found;
        // A later error must not undo the find if the peer closes before deleteLater ran.
        disconnect(socket, nullptr, this, nullptr);
        if (found)
            socket->disconnectFromHost();
        else
            socket->abort();
        socket->deleteLater();
        state.socket = nullptr;
        state.portDone = true;
        maybeFinish();
    };
    connect(socket, &QTcpSocket::connected, this, [settle]() { settle(true); });
    connect(socket, &QTcpSocket::errorOccurred, this, [settle](QAbstractSocket::SocketError) {
        settle(false);
    });

    // NEVER send anything on this socket beyond the bare TCP connect: 127.0.0.1:8080 may be
    // Diogo's real library, and CLAUDE.md permits only the connect-and-close probe. C14 E8
    // holds the other engines to the same.
    socket->connectToHost(QHostAddress(QHostAddress::LocalHost), m_found[engine].port);
    QTimer::singleShot(m_probeTimeoutMs, this, [settle]() { settle(false); });
}

void ServerDetector::startDockerProbe()
{
    m_dockerProcess = new QProcess(this);
    runCommand(
        m_dockerProcess,
        m_dockerCommand,
        {QStringLiteral("ps"),
         QStringLiteral("--format"),
         QStringLiteral("{{.Names}} {{.Image}} {{.Ports}}")},
        [this](QProcess *process) {
            const QString output = QString::fromUtf8(process->readAllStandardOutput());
            const auto lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.size() < 2)
                    continue;
                const QString name = parts.at(0);
                const QString image = parts.at(1);
                const QString ports =
                    parts.size() > 2 ? parts.mid(2).join(QLatin1Char(' ')) : QString();
                for (const EngineKind engine : kKnownEngines) {
                    const QString word = engineKindToString(engine);
                    Found &found = m_found[engine];
                    if (!found.dockerName.isEmpty() || !(name.contains(word, Qt::CaseInsensitive) ||
                                                         image.contains(word, Qt::CaseInsensitive)))
                        continue;
                    found.dockerName = name;
                    // Look for a "...:HOSTPORT->8080/tcp" mapping of the engine's own port.
                    const QRegularExpression portRe(
                        QStringLiteral(":(\\d+)->%1/tcp").arg(usualPort(engine)));
                    const auto match = portRe.match(ports);
                    if (match.hasMatch())
                        found.dockerHostPort = match.captured(1);
                    break;
                }
            }
        },
        &m_dockerDone);
}

void ServerDetector::startProcessProbe()
{
    m_pgrepProcess = new QProcess(this);
    // pgrep takes an extended regex, matched against the kernel's 15-character name. `-l`
    // adds the matched name after the pid.
    runCommand(
        m_pgrepProcess,
        m_pgrepCommand,
        {QStringLiteral("-l"),
         QStringLiteral("-x"),
         QStringLiteral("qbittorrent(-nox)?|transmission-(da|daemon|gt|gtk|qt)|deluged|deluge-web|"
                        "deluge-gtk|deluge")},
        [this](QProcess *process) {
            const QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
            const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.isEmpty())
                    continue;
                // A bare pid (pgrep without -l): qBittorrent's, as before.
                const QString name = parts.size() > 1 ? fullProcessName(parts.at(1))
                                                      : QStringLiteral("qbittorrent-nox");
                const EngineKind engine = engineOfProcess(name);
                if (engine == EngineKind::Unknown)
                    continue;
                Found &found = m_found[engine];
                ++found.processCount;
                // deluge-web is what omaseed talks to; a daemon alone is named after it.
                if (found.processName.isEmpty() || name == QLatin1String("deluge-web"))
                    found.processName = name;
            }
        },
        &m_processDone);
}

void ServerDetector::maybeFinish()
{
    if (m_finished || !m_dockerDone || !m_processDone)
        return;
    for (const Found &found : std::as_const(m_found)) {
        if (!found.portDone)
            return;
    }
    m_finished = true;

    QList<DetectedServer> candidates;
    for (const EngineKind engine : kKnownEngines) {
        DetectedServer candidate;
        if (candidateFor(engine, &candidate))
            candidates.append(candidate);
    }
    Q_EMIT finished(candidates);
}

bool ServerDetector::candidateFor(EngineKind engine, DetectedServer *candidate) const
{
    const Found found = m_found.value(engine);
    const QString name = engineDisplayName(engine);
    candidate->engine = engineKindToString(engine);
    candidate->label = tr("Use %1 on this machine").arg(name);

    if (!found.dockerName.isEmpty()) {
        candidate->kind = QStringLiteral("docker");
        const QString port = found.dockerHostPort.isEmpty() ? QString::number(usualPort(engine))
                                                            : found.dockerHostPort;
        candidate->url = QStringLiteral("http://127.0.0.1:%1").arg(port);
        candidate->detail =
            QStringLiteral("127.0.0.1:%1 · Docker container %2").arg(port, found.dockerName);
        return true;
    }
    if (found.portFound) {
        candidate->kind = QStringLiteral("port");
        candidate->url = QStringLiteral("http://127.0.0.1:%1").arg(found.port);
        candidate->detail = QStringLiteral("127.0.0.1:%1").arg(found.port);
        return true;
    }
    if (found.processCount == 0)
        return false;

    candidate->kind = QStringLiteral("process");
    const RemoteConfig config = readConfig(engine, found.configPaths);
    const QString port = QString::number(config.port > 0 ? config.port : usualPort(engine));
    candidate->url = QStringLiteral("http://127.0.0.1:%1%2").arg(port, config.path);
    QString detail = QStringLiteral("127.0.0.1:%1 · %2 running").arg(port, found.processName);
    switch (engine) {
    case EngineKind::Qbittorrent:
        // The desktop client ships with its Web UI off, and omaseed can only talk to that.
        if (found.processName == QLatin1String("qbittorrent") && !config.enabled)
            detail = QStringLiteral("127.0.0.1:%1 · qbittorrent running, its Web UI is off: turn "
                                    "it on in Tools › Options › Web UI")
                         .arg(port);
        break;
    case EngineKind::Transmission:
        if (!config.enabled)
            detail = QStringLiteral("127.0.0.1:%1 · %2 running, its remote access is off: turn "
                                    "it on in its preferences (Remote)")
                         .arg(port, found.processName);
        break;
    case EngineKind::Deluge:
        // omaseed talks to deluge-web, not to the daemon.
        if (found.processName != QLatin1String("deluge-web"))
            detail = QStringLiteral("127.0.0.1:%1 · %2 running, deluge-web isn't: start "
                                    "deluge-web")
                         .arg(port, found.processName);
        break;
    case EngineKind::Unknown:
        break;
    }
    candidate->detail = detail;
    return true;
}

} // namespace omaseed::core
