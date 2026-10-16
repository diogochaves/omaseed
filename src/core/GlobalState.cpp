#include "core/GlobalState.hpp"

#include "core/Format.hpp"

namespace omaseed::core {

namespace {

/// Assigns and fires the NOTIFY only when the value really moved: the hero binds to these
/// and a poll every 1.5 s would otherwise repaint everything.
template<class T, class Owner, class Signal>
void assign(T &field, const T &value, Owner *owner, Signal signal)
{
    if (field == value)
        return;
    field = value;
    Q_EMIT(owner->*signal)();
}

/// Same, but only for figures the poll carried.
template<class T, class Owner, class Signal>
void take(const std::optional<T> &value, T &field, Owner *owner, Signal signal)
{
    if (value)
        assign(field, *value, owner, signal);
}

} // namespace

GlobalState::GlobalState(QObject *parent)
    : QObject(parent)
{}

QString GlobalState::dlSpeedText() const
{
    return Format::speed(m_dlInfoSpeed);
}

QString GlobalState::upSpeedText() const
{
    return Format::speed(m_upInfoSpeed);
}

QString GlobalState::dlRateLimitText() const
{
    return Format::limit(m_dlRateLimit);
}

QString GlobalState::upRateLimitText() const
{
    return Format::limit(m_upRateLimit);
}

QString GlobalState::freeSpaceText() const
{
    return Format::size(m_freeSpaceOnDisk);
}

QString GlobalState::lastSyncText() const
{
    return m_lastSyncTime.isValid() ? m_lastSyncTime.toString(QStringLiteral("HH:mm")) : QString();
}

void GlobalState::applyServerState(const ServerStatePatch &s)
{
    take(s.dlSpeed, m_dlInfoSpeed, this, &GlobalState::dlInfoSpeedChanged);
    take(s.upSpeed, m_upInfoSpeed, this, &GlobalState::upInfoSpeedChanged);
    take(s.dlSession, m_dlInfoData, this, &GlobalState::dlInfoDataChanged);
    take(s.upSession, m_upInfoData, this, &GlobalState::upInfoDataChanged);
    take(s.freeSpaceOnDisk, m_freeSpaceOnDisk, this, &GlobalState::freeSpaceOnDiskChanged);
    take(s.altSpeedLimits, m_useAltSpeedLimits, this, &GlobalState::useAltSpeedLimitsChanged);
    take(s.connectionStatus, m_connectionStatus, this, &GlobalState::connectionStatusChanged);
    take(s.dhtNodes, m_dhtNodes, this, &GlobalState::dhtNodesChanged);
    take(
        s.peerConnections, m_totalPeerConnections, this, &GlobalState::totalPeerConnectionsChanged);
    take(s.dlRateLimit, m_dlRateLimit, this, &GlobalState::dlRateLimitChanged);
    take(s.upRateLimit, m_upRateLimit, this, &GlobalState::upRateLimitChanged);
    take(s.queueing, m_queueing, this, &GlobalState::queueingChanged);
    take(s.refreshInterval, m_refreshInterval, this, &GlobalState::refreshIntervalChanged);
    take(s.allTimeUpload, m_allTimeUpload, this, &GlobalState::allTimeChanged);
    take(s.allTimeDownload, m_allTimeDownload, this, &GlobalState::allTimeChanged);
    take(s.globalRatio, m_globalRatio, this, &GlobalState::allTimeChanged);
}

void GlobalState::setConnected(bool connected)
{
    assign(m_connected, connected, this, &GlobalState::connectedChanged);
}

void GlobalState::setSyncing(bool syncing)
{
    assign(m_syncing, syncing, this, &GlobalState::syncingChanged);
}

void GlobalState::setLastError(const ApiError &error)
{
    assign(m_lastError,
           error.isError() ? error.toString() : QString(),
           this,
           &GlobalState::lastErrorChanged);
}

void GlobalState::setLastSyncTime(const QDateTime &when)
{
    assign(m_lastSyncTime, when, this, &GlobalState::lastSyncTimeChanged);
}

void GlobalState::setServerName(const QString &name)
{
    assign(m_serverName, name, this, &GlobalState::serverNameChanged);
}

void GlobalState::setServerVersions(const QString &version, const QString &apiVersion)
{
    assign(m_serverVersion, version, this, &GlobalState::serverVersionChanged);
    assign(m_apiVersion, apiVersion, this, &GlobalState::apiVersionChanged);
}

void GlobalState::setRetryInSeconds(int seconds)
{
    assign(m_retryInSeconds, seconds, this, &GlobalState::retryInSecondsChanged);
}

void GlobalState::reset()
{
    ServerStatePatch defaults;
    defaults.dlSpeed = 0;
    defaults.upSpeed = 0;
    defaults.dlSession = 0;
    defaults.upSession = 0;
    defaults.freeSpaceOnDisk = 0;
    defaults.altSpeedLimits = false;
    defaults.connectionStatus = QStringLiteral("disconnected");
    defaults.dhtNodes = 0;
    defaults.peerConnections = 0;
    defaults.dlRateLimit = 0;
    defaults.upRateLimit = 0;
    defaults.queueing = false;
    defaults.refreshInterval = 1500;
    defaults.allTimeUpload = 0;
    defaults.allTimeDownload = 0;
    defaults.globalRatio = -1.0;
    applyServerState(defaults);
    setConnected(false);
    setSyncing(false);
    setLastError(ApiError());
    setLastSyncTime(QDateTime());
    setServerVersions(QString(), QString());
    setRetryInSeconds(0);
}

} // namespace omaseed::core
