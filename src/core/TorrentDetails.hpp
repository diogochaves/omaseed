#pragma once

// TorrentDetails — everything the details pane of spec §2 needs for one torrent.
//
// It owns the tab models (files, trackers, peers, web seeds), keeps the General tab's key/value
// strings in `properties`, and runs the polling that keeps them fresh:
//
//   * properties + files + trackers every 2 s while a torrent is selected and the pane is
//     visible (they are cheap and all three feed something on screen);
//   * peers every 2 s, but only while the Peers tab is the one on show — setPeersActive() —
//     because `sync/torrentPeers` is the one call of the four that costs the server real
//     work, and its rid diff is worthless when nobody is looking;
//   * pieceStates (docs/12 G3's bar) with properties, but only while the General tab is the
//     one on show — setGeneralActive() — since it is one number per piece;
//   * webseeds (#10's HTTP sources tab) on every selection change, every tick while that
//     tab is on show — setWebSeedsActive() — and every tenth tick otherwise. No other call
//     says whether a torrent has web seeds at all, and the pane shows the tab only when it
//     does; the list changes only when someone edits it (or a magnet's metadata arrives),
//     so 20 s is soon enough to notice from another tab.
//
// setVisible(false) stops everything (the window went away); setHash({}) means "no torrent",
// clears the models and stops the timers. Errors are values: a failed poll is logged once
// per selection, left in `lastError`, and retried on the next tick — the pane keeps showing
// the numbers it already has rather than blanking. A 404 is the exception: the torrent is
// gone (deleted under the pane), so polling stops quietly until another hash is set, and a
// hash the synced TorrentModel no longer holds is not asked about at all.
//
// applyDemo() is the door for `--demo details` and for the QML tests: it fills the same
// models and the same `properties` from a JSON object, with no client and no timers.
//
// With `rememberViewState` on (App turns it on for a real run; demos and tests leave it off,
// so they never write state.json), the Files tab keeps what #9 asks for through
// FilesViewState: each torrent's filter, put back when that torrent is selected again, and
// the hide-skipped preference.

#include "core/ApiError.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/TorrentPeersModel.hpp"
#include "core/TorrentTrackersModel.hpp"
#include "core/TorrentWebSeedsModel.hpp"

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
    Q_PROPERTY(omaseed::core::TorrentWebSeedsModel *webSeeds READ webSeeds CONSTANT)

    /// The General tab's ready-to-draw strings (spec §2 Fig. 1's `.kv` block). Keys:
    /// name, savePath, hashV1, hashV2, downloaded, uploaded, wasted, pieces ("1 264 × 1 MiB"),
    /// piecesHave ("617 of 1 264"), availability, seeds, peers, timeActive, shareLimit,
    /// createdBy, comment, createdOn, and docs/12 G2's connections, lastSeen, seedingTime,
    /// completedOn, avgSpeed, reannounce, private, magnet (the whole link, from the torrent
    /// row) and magnetText (the same, shortened for the eye). Every value is a QString
    /// already through Format; a value the server did not send, or sent as "unknown", is an
    /// empty string, and the view leaves that row out.
    Q_PROPERTY(QVariantMap properties READ properties NOTIFY propertiesChanged)

    /// True while any real tracker of this torrent is failing — the Trackers tab's badge.
    Q_PROPERTY(bool hasTrackerError READ hasTrackerError NOTIFY hasTrackerErrorChanged)

    /// docs/12 G3: how many pieces `torrents/pieceStates` last answered with, and how many of
    /// them are had. 0 pieces means "no bar" (no metadata yet, or the General tab not shown).
    Q_PROPERTY(int pieceCount READ pieceCount NOTIFY pieceStatesChanged)
    Q_PROPERTY(int piecesHaveCount READ piecesHaveCount NOTIFY pieceStatesChanged)
    /// Empty while the polls are going through; the last failure's message otherwise.
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool rememberViewState READ rememberViewState WRITE setRememberViewState NOTIFY
                   rememberViewStateChanged)

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
    TorrentWebSeedsModel *webSeeds() const { return m_webSeeds; }

    QString hash() const { return m_hash; }
    QVariantMap properties() const { return m_properties; }
    bool hasTrackerError() const;
    QString lastError() const { return m_lastError; }

    /// Selects a torrent; an empty hash clears the pane. Polls once straight away.
    void setHash(const QString &hash);

    bool rememberViewState() const { return m_rememberViewState; }
    /// On: reads hideSkipped and this torrent's filter from state.json now, and keeps both
    /// there from then on.
    void setRememberViewState(bool remember);
    /// The Peers tab is the one on show, so sync/torrentPeers is worth asking for.
    void setPeersActive(bool active);
    /// The General tab is the one on show, so torrents/pieceStates is worth asking for
    /// (docs/12 G3). True by default: General is the tab the pane opens on.
    void setGeneralActive(bool active);
    /// The HTTP sources tab is the one on show: its list is asked for on every tick.
    void setWebSeedsActive(bool active);
    /// Asks for the web seeds now (after an add or a remove).
    void refreshWebSeeds();

    int pieceCount() const { return int(m_pieceStates.size()); }
    int piecesHaveCount() const;
    /// The pieces bar's cells for a bar `buckets` pixels wide: PieceStates::bucket().
    Q_INVOKABLE QList<int> pieceBuckets(int buckets) const;
    /// The window (or the pane) went away: everything stops until it comes back.
    void setVisible(bool visible);
    /// Polls now instead of on the next tick.
    void refreshNow();

    /// Fills the models with `{hash, properties, files, trackers, peers, pieceStates,
    /// webSeeds}` (webSeeds as `torrents/webseeds` answers) and
    /// stops polling.
    /// `--demo details` and the QML tests use this; nothing here touches a network.
    void applyDemo(const QJsonObject &details);

    /// The poll cadence, 2000 ms by default. Only the tests move it.
    int pollInterval() const;
    void setPollInterval(int milliseconds);

Q_SIGNALS:
    void hashChanged();
    void propertiesChanged();
    void hasTrackerErrorChanged();
    void pieceStatesChanged();
    void lastErrorChanged();
    void rememberViewStateChanged();
    /// One poll cycle of properties + files + trackers came back (for the live tests).
    void refreshed();

private:
    /// Empties the tab models for `hash`, keeping the Files filter of the torrent being left
    /// and putting back the one remembered for `hash` (with rememberViewState).
    void switchModels(const QString &hash);
    void saveFilesFilter();
    void poll();
    void pollPeers();
    void pollPieceStates();
    void pollWebSeeds();
    void updateTimers();
    void applyProperties(const QJsonObject &json);
    void setPieceStates(const QByteArray &states);
    void noteError(const QString &what, const ApiError &error);
    /// False once the torrent is known to be gone (a 404, or no longer in the synced
    /// model): nothing more is asked about it until another hash is selected.
    bool torrentExists() const;
    void noteSuccess();
    void setLastError(const QString &message);

    QPointer<QbtClient> m_client;
    QPointer<TorrentModel> m_torrents;

    TorrentFilesModel *m_files = nullptr;
    TorrentTrackersModel *m_trackers = nullptr;
    TorrentPeersModel *m_peers = nullptr;
    TorrentWebSeedsModel *m_webSeeds = nullptr;

    QTimer *m_timer = nullptr;
    QTimer *m_peersTimer = nullptr;
    /// Typing a filter writes state.json once it rests, not on every key.
    QTimer *m_filterSaveTimer = nullptr;
    /// The hash whose filter the Files model holds, saved under it.
    QString m_filterHash;

    QString m_hash;
    QJsonObject m_rawProperties;
    QVariantMap m_properties;
    QString m_lastError;
    QByteArray m_pieceStates; ///< one PieceStates::State per piece

    bool m_visible = true;
    bool m_peersActive = false;
    bool m_generalActive = true;
    bool m_webSeedsActive = false;
    int m_pollTick = 0; ///< poll() calls since the selection changed: webseeds every tenth
    bool m_demo = false;
    bool m_errorLogged = false;
    bool m_trackerError = false;
    bool m_rememberViewState = false;
    bool m_switching = false; ///< switchModels() is clearing and refilling the Files filter
    bool m_gone = false;      ///< the server answered 404 for m_hash: deleted under the pane
};

} // namespace omaseed::core
