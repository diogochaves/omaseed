#include "core/ServerDetector.hpp"

#include <QProcess>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

namespace omaseed::core {

ServerDetector::ServerDetector(QObject *parent)
    : QObject(parent)
{}

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

void ServerDetector::start()
{
    m_finished = false;
    m_portDone = m_dockerDone = m_processDone = false;
    m_portFound = false;
    m_dockerName.clear();
    m_dockerImage.clear();
    m_dockerHostPort.clear();
    m_processFound = false;
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
    connect(m_dockerProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0) {
            const QString output = QString::fromUtf8(m_dockerProcess->readAllStandardOutput());
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
        }
        m_dockerDone = true;
        maybeFinish();
    });
    connect(m_dockerProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_dockerDone = true;
        maybeFinish();
    });

    m_dockerProcess->start(m_dockerCommand,
                           {QStringLiteral("ps"),
                            QStringLiteral("--format"),
                            QStringLiteral("{{.Names}} {{.Image}} {{.Ports}}")});
}

void ServerDetector::startProcessProbe()
{
    m_pgrepProcess = new QProcess(this);
    connect(m_pgrepProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0) {
            const QString output =
                QString::fromUtf8(m_pgrepProcess->readAllStandardOutput()).trimmed();
            if (!output.isEmpty()) {
                m_processFound = true;
                m_processName = QStringLiteral("qbittorrent-nox");
            }
        }
        m_processDone = true;
        maybeFinish();
    });
    connect(m_pgrepProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_processDone = true;
        maybeFinish();
    });

    m_pgrepProcess->start(m_pgrepCommand,
                          {QStringLiteral("-x"), QStringLiteral("qbittorrent-nox")});
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
        candidate.url = QStringLiteral("http://127.0.0.1:8080");
        candidate.label = tr("Use the server on this machine");
        candidate.detail = QStringLiteral("127.0.0.1:8080 · %1 running").arg(m_processName);
        candidates.append(candidate);
    }

    Q_EMIT finished(candidates);
}

} // namespace omaseed::core
