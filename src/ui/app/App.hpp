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

#include "core/AddFilesModel.hpp"
#include "core/AutoBackup.hpp"
#include "core/CategoryModel.hpp"
#include "core/FilterCounts.hpp"
#include "core/FreeSpace.hpp"
#include "core/GlobalState.hpp"
#include "core/OmaseedBackup.hpp"
#include "core/QbtClient.hpp"
#include "core/RenameDisk.hpp"
#include "core/SecretStore.hpp"
#include "core/SeedConsole.hpp"
#include "core/SeedLedger.hpp"
#include "core/Server.hpp"
#include "core/ServerDetector.hpp"
#include "core/ServerListModel.hpp"
#include "core/ServerPreferences.hpp"
#include "core/ServerStore.hpp"
#include "core/SyncEngine.hpp"
#include "core/TagModel.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFile.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"
#include "integration/BarWidget.hpp"
#include "integration/Tray.hpp"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QQmlEngine;
QT_END_NAMESPACE

namespace omaseed::integration {
class Notifier;
class StatusWriter;
class ThemeReader;
class WidgetHeartbeat;
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
    /// No client is configured and the engine stays stopped. With `seedServer`, the server
    /// list gets the fictional "home" the hero names, so Settings › Servers has a row to show
    /// (batch-2 P5); in memory only, like every server change during a demo.
    void startDemo(int rows, bool seedServer = true);
    /// The same from QML, for the qmltestrunner tests of the views: the engine-owned fallback
    /// `App` of a test has no server and no data until a test calls `App.loadDemo(16)`. It
    /// seeds no server row, so the Settings tests start from an empty list.
    Q_INVOKABLE void loadDemo(int rows) { startDemo(rows, false); }
    /// The demo path for anything that has its own JSON: tests build a maindata object,
    /// change a row, and apply it here.
    void applyDemoData(const QJsonObject &maindata);

    /// The tray, created by main.cpp once the window exists. App wires the tray's menu to
    /// its own actions and keeps the icon state, tooltip and stats up to date. Not owned.
    void attachTray(omaseed::integration::Tray *tray);
    /// The window the file pickers belong to, set by main.cpp once it exists. Not owned.
    void setDialogParent(QWindow *window);

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
    /// For tests: its probe runner and keychain hooks keep them off the desktop's keyring.
    omaseed::core::SecretStore *secretStore() const { return m_secrets; }

    // --- the chosen server ---------------------------------------------------------------
    QString serverName() const;
    QString serverUrlText() const;
    bool hasServer() const { return m_hasServer; }
    int serverCount() const;
    bool isDemoMode() const { return m_demoMode; }
    /// True after serverLost/authRequired until a sync succeeds again (drives the tray).
    bool isServerLost() const { return m_serverLost; }

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

    /// Raised by the tray menu, the hero's gear, `,` and a notice's "settings". MainView opens
    /// the settings modal on it; main.cpp brings the window up.
    void openSettings();
    /// Raised by the tray menu, the hero's `+` and `a`. MainView opens the add sheet.
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
    QPointer<QWindow> m_dialogParent;

    omaseed::core::Server m_server;
    bool m_hasServer = false;
    bool m_demoMode = false;
    bool m_serverLost = false;
    /// Bumped whenever the live server changes; late keychain answers compare against it.
    quint64 m_serverGeneration = 0;

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
    /// The General tab is the one on show, so torrents/pieceStates is worth polling (docs/12
    /// G3).
    Q_INVOKABLE void setGeneralTabActive(bool active);
    /// The HTTP sources tab is the one on show, so torrents/webseeds is polled every tick
    /// (#10); away from it, one tick in ten.
    Q_INVOKABLE void setWebSeedsTabActive(bool active);

    /// POST torrents/filePrio. `fileIndexes` is a list of ints (the `fileIndexes` role of a
    /// TorrentFilesModel row); a no-op in demo mode, where there is no server to tell.
    /// Skipping (priority 0) a file of the torrent on show remembers the priority it had,
    /// for setFilesWanted() to bring back (docs/27 D3).
    Q_INVOKABLE void
    setFilePriority(const QString &hash, const QVariantList &fileIndexes, int priority);
    /// The Files tab's tick column, its head and `x` (#42): `wanted` false skips the files;
    /// true brings each skipped one back at the priority it had before it was skipped while
    /// omaseed has been running (normal when there is none), and leaves the others alone.
    Q_INVOKABLE void
    setFilesWanted(const QString &hash, const QVariantList &fileIndexes, bool wanted);

    /// The fields `Details.qml`'s `torrent` property expects, for one hash: name, category,
    /// tagsText, addedOnText, sizeText, progressText, ratioText, etaLongText, stateText,
    /// stateRole, hasTrackerError. An unknown hash gives an empty map. It is a snapshot, not
    /// a binding — rebind it on `App.torrents.updated` to follow a live torrent.
    Q_INVOKABLE QVariantMap torrentRow(const QString &hash) const;

    /// `OMASEED_DEMO_TAB` — which tab `--demo details` opens on ("", "files", "trackers",
    /// "peers"). Only the demo host reads it.
    Q_INVOKABLE QString demoTab() const;

Q_SIGNALS:
    /// Declared in Phase 3 for the details pane's `a`/`d`/`R`/`b` keys; nothing in the app
    /// emits or connects them today. `action` is one of "add", "remove", "reannounce" /
    /// "ban", "add".
    void trackerActionRequested(const QString &hash, const QString &action, const QString &url);
    void peerActionRequested(const QString &hash, const QString &action, const QString &address);

private:
    /// Created here rather than in App's constructor so that all of T3.4 stays inside this
    /// block; `this` in a default member initializer is the Qt parent, as everywhere else.
    /// The client and the torrent model are handed over on the first selectTorrent().
    omaseed::core::TorrentDetails *m_details = new omaseed::core::TorrentDetails(this);
    /// #42: per torrent hash, each skipped file's priority before the skip, for the session.
    QHash<QString, QHash<int, int>> m_beforeSkip;
    // --- end T3.4 ------------------------------------------------------------------------

    // --- T3.5: the keymap's actions ------------------------------------------------------
    // Same arrangement as the T3.4 block: everything the main window adds to App sits at the
    // end of the class so the parallel Phase 3 branches merge without touching each other.
public:
    /// A key of spec §7 Fig. 26's "act" column, re-emitted as `torrentActionRequested`.
    /// `MainView` collects the hashes — the selection, or the cursor row alone — and calls
    /// this. The actions themselves run through the T4.3 invokables below; the signal is only
    /// logged (main.cpp). QML cannot emit a C++ signal, hence the invokable.
    Q_INVOKABLE void requestTorrentAction(const QString &action, const QStringList &hashes);

    /// `Format::size` for QML (the loading state prints how much of the first sync arrived).
    Q_INVOKABLE QString sizeText(qint64 bytes) const;
    /// A rate limit in the hero's short form ("1.00M", "200K"); "∞" when there is none.
    Q_INVOKABLE static QString limitShortText(qint64 bytesPerSecond);
    /// A speed in the same short form ("4.21M", "812K", "0"), for the hero's last step.
    Q_INVOKABLE static QString speedShortText(qint64 bytesPerSecond);

    /// A client error ("network: GET sync/maindata: Connection refused") stripped to the
    /// reason Fig. 21's notice and Fig. 23's banner print ("connection refused").
    Q_INVOKABLE static QString errorReasonText(const QString &text);
    /// The lost-server banner's reason for `lastError` (GlobalState::lastError): a refused
    /// login in words for the server's auth mode ("this server asks for a login" when it has
    /// none), otherwise `errorReasonText`.
    Q_INVOKABLE QString lostReasonText(const QString &lastError) const;

Q_SIGNALS:
    void torrentActionRequested(const QString &action, const QStringList &hashes);
    // --- end T3.5 ------------------------------------------------------------------------

    // --- T4.7: notifications ---------------------------------------------------------------
    // Wires SyncEngine's four events (spec §6, minus Fig. 20's tracker notice — dropped in
    // T5.5 per docs/DECISIONS.md Q16: tracker health is the Trackers tab badge and the rail
    // filter, as in qBittorrent's own GUI) to the real Notifier, handles the shell's
    // ActionInvoked for them, and keeps the per-event enable/disable toggles the Notifications
    // pane (T4.5) reads and writes. Everything this adds to App sits in this one block, at the
    // end of the class and at the end of App.cpp, the same arrangement T3.4 and T3.5 use.
public:
    /// Every notice from here on goes through `notifier`; not owned. Safe to call once, from
    /// main.cpp, right after the tray. A null pointer only stops wiring; nothing else changes.
    void attachNotifier(omaseed::integration::Notifier *notifier);
    /// `xdg` reveals a completed torrent's folder ("open folder"). Not owned.
    void attachXdg(omaseed::integration::Xdg *xdg);

    /// Three booleans QML can read as `enabled.completed`, `.serverLost`, `.diskFull`;
    /// `serverLost` covers both the lost and the back notice. Persisted under
    /// `"notifications"` in `~/.config/omaseed/state.json`, default true. An older file's
    /// `trackerError` key is ignored.
    Q_PROPERTY(QVariantMap notificationsEnabled READ notificationsEnabled NOTIFY
                   notificationsEnabledChanged)
    QVariantMap notificationsEnabled() const;
    /// `key` is one of "completed", "serverLost", "diskFull".
    Q_INVOKABLE void setNotificationEnabled(const QString &key, bool enabled);

    /// Where "disk almost full" fires, in GiB: default 2, never below 1 (the `diskFull`
    /// toggle is the off switch). Persisted next to the toggles as `lowDiskThresholdGiB` and
    /// pushed into SyncEngine::lowDiskThreshold as bytes. It watches one number, the
    /// server's `free_space_on_disk`, which qBittorrent reports for its default save path
    /// only; other disks and per-category paths are not covered (docs/DECISIONS.md Q16).
    Q_PROPERTY(int lowDiskThresholdGiB READ lowDiskThresholdGiB WRITE setLowDiskThresholdGiB NOTIFY
                   lowDiskThresholdGiBChanged)
    int lowDiskThresholdGiB() const;
    void setLowDiskThresholdGiB(int gib);

Q_SIGNALS:
    void notificationsEnabledChanged();
    void lowDiskThresholdGiBChanged();
    /// "show": raise the window and move the table cursor to this hash. MainView's
    /// `Connections { target: App }` block, when it has one, is where this lands.
    void showTorrentRequested(const QString &hash);

private:
    void loadNotificationToggles();
    void saveNotificationToggles();
    /// `m_lowDiskThresholdGiB` into the engine, as bytes.
    void applyLowDiskThreshold();
    void onNotifierActionInvoked(const QString &eventClass,
                                 const QString &subjectId,
                                 const QString &actionKey);

public:
    /// The chosen server's PathMap applied to a server-side content path; the input unchanged
    /// (and unusable as a local path) when no mapping applies or Settings' "apply path map"
    /// (`pathMapEnabled`) is off.
    QString mapContentPathToLocal(const QString &remotePath) const;

private:
    /// The chosen server's path map, as configured (whether or not it is switched on).
    core::PathMap serverPathMap() const;
    /// Feeds core::FreeSpaceProbe the server's default save path and free space again.
    void updateFreeSpace();
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
    bool m_notifyServerLostEnabled = true;
    /// The quiet start (setLaunchHidden): running while a loss may still go unannounced.
    QTimer m_quietStart;
    std::chrono::milliseconds m_quietStartGrace{std::chrono::minutes(2)};
    /// A loss held back by the quiet start, with the retry period it would have announced.
    std::optional<std::pair<omaseed::core::ApiError, int>> m_heldLoss;
    bool m_notifyDiskFullEnabled = true;
    bool m_notifyErroredEnabled = true; ///< docs/28 D4 a
    int m_lowDiskThresholdGiB = 2;
    bool m_notificationTogglesLoaded = false;

    // --- T4.5: settings ------------------------------------------------------------------
    // Everything the settings modal (spec v2 §5 Fig. 16) and the server sheet (§3 Fig. 9)
    // need from App: the server list as a model, the four mutations that go with it, a
    // throwaway connection test, and the handful of persisted flags the General and
    // Appearance panes turn. Same arrangement as the T3.4/T3.5/T4.7 blocks: one delimited
    // region at the end of the class and at the end of App.cpp.
public:
    /// The server list as a `QAbstractListModel` for the Servers pane. Never swapped out.
    Q_PROPERTY(omaseed::core::ServerListModel *serverList READ serverList CONSTANT)
    omaseed::core::ServerListModel *serverList() const { return m_serverList; }

    /// The theme reader the Appearance pane's two overrides drive. main.cpp calls this once,
    /// before the QML engine exists; not owned. Applies whatever state.json remembers.
    void attachTheme(omaseed::integration::ThemeReader *reader);

    /// Adds a server (`fields` is `ServerListModel::serverFromMap`'s shape) and returns its
    /// id, or an empty string when the fields are unusable. `secret` is the API key or the
    /// password; it goes to `SecretStore`, never to `servers.json`.
    Q_INVOKABLE QString addServer(const QVariantMap &fields, const QString &secret = QString());
    /// Edits one. An empty `secret` keeps the stored one.
    Q_INVOKABLE bool updateServer(const QVariantMap &fields, const QString &secret = QString());
    /// Removes one, and its secrets with it.
    Q_INVOKABLE bool removeServer(const QString &id);
    /// Makes `id` the default and moves the live engine onto it: stop, reconfigure the
    /// client (including its secret), start. The hero follows through `serverChanged`.
    Q_INVOKABLE bool setDefaultServer(const QString &id);

    /// One throwaway `QbtClient` against `fields`: app/version, app/webapiVersion, then
    /// torrents/info to count. Answers with `serverTested()`, always exactly once. An empty
    /// `secret` on a server that already exists reads the stored one first. In demo mode the
    /// result is fictional (there is no network to reach).
    Q_INVOKABLE void testServer(const QVariantMap &fields, const QString &secret = QString());

    // General pane -----------------------------------------------------------------------
    /// Fig. 16's first behaviour row: whether the rail shows its SERVERS group at all.
    Q_PROPERTY(bool showServersGroup READ showServersGroup WRITE setShowServersGroup NOTIFY
                   settingsChanged)
    bool showServersGroup() const;
    void setShowServersGroup(bool show);

    /// Fig. 16's second row: apply the server's path map before asking the file manager.
    Q_PROPERTY(
        bool pathMapEnabled READ pathMapEnabled WRITE setPathMapEnabled NOTIFY settingsChanged)
    bool pathMapEnabled() const;
    void setPathMapEnabled(bool enabled);

    // Appearance pane --------------------------------------------------------------------
    /// Off pins the bundled Tokyo Night instead of following `omarchy-theme-set`.
    Q_PROPERTY(bool followOmarchyTheme READ followOmarchyTheme WRITE setFollowOmarchyTheme NOTIFY
                   settingsChanged)
    bool followOmarchyTheme() const;
    void setFollowOmarchyTheme(bool follow);

    /// A pixel size that replaces `shell.toml`'s `[font] base-size`; 0 follows the shell.
    Q_PROPERTY(
        int baseSizeOverride READ baseSizeOverride WRITE setBaseSizeOverride NOTIFY settingsChanged)
    int baseSizeOverride() const;
    void setBaseSizeOverride(int pixels);

    /// Window transparency in percent, 50…100; 0 means "the default", which is
    /// `Theme.defaultWindowAlpha` (docs/DECISIONS.md Q1: `shell.toml` `[launcher]
    /// background-alpha`, 0.95 when missing); this is its override.
    Q_PROPERTY(int windowAlphaOverride READ windowAlphaOverride WRITE setWindowAlphaOverride NOTIFY
                   settingsChanged)
    int windowAlphaOverride() const;
    void setWindowAlphaOverride(int percent);

    /// Which side of the hero the download/upload pair sits on: "left" (the spec's own
    /// placement, beside the server name) or "right" (against the search field). Anything
    /// else is read back as "left".
    Q_PROPERTY(QString speedsAlignment READ speedsAlignment WRITE setSpeedsAlignment NOTIFY
                   settingsChanged)
    QString speedsAlignment() const;
    void setSpeedsAlignment(const QString &side);

    /// #24 T2: the last 60 s of upload (and of download) drawn faintly behind the hero's ↑
    /// (↓) number. Both off by default: Diogo found them crowding the figures (2026-10-03).
    Q_PROPERTY(bool uploadTrend READ uploadTrend WRITE setUploadTrend NOTIFY settingsChanged)
    bool uploadTrend() const;
    void setUploadTrend(bool on);
    Q_PROPERTY(bool downloadTrend READ downloadTrend WRITE setDownloadTrend NOTIFY settingsChanged)
    bool downloadTrend() const;
    void setDownloadTrend(bool on);

    // docs/08 §5 — the rail's look, under Settings -> Appearance.
    /// How the rail, and since N3 every palette, says there is more above or below:
    /// "chevrons" (a counted line at each end, the default), "fade" or "scrollbar". Anything
    /// else reads back as "chevrons". Settings calls it "Scroll hint"; the name is kept so
    /// the stored key stays readable.
    Q_PROPERTY(
        QString railScrollHint READ railScrollHint WRITE setRailScrollHint NOTIFY settingsChanged)
    QString railScrollHint() const;
    void setRailScrollHint(const QString &hint);
    /// The fill the rail and details sit on, as a percentage of the foreground: 0 (none, the
    /// default), 4 or 8 — the plan's own fill steps, so every theme still holds together.
    Q_PROPERTY(int surfaceTint READ surfaceTint WRITE setSurfaceTint NOTIFY settingsChanged)
    int surfaceTint() const;
    void setSurfaceTint(int percent);
    /// How a tracker host is fitted into the rail: "prefix" (drop a leading tracker./announce./
    /// open., the default), "left" (elide the start) or "right" (elide the end).
    Q_PROPERTY(QString trackerHostStyle READ trackerHostStyle WRITE setTrackerHostStyle NOTIFY
                   settingsChanged)
    QString trackerHostStyle() const;
    void setTrackerHostStyle(const QString &style);
    /// docs/10 §5: where Details sits. "auto" (the default: right when the window is wide
    /// enough, under the table otherwise), "right" or "bottom". `I` cycles it.
    Q_PROPERTY(QString detailsPlacement READ detailsPlacement WRITE setDetailsPlacement NOTIFY
                   settingsChanged)
    QString detailsPlacement() const;
    void setDetailsPlacement(const QString &placement);
    /// The rail is hidden (`ctrl+b`); remembered across runs.
    Q_PROPERTY(bool railHidden READ railHidden WRITE setRailHidden NOTIFY settingsChanged)
    bool railHidden() const;
    void setRailHidden(bool hidden);

    /// "auto" | "always" | "never" (plan Phase 6 T6.3). `auto` behaves as `always` until the
    /// companion widget exists; the row is here now because it is cheap.
    Q_PROPERTY(QString trayIconMode READ trayIconMode WRITE setTrayIconMode NOTIFY settingsChanged)
    QString trayIconMode() const;
    void setTrayIconMode(const QString &mode);

    /// "Start at login" (Settings › General): "off", "tray" (in the tray, no window) or
    /// "window". Read from the XDG autostart entry on disk every time, so it is what the next
    /// login will do; writing it writes or removes that entry (integration/Autostart).
    Q_PROPERTY(QString startAtLogin READ startAtLogin WRITE setStartAtLogin NOTIFY settingsChanged)
    QString startAtLogin() const;
    void setStartAtLogin(const QString &mode);

    /// This process was started with `--hidden` (the autostart entry's "tray" mode): the
    /// window does not show until asked for. First run ignores it: there is nothing to do in
    /// the tray without a server.
    Q_PROPERTY(bool launchHidden READ launchHidden CONSTANT)
    bool launchHidden() const { return m_launchHidden; }
    /// Also starts the quiet start: for `quietStartGrace` a lost server sends no notice, and
    /// if it answers within that time it sends no "back" either. At login omaseed often runs
    /// before qBittorrent (a container, a service) is up; two toasts for that are noise.
    void setLaunchHidden(bool hidden);
    /// How long a hidden start waits quietly for its server (2 min; tests shorten it). Set it
    /// before setLaunchHidden().
    void setQuietStartGrace(std::chrono::milliseconds grace) { m_quietStartGrace = grace; }

    // About pane -------------------------------------------------------------------------
    /// "omaseed 0.1.0". Not `core::versionLine()`: the About pane prints the Qt version on
    /// its own row (`qtVersionText`), so VERSION repeating it read twice (batch-2 J8).
    Q_PROPERTY(QString versionText READ versionText CONSTANT)
    QString versionText() const;
    /// The Qt this binary runs against, on its own.
    Q_PROPERTY(QString qtVersionText READ qtVersionText CONSTANT)
    QString qtVersionText() const;
    /// The public repository, and the copyable line that puts the bar widget in the bar from a
    /// terminal (Settings › Appearance does the same with a switch).
    Q_PROPERTY(QString repoUrl READ repoUrl CONSTANT)
    QString repoUrl() const;
    Q_PROPERTY(QString barWidgetCommand READ barWidgetCommand CONSTANT)
    QString barWidgetCommand() const;

Q_SIGNALS:
    /// Any of the persisted settings above moved.
    void settingsChanged();
    /// One `testServer()` finished. `id` is the id the caller passed in `fields` (empty for a
    /// server that is not saved yet), so a sheet can tell its own answer from another's.
    /// When the server turned the credential down, `error` says so in words
    /// (`credentialRefusedText`) and `errorDetail` carries the raw error, for a dim second
    /// line; otherwise `error` is the raw error and `errorDetail` is empty.
    void serverTested(const QString &id,
                      bool ok,
                      const QString &version,
                      const QString &apiVersion,
                      int torrentCount,
                      int latencyMs,
                      const QString &error,
                      const QString &errorDetail);

private:
    void loadSettings();
    void saveSettings();
    void applyAppearance();
    /// state.json, section by section. A demo reads it but never writes it: its changes
    /// (appearance, notices, columns, the delete default) stay in memory, so an agent's
    /// `--demo` run cannot change how the real app starts next time.
    QJsonObject stateSection(const QString &key) const;
    void writeStateSection(const QString &key, const QJsonObject &value);
    QJsonObject m_demoState;
    /// The shared tail of `testServer()`: run the probe with the secret already resolved.
    void runServerTest(const omaseed::core::Server &server, const QString &secret);

    omaseed::core::ServerListModel *m_serverList =
        new omaseed::core::ServerListModel(m_servers, m_secrets, this);
    QPointer<omaseed::integration::ThemeReader> m_themeReader;

    bool m_showServersGroup = true;
    bool m_pathMapEnabled = true;
    bool m_followOmarchyTheme = true;
    int m_baseSizeOverride = 0;
    int m_windowAlphaOverride = 0;
    QString m_trayIconMode = QStringLiteral("auto");
    bool m_launchHidden = false;
    QString m_speedsAlignment = QStringLiteral("left");
    bool m_uploadTrend = false;
    bool m_downloadTrend = false;
    QString m_railScrollHint = QStringLiteral("chevrons");
    QString m_detailsPlacement = QStringLiteral("auto");
    int m_surfaceTint = 0;
    QString m_trackerHostStyle = QStringLiteral("prefix");
    bool m_railHidden = false;
    bool m_settingsLoaded = false;
    // --- end T4.5 --------------------------------------------------------------------------
    // --- T4.3: torrent actions ---
    // Every action of spec §3 Fig. 6/7/8/11 and Fig. 26's "act" column that touches the
    // server lives here, over `QbtClient`. Each one no-ops in demo mode (logging
    // "demo: <action> <n>") and otherwise reports a failure through `actionFailed` rather
    // than throwing it away, the way `toggleAltSpeed`/`stopAll` do above.
public:
    /// The remembered state of Fig. 6's "also delete files from disk" toggle. Persisted under
    /// the `"deleteFiles"` section of `~/.config/omaseed/state.json`.
    Q_PROPERTY(bool deleteFilesDefault READ deleteFilesDefault WRITE setDeleteFilesDefault NOTIFY
                   deleteFilesDefaultChanged)
    bool deleteFilesDefault() const;
    void setDeleteFilesDefault(bool value);

    /// `p`/`P`: stop/start decided from the cursor torrent's own state (the first hash of
    /// `hashes`), applied to every hash in the selection; `forceStart` always starts with the
    /// force flag set.
    Q_INVOKABLE void stopStart(const QStringList &hashes);
    /// The selection bar's two buttons (docs/17 M9.3): a selection can hold stopped and
    /// running torrents, so each one says which way it goes instead of following the first.
    Q_INVOKABLE void startTorrents(const QStringList &hashes);
    Q_INVOKABLE void stopTorrents(const QStringList &hashes);
    Q_INVOKABLE void forceStart(const QStringList &hashes);
    Q_INVOKABLE void recheck(const QStringList &hashes);
    Q_INVOKABLE void reannounce(const QStringList &hashes);
    /// The Trackers tab's `d` (Fig. 3): trackers off one torrent, in one call (the tab's
    /// selection, or the cursor's row, #51 D15).
    Q_INVOKABLE void removeTrackers(const QString &hash, const QStringList &urls);
    /// The Trackers tab's `a` (ruling Q4): announce URLs onto one torrent. The details pane
    /// polls again once the server has them.
    Q_INVOKABLE void addTrackers(const QString &hash, const QStringList &urls);
    /// The HTTP sources tab's `a` (#10): web seed URLs onto one torrent; the list is asked
    /// for again once the server has them.
    Q_INVOKABLE void addWebSeeds(const QString &hash, const QStringList &urls);
    /// The HTTP sources tab's `d` (#10): web seeds off one torrent, in one call.
    Q_INVOKABLE void removeWebSeeds(const QString &hash, const QStringList &urls);
    /// The Peers tab's `a` (ruling Q4): `host:port` peers onto one torrent.
    Q_INVOKABLE void addPeers(const QString &hash, const QStringList &peers);
    /// The Peers tab's `b` (ruling Q4): `host:port` peers into qBittorrent's IP filter, in
    /// one call. The ban is permanent and global (every torrent, until removed in
    /// qBittorrent's settings), so the view asks first.
    Q_INVOKABLE void banPeers(const QStringList &addresses);
    /// The add-tracker and add-peer sheets' light check (core::PeerInput): `{entries,
    /// invalid}`, the accepted entries and the first refused one ("" when none was).
    Q_INVOKABLE QVariantMap checkTrackerUrls(const QString &text) const;
    Q_INVOKABLE QVariantMap checkPeers(const QString &text) const;
    /// The add-HTTP-source sheet's check: http or https URLs with a host (#10).
    Q_INVOKABLE QVariantMap checkWebSeedUrls(const QString &text) const;

    Q_INVOKABLE void setCategory(const QStringList &hashes, const QString &name);
    Q_INVOKABLE void addTags(const QStringList &hashes, const QStringList &tags);
    Q_INVOKABLE void removeTags(const QStringList &hashes, const QStringList &tags);

    Q_INVOKABLE void setLocation(const QStringList &hashes, const QString &path);
    /// `dl`/`ul` in bytes/s, -1 = unchanged, 0 = unlimited. `ratio` and `seedingMinutes`
    /// always go together through torrents/setShareLimits: -2 = use the global default,
    /// -1 = no limit, >=0 the limit itself (`seedingMinutes` is the API's own unit).
    /// `shareLimitAction` (T5.5 Q14) is the wire name of what happens when a limit is
    /// reached — Default, Stop, Remove, RemoveWithContent, EnableSuperSeeding; empty reads
    /// as Default. It goes on the wire when `supportsShareLimitAction` is true (Default
    /// included: 5.2.3 answers 400 without the field) and is left off for a server known to
    /// be older than API 2.15.1. `inactiveSeedingMinutes` rides in the same call (same
    /// sentinels); the sheet has no field for it, so it passes each torrent's own value
    /// (`torrentRow().inactiveSeedingTimeLimit`) and an untouched limit survives the apply.
    Q_INVOKABLE void setLimits(const QStringList &hashes,
                               qint64 dl,
                               qint64 ul,
                               double ratio,
                               qint64 seedingMinutes,
                               const QString &shareLimitAction = QString(),
                               qint64 inactiveSeedingMinutes = -2);

    /// The sheet's fallback rename (the table's inline editor is the primary one).
    Q_INVOKABLE void rename(const QString &hash, const QString &name);

    Q_INVOKABLE void queueUp(const QStringList &hashes);
    Q_INVOKABLE void queueDown(const QStringList &hashes);

    Q_INVOKABLE void remove(const QStringList &hashes, bool deleteFiles);

    /// docs/12 G4: sequential download, first/last pieces first, super seeding and automatic
    /// torrent management, set to `on` on every hash (a mixed selection comes out uniform).
    /// In demo mode the fictional rows change locally.
    Q_INVOKABLE void setSequentialDownload(const QStringList &hashes, bool on);
    Q_INVOKABLE void setFirstLastPiecePrio(const QStringList &hashes, bool on);
    Q_INVOKABLE void setSuperSeeding(const QStringList &hashes, bool on);
    Q_INVOKABLE void setAutoManagement(const QStringList &hashes, bool on);

    /// `y`/`Y`: no server round trip, so it runs in demo mode too.
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    /// `o`: the cursor torrent's `contentPath`, mapped through the server's `PathMap` and
    /// handed to `Xdg::showInFileManager`.
    Q_INVOKABLE void openFolder(const QString &hash);
    /// `browse` / `ctrl+o` in the location sheet and Save to's `browse` in the add sheet (#41):
    /// the portal's folder picker, starting at
    /// `startDir`. An empty result means the picker was cancelled or no portal answered; the
    /// caller keeps whatever was typed.
    Q_INVOKABLE QString pickDirectory(const QString &startDir) const;
    /// Browse (#41, docs/27 D12 a): where the folder picker opens for `serverPath`, a path
    /// the server sees. Its local side when the server's path map (switched on) covers it,
    /// else the path itself.
    Q_INVOKABLE QString browseStartFor(const QString &serverPath) const;
    /// Browse: a folder picked on this machine, as the server is to be told it.
    /// `{path, local, mapped, outside, mappings}`: `path` is what the field takes; `mapped`
    /// when the map rewrote it; `outside` when the server has a map (switched on) and the pick
    /// lies outside every mapping, so the server may not see it; `mappings` lists them as
    /// "local → remote, …". Without a map the pick is taken as it is.
    Q_INVOKABLE QVariantMap serverPathForPick(const QString &localPath) const;

Q_SIGNALS:
    void deleteFilesDefaultChanged();
    /// A server action from this block came back with an error. MainView shows it in the
    /// footer facts for a few seconds.
    void actionFailed(const QString &action, const QString &message);
    /// `loadDuplicateTrackers`' answer: `missing` (what a merge would add) and `existing`
    /// (the torrent's real trackers, without qBittorrent's DHT / PeX / LSD rows).
    void duplicateTrackersLoaded(const QString &hash,
                                 const QStringList &missing,
                                 const QStringList &existing);
    /// `mergeTrackers` went through: `count` trackers were added to `hash`.
    void trackersMerged(const QString &hash, int count);
    /// `findFilesMatch`' answer for `token`: one map per library torrent holding the files,
    /// seeding ones first: `hash`, `name`, `tracker` (its host, or ""), `stateText`,
    /// `ratioText`, `seeding`, `savePath` and `layout` (what the add should take: "Original" or
    /// "NoSubfolder"), `fileCount`, `contentPath` (the folder on the server). Torrents that
    /// already share one folder are answered once.
    void filesMatchFound(const QString &token, const QVariantList &matches);
    /// `C`/`t`: T4.4's category/tag palettes answer these; this block only raises them.
    void categoryPickerRequested(const QStringList &hashes);
    void tagPickerRequested(const QStringList &hashes);

private:
    /// True (and logged) in demo mode, where none of these actions may touch a network.
    bool demoGuard(const QString &action, int count);
    /// G4: the hashes whose `field` is not `on` yet (what an endpoint must change). A change
    /// still pending in the engine (`key` is its maindata name) counts as done, so a second
    /// press before the server has answered does not flip the same torrents back.
    QStringList hashesNotAt(const QStringList &hashes,
                            bool on,
                            bool omaseed::core::Torrent::*field,
                            const QString &key) const;
    /// G4 / M12: shows `key` = `on` on `hashes` at once (SyncEngine's pending values) and
    /// sends `reply`'s request; its answer settles them, a refusal puts the old value back.
    void sendOption(const QStringList &hashes,
                    bool on,
                    bool omaseed::core::Torrent::*field,
                    const QString &key,
                    omaseed::core::QbtReply<QByteArray> *reply,
                    const QString &action);
    /// G4 in demo mode: sets `key` to `on` on the fictional rows and returns true.
    bool applyDemoOption(const QStringList &hashes, const QString &key, bool on);
    void reportActionResult(const QString &action,
                            const omaseed::core::QbtResult<QByteArray> &result);
    void loadDeleteFilesDefault();
    /// StoppedDownload/StoppedUpload/PausedDownload/PausedUpload: what `stopStart()` and
    /// `torrentRow()`'s `stopped` field both call "stopped".
    static bool isStoppedState(omaseed::core::TorrentState state);
    /// What `p` (stopStart) does to a torrent in `state`: start it when stopped, or in error
    /// ("try again", docs/28), stop it otherwise.
    static bool startsOnToggle(omaseed::core::TorrentState state);

    bool m_deleteFilesDefault = false;
    bool m_deleteFilesDefaultLoaded = false;
    // --- end T4.3 ------------------------------------------------------------------------
    // --- end T4.7 ----------------------------------------------------------------------------

    // --- T4.4: palettes ---------------------------------------------------------------------
    // Jump (`ctrl+k`), category, tags, sort and columns are all `Kit/Palette` over data this
    // block hands them. None of it is view state (that stays in the QML palettes and in
    // `TorrentTable`); this is only the parts that need the client, the models App already
    // owns, or `StateFile` (the columns/sort round trip, keyed by server id).
public:
    /// The jump palette's rows: every torrent whose name or tracker host matches `query`
    /// (case-insensitive substring-or-subsequence per whitespace-separated word, empty query
    /// matches everything), or whose v1/v2 infohash *starts with* it (a hex-only query, no
    /// subsequence: 40 hex characters match almost any subsequence). Newest match first is
    /// NOT guaranteed — the
    /// library's own order is kept. Capped at `limit` rows; `jumpMatchCount()` is the true
    /// count. Each row is `{key: hash, title: name, meta: stateText, detail: "<category> ·
    /// <sizeText> · <progressText or "ratio X.X">"}` (spec §4 Fig. 12).
    Q_INVOKABLE QVariantList jumpMatches(const QString &query, int limit = 50) const;
    /// The true number of matches `jumpMatches()` would find without the cap.
    Q_INVOKABLE int jumpMatchCount(const QString &query) const;

    /// POST torrents/createCategory with no save path (Fig. 13's "new name creates it").
    Q_INVOKABLE void createCategory(const QString &name);
    /// The category palette's `create "x"` on torrents: set the category only once the server
    /// has created it (two parallel requests could set it first and get a 409). Both
    /// failures reach the footer.
    Q_INVOKABLE void createAndApplyCategory(const QStringList &hashes, const QString &name);
    /// POST torrents/setCategory. A no-op in demo mode beyond a log line.
    Q_INVOKABLE void applyCategory(const QStringList &hashes, const QString &name);

    /// POST torrents/createTags with one tag.
    Q_INVOKABLE void createTag(const QString &name);
    /// torrents/addTags for `added`, torrents/removeTags for `removed`. A no-op in demo mode
    /// beyond a log line.
    Q_INVOKABLE void
    applyTags(const QStringList &hashes, const QStringList &added, const QStringList &removed);
    /// The tags of one torrent, split the way `torrentRow()`'s `tagsText` joins them, for the
    /// tag palette's per-row `checked` state.
    Q_INVOKABLE QStringList tagsOf(const QString &hash) const;

    /// The key `saveTableState()`/`tableState()` persist under: the chosen server's id, or
    /// `"demo"` under `--demo`, or an empty string with no server (nothing persists then).
    Q_INVOKABLE QString tableStateKey() const;
    /// Writes `{"columns": [...], "sortRole": "...", "sortOrder": int}` under
    /// `"tables"."<key>"` of `state.json`. A no-op when `tableStateKey()` is empty.
    Q_INVOKABLE void saveTableState(const QString &key, const QVariantMap &state);
    /// The map `saveTableState()` wrote for `key`, or an empty map.
    Q_INVOKABLE QVariantMap tableState(const QString &key) const;

    /// docs/30 D7 (#61): the action ids of the last five commands run from the command
    /// palette, newest first, from `"commands"."recent"` of `state.json`.
    Q_INVOKABLE QStringList recentCommands() const;
    /// Puts `id` at the front of `recentCommands()`, dropping an older copy and the sixth.
    Q_INVOKABLE void noteCommandRun(const QString &id);

Q_SIGNALS:
    /// The jump palette's `tab` (Fig. 12's "tab actions"): jump to `hash`, then open the
    /// actions menu on it. `MainView`'s `Connections` block answers it.
    void showActionsRequested(const QString &hash);
    /// T4.3's `C`/`t` (or the AddSheet's chips) ask App for the category/tag picker; this
    /// block's `Connections { target: App }` in MainView opens `CategoryPalette`/`TagPalette`
    /// on `hashes` in response.

private:
    static bool matchesQuery(const QString &text, const QString &query);
    /// True when `query` is a non-empty hex string and `hash` starts with it.
    static bool matchesHashPrefix(const QString &hash, const QString &query);
    /// The jump palette's whole per-torrent test: name/tracker fuzzy, hash by prefix.
    static bool matchesTorrent(const core::Torrent &torrent, const QString &query);
    // --- end T4.4 ------------------------------------------------------------------------
    // --- T4.2: add flow ---------------------------------------------------------------------
    // Everything the add sheet (spec §3 Fig. 5) and the duplicate sheet (Fig. 10) need from
    // App: local parsing of what the user pasted, dropped, picked or handed over on the
    // command line, the duplicate test, and the POST itself. Same arrangement as the blocks
    // above: one block at the end of the class and one at the end of App.cpp.
    /// The add sheet's file picker (docs/11 D1): the files of the `.torrent` the sheet shows,
    /// filled by `loadAddFiles(token)`.
    Q_PROPERTY(omaseed::core::AddFilesModel *addFiles READ addFiles CONSTANT)

public:
    omaseed::core::AddFilesModel *addFiles() const { return m_addFiles; }

    /// Shows the files of the parsed `.torrent` behind `token` in `addFiles`, every one at
    /// "normal"; empties it when the token names nothing (a magnet, a URL, a stale parse).
    Q_INVOKABLE void loadAddFiles(const QString &token);

    /// What one line of "magnet / http(s) URL / .torrent path" is, as the sheet draws it:
    ///   kind         "magnet" | "url" | "file" | "none"
    ///   valid        bool, false when nothing recognisable was in `text`
    ///   pending      bool, true while a .torrent is still being read and hashed
    ///   name         the parsed name, or a URL's last path segment
    ///   sizeText     "2.34 GB", empty when the source does not say
    ///   totalSize    the same in bytes, absent when the source does not say
    ///   trackers     a `.torrent`'s announce URLs, or a magnet's `tr=`, deduplicated
    ///   isPrivate    a `.torrent` carries the private flag (always false for a magnet)
    ///   infoHashV1 / infoHashV2   lowercase hex, empty when unknown
    ///   value        what goes to the server (the magnet, the URL, the file path)
    ///   token        the key `addTorrent` uses to find a picked file's bytes again
    ///   fileCount    a `.torrent`'s number of files (0 for a magnet or a URL)
    ///   error        why `valid` is false
    /// A magnet and a URL are answered in the return value. A `.torrent` is read and hashed
    /// on a worker thread (plan §4: never on the GUI thread) and answered a second time
    /// through `addableParsed`; the returned map has `pending: true` until then.
    Q_INVOKABLE QVariantMap parseAddable(const QString &text);

    /// `ctrl+o` in the add sheet: the file chooser for one `.torrent`. Uses
    /// QFileDialog::getOpenFileContent, so no widget window of our own is ever mapped and
    /// the XDG portal serves the dialog under Wayland. The chosen file is parsed like any
    /// other and reported through `addableParsed` (with `source: "file"`).
    Q_INVOKABLE void pickTorrentFile();
    /// #40: the same chooser for several `.torrent` files at once (QFileDialog's portal
    /// picker, multi-select); their paths, or an empty list when it was cancelled.
    Q_INVOKABLE QStringList pickTorrentFiles();
    /// #40: every source in a pasted or dropped text, in order: one per line, links side by
    /// side on one line, and magnets glued together (a single-line field drops the line
    /// breaks between them). Each is what `parseAddable` takes.
    Q_INVOKABLE QStringList splitAddables(const QString &text) const;

    /// Fig. 10's test: the library row either infohash already occupies (`torrentRow`'s
    /// fields), or an empty map. Both hashes are matched against the maindata `infohash_v1`/
    /// `infohash_v2` fields as well as the key (see `TorrentFile::torrentIdFor`).
    Q_INVOKABLE QVariantMap findDuplicate(const QString &hashV1, const QString &hashV2) const;

    /// docs/25 §2 (#26): Fig. 10 asks which of the duplicate source's `urls` the torrent
    /// `hash` lacks. Answered through `duplicateTrackersLoaded` once `torrents/trackers` has
    /// come back (core::TrackerMerge decides what counts as the same tracker).
    Q_INVOKABLE void loadDuplicateTrackers(const QString &hash, const QStringList &urls);
    /// Fig. 10's `merge N trackers`: adds `urls` to `hash`, then `trackersMerged`.
    Q_INVOKABLE void mergeTrackers(const QString &hash, const QStringList &urls);

    /// docs/25 §3 (#27): looks in the library for the files of the `.torrent` parsed under
    /// `token`, already on disk under another (complete) torrent: same total size first, then
    /// every file's path and size through `torrents/files` (core::LibraryMatch, exact matches
    /// only). Answered through `filesMatchFound`; an empty list when there is none.
    Q_INVOKABLE void findFilesMatch(const QString &token);

    /// Adds what the sheet holds: `parseAddable`'s map plus `category`, `tags` (list),
    /// `savePath`, `autoTMM`, `contentLayout` ("Original" | "Subfolder" | "NoSubfolder"),
    /// `upLimitKb` (KB/s, 0 or absent = unlimited), `sequentialDownload`, `skipChecking` and
    /// `stopped`, plus `rename` (the torrent's name; empty keeps its own), `dlLimitKb`,
    /// `firstLastPiecePrio` and, for a `.torrent`, `filePriorities` (one per file id,
    /// `addFiles.priorityList()`). qBittorrent takes no file priorities on a file upload, so
    /// when any of them is not "normal" the torrent is added stopped, the priorities are set
    /// with `torrents/filePrio`, and it is started unless `stopped` asked otherwise.
    /// Answers with `addFinished`; on success the new hash, once the next sync
    /// brings it, also travels as `showTorrentRequested` so the table's cursor lands on it.
    /// In demo mode nothing is sent: the add is logged and reported as a success.
    ///
    /// #40, the sheet's batch: a request with `items` (a list of `parseAddable` maps) adds
    /// all of them in one `torrents/add` under the request's options (no rename, no file
    /// choices), and answers one `addFinished` with the first one's hash.
    Q_INVOKABLE void addTorrent(const QVariantMap &request);

    /// `OMASEED_DEMO_ADD`: the text a `--demo` run drops into the sheet when it opens, so
    /// Fig. 5 and Fig. 10 can be screenshot reproducibly. Empty everywhere else.
    Q_INVOKABLE QString demoAddText() const;
    /// The example `.torrent` behind `OMASEED_DEMO_ADD=torrent` (DemoData::demoTorrentBytes),
    /// written once into a private temporary directory of this process; its path. Empty
    /// outside demo mode.
    Q_INVOKABLE QString demoTorrentPath() const;

    /// The add sheet's "in Downloads" (manual box M7): the newest `.torrent` files in
    /// `downloadsFolder` (core::TorrentFolderScan, at most five), filled by `scanDownloads()`.
    /// Each entry: `path`, `name`, `sizeText`, `ageText` (the file's time, as
    /// Format::relativeTime words it), `infoHashV1`, `infoHashV2`, and `inLibrary` (the
    /// library already holds that infohash: `findDuplicate`).
    Q_PROPERTY(
        QVariantList downloadsTorrents READ downloadsTorrents NOTIFY downloadsTorrentsChanged)
    /// Where `scanDownloads` looks: the user's Downloads (QStandardPaths::DownloadLocation)
    /// until something is written here. A test seam: nothing in the app writes it. A demo run
    /// lists nothing unless it is set, so no real file name lands in a screenshot.
    Q_PROPERTY(QString downloadsFolder READ downloadsFolder WRITE setDownloadsFolder NOTIFY
                   downloadsFolderChanged)
    Q_PROPERTY(int freeSpaceRevision READ freeSpaceRevision NOTIFY freeSpaceChanged)

public:
    QVariantList downloadsTorrents() const { return m_downloadsTorrents; }
    QString downloadsFolder() const;
    void setDownloadsFolder(const QString &folder);

    /// Reads `downloadsFolder` on the thread pool (the files are parsed and hashed) and
    /// answers through `downloadsTorrents`. The sheet calls it each time it opens; a scan
    /// overtaken by a newer one is dropped. Each entry's `inLibrary` then follows the library
    /// as torrents come and go, without another scan.
    Q_INVOKABLE void scanDownloads();

    /// docs/25 §4 (#29): how much room Save to has, for the add sheet's line under it.
    ///   source        "default" (the server's own `free_space_on_disk`), "local" (read on
    ///                 this machine, core::FreeSpaceProbe) or "unknown"
    ///   bytesFree     the room, 0 when unknown
    ///   bytesTotal    the drive's size for "local", 0 otherwise
    ///   where         the mount point for "local", "" otherwise
    ///   defaultFree   the server's own figure, for scale when the answer is unknown
    /// `freeSpaceRevision` moves whenever any of it may have changed.
    Q_INVOKABLE QVariantMap freeSpaceFor(const QString &savePath) const;
    int freeSpaceRevision() const { return m_freeSpaceRevision; }

    /// The URI hand-off: this process's own argv and every `SingleInstance::openRequested`
    /// from a second launch. Re-emitted as `addUrisRequested` for the sheet to queue.
    void openUris(const QStringList &uris);

Q_SIGNALS:
    void freeSpaceChanged();
    /// The second half of `parseAddable` for a `.torrent`, and the whole answer of
    /// `pickTorrentFile()`. Same map, with `pending: false`.
    void addableParsed(QVariantMap result);
    /// `openUris` reached QML: the first one opens the sheet, the rest queue behind it.
    void addUrisRequested(QStringList uris);
    /// `ok` false: `message` is what the sheet prints in red while staying open. `ok` true:
    /// `hash` is the new torrent's infohash, or empty when the source did not say.
    void addFinished(bool ok, QString message, QString hash);
    void downloadsTorrentsChanged();
    void downloadsFolderChanged();

private:
    /// A `.torrent`'s bytes, kept between the parse and the POST because the portal's file
    /// chooser hands over content, not a path. Keyed by the map's `token`.
    struct PendingTorrentFile
    {
        QString fileName;
        QByteArray bytes;
        /// The parsed file list, for `loadAddFiles`, and the id qBittorrent will key it by.
        QList<omaseed::core::TorrentFileEntry> files;
        QString torrentId;
        /// For the files match (docs/25 §3): the torrent's name and whether it is a folder.
        QString name;
        bool multiFile = false;
        qint64 pieceLength = 0;
    };

    /// Reads `path` (when `bytes` is empty) and parses the .torrent on the global thread pool
    /// — bencode and both info-hashes, never on the GUI thread (plan §4 T4.2) — then emits
    /// `addableParsed` back here and remembers the bytes under `token`.
    void parseTorrentInBackground(const QString &token,
                                  const QString &fileName,
                                  const QString &value,
                                  const QString &path,
                                  const QByteArray &bytes);
    /// The addable map for a parsed (or failed) .torrent. Pure: no App state.
    static QVariantMap torrentFileMap(const QString &token,
                                      const QString &fileName,
                                      const QString &value,
                                      const omaseed::core::TorrentFile &parsed,
                                      const QString &error);

    /// Keeps a parsed file for addTorrent, dropping the oldest past a fixed number, so files
    /// parsed and then abandoned do not pile up for the life of the process.
    void rememberTorrentFile(const QString &token, const PendingTorrentFile &file);
    void forgetTorrentFile(const QString &token);
    /// #40: addTorrent's batch half.
    void addBatch(const QVariantMap &request);

public:
    /// The thread that ran the most recent .torrent parse, so a test can check it was not the
    /// GUI thread.
    static QThread *lastTorrentParseThread();
    /// How many parsed .torrent files are held for a later add (bounded).
    int pendingTorrentFileCount() const;

private:
    /// After a stopped add: `torrents/filePrio` for each group of `priorities`, then
    /// `torrents/start` unless `keepStopped`, then `addFinished`. A just-added torrent can
    /// take a moment to exist on an older server, so a 404/409 is retried for a few seconds.
    void applyAddFilePriorities(const QString &id,
                                const QVariantList &priorities,
                                bool keepStopped,
                                int attempt = 0);
    /// After a stopped add, before the priorities (manual box M18): reads `torrents/files`
    /// once to learn the root folder the server puts in front of the paths (`anchor` is a
    /// file id and its metainfo path), then sends each of `renames` in order through
    /// `torrents/renameFile` / `torrents/renameFolder`, then goes on to
    /// `applyAddFilePriorities`. A failed rename stops there: the torrent stays stopped and
    /// `addFinished` says the names were not applied. A 404/409 or an empty file list (the
    /// metadata not loaded yet) is retried for a few seconds, as the priorities are.
    void applyAddRenames(const QString &id,
                         const QVariantList &renames,
                         const QVariantMap &anchor,
                         const QVariantList &priorities,
                         bool keepStopped,
                         int attempt = 0);
    /// #64: what the details torrent's renames would land on, on disk. Sees nothing (and so
    /// refuses nothing) unless `hash` is the details torrent and this machine reads the
    /// server's disks: FreeSpaceProbe's calibration (#29), through the path map when there
    /// is one.
    omaseed::core::RenameDisk renameDisk(const QString &hash) const;
    QString m_demoDiskFolder;
    /// Sends `moves[at]` of a renameTorrentFiles() run and the rest after it.
    void
    sendFileRename(const QString &hash, const QList<std::pair<QString, QString>> &moves, int at);
    /// Sends `renames[at]` (paths prefixed with `root`) and the rest after it.
    void sendAddRename(const QString &id,
                       const QVariantList &renames,
                       int at,
                       const QString &root,
                       const QVariantList &priorities,
                       bool keepStopped);

    /// Builds the client's options from the sheet's payload.
    omaseed::core::QbtClient::AddOptions addOptionsFrom(const QVariantMap &request) const;
    /// Waits up to 10 s for `hash` to turn up in the model, then asks for the cursor.
    void watchForAddedHash(const QString &hash);

    QHash<QString, PendingTorrentFile> m_pendingTorrentFiles;
    core::FreeSpaceProbe m_freeSpace;
    int m_freeSpaceRevision = 0;
    QStringList m_pendingTorrentOrder; ///< tokens of m_pendingTorrentFiles, oldest first
    mutable std::unique_ptr<QTemporaryDir> m_demoTempDir;
    mutable QString m_demoTorrentPath;
    QString m_awaitedAddHash;
    QTimer *m_addWatchTimer = nullptr;
    QMetaObject::Connection m_addWatch;
    int m_nextAddableToken = 0;
    omaseed::core::AddFilesModel *m_addFiles = new omaseed::core::AddFilesModel(this);
    QVariantList m_downloadsTorrents;
    /// Each entry's `inLibrary` read again: the library moved since the scan (#20).
    void refreshDownloadsInLibrary();
    QString m_downloadsFolder; ///< empty: QStandardPaths::DownloadLocation
    int m_downloadsScan = 0;   ///< the newest scan's number; older answers are dropped
    // --- end T4.2 ------------------------------------------------------------------------

    // --- T4.6: first run -------------------------------------------------------------------
    // Everything `Views/FirstRunPage.qml` (spec v2 §5 Fig. 15) needs from App: whether the
    // page should show at all, the async local-server detection `ServerDetector` performs,
    // and the "install qbittorrent-nox here" step's command text and `pkexec` run. Adding a
    // server, testing it and making it the default are already T4.5's `addServer`/
    // `testServer`/`setDefaultServer` above; this block only adds what is new for first run.
public:
    /// True while the server store is empty — nothing was ever configured, so MainView shows
    /// the first-run page instead of `EmptyState`. Independent of the environment-variable
    /// fallback (`OMASEED_SERVER_URL`): an env-only session still has an empty store and
    /// would show first run again next time it runs without the env set, which is correct.
    Q_PROPERTY(bool firstRunNeeded READ firstRunNeeded NOTIFY serverChanged)
    bool firstRunNeeded() const { return m_servers->count() == 0; }

    /// Why `servers.json` could not be read at start (corrupt, or from a newer build); empty
    /// otherwise. First run then shows although the user has servers: this says why. The
    /// unreadable file is never overwritten: it is moved aside as soon as it fails to load.
    Q_PROPERTY(QString serversLoadError READ serversLoadError NOTIFY serverChanged)
    QString serversLoadError() const { return m_servers->loadError(); }
    /// Where that unreadable file is kept (`servers.json.unreadable-<timestamp>`, or
    /// `servers.json` itself if it could not be moved); empty when servers.json loaded. The
    /// first-run page names it (review Q9).
    Q_PROPERTY(QString serversKeptPath READ serversKeptPath NOTIFY serverChanged)
    QString serversKeptPath() const { return m_servers->keptPath(); }

    /// The directory that holds the API keys and passwords in plain (0600) files while no
    /// keychain answered, so Settings › Servers can say so (review Q10); empty while the
    /// keychain is in use, before the backend is known, and in a demo. Reading it never
    /// starts the keychain probe.
    Q_PROPERTY(QString plainSecretsPath READ plainSecretsPath NOTIFY plainSecretsPathChanged)
    QString plainSecretsPath() const;

    /// Starts `ServerDetector` (a TCP probe of 127.0.0.1:8080 — connect-and-close only, never
    /// an HTTP request — plus a `docker ps` and a `pgrep` check). Answers with
    /// `serversDetected()`, always exactly once. Safe to call again while a previous run is
    /// still in flight; only the last one's answer is emitted.
    Q_INVOKABLE void detectServers();

    /// The exact shell commands the install step's copyable field shows, one per line: the
    /// package (`sudo pacman -S …`: text for the user to run in their own terminal, never
    /// run by the app), then enabling the per-user template unit qbittorrent-nox ships
    /// (`qbittorrent-nox@.service`, `pacman -Ql qbittorrent-nox | grep service`).
    Q_INVOKABLE QString installCommandsText() const;
    /// Runs `pkexec pacman -S --noconfirm qbittorrent-nox` — never `sudo` from inside the
    /// app. Answers with `installFinished()`. Does not enable the service: that needs a user
    /// systemd instance the installer cannot assume is the one running this session.
    Q_INVOKABLE void runInstallWithPkexec();
    /// What runInstallWithPkexec runs, with the command left open so a test can drive the
    /// failure paths (a crash, a missing program) with something harmless. Always exactly
    /// one `installFinished()` per call.
    void runInstall(const QString &program, const QStringList &arguments);

Q_SIGNALS:
    /// One `detectServers()` answer: each candidate is `{kind: "port"|"docker"|"process",
    /// url, label, detail}` per `ServerDetector::DetectedServer`. `anotherRunning` is true
    /// when more than one qBittorrent process runs here, though at most one is a candidate
    /// (Q26: the done screen says the other can be added in Settings › Servers).
    void serversDetected(QVariantList candidates, bool anotherRunning);
    /// One `runInstallWithPkexec()` answer.
    void installFinished(bool ok, QString message);
    void plainSecretsPathChanged();

private:
    QPointer<omaseed::core::ServerDetector> m_serverDetector;
    QPointer<QProcess> m_installProcess;
    // --- end T4.6 ------------------------------------------------------------------------

    // --- T5.5 Q14: share-limit action ------------------------------------------------------
public:
    /// True once the connected server's Web API is known to be 2.15.1 or newer (the version
    /// the `shareLimitAction` field and the `share_limit_action` row appeared in), so the
    /// Limits sheet can show its "when the limit is reached" row. Follows
    /// `globalState.apiVersion`; false while the version is still unknown. The demo library
    /// reports 2.15.1, so the row shows there too.
    Q_PROPERTY(bool supportsShareLimitAction READ supportsShareLimitAction NOTIFY
                   supportsShareLimitActionChanged)
    bool supportsShareLimitAction() const;

Q_SIGNALS:
    void supportsShareLimitActionChanged();
    /// Every `setLimits()` call as it was received, before the demo guard and the API-version
    /// decision — the QML tests' view of what the Limits sheet converted its fields into.
    void limitsRequested(QStringList hashes,
                         qint64 dl,
                         qint64 ul,
                         double ratio,
                         qint64 seedingMinutes,
                         QString shareLimitAction,
                         qint64 inactiveSeedingMinutes);
    // --- end T5.5 Q14 ----------------------------------------------------------------------

    // --- docs/08 §3: the hero's ⋯ menu -----------------------------------------------------
public:
    /// Fetch the server's four global limits (bytes/s, 0 = none); they arrive through
    /// `globalLimitsLoaded`. The demo answers with its own figures.
    Q_INVOKABLE void requestGlobalLimits();
    /// Write the global limits through app/setPreferences. -1 leaves a value as it is, 0 lifts
    /// it; only the values that are not -1 are sent.
    Q_INVOKABLE void setGlobalLimits(qint64 dl, qint64 up, qint64 altDl, qint64 altUp);

    /// docs/16: qBittorrent's own settings for the server the app is on. Emptied on a switch,
    /// read when the server connects and whenever Settings asks.
    Q_PROPERTY(omaseed::core::ServerPreferences *serverPrefs READ serverPrefs CONSTANT)
    omaseed::core::ServerPreferences *serverPrefs() const { return m_prefs; }

private:
    /// A change the server took may move omaseed's own way in (docs/16 §3): the Web UI's
    /// port or HTTPS switch changes the saved URL, and its login changes a cookie server's
    /// saved username and password.
    void followServerPreferences(const QVariantMap &changes, const QVariantMap &before);
    /// A new Web UI API key: a server signed in with a key keeps working with the new one.
    void followRotatedApiKey(const QString &key);
    /// Whether a Web UI answers at `base` in its scheme: any HTTP answer, or (https) a
    /// certificate shown, trusted or not. Tries `attempts` times, half a second apart, while
    /// qBittorrent restarts its Web UI; `done(false)` when it never did.
    void probeWebUi(const QUrl &base, std::function<void(bool)> done, int attempts);
    omaseed::core::ServerPreferences *m_prefs = nullptr;
    QNetworkAccessManager *m_probeNam = nullptr;

public:
    /// The URL the saved server should have after `changes` (docs/16 §3), or the same URL
    /// when nothing in them moves it. Only a URL on the Web UI's own port follows a port
    /// change: one behind a reverse proxy does not.
    static QUrl
    urlAfterPreferences(const QUrl &url, const QVariantMap &changes, const QVariantMap &before);

Q_SIGNALS:
    /// A Web UI change the app did not follow, in words: HTTPS turned on that qBittorrent
    /// could not start (no certificate), so it stays on HTTP and so does the saved server.
    void webUiNotFollowed(const QString &why);
    void globalLimitsLoaded(qint64 dl, qint64 up, qint64 altDl, qint64 altUp);
    /// Every `setGlobalLimits()` call as received, before the demo guard (for the QML tests).
    void globalLimitsRequested(qint64 dl, qint64 up, qint64 altDl, qint64 altUp);
    // --- end docs/08 §3 ---------------------------------------------------------------------

    // --- N1: the Files tab's actions --------------------------------------------------------
    // Everything the Files tab does to one file or folder of the torrent the pane shows. The
    // priority itself still goes through setFilePriority() above. `relativePath` is always a
    // row's path inside the torrent (TorrentFilesModel::pathAt).
public:
    /// The file's path on this machine: the server's path (TorrentFilesModel::serverPathOf)
    /// through the server's path map. Empty for an unknown torrent.
    Q_INVOKABLE QString torrentFileLocalPath(const QString &hash,
                                             const QString &relativePath) const;
    /// `O` / a double click: open the file with the desktop's default application.
    Q_INVOKABLE void openTorrentFile(const QString &hash, const QString &relativePath);
    /// `o`: show the file (or folder) in the file manager.
    Q_INVOKABLE void revealTorrentFile(const QString &hash, const QString &relativePath);
    /// `r`: rename one file or folder to `newName` (a name, not a path) inside its own
    /// folder, through torrents/renameFile or torrents/renameFolder. Where this machine sees
    /// the server's disks, a new name that something the torrent does not own already holds
    /// is refused before it is sent (#64): `actionFailed`, as qBittorrent's own refusal of a
    /// name another of the torrent's files has.
    Q_INVOKABLE void renameTorrentFile(const QString &hash,
                                       const QString &oldPath,
                                       const QString &newName,
                                       bool folder);
    /// The rename sheet's preview (#11, docs/18): `paths` are the files to rename in the
    /// tab's order, `options` `{find, replace, regex, everyMatch, matchCase,
    /// includeExtension}`. Answers `{rows: [{path, name, newName, status, marks}], error,
    /// changed, collisions, invalid, canRun}`; `status` is "unchanged", "changed", "collides"
    /// or "invalid", `marks` `[[start, length]…]` in newName. The details torrent's other
    /// files count for collisions, and so does a file it does not own already on disk at a
    /// new path, where this machine sees the server's disks (#64).
    Q_INVOKABLE QVariantMap planFileRenames(const QStringList &paths,
                                            const QVariantMap &options) const;
    /// Runs that plan for the details torrent: one torrents/renameFile per changed file, in
    /// an order where each target is free, stopping at the first refusal. Answers with
    /// `torrentFilesRenamed`.
    Q_INVOKABLE void
    renameTorrentFiles(const QString &hash, const QStringList &paths, const QVariantMap &options);
    /// A test seam (#64): in demo mode, the folder on this machine the details torrent's files
    /// are read under, so the rename guard has a disk to look at. Nothing in the app calls
    /// it; outside demo mode the folder comes from the server (renameDisk()).
    Q_INVOKABLE void setDemoDiskFolder(const QString &folder) { m_demoDiskFolder = folder; }
    /// qBittorrent's "by shown file order": `groups` is TorrentFilesModel::shownOrderGroups(),
    /// one torrents/filePrio call per group.
    Q_INVOKABLE void setFilePriorityGroups(const QString &hash, const QVariantList &groups);

Q_SIGNALS:
    /// Every rename as asked, before the demo guard (for the QML tests).
    void torrentFileRenameRequested(QString hash, QString oldPath, QString newPath, bool folder);
    /// A renameTorrentFiles() run ended: `done` of `total` renamed; on a refusal `failedPath`
    /// is the file qBittorrent refused and `error` says why (both empty when all went).
    void torrentFilesRenamed(QString hash, int done, int total, QString failedPath, QString error);
    // --- end N1 -----------------------------------------------------------------------------

    // --- #2: trusted certificates -------------------------------------------------------------
    // Trust on first use (docs/DECISIONS.md Q27): a server whose certificate the system does
    // not trust is refused with ApiError::Kind::Certificate, the server sheet and first run
    // show the certificate's fingerprint, and "trust this certificate" saves it with the
    // server (CertificatePin). Only that exact certificate is accepted afterwards.
public:
    /// The id of the server the engine is on; empty without one (or from the environment).
    Q_PROPERTY(QString serverId READ serverId NOTIFY serverChanged)
    QString serverId() const;

    /// Saves `sha256` as the certificate trusted for server `id` (empty forgets it) and, when
    /// that is the server the engine is on, reconnects with it. False for an unknown id or a
    /// value that is not a SHA-256.
    Q_INVOKABLE bool trustCertificate(const QString &id, const QString &sha256);
    /// A fingerprint as people compare it: "AB:CD:…" (CertificatePin::displayText).
    Q_INVOKABLE static QString certificateText(const QString &sha256);
    /// True when `lastError` (GlobalState::lastError) is a refused certificate.
    Q_INVOKABLE static bool isCertificateError(const QString &lastError);

Q_SIGNALS:
    /// A `testServer()` met a certificate the system does not trust; sent just before its
    /// `serverTested()`. `sha256` is the certificate shown, `pinned` the one trusted for the
    /// server so far (empty for none: a different one means the certificate changed).
    void serverCertificateRefused(const QString &id, const QString &sha256, const QString &pinned);
    /// The engine's server refused its certificate (once per loss): the views offer it to be
    /// reviewed and trusted on the server sheet.
    void certificateRefused(const QString &id);
    // --- end #2 ---------------------------------------------------------------------------------

    // --- #14: the bar widget's heartbeat ------------------------------------------------------
    // docs/20 Q7: while the companion bar widget runs (WidgetHeartbeat), the tray icon mode
    // "auto" hides the tray icon, so the bar shows one omaseed mark, not two.
public:
    /// main.cpp's heartbeat watcher. Not owned; none (tests, demos) means "no widget".
    void attachWidgetHeartbeat(omaseed::integration::WidgetHeartbeat *heartbeat);
    /// True while the bar widget's heartbeat is fresh.
    Q_PROPERTY(bool barWidgetRunning READ barWidgetRunning NOTIFY barWidgetRunningChanged)
    bool barWidgetRunning() const;

Q_SIGNALS:
    void barWidgetRunningChanged();

private:
    /// The tray icon shown or hidden for `trayIconMode` and the heartbeat.
    void applyTrayIconVisibility();
    QPointer<omaseed::integration::WidgetHeartbeat> m_widgetHeartbeat;
    // --- end #14 --------------------------------------------------------------------------------

    // --- #12: the bar widget's status file ------------------------------------------------------
public:
    /// main.cpp's StatusWriter (plugin/STATUS.md): App gives it the engine, the server's
    /// preferences, the server it is on, why that server is lost, completions, the path map
    /// and the duplicate test, then starts it. Not owned.
    void attachStatusWriter(omaseed::integration::StatusWriter *writer);

private:
    void updateStatusServer();
    QPointer<omaseed::integration::StatusWriter> m_statusWriter;
    // --- end #12 --------------------------------------------------------------------------------

    // --- #17: the bar widget is part of the install (ruled 2026-10-02) -----------------------
public:
    /// First run's "bar" step and Settings › Appearance's switch: links the packaged widget
    /// into the shell's plugins folder and puts it in the bar. main.cpp gives it the source
    /// tree's plugin/ in a development build.
    Q_PROPERTY(omaseed::integration::BarWidget *barWidget READ barWidget CONSTANT)
    omaseed::integration::BarWidget *barWidget() const { return m_barWidget; }

private:
    omaseed::integration::BarWidget *m_barWidget =
        new omaseed::integration::BarWidget(QString(), this);
    // --- end #17 --------------------------------------------------------------------------------

    // --- #24: the seed console (docs/22) -------------------------------------------------------
    // The ledger keeps what the server gave and took over time (one file per server); the
    // console works out every block of the seed console from it and the library.
public:
    Q_PROPERTY(omaseed::core::SeedConsole *console READ console CONSTANT)
    omaseed::core::SeedConsole *console() const { return m_console; }
    omaseed::core::SeedLedger *ledger() const { return m_ledger.get(); }

Q_SIGNALS:
    /// #24 T7: something worth a moment in the floating bar's spot: `kind` "milestone"
    /// (paid back, 2×, 5×) or "lastSeed" (you just became a torrent's only seed).
    void seedNotice(const QString &kind,
                    const QString &hash,
                    const QString &title,
                    const QString &detail);

private:
    /// Opens the ledger of the server the engine is about to poll (memory only in demo mode).
    void openLedger();
    /// One poll into the ledger: the server's totals, the torrents that moved, this second's
    /// speeds. Only the poll's diff is walked (#36); the whole library after a full update.
    void feedLedger();
    /// T7 for one torrent as it is now, with the seeds it had at the last poll (`before`, 0
    /// unknown): keeps `m_lastSwarm`, and says "now the only seed" when it went down to one.
    void checkLastSeed(const omaseed::core::Torrent &t, int before, bool announce);
    std::unique_ptr<omaseed::core::SeedLedger> m_ledger =
        std::make_unique<omaseed::core::SeedLedger>();
    omaseed::core::SeedConsole *m_console = new omaseed::core::SeedConsole(this);
    QString m_ledgerKey;
    bool m_ledgerDemoDone = false;
    /// Seeds in the swarm per complete torrent at the last poll, for "now the only seed".
    QHash<QString, int> m_lastSwarm;
    /// TorrentModel::revision() when the last poll was fed: the next one follows on from it,
    /// or the whole library is walked again.
    quint64 m_fedRevision = 0;
    bool m_fed = false;
    // --- end #24 --------------------------------------------------------------------------------

    // --- #31: omaseed's own backup (docs/24 §3, §5) ---------------------------------------------
    // Settings › Backup: back up to a file, restore from one, and the automatic copies. The
    // file work is OmaseedBackup's; App flushes and closes the ledger around it, reloads what
    // it keeps in memory after a restore, and runs AutoBackup's check once an hour.
public:
    /// "off", "weekly" (the default, D3 a) or "daily". Kept in state.json's "backup" section.
    Q_PROPERTY(
        QString autoBackupMode READ autoBackupMode WRITE setAutoBackupMode NOTIFY backupChanged)
    QString autoBackupMode() const;
    void setAutoBackupMode(const QString &mode);
    /// "Sunday 21:04", "3 Oct 21:04" when older than a week, empty before the first copy.
    Q_PROPERTY(QString lastBackupText READ lastBackupText NOTIFY backupChanged)
    QString lastBackupText() const;
    /// What a backup would hold, for the row: "Settings, 2 servers and the seed history since
    /// 12 Mar 2026: about 4.2 MB."
    Q_PROPERTY(QString backupSummary READ backupSummary NOTIFY backupChanged)
    QString backupSummary() const;
    /// `~/.local/share/omaseed/backups`, as the row prints it (home as ~).
    Q_PROPERTY(QString backupsDirText READ backupsDirText CONSTANT)
    QString backupsDirText() const;

    /// The portal's save dialog, proposing `~/omaseed-<date>.omaseed`. Empty when cancelled.
    Q_INVOKABLE QString pickBackupSavePath() const;
    /// Writes a backup to `path`. Empty on success, otherwise why it failed.
    Q_INVOKABLE QString backUpTo(const QString &path);
    /// The portal's open dialog, starting in the backups folder. Empty when cancelled.
    Q_INVOKABLE QString pickBackupFile() const;
    /// Reads a backup for the restore sheet: `{ok, error, title, sub, safety, parts: [{key,
    /// label, side, detail, checked, enabled, needsServers}]}`. A part's key is "settings",
    /// "servers" or "history:<server id in the backup>". Nothing changes yet.
    Q_INVOKABLE QVariantMap readBackup(const QString &path);
    /// Restores the backup readBackup() last read: `choice` = `{settings, servers, history:
    /// [ids]}`. Answers `{ok, text}`.
    Q_INVOKABLE QVariantMap restoreBackup(const QVariantMap &choice);
    Q_INVOKABLE void openBackupsFolder() const;
    /// Re-reads what the Backup pane prints (the summary, the last copy) when it is shown.
    Q_INVOKABLE void refreshBackupInfo() { Q_EMIT backupChanged(); }

Q_SIGNALS:
    void backupChanged();
    /// A restore replaced the settings, the table layouts among them (#34): the window shows
    /// the restored layout now, not at the next start. MainView answers it.
    void tableStatesRestored();

private:
    omaseed::core::OmaseedBackup::Sources backupSources() const;
    void reloadAfterRestore();
    omaseed::core::AutoBackup *m_autoBackup = new omaseed::core::AutoBackup(this);
    QTimer *m_autoBackupTimer = new QTimer(this);
    std::optional<omaseed::core::OmaseedBackup::Contents> m_pendingRestore;
    QString m_pendingRestorePath;
    // --- end #31 --------------------------------------------------------------------------------

    // --- #28: data shared between torrents --------------------------------------------------
    // docs/26: the delete confirm, the location sheet and a category change ask before they
    // take files from under another torrent that uses the same data (core::SharedData). The
    // methods live in AppSharedData.cpp.
public:
    /// `hashes` against the library: `{sharing: [{hash, name, sharers: [{hash, name, tracker,
    /// stateText, ratioText, missingFiles}]}], outside: [hash], insideCount, insideSize,
    /// diskSize, diskSizeWithOutside, pending}`. `sharing` lists the selected torrents with
    /// sharers outside `hashes`; `inside*` the ones that share among themselves. The file
    /// lists a no-root torrent needs are fetched in the background (`pending` until then) and
    /// `sharedFilesLoaded` says when a new call would answer differently. `fresh` forgets the
    /// file lists fetched before.
    Q_INVOKABLE QVariantMap sharedData(const QStringList &hashes, bool fresh = false);
    /// Of `hashes`, the ones setting `category` would move on disk: automatic management on,
    /// and another category now.
    Q_INVOKABLE QStringList movedByCategory(const QStringList &hashes,
                                            const QString &category) const;
    /// Demo mode only (the QML tests, screenshots): merges `torrents` (hash → maindata keys)
    /// into the fictional library.
    Q_INVOKABLE void patchDemoTorrents(const QVariantMap &torrents);
    /// Demo mode only (the QML tests): the file lists (hash → torrents/files names) a no-root
    /// torrent's check "fetches", answered on a later turn of the event loop as a server's
    /// reply would be. An empty list takes a hash's away.
    Q_INVOKABLE void patchDemoFiles(const QVariantMap &files);

Q_SIGNALS:
    void sharedFilesLoaded();

private:
    QHash<QString, QStringList> m_sharedFiles;
    QSet<QString> m_sharedFilesInFlight;
    QHash<QString, QStringList> m_demoSharedFiles;
    // --- end #28 ----------------------------------------------------------------------------
};

} // namespace omaseed::app
