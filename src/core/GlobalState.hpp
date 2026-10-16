#pragma once

// GlobalState — everything the hero, the footer and the tray read: the server's own figures
// (SyncDelta's server state) plus what only the engine knows (are we connected, when did the last
// poll land, which server is this, how long until the next retry).
//
// It is a plain QObject with properties so QML can bind to it directly in phase 3; the
// formatted twins (dlSpeedText, freeSpaceText, …) exist so no view has to know Format.

#include "core/ApiError.hpp"
#include "core/engines/SyncDelta.hpp"

#include <QDateTime>
#include <QObject>
#include <QString>

namespace omaseed::core {

class GlobalState : public QObject
{
    Q_OBJECT

    // --- server_state --------------------------------------------------------------------
    Q_PROPERTY(qint64 dlInfoSpeed READ dlInfoSpeed NOTIFY dlInfoSpeedChanged)
    Q_PROPERTY(qint64 upInfoSpeed READ upInfoSpeed NOTIFY upInfoSpeedChanged)
    Q_PROPERTY(qint64 dlInfoData READ dlInfoData NOTIFY dlInfoDataChanged)
    Q_PROPERTY(qint64 upInfoData READ upInfoData NOTIFY upInfoDataChanged)
    Q_PROPERTY(qint64 freeSpaceOnDisk READ freeSpaceOnDisk NOTIFY freeSpaceOnDiskChanged)
    Q_PROPERTY(bool useAltSpeedLimits READ useAltSpeedLimits NOTIFY useAltSpeedLimitsChanged)
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)
    Q_PROPERTY(int dhtNodes READ dhtNodes NOTIFY dhtNodesChanged)
    Q_PROPERTY(
        int totalPeerConnections READ totalPeerConnections NOTIFY totalPeerConnectionsChanged)
    Q_PROPERTY(qint64 dlRateLimit READ dlRateLimit NOTIFY dlRateLimitChanged)
    Q_PROPERTY(qint64 upRateLimit READ upRateLimit NOTIFY upRateLimitChanged)
    Q_PROPERTY(bool queueing READ queueing NOTIFY queueingChanged)
    Q_PROPERTY(int refreshInterval READ refreshInterval NOTIFY refreshIntervalChanged)
    /// What this server ever moved, and its global ratio (`global_ratio` is a string in the
    /// API; -1 here until the server says, or when it says something that is not a number).
    Q_PROPERTY(qint64 allTimeUpload READ allTimeUpload NOTIFY allTimeChanged)
    Q_PROPERTY(qint64 allTimeDownload READ allTimeDownload NOTIFY allTimeChanged)
    Q_PROPERTY(double globalRatio READ globalRatio NOTIFY allTimeChanged)

    // --- engine --------------------------------------------------------------------------
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QDateTime lastSyncTime READ lastSyncTime NOTIFY lastSyncTimeChanged)
    Q_PROPERTY(QString serverName READ serverName WRITE setServerName NOTIFY serverNameChanged)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY serverVersionChanged)
    Q_PROPERTY(QString apiVersion READ apiVersion NOTIFY apiVersionChanged)
    Q_PROPERTY(int retryInSeconds READ retryInSeconds NOTIFY retryInSecondsChanged)

    // --- formatted, for the hero and the tray --------------------------------------------
    Q_PROPERTY(QString dlSpeedText READ dlSpeedText NOTIFY dlInfoSpeedChanged)
    Q_PROPERTY(QString upSpeedText READ upSpeedText NOTIFY upInfoSpeedChanged)
    /// The limits in force right now ("10 KB/s" or "∞"); while alt-speed is on these are
    /// the alternative limits, which is what the hero's alt-speed chip prints.
    Q_PROPERTY(QString dlRateLimitText READ dlRateLimitText NOTIFY dlRateLimitChanged)
    Q_PROPERTY(QString upRateLimitText READ upRateLimitText NOTIFY upRateLimitChanged)
    Q_PROPERTY(QString freeSpaceText READ freeSpaceText NOTIFY freeSpaceOnDiskChanged)
    Q_PROPERTY(QString lastSyncText READ lastSyncText NOTIFY lastSyncTimeChanged)

public:
    explicit GlobalState(QObject *parent = nullptr);

    qint64 dlInfoSpeed() const { return m_dlInfoSpeed; }
    qint64 upInfoSpeed() const { return m_upInfoSpeed; }
    qint64 dlInfoData() const { return m_dlInfoData; }
    qint64 upInfoData() const { return m_upInfoData; }
    qint64 freeSpaceOnDisk() const { return m_freeSpaceOnDisk; }
    bool useAltSpeedLimits() const { return m_useAltSpeedLimits; }
    QString connectionStatus() const { return m_connectionStatus; }
    int dhtNodes() const { return m_dhtNodes; }
    int totalPeerConnections() const { return m_totalPeerConnections; }
    qint64 dlRateLimit() const { return m_dlRateLimit; }
    qint64 upRateLimit() const { return m_upRateLimit; }
    bool queueing() const { return m_queueing; }
    int refreshInterval() const { return m_refreshInterval; }
    qint64 allTimeUpload() const { return m_allTimeUpload; }
    qint64 allTimeDownload() const { return m_allTimeDownload; }
    double globalRatio() const { return m_globalRatio; }

    bool connected() const { return m_connected; }
    bool syncing() const { return m_syncing; }
    QString lastError() const { return m_lastError; }
    QDateTime lastSyncTime() const { return m_lastSyncTime; }
    QString serverName() const { return m_serverName; }
    QString serverVersion() const { return m_serverVersion; }
    QString apiVersion() const { return m_apiVersion; }
    int retryInSeconds() const { return m_retryInSeconds; }

    QString dlSpeedText() const;
    QString upSpeedText() const;
    QString dlRateLimitText() const;
    QString upRateLimitText() const;
    QString freeSpaceText() const;
    QString lastSyncText() const;

    /// Merges the figures present in `serverState`; absent ones keep their value.
    void applyServerState(const ServerStatePatch &serverState);

    // Set by SyncEngine.
    void setConnected(bool connected);
    void setSyncing(bool syncing);
    void setLastError(const ApiError &error);
    void setLastSyncTime(const QDateTime &when);
    void setServerName(const QString &name);
    /// Invokable so the view tests can move the API version and watch what it gates (the
    /// Limits sheet's share-limit action row, T5.5 Q14).
    Q_INVOKABLE void setServerVersions(const QString &version, const QString &apiVersion);
    void setRetryInSeconds(int seconds);
    /// Back to the defaults, keeping the server's name: used when a server is swapped out.
    void reset();

Q_SIGNALS:
    void dlInfoSpeedChanged();
    void upInfoSpeedChanged();
    void dlInfoDataChanged();
    void upInfoDataChanged();
    void freeSpaceOnDiskChanged();
    void useAltSpeedLimitsChanged();
    void connectionStatusChanged();
    void dhtNodesChanged();
    void totalPeerConnectionsChanged();
    void dlRateLimitChanged();
    void upRateLimitChanged();
    void queueingChanged();
    void refreshIntervalChanged();
    void allTimeChanged();
    void connectedChanged();
    void syncingChanged();
    void lastErrorChanged();
    void lastSyncTimeChanged();
    void serverNameChanged();
    void serverVersionChanged();
    void apiVersionChanged();
    void retryInSecondsChanged();

private:
    qint64 m_dlInfoSpeed = 0;
    qint64 m_upInfoSpeed = 0;
    qint64 m_dlInfoData = 0;
    qint64 m_upInfoData = 0;
    qint64 m_freeSpaceOnDisk = 0;
    bool m_useAltSpeedLimits = false;
    QString m_connectionStatus = QStringLiteral("disconnected");
    int m_dhtNodes = 0;
    int m_totalPeerConnections = 0;
    qint64 m_dlRateLimit = 0;
    qint64 m_upRateLimit = 0;
    bool m_queueing = false;
    int m_refreshInterval = 1500;
    qint64 m_allTimeUpload = 0;
    qint64 m_allTimeDownload = 0;
    double m_globalRatio = -1.0;

    bool m_connected = false;
    bool m_syncing = false;
    QString m_lastError;
    QDateTime m_lastSyncTime;
    QString m_serverName;
    QString m_serverVersion;
    QString m_apiVersion;
    int m_retryInSeconds = 0;
};

} // namespace omaseed::core
