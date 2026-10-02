#include "core/ServerDetector.hpp"

#include <QDir>
#include <QFile>
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

/// What qBittorrent's config says about its Web UI: the port (0 when it does not say) and
/// whether it is on (the desktop client's switch; qbittorrent-nox always serves it).
struct WebUiConfig
{
    int port = 0;
    bool enabled = true;
};

WebUiConfig readWebUiConfig(const QStringList &paths)
{
    WebUiConfig config;
    for (const QString &path : paths) {
        if (!QFile::exists(path))
            continue;
        // qBittorrent writes Qt's own INI: `[Preferences]` `WebUI\Port=8080`.
        const QSettings settings(path, QSettings::IniFormat);
        config.port = settings.value(QStringLiteral("Preferences/WebUI/Port"), 0).toInt();
        config.enabled = settings.value(QStringLiteral("Preferences/WebUI/Enabled"), true).toBool();
        break;
    }
    return config;
}

} // namespace

ServerDetector::ServerDetector(QObject *parent)
    : QObject(parent)
    , m_configPaths{
          QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
              QStringLiteral("/qBittorrent/qBittorrent.conf"),
          QDir::homePath() +
              QStringLiteral("/.var/app/org.qbittorrent.qBittorrent/config/qBittorrent/"
                             "qBittorrent.conf"),
      }
{}

void ServerDetector::setConfigPaths(const QStringList &paths)
{
    m_configPaths = paths;
}

ServerDetector::~ServerDetector() = default;

void ServerDetector::setProbePort(quint16 port)
{
    m_probePort = port;
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
    m_portDone = m_dockerDone = m_processDone = false;
    m_portFound = false;
    m_dockerName.clear();
    m_dockerImage.clear();
    m_dockerHostPort.clear();
    m_processFound = false;
    m_processCount = 0;
    m_processName.clear();

    startPortProbe();
    startDockerProbe();
    startProcessProbe();
}

void ServerDetector::startPortProbe()
{
    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected, this, [this]() {
        m_portFound = true;
        // The errorOccurred handler below must not undo the find if the peer closes
        // before deleteLater has run.
        disconnect(m_socket, nullptr, this, nullptr);
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
        m_portDone = true;
        maybeFinish();
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        m_portFound = false;
        if (m_socket) {
            m_socket->deleteLater();
            m_socket = nullptr;
        }
        m_portDone = true;
        maybeFinish();
    });

    // NEVER send anything on this socket beyond the bare TCP connect: 127.0.0.1:8080 may be
    // Diogo's real library, and CLAUDE.md permits only the connect-and-close probe.
    m_socket->connectToHost(QHostAddress(QHostAddress::LocalHost), m_probePort);

    QTimer::singleShot(m_probeTimeoutMs, this, [this]() {
        if (m_portDone || !m_socket)
            return;
        m_portFound = false;
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
        m_portDone = true;
        maybeFinish();
    });
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
                if (name.contains(QStringLiteral("qbittorrent"), Qt::CaseInsensitive) ||
                    image.contains(QStringLiteral("qbittorrent"), Qt::CaseInsensitive)) {
                    m_dockerName = name;
                    m_dockerImage = image;
                    // Look for a "...:HOSTPORT->8080/tcp" mapping.
                    static const QRegularExpression portRe(QStringLiteral(":(\\d+)->8080/tcp"));
                    const auto match = portRe.match(ports);
                    if (match.hasMatch())
                        m_dockerHostPort = match.captured(1);
                    break;
                }
            }
        },
        &m_dockerDone);
}

void ServerDetector::startProcessProbe()
{
    m_pgrepProcess = new QProcess(this);
    // pgrep takes an extended regex: the headless daemon or the desktop client. `-l` adds
    // the matched name after the pid.
    runCommand(
        m_pgrepProcess,
        m_pgrepCommand,
        {QStringLiteral("-l"), QStringLiteral("-x"), QStringLiteral("qbittorrent(-nox)?")},
        [this](QProcess *process) {
            const QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
            if (output.isEmpty())
                return;
            m_processFound = true;
            m_processCount = int(output.count(QLatin1Char('\n'))) + 1;
            const QStringList first =
                output.section(QLatin1Char('\n'), 0, 0).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            m_processName = first.size() > 1 ? first.at(1) : QStringLiteral("qbittorrent-nox");
        },
        &m_processDone);
}

void ServerDetector::maybeFinish()
{
    if (m_finished || !m_portDone || !m_dockerDone || !m_processDone)
        return;
    m_finished = true;

    QList<DetectedServer> candidates;

    if (!m_dockerName.isEmpty()) {
        DetectedServer candidate;
        candidate.kind = QStringLiteral("docker");
        const QString port = m_dockerHostPort.isEmpty() ? QStringLiteral("8080") : m_dockerHostPort;
        candidate.url = QStringLiteral("http://127.0.0.1:%1").arg(port);
        candidate.label = tr("Use the server on this machine");
        candidate.detail =
            QStringLiteral("127.0.0.1:%1 · Docker container %2").arg(port, m_dockerName);
        candidates.append(candidate);
    } else if (m_portFound) {
        DetectedServer candidate;
        candidate.kind = QStringLiteral("port");
        candidate.url = QStringLiteral("http://127.0.0.1:%1").arg(m_probePort);
        candidate.label = tr("Use the server on this machine");
        candidate.detail = QStringLiteral("127.0.0.1:%1").arg(m_probePort);
        candidates.append(candidate);
    } else if (m_processFound) {
        DetectedServer candidate;
        candidate.kind = QStringLiteral("process");
        const WebUiConfig config = readWebUiConfig(m_configPaths);
        const QString port = QString::number(config.port > 0 ? config.port : 8080);
        candidate.url = QStringLiteral("http://127.0.0.1:%1").arg(port);
        candidate.label = tr("Use the server on this machine");
        // The desktop client ships with its Web UI off, and omaseed can only talk to that.
        const bool desktop = m_processName == QLatin1String("qbittorrent");
        candidate.detail =
            desktop && !config.enabled
                ? QStringLiteral("127.0.0.1:%1 · qbittorrent running, its Web UI is off: turn "
                                 "it on in Tools › Options › Web UI")
                      .arg(port)
                : QStringLiteral("127.0.0.1:%1 · %2 running").arg(port, m_processName);
        candidates.append(candidate);
    }

    Q_EMIT finished(candidates);
}

} // namespace omaseed::core
