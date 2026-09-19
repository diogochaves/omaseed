#pragma once

// TorrentDetails — everything the details pane of spec §2 needs for one torrent.
//
// It owns the three tab models (files, trackers, peers), keeps the General tab's key/value
// strings in `properties`, and runs the polling that keeps them fresh:
//
//   * properties + files + trackers every 2 s while a torrent is selected and the pane is
//     visible (they are cheap and all three feed something on screen);
//   * peers every 2 s, but only while the Peers tab is the one on show — setPeersActive() —
//     because `sync/torrentPeers` is the one call of the four that costs the server real
//     work, and its rid diff is worthless when nobody is looking.
//
// setVisible(false) stops everything (the window went away); setHash({}) means "no torrent",
// clears the models and stops the timers. Errors are values: a failed poll is logged once
// per selection, left in `lastError`, and retried on the next tick — the pane keeps showing
// the numbers it already has rather than blanking.
//
// applyDemo() is the door for `--demo details` and for the QML tests: it fills the same
// models and the same `properties` from a JSON object, with no client and no timers.

#include "core/TorrentFilesModel.hpp"
#include "core/TorrentPeersModel.hpp"
#include "core/TorrentTrackersModel.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace omaseed::core {

class QbtClient;
class TorrentModel;

class TorrentDetails : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString hash READ hash NOTIFY hashChanged)
    Q_PROPERTY(omaseed::core::TorrentFilesModel *files READ files CONSTANT)
    Q_PROPERTY(omaseed::core::TorrentTrackersModel *trackers READ trackers CONSTANT)
    Q_PROPERTY(omaseed::core::TorrentPeersModel *peers READ peers CONSTANT)

    /// The General tab's ready-to-draw strings (spec §2 Fig. 1's `.kv` block). Keys:
    /// name, savePath, hashV1, hashV2, downloaded, uploaded, wasted, pieces, availability,
    /// seeds, peers, timeActive, shareLimit, createdBy, comment, createdOn. Every value is a
    /// QString already through Format; a value the server did not send is an empty string,
    /// and the view leaves that row out.
    Q_PROPERTY(QVariantMap properties READ properties NOTIFY propertiesChanged)

    /// True while any real tracker of this torrent is failing — the Trackers tab's badge.
    Q_PROPERTY(bool hasTrackerError READ hasTrackerError NOTIFY hasTrackerErrorChanged)
    /// Empty while the polls are going through; the last failure's message otherwise.
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit TorrentDetails(QObject *parent = nullptr);
    ~TorrentDetails() override;

    /// The client to poll through. Not owned; nullptr means "demo or no server".
    void setClient(QbtClient *client);
    /// Where the share limits come from: `torrents/properties` does not carry them, but the
    /// torrent row of `sync/maindata` does. Not owned; optional.
    void setTorrentModel(TorrentModel *torrents);

    TorrentFilesModel *files() const { return m_files; }
    TorrentTrackersModel *trackers() const { return m_trackers; }
    TorrentPeersModel *peers() const { return m_peers; }

    QString hash() const { return m_hash; }
    QVariantMap properties() const { return m_properties; }
    bool hasTrackerError() const;
    QString lastError() const { return m_lastError; }

    /// Selects a torrent; an empty hash clears the pane. Polls once straight away.
    void setHash(const QString &hash);
    /// The Peers tab is the one on show, so sync/torrentPeers is worth asking for.
    void setPeersActive(bool active);
    /// The window (or the pane) went away: everything stops until it comes back.
    void setVisible(bool visible);
    /// Polls now instead of on the next tick.
    void refreshNow();

    /// Fills the models with `{hash, properties, files, trackers, peers}` and stops polling.
    /// `--demo details` and the QML tests use this; nothing here touches a network.
    void applyDemo(const QJsonObject &details);

    /// The poll cadence, 2000 ms by default. Only the tests move it.
    int pollInterval() const;
    void setPollInterval(int milliseconds);

Q_SIGNALS:
    void hashChanged();
    void propertiesChanged();
    void hasTrackerErrorChanged();
    void lastErrorChanged();
    /// One poll cycle of properties + files + trackers came back (for the live tests).
    void refreshed();

private:
    void poll();
    void pollPeers();
    void updateTimers();
    void applyProperties(const QJsonObject &json);
    void noteError(const QString &what, const QString &message);
    void noteSuccess();
    void setLastError(const QString &message);

    QPointer<QbtClient> m_client;
    QPointer<TorrentModel> m_torrents;

    TorrentFilesModel *m_files = nullptr;
    TorrentTrackersModel *m_trackers = nullptr;
    TorrentPeersModel *m_peers = nullptr;

    QTimer *m_timer = nullptr;
    QTimer *m_peersTimer = nullptr;

    QString m_hash;
    QJsonObject m_rawProperties;
    QVariantMap m_properties;
    QString m_lastError;

    bool m_visible = true;
    bool m_peersActive = false;
    bool m_demo = false;
    bool m_errorLogged = false;
    bool m_trackerError = false;
};

} // namespace omaseed::core
