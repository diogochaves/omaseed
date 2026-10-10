#include "integration/HyprlandEvents.hpp"

#include <QLocalSocket>

#include <algorithm>

namespace omaseed::integration {

namespace {

constexpr int kFirstRetryMs = 1000;
constexpr int kLongestRetryMs = 30000;

} // namespace

QString HyprlandEvents::defaultSocketPath()
{
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (signature.isEmpty() || runtime.isEmpty())
        return {};
    return QStringLiteral("%1/hypr/%2/.socket2.sock").arg(runtime, signature);
}

HyprlandEvents::HyprlandEvents(const QString &socketPath, QObject *parent)
    : QObject(parent)
    , m_path(socketPath)
{
    if (m_path.isEmpty())
        return;
    m_socket = new QLocalSocket(this);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &HyprlandEvents::connectToHyprland);
    connect(m_socket, &QLocalSocket::connected, this, [this] {
        m_retryMs = 0;
        Q_EMIT connected();
    });
    connect(m_socket, &QLocalSocket::readyRead, this, &HyprlandEvents::readLines);
    connect(m_socket, &QLocalSocket::disconnected, this, &HyprlandEvents::retryLater);
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        // A refused or missing socket never connects, so no `disconnected` follows.
        if (m_socket->state() == QLocalSocket::UnconnectedState)
            retryLater();
    });
    connectToHyprland();
}

bool HyprlandEvents::isConnected() const
{
    return m_socket && m_socket->state() == QLocalSocket::ConnectedState;
}

void HyprlandEvents::connectToHyprland()
{
    if (m_socket->state() != QLocalSocket::UnconnectedState)
        return;
    m_pending.clear();
    m_socket->connectToServer(m_path, QIODevice::ReadOnly);
}

void HyprlandEvents::readLines()
{
    m_pending += m_socket->readAll();
    qsizetype end;
    while ((end = m_pending.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_pending.left(end));
        m_pending.remove(0, end + 1);
        const qsizetype split = line.indexOf(QLatin1String(">>"));
        if (split < 0)
            continue;
        const QString name = line.left(split);
        Q_EMIT event(name, line.mid(split + 2));
        if (name == QLatin1String("configreloaded"))
            Q_EMIT configReloaded();
    }
}

void HyprlandEvents::retryLater()
{
    if (m_retry.isActive())
        return;
    m_retryMs = m_retryMs == 0 ? kFirstRetryMs : std::min(m_retryMs * 2, kLongestRetryMs);
    m_retry.start(m_retryMs);
}

} // namespace omaseed::integration
