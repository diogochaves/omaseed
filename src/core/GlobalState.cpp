#include "core/GlobalState.hpp"

#include "core/Format.hpp"

#include <QJsonValue>

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

/// Same, but only for keys the server actually sent.
template<class T, class Owner, class Signal, class Convert>
void takeFrom(const QJsonObject &json,
              QLatin1StringView key,
              T &field,
              Owner *owner,
              Signal signal,
              Convert convert)
{
    const auto it = json.constFind(key);
    if (it == json.constEnd())
        return;
    assign(field, T(convert(it.value())), owner, signal);
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

void GlobalState::applyServerState(const QJsonObject &s)
{
    const auto integer = [](const QJsonValue &v) { return v.toInteger(0); };
    const auto boolean = [](const QJsonValue &v) { return v.toBool(false); };
    const auto text = [](const QJsonValue &v) { return v.toString(); };

    takeFrom(s,
             QLatin1StringView("dl_info_speed"),
             m_dlInfoSpeed,
             this,
             &GlobalState::dlInfoSpeedChanged,
             integer);
    takeFrom(s,
             QLatin1StringView("up_info_speed"),
             m_upInfoSpeed,
             this,
             &GlobalState::upInfoSpeedChanged,
             integer);
    takeFrom(s,
             QLatin1StringView("dl_info_data"),
             m_dlInfoData,
             this,
             &GlobalState::dlInfoDataChanged,
             integer);
    takeFrom(s,
             QLatin1StringView("up_info_data"),
             m_upInfoData,
             this,
             &GlobalState::upInfoDataChanged,
             integer);
    takeFrom(s,
             QLatin1StringView("free_space_on_disk"),
             m_freeSpaceOnDisk,
             this,
             &GlobalState::freeSpaceOnDiskChanged,
             integer);
    takeFrom(s,
             QLatin1StringView("use_alt_speed_limits"),
             m_useAltSpeedLimits,
             this,
             &GlobalState::useAltSpeedLimitsChanged,
             boolean);
    takeFrom(s,
             QLatin1StringView("connection_status"),
             m_connectionStatus,
             this,
             &GlobalState::connectionStatusChanged,
             text);
    takeFrom(s,
             QLatin1StringView("dht_nodes"),
             m_dhtNodes,
             this,
             &GlobalState::dhtNodesChanged,
             integer);
    takeFrom(s,
             QLatin1StringView("total_peer_connections"),
             m_totalPeerConnections,
             this,
             &GlobalState::totalPeerConnectionsChanged,
             integer);
    takeFrom(s,
             QLatin1StringView("dl_rate_limit"),
             m_dlRateLimit,
             this,
             &GlobalState::dlRateLimitChanged,
             integer);
    takeFrom(s,
             QLatin1StringView("up_rate_limit"),
             m_upRateLimit,
             this,
             &GlobalState::upRateLimitChanged,
             integer);
    takeFrom(
        s, QLatin1StringView("queueing"), m_queueing, this, &GlobalState::queueingChanged, boolean);
    takeFrom(s,
             QLatin1StringView("refresh_interval"),
             m_refreshInterval,
             this,
             &GlobalState::refreshIntervalChanged,
             integer);
    takeFrom(s,
             QLatin1StringView("alltime_ul"),
             m_allTimeUpload,
             this,
             &GlobalState::allTimeChanged,
             integer);
    takeFrom(s,
             QLatin1StringView("alltime_dl"),
             m_allTimeDownload,
             this,
             &GlobalState::allTimeChanged,
             integer);
    // "2.16" in qBittorrent 5.x; a number is taken too, anything else is "unknown".
    takeFrom(s,
             QLatin1StringView("global_ratio"),
             m_globalRatio,
             this,
             &GlobalState::allTimeChanged,
             [](const QJsonValue &v) {
                 if (v.isDouble())
                     return v.toDouble();
                 bool ok = false;
                 const double ratio = v.toString().toDouble(&ok);
                 return ok ? ratio : -1.0;
             });
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

void GlobalState::setSilentSeconds(int seconds)
{
    assign(m_silentSeconds, seconds, this, &GlobalState::silentSecondsChanged);
}

void GlobalState::reset()
{
    applyServerState(QJsonObject{
        {QStringLiteral("dl_info_speed"), 0},
        {QStringLiteral("up_info_speed"), 0},
        {QStringLiteral("dl_info_data"), 0},
        {QStringLiteral("up_info_data"), 0},
        {QStringLiteral("free_space_on_disk"), 0},
        {QStringLiteral("use_alt_speed_limits"), false},
        {QStringLiteral("connection_status"), QStringLiteral("disconnected")},
        {QStringLiteral("dht_nodes"), 0},
        {QStringLiteral("total_peer_connections"), 0},
        {QStringLiteral("dl_rate_limit"), 0},
        {QStringLiteral("up_rate_limit"), 0},
        {QStringLiteral("queueing"), false},
        {QStringLiteral("refresh_interval"), 1500},
        {QStringLiteral("alltime_ul"), 0},
        {QStringLiteral("alltime_dl"), 0},
        {QStringLiteral("global_ratio"), QString()},
    });
    setConnected(false);
    setSyncing(false);
    setLastError(ApiError());
    setLastSyncTime(QDateTime());
    setServerVersions(QString(), QString());
    setRetryInSeconds(0);
    setSilentSeconds(0);
    if (!m_dlHistory.isEmpty() || !m_upHistory.isEmpty()) {
        m_dlHistory.clear();
        m_upHistory.clear();
        Q_EMIT speedHistoryChanged();
    }
}

namespace {
QVariantList toVariants(const QList<qint64> &samples)
{
    QVariantList list;
    list.reserve(samples.size());
    for (const qint64 sample : samples)
        list.append(sample);
    return list;
}
} // namespace

QVariantList GlobalState::dlHistory() const
{
    return toVariants(m_dlHistory);
}

QVariantList GlobalState::upHistory() const
{
    return toVariants(m_upHistory);
}

void GlobalState::recordSpeeds()
{
    m_dlHistory.append(m_dlInfoSpeed);
    m_upHistory.append(m_upInfoSpeed);
    while (m_dlHistory.size() > kSpeedSamples)
        m_dlHistory.removeFirst();
    while (m_upHistory.size() > kSpeedSamples)
        m_upHistory.removeFirst();
    Q_EMIT speedHistoryChanged();
}

} // namespace omaseed::core
