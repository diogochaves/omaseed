#pragma once

// App — the composition root, and the one object QML talks to.
//
// Everything the views need hangs off here: the server list, the client that talks to the
// chosen server, the sync loop, the models it feeds and the proxy the rail and the search
// box drive. `main.cpp` creates exactly one, hands it to the QML engine as the `App`
// singleton of module `Omaseed` (see `src/ui/qml/AppForeign.hpp`) and gives it the tray.
//
// Where the server comes from, in order:
//   1. `--demo table`: no server at all. `demoMode` is true, the engine never starts, and
//      the models are filled from DemoData.
//   2. ServerStore's default server (or its only one), with its secret fetched from
//      SecretStore. This is the real path once T4.6 writes the first server.
//   3. While no server is configured, the environment: OMASEED_SERVER_URL, plus optional
//      OMASEED_API_KEY or OMASEED_USERNAME/OMASEED_PASSWORD (and OMASEED_SERVER_NAME for the
//      hero line). Nothing is persisted; the log line says "(from environment)".
//   4. Nothing: `hasServer` is false, the engine never starts, and the views show the
//      first-run/empty state. There is no default port — omaseed never guesses at one.
//
// `hasServer` is true in demo mode too: there is a library to draw, it is simply fictional.
// `demoMode` is the flag that says "do not touch the network".
//
// App owns no view state (cursor, selection, open sheets): that belongs to the QML side.

#include "core/CategoryModel.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/QbtClient.hpp"
#include "core/SecretStore.hpp"
#include "core/Server.hpp"
#include "core/ServerStore.hpp"
#include "core/SyncEngine.hpp"
#include "core/TagModel.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"
#include "integration/Tray.hpp"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace omaseed::integration {
class Notifier;
class Xdg;
} // namespace omaseed::integration

namespace omaseed::app {

class App : public QObject
{
    Q_OBJECT

    // The models. None of these objects is ever swapped out — a server change refills them
    // in place — so every one of them is CONSTANT and QML can bind straight through.
    Q_PROPERTY(omaseed::core::TorrentModel *torrents READ torrents CONSTANT)
    Q_PROPERTY(omaseed::core::TorrentFilterModel *filtered READ filtered CONSTANT)
    Q_PROPERTY(omaseed::core::FilterCounts *counts READ counts CONSTANT)
    Q_PROPERTY(omaseed::core::CategoryModel *categories READ categories CONSTANT)
    Q_PROPERTY(omaseed::core::TagModel *tags READ tags CONSTANT)
    Q_PROPERTY(omaseed::core::TrackerModel *trackers READ trackers CONSTANT)
    Q_PROPERTY(omaseed::core::GlobalState *globalState READ globalState CONSTANT)
    Q_PROPERTY(omaseed::core::SyncEngine *engine READ engine CONSTANT)
    Q_PROPERTY(omaseed::core::ServerStore *servers READ servers CONSTANT)

    // The server the hero line prints, and whether there is one at all.
    Q_PROPERTY(QString serverName READ serverName NOTIFY serverChanged)
    Q_PROPERTY(QString serverUrlText READ serverUrlText NOTIFY serverChanged)
    Q_PROPERTY(bool hasServer READ hasServer NOTIFY serverChanged)
    Q_PROPERTY(int serverCount READ serverCount NOTIFY serverChanged)

    /// True under `--demo table`: the views must not offer anything that would touch a
    /// network, because there is no server behind the numbers.
    Q_PROPERTY(bool demoMode READ isDemoMode NOTIFY demoModeChanged)

    // --- T3.4: details ---
    Q_PROPERTY(omaseed::core::TorrentDetails *details READ details CONSTANT)

public:
    explicit App(QObject *parent = nullptr);
    ~App() override;

    // --- the QML singleton ---------------------------------------------------------------
    /// main.cpp calls this before the QML engine exists, the way Theme and Xdg do.
    static void setDefaultInstance(App *instance);
    /// The `App` singleton factory of module `Omaseed`. Without a default instance (a QML
    /// test, a tool) the engine gets one of its own, stopped and serverless.
    static App *createForEngine(QQmlEngine *engine);

    // --- lifecycle -----------------------------------------------------------------------
    /// Loads the server list, picks the server (store, then environment) and starts the
    /// sync loop. Does nothing in demo mode, and nothing when there is no server.
    void start();
    /// Fills the models with `rows` fictional torrents (DemoData) and turns `demoMode` on.
    /// No client is configured and the engine stays stopped.
    void startDemo(int rows);
    /// The same from QML, for the qmltestrunner tests of the views: the engine-owned fallback
    /// `App` of a test has no server and no data until a test calls `App.loadDemo(16)`.
    Q_INVOKABLE void loadDemo(int rows) { startDemo(rows); }
    /// The demo path for anything that has its own JSON: tests build a maindata object,
    /// change a row, and apply it here.
    void applyDemoData(const QJsonObject &maindata);

    /// The tray, created by main.cpp once the window exists. App wires the tray's menu to
    /// its own actions and keeps the icon state, tooltip and stats up to date. Not owned.
    void attachTray(omaseed::integration::Tray *tray);

    // --- models --------------------------------------------------------------------------
    omaseed::core::TorrentModel *torrents() const;
    omaseed::core::TorrentFilterModel *filtered() const { return m_filtered; }
    omaseed::core::FilterCounts *counts() const;
    omaseed::core::CategoryModel *categories() const;
    omaseed::core::TagModel *tags() const;
    omaseed::core::TrackerModel *trackers() const;
    omaseed::core::GlobalState *globalState() const;
    omaseed::core::SyncEngine *engine() const { return m_engine; }
    omaseed::core::ServerStore *servers() const { return m_servers; }
    omaseed::core::QbtClient *client() const { return m_client; }

    // --- the chosen server ---------------------------------------------------------------
    QString serverName() const;
    QString serverUrlText() const;
    bool hasServer() const { return m_hasServer; }
    int serverCount() const;
    bool isDemoMode() const { return m_demoMode; }

    /// The tray's icon state for a given world (spec §6 Fig. 17). Highest priority first:
    /// disconnected, error, alt-speed, downloading, idle. Static and pure so the mapping is
    /// testable without a tray.
    static omaseed::integration::Tray::State
    trayStateFor(bool connected, bool serverLost, int errored, int downloading, bool altSpeed);

public Q_SLOTS:
    /// Skips the reconnect countdown and polls now (spec §7 Fig. 23's "retry now").
    void retryNow();
    /// The window's visibility: the poll loop slows to 5 s while it is false.
    void setVisible(bool visible);

    /// POST transfer/toggleSpeedLimitsMode. The tray's checkbox is not moved here — it
    /// follows `globalState.useAltSpeedLimits`, which the next poll brings back.
    void toggleAltSpeed();
    /// POST torrents/stop with hashes=all.
    void stopAll();
    /// POST torrents/start with hashes=all.
    void startAll();

Q_SIGNALS:
    void serverChanged();
    void demoModeChanged();

    /// Raised by the tray menu, the hero's gear and `,`. Phase 4 opens the settings modal on
    /// it; until then it is only a signal QML may emit and listen to.
    void openSettings();
    /// Raised by the tray menu, the hero's `+` and `a`. Phase 4 opens the add sheet.
    void addRequested();

private:
    void configureFromStore();
    bool configureFromEnvironment();
    void applyServer(const omaseed::core::Server &server);
    void startEngine();
    void updateTray();
    void updateTrayNumbers();

    omaseed::core::QbtClient *m_client = nullptr;
    omaseed::core::SyncEngine *m_engine = nullptr;
    omaseed::core::TorrentFilterModel *m_filtered = nullptr;
    omaseed::core::ServerStore *m_servers = nullptr;
    omaseed::core::SecretStore *m_secrets = nullptr;

    QPointer<omaseed::integration::Tray> m_tray;

    omaseed::core::Server m_server;
    bool m_hasServer = false;
    bool m_demoMode = false;
    bool m_serverLost = false;

    // --- T3.4: details -------------------------------------------------------------------
    // Everything the details pane (spec §2) adds to App lives in this one block, at the end
    // of the class and at the end of App.cpp, so the three parallel Phase 3 branches merge
    // without touching each other's lines.
public:
    /// The details pane's controller: the three tab models and the General tab's strings.
    /// Never swapped out, so QML binds straight through it.
    omaseed::core::TorrentDetails *details() const { return m_details; }

    /// The table's cursor drives this. An empty hash clears the pane. In demo mode the
    /// models are filled from DemoData instead of from the server.
    Q_INVOKABLE void selectTorrent(const QString &hash);
    /// The pane's own visibility (`i` collapses it): the detail polls stop while it is off.
    Q_INVOKABLE void setDetailsVisible(bool visible);
    /// The Peers tab is the one on show, so sync/torrentPeers is worth polling.
    Q_INVOKABLE void setPeersTabActive(bool active);

    /// POST torrents/filePrio. `fileIndexes` is a list of ints (the `fileIndexes` role of a
    /// TorrentFilesModel row); a no-op in demo mode, where there is no server to tell.
    Q_INVOKABLE void
    setFilePriority(const QString &hash, const QVariantList &fileIndexes, int priority);

    /// The fields `Details.qml`'s `torrent` property expects, for one hash: name, category,
    /// tagsText, addedOnText, sizeText, progressText, ratioText, etaLongText, stateText,
    /// stateRole, hasTrackerError. An unknown hash gives an empty map. It is a snapshot, not
    /// a binding — rebind it on `App.torrents.updated` to follow a live torrent.
    Q_INVOKABLE QVariantMap torrentRow(const QString &hash) const;

    /// `OMASEED_DEMO_TAB` — which tab `--demo details` opens on ("", "files", "trackers",
    /// "peers"). Only the demo host reads it.
    Q_INVOKABLE QString demoTab() const;

Q_SIGNALS:
    /// Phase 4 wires these to the tracker and peer endpoints; Phase 3 only emits them, so
    /// the details pane's `a`/`d`/`R`/`b` keys already have somewhere to go.
    /// `action` is one of "add", "remove", "reannounce" / "ban", "add".
    void trackerActionRequested(const QString &hash, const QString &action, const QString &url);
    void peerActionRequested(const QString &hash, const QString &action, const QString &address);

private:
    /// Created here rather than in App's constructor so that all of T3.4 stays inside this
    /// block; `this` in a default member initializer is the Qt parent, as everywhere else.
    /// The client and the torrent model are handed over on the first selectTorrent().
    omaseed::core::TorrentDetails *m_details = new omaseed::core::TorrentDetails(this);
    // --- end T3.4 ------------------------------------------------------------------------

    // --- T3.5: the keymap's actions ------------------------------------------------------
    // Same arrangement as the T3.4 block: everything the main window adds to App sits at the
    // end of the class so the parallel Phase 3 branches merge without touching each other.
public:
    /// Every key of spec §7 Fig. 26's "act" column that still needs Phase 4 leaves through
    /// here (`p P d D c R C t m L r [ ] o y Y`, plus `rename` from the table's inline
    /// editor). `MainView` collects the hashes — the selection, or the cursor row alone —
    /// and calls this; Phase 4 connects the signal to the real endpoints, the confirm dialog
    /// and the sheets. QML cannot emit a C++ signal, hence the invokable.
    Q_INVOKABLE void requestTorrentAction(const QString &action, const QStringList &hashes);

    /// `Format::size` for QML (the loading state prints how much of the first sync arrived).
    Q_INVOKABLE QString sizeText(qint64 bytes) const;

Q_SIGNALS:
    void torrentActionRequested(const QString &action, const QStringList &hashes);
    // --- end T3.5 ------------------------------------------------------------------------

    // --- T4.7: notifications ---------------------------------------------------------------
    // Wires SyncEngine's five events (spec §6) to the real Notifier, handles the shell's
    // ActionInvoked for them, and keeps the per-event enable/disable toggles the Notifications
    // pane (T4.5) reads and writes. Everything this adds to App sits in this one block, at the
    // end of the class and at the end of App.cpp, the same arrangement T3.4 and T3.5 use.
public:
    /// Every notice from here on goes through `notifier`; not owned. Safe to call once, from
    /// main.cpp, right after the tray. A null pointer only stops wiring; nothing else changes.
    void attachNotifier(omaseed::integration::Notifier *notifier);
    /// `xdg` reveals a completed torrent's folder ("open folder"). Not owned.
    void attachXdg(omaseed::integration::Xdg *xdg);

    /// Four booleans QML can read as `enabled.completed`, `.trackerError`, `.serverLost`,
    /// `.diskFull`; `serverLost` covers both the lost and the back notice. Persisted under
    /// `"notifications"` in `~/.config/omaseed/state.json`, default true.
    Q_PROPERTY(QVariantMap notificationsEnabled READ notificationsEnabled NOTIFY
                   notificationsEnabledChanged)
    QVariantMap notificationsEnabled() const;
    /// `key` is one of "completed", "trackerError", "serverLost", "diskFull".
    Q_INVOKABLE void setNotificationEnabled(const QString &key, bool enabled);

Q_SIGNALS:
    void notificationsEnabledChanged();
    /// "show": raise the window and move the table cursor to this hash. MainView's
    /// `Connections { target: App }` block, when it has one, is where this lands.
    void showTorrentRequested(const QString &hash);
    /// "show trackers": as above, plus open the details pane's Trackers tab.
    void showTrackersRequested(const QString &hash);

private:
    void loadNotificationToggles();
    void saveNotificationToggles();
    void onNotifierActionInvoked(const QString &eventClass,
                                 const QString &subjectId,
                                 const QString &actionKey);
    /// The chosen server's PathMap applied to a server-side content path; the input unchanged
    /// (and unusable as a local path) when no mapping applies.
    QString mapContentPathToLocal(const QString &remotePath) const;
    /// "host:port" of the chosen server, for the server-lost body (spec §6 Fig. 21).
    QString serverHostPortText() const;
    /// Strips a `serverLost` ApiError down to the reason text the notice wants.
    static QString serverLostReasonText(const omaseed::core::ApiError &error);

    QPointer<omaseed::integration::Notifier> m_notifier;
    QPointer<omaseed::integration::Xdg> m_xdg;

    /// hash -> local content path, filled by every "completed" notice, read back by its
    /// "open folder" action.
    QHash<QString, QString> m_completedContentPaths;

    bool m_notifyCompletedEnabled = true;
    bool m_notifyTrackerErrorEnabled = true;
    bool m_notifyServerLostEnabled = true;
    bool m_notifyDiskFullEnabled = true;
    bool m_notificationTogglesLoaded = false;
    // --- end T4.7 ----------------------------------------------------------------------------
};

} // namespace omaseed::app
