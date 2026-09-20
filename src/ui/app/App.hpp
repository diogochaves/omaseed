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
#include "core/ServerDetector.hpp"
#include "core/ServerListModel.hpp"
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
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace omaseed::integration {
class Notifier;
class ThemeReader;
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

    /// A client error ("network: GET sync/maindata: Connection refused") stripped to the
    /// reason Fig. 21's notice and Fig. 23's banner print ("connection refused").
    Q_INVOKABLE static QString errorReasonText(const QString &text);

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
    /// `Theme.defaultWindowAlpha` (docs/QUESTIONS.md Q1: `shell.toml` `[launcher]
    /// background-alpha`, 0.95 when missing); this is its override.
    Q_PROPERTY(int windowAlphaOverride READ windowAlphaOverride WRITE setWindowAlphaOverride NOTIFY
                   settingsChanged)
    int windowAlphaOverride() const;
    void setWindowAlphaOverride(int percent);

    /// "auto" | "always" | "never" (plan Phase 6 T6.3). `auto` behaves as `always` until the
    /// companion widget exists; the row is here now because it is cheap.
    Q_PROPERTY(QString trayIconMode READ trayIconMode WRITE setTrayIconMode NOTIFY settingsChanged)
    QString trayIconMode() const;
    void setTrayIconMode(const QString &mode);

    // About pane -------------------------------------------------------------------------
    /// "omaseed 0.1.0 · Qt 6.11.0" — `core::versionLine()`.
    Q_PROPERTY(QString versionText READ versionText CONSTANT)
    QString versionText() const;
    /// The Qt this binary runs against, on its own.
    Q_PROPERTY(QString qtVersionText READ qtVersionText CONSTANT)
    QString qtVersionText() const;
    /// The public repository, and the copyable line that installs the companion bar widget.
    Q_PROPERTY(QString repoUrl READ repoUrl CONSTANT)
    QString repoUrl() const;
    Q_PROPERTY(QString pluginAddCommand READ pluginAddCommand CONSTANT)
    QString pluginAddCommand() const;

Q_SIGNALS:
    /// Any of the persisted settings above moved.
    void settingsChanged();
    /// One `testServer()` finished. `id` is the id the caller passed in `fields` (empty for a
    /// server that is not saved yet), so a sheet can tell its own answer from another's.
    void serverTested(const QString &id,
                      bool ok,
                      const QString &version,
                      const QString &apiVersion,
                      int torrentCount,
                      int latencyMs,
                      const QString &error);

private:
    void loadSettings();
    void saveSettings();
    void applyAppearance();
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
    Q_INVOKABLE void forceStart(const QStringList &hashes);
    Q_INVOKABLE void recheck(const QStringList &hashes);
    Q_INVOKABLE void reannounce(const QStringList &hashes);

    Q_INVOKABLE void setCategory(const QStringList &hashes, const QString &name);
    Q_INVOKABLE void addTags(const QStringList &hashes, const QStringList &tags);
    Q_INVOKABLE void removeTags(const QStringList &hashes, const QStringList &tags);

    Q_INVOKABLE void setLocation(const QStringList &hashes, const QString &path);
    /// `dl`/`ul` in bytes/s, -1 = unchanged, 0 = unlimited. `ratio` and `seedingMinutes`
    /// always go together through torrents/setShareLimits: -2 = use the global default,
    /// -1 = no limit, >=0 the limit itself (`seedingMinutes` is the API's own unit).
    Q_INVOKABLE void
    setLimits(const QStringList &hashes, qint64 dl, qint64 ul, double ratio, qint64 seedingMinutes);

    /// The sheet's fallback rename (the table's inline editor is the primary one).
    Q_INVOKABLE void rename(const QString &hash, const QString &name);

    Q_INVOKABLE void queueUp(const QStringList &hashes);
    Q_INVOKABLE void queueDown(const QStringList &hashes);

    Q_INVOKABLE void remove(const QStringList &hashes, bool deleteFiles);

    /// `y`/`Y`: no server round trip, so it runs in demo mode too.
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    /// `o`: the cursor torrent's `contentPath`, mapped through the server's `PathMap` and
    /// handed to `Xdg::showInFileManager`.
    Q_INVOKABLE void openFolder(const QString &hash);
    /// `ctrl+o` in the location sheet: `QFileDialog::getExistingDirectory`, starting at
    /// `startDir`. An empty result means the picker was cancelled or no portal answered; the
    /// caller keeps whatever was typed.
    Q_INVOKABLE QString pickDirectory(const QString &startDir) const;

Q_SIGNALS:
    void deleteFilesDefaultChanged();
    /// A server action from this block came back with an error. MainView shows it in the
    /// footer facts for a few seconds.
    void actionFailed(const QString &action, const QString &message);
    /// `C`/`t`: T4.4's category/tag palettes answer these; this block only raises them.
    void categoryPickerRequested(const QStringList &hashes);
    void tagPickerRequested(const QStringList &hashes);

private:
    /// True (and logged) in demo mode, where none of these actions may touch a network.
    bool demoGuard(const QString &action, int count);
    void reportActionResult(const QString &action,
                            const omaseed::core::QbtResult<QByteArray> &result);
    void loadDeleteFilesDefault();
    /// StoppedDownload/StoppedUpload/PausedDownload/PausedUpload: what `stopStart()` and
    /// `torrentRow()`'s `stopped` field both call "stopped".
    static bool isStoppedState(omaseed::core::TorrentState state);

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
public:
    /// What one line of "magnet / http(s) URL / .torrent path" is, as the sheet draws it:
    ///   kind         "magnet" | "url" | "file" | "none"
    ///   valid        bool, false when nothing recognisable was in `text`
    ///   pending      bool, true while a .torrent is still being read and hashed
    ///   name         the parsed name, or a URL's last path segment
    ///   sizeText     "2.34 GB", empty when the source does not say
    ///   infoHashV1 / infoHashV2   lowercase hex, empty when unknown
    ///   value        what goes to the server (the magnet, the URL, the file path)
    ///   token        the key `addTorrent` uses to find a picked file's bytes again
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

    /// Fig. 10's test: the library row either infohash already occupies (`torrentRow`'s
    /// fields), or an empty map. v2 is matched against the maindata `infohash_v2` field as
    /// well as the key, because a hybrid torrent is keyed by its v1 hash.
    Q_INVOKABLE QVariantMap findDuplicate(const QString &hashV1, const QString &hashV2) const;

    /// Adds what the sheet holds: `parseAddable`'s map plus `category`, `tags` (list),
    /// `savePath`, `autoTMM`, `contentLayout` ("Original" | "Subfolder" | "NoSubfolder"),
    /// `upLimitKb` (KB/s, 0 or absent = unlimited), `sequentialDownload`, `skipChecking` and
    /// `stopped`. Answers with `addFinished`; on success the new hash, once the next sync
    /// brings it, also travels as `showTorrentRequested` so the table's cursor lands on it.
    /// In demo mode nothing is sent: the add is logged and reported as a success.
    Q_INVOKABLE void addTorrent(const QVariantMap &request);

    /// `OMASEED_DEMO_ADD`: the text a `--demo` run drops into the sheet when it opens, so
    /// Fig. 5 and Fig. 10 can be screenshot reproducibly. Empty everywhere else.
    Q_INVOKABLE QString demoAddText() const;

    /// The URI hand-off: this process's own argv and every `SingleInstance::openRequested`
    /// from a second launch. Re-emitted as `addUrisRequested` for the sheet to queue.
    void openUris(const QStringList &uris);

Q_SIGNALS:
    /// The second half of `parseAddable` for a `.torrent`, and the whole answer of
    /// `pickTorrentFile()`. Same map, with `pending: false`.
    void addableParsed(QVariantMap result);
    /// `openUris` reached QML: the first one opens the sheet, the rest queue behind it.
    void addUrisRequested(QStringList uris);
    /// `ok` false: `message` is what the sheet prints in red while staying open. `ok` true:
    /// `hash` is the new torrent's infohash, or empty when the source did not say.
    void addFinished(bool ok, QString message, QString hash);

private:
    /// A `.torrent`'s bytes, kept between the parse and the POST because the portal's file
    /// chooser hands over content, not a path. Keyed by the map's `token`.
    struct PendingTorrentFile
    {
        QString fileName;
        QByteArray bytes;
    };

    /// Parses `bytes` on the global thread pool and emits `addableParsed` back here.
    void parseTorrentBytes(const QString &token,
                           const QString &fileName,
                           const QString &value,
                           const QByteArray &bytes);
    /// Turns a finished TorrentFile into the map above and remembers its bytes.
    QVariantMap torrentFileMap(const QString &token,
                               const QString &fileName,
                               const QString &value,
                               const QByteArray &bytes) const;
    /// Builds the client's options from the sheet's payload.
    omaseed::core::QbtClient::AddOptions addOptionsFrom(const QVariantMap &request) const;
    /// Waits up to 10 s for `hash` to turn up in the model, then asks for the cursor.
    void watchForAddedHash(const QString &hash);

    QHash<QString, PendingTorrentFile> m_pendingTorrentFiles;
    QString m_awaitedAddHash;
    QTimer *m_addWatchTimer = nullptr;
    QMetaObject::Connection m_addWatch;
    int m_nextAddableToken = 0;
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

    /// Starts `ServerDetector` (a TCP probe of 127.0.0.1:8080 — connect-and-close only, never
    /// an HTTP request — plus a `docker ps` and a `pgrep` check). Answers with
    /// `serversDetected()`, always exactly once. Safe to call again while a previous run is
    /// still in flight; only the last one's answer is emitted.
    Q_INVOKABLE void detectServers();

    /// The exact shell commands the install step's copyable field shows, one per line: the
    /// package, then enabling the per-user template unit qbittorrent-nox ships
    /// (`qbittorrent-nox@.service`, `pacman -Ql qbittorrent-nox | grep service`).
    Q_INVOKABLE QString installCommandsText() const;
    /// Runs `pkexec pacman -S --noconfirm qbittorrent-nox` — never `sudo` from inside the
    /// app. Answers with `installFinished()`. Does not enable the service: that needs a user
    /// systemd instance the installer cannot assume is the one running this session.
    Q_INVOKABLE void runInstallWithPkexec();

Q_SIGNALS:
    /// One `detectServers()` answer: each candidate is `{kind: "port"|"docker"|"process",
    /// url, label, detail}` per `ServerDetector::DetectedServer`.
    void serversDetected(QVariantList candidates);
    /// One `runInstallWithPkexec()` answer.
    void installFinished(bool ok, QString message);

private:
    QPointer<omaseed::core::ServerDetector> m_serverDetector;
    QPointer<QProcess> m_installProcess;
    // --- end T4.6 ------------------------------------------------------------------------
};

} // namespace omaseed::app
