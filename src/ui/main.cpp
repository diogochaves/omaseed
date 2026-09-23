#include "core/ApiError.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/QbtClient.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"
#include "core/Version.hpp"
#include "integration/AppIdentity.hpp"
#include "integration/Notifier.hpp"
#include "integration/SingleInstance.hpp"
#include "integration/Theme.hpp"
#include "integration/ThemeReader.hpp"
#include "integration/Tray.hpp"
#include "integration/Xdg.hpp"
#include "ui/app/App.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <QtGlobal>

using omaseed::integration::AppIdentity::applicationId;

namespace {

/// Demo names the plan reserves. Unknown names are accepted too: every one of them logs and
/// continues until the views behind them exist.
const QStringList &reservedDemoNames()
{
    static const QStringList names{
        QStringLiteral("details"),
        QStringLiteral("firstrun"),
        QStringLiteral("gallery"),
        QStringLiteral("notify"),
        QStringLiteral("overlays"),
        QStringLiteral("rail"),
        QStringLiteral("states"),
        QStringLiteral("sync"),
        QStringLiteral("table"),
        QStringLiteral("torrents"),
        QStringLiteral("tray"),
    };
    return names;
}

/// `--demo notify`: sends the three notices of spec §6 (completed, disk almost full, server
/// lost; the tracker notice of Fig. 20 was dropped in Q16) through the real Notifier and
/// keeps the event loop alive (no window) until NotificationClosed arrived for all three or
/// 30 s passed. Every ActionInvoked is logged as "action: <class> <subject> <key>".
int runNotifyDemo()
{
    using omaseed::integration::Notifier;

    Notifier notifier;

    const QString hash = QStringLiteral("2f1e9c7a4b6d8e0f1a2b3c4d5e6f7089abcdef01");
    const QString serverId = QStringLiteral("home");

    QSet<QString> pending{
        QStringLiteral("completed|%1").arg(hash),
        QStringLiteral("disk|%1").arg(serverId),
        QStringLiteral("server|%1").arg(serverId),
    };

    QObject::connect(
        &notifier,
        &Notifier::actionInvoked,
        [](const QString &eventClass, const QString &subjectId, const QString &actionKey) {
            qInfo().noquote()
                << QStringLiteral("action: %1 %2 %3").arg(eventClass, subjectId, actionKey);
        });
    QObject::connect(&notifier,
                     &Notifier::noticeClosed,
                     [&pending](const QString &eventClass, const QString &subjectId) {
                         pending.remove(QStringLiteral("%1|%2").arg(eventClass, subjectId));
                         if (pending.isEmpty())
                             QCoreApplication::quit();
                     });

    const quint32 completedId =
        notifier.notifyCompleted(hash,
                                 QStringLiteral("archlinux-2026.09.01-x86_64.iso"),
                                 QStringLiteral("1.31 GB"),
                                 QStringLiteral("linux"),
                                 /*hasFolder=*/true);
    qInfo().noquote() << QStringLiteral("notified completed id=%1").arg(completedId);

    const quint32 diskId = notifier.notifyDiskAlmostFull(serverId, QStringLiteral("1.8 GB"));
    qInfo().noquote() << QStringLiteral("notified disk id=%1").arg(diskId);

    const quint32 serverLostId = notifier.notifyServerLost(serverId,
                                                           QStringLiteral("home"),
                                                           QStringLiteral("nas.local:9090"),
                                                           QStringLiteral("connection refused"),
                                                           10);
    qInfo().noquote() << QStringLiteral("notified server id=%1").arg(serverLostId);

    QTimer::singleShot(30000, &notifier, []() { QCoreApplication::quit(); });

    return QCoreApplication::exec();
}

/// "<state>" for the --demo tray log line.
QString trayStateName(omaseed::integration::Tray::State state)
{
    using Tray = omaseed::integration::Tray;
    switch (state) {
    case Tray::Disconnected:
        return QStringLiteral("disconnected");
    case Tray::Idle:
        return QStringLiteral("idle");
    case Tray::Downloading:
        return QStringLiteral("downloading");
    case Tray::AltSpeed:
        return QStringLiteral("alt-speed");
    case Tray::Error:
        return QStringLiteral("error");
    }
    return QString();
}

/// `--demo tray`: cycles the five icon states every 2 s with the spec's sample tooltip and
/// stats, no window, so a screenshot of the bar strip can be taken at leisure. Runs until
/// killed.
int runTrayDemo()
{
    using omaseed::integration::ThemeReader;
    using omaseed::integration::Tray;

    ThemeReader themeReader;
    Tray tray(&themeReader);

    tray.setTooltip(
        QStringLiteral("home"), QStringLiteral("4.21 MB/s"), QStringLiteral("812 KB/s"), 6, 118);
    tray.setStats(
        QStringLiteral("4.21 MB/s"), QStringLiteral("812 KB/s"), 6, 118, QStringLiteral("1.8 TB"));
    tray.setDownloadingCount(6);

    const QList<Tray::State> states{
        Tray::Disconnected,
        Tray::Idle,
        Tray::Downloading,
        Tray::AltSpeed,
        Tray::Error,
    };
    int index = 0;

    QTimer cycle;
    QObject::connect(&cycle, &QTimer::timeout, [&]() {
        const Tray::State state = states.at(index);
        tray.setState(state);
        qInfo().noquote() << QStringLiteral("tray state: %1").arg(trayStateName(state));
        index = (index + 1) % states.size();
    });
    cycle.start(2000);

    tray.setState(states.at(index));
    qInfo().noquote() << QStringLiteral("tray state: %1").arg(trayStateName(states.at(index)));
    index = (index + 1) % states.size();

    return QCoreApplication::exec();
}

/// `--demo sync`: polls QBT_TEST_URL (default: the :8081 playground) through SyncEngine,
/// prints the table of spec §1 Fig. 1 and the hero's numbers once the first full update has
/// landed, and exits. No window; it only reads.
int runSyncDemo()
{
    using namespace omaseed::core;

    const QString fromEnv =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("QBT_TEST_URL"));
    const QUrl url(fromEnv.isEmpty() ? QStringLiteral("http://127.0.0.1:8081") : fromEnv);

    QbtClient client;
    client.setBaseUrl(url);
    const QString apiKey =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("QBT_API_KEY"));
    if (!apiKey.isEmpty())
        client.setApiKey(apiKey);

    SyncEngine engine;
    engine.setClient(&client);

    int exitCode = 0;
    const auto printTable = [&engine, &url]() {
        const TorrentModel *torrents = engine.torrents();
        const GlobalState *state = engine.globalState();

        QTextStream out(stdout);
        out << QStringLiteral("omaseed · %1 · qBittorrent %2 · API %3\n")
                   .arg(url.toString(), state->serverVersion(), state->apiVersion());
        out << QStringLiteral("%1  %2  %3  %4  %5  %6  %7  %8\n")
                   .arg(QStringLiteral("name"), -44)
                   .arg(QStringLiteral("size"), 10)
                   .arg(QStringLiteral("progress"), 8)
                   .arg(QStringLiteral("status"), -12)
                   .arg(QStringLiteral("down"), 10)
                   .arg(QStringLiteral("up"), 10)
                   .arg(QStringLiteral("eta"), 8)
                   .arg(QStringLiteral("category"), -12);
        for (int row = 0; row < torrents->count(); ++row) {
            const QModelIndex index = torrents->index(row);
            const auto text = [&](int role) { return torrents->data(index, role).toString(); };
            out << QStringLiteral("%1  %2  %3  %4  %5  %6  %7  %8\n")
                       .arg(text(TorrentModel::NameRole), -44)
                       .arg(text(TorrentModel::SizeTextRole), 10)
                       .arg(text(TorrentModel::ProgressTextRole), 8)
                       .arg(text(TorrentModel::StateTextRole), -12)
                       .arg(text(TorrentModel::DlspeedTextRole), 10)
                       .arg(text(TorrentModel::UpspeedTextRole), 10)
                       .arg(text(TorrentModel::EtaTextRole), 8)
                       .arg(text(TorrentModel::CategoryRole), -12);
        }
        out << QStringLiteral(
                   "%1 torrents · %2 downloading · %3 seeding · ↓ %4 · ↑ %5 · %6 free · %7\n")
                   .arg(torrents->count())
                   .arg(engine.counts()->downloading())
                   .arg(engine.counts()->seeding())
                   .arg(state->dlSpeedText(),
                        state->upSpeedText(),
                        state->freeSpaceText(),
                        state->connectionStatus());
    };

    const auto fail = [&exitCode](const ApiError &error) {
        qWarning().noquote() << error.toString();
        exitCode = 1;
        QCoreApplication::quit();
    };

    QObject::connect(&engine, &SyncEngine::firstSyncFinished, &engine, [&]() {
        printTable();
        QCoreApplication::quit();
    });
    QObject::connect(&engine, &SyncEngine::serverLost, &engine, fail);
    QObject::connect(&engine, &SyncEngine::authRequired, &engine, fail);
    QTimer::singleShot(30000, &engine, [&exitCode]() {
        qWarning().noquote() << QStringLiteral("no full update within 30 s");
        exitCode = 1;
        QCoreApplication::quit();
    });

    engine.start();
    QCoreApplication::exec();
    engine.stop();
    return exitCode;
}

/// `--headless --demo table`: builds the whole app object graph with fictional data and no
/// server, prints what landed in the models, and exits. This is the cheap way to check that
/// a 10 000-row library still loads (and how fast) without opening a window.
int runHeadlessTableDemo(int rows)
{
    using namespace omaseed::core;

    QElapsedTimer timer;
    timer.start();

    omaseed::app::App app;
    app.startDemo(rows);

    const qint64 elapsed = timer.elapsed();
    const GlobalState *state = app.globalState();
    qInfo().noquote() << QStringLiteral("demo table: %1 torrents in %2 ms")
                             .arg(app.torrents()->count())
                             .arg(elapsed);
    qInfo().noquote()
        << QStringLiteral(
               "filtered %1 · %2 downloading · %3 seeding · %4 errored · %5 categories · "
               "%6 tags · %7 trackers · ↓ %8 ↑ %9 · %10 free")
               .arg(app.filtered()->count())
               .arg(app.counts()->downloading())
               .arg(app.counts()->seeding())
               .arg(app.counts()->errored())
               .arg(app.categories()->count())
               .arg(app.tags()->count())
               .arg(app.trackers()->count())
               .arg(state->dlSpeedText(), state->upSpeedText(), state->freeSpaceText());
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    // File pickers go through the FileChooser portal, so the chooser is xdg-desktop-portal's
    // window, which Omarchy floats like every other app's picker. Omarchy sets
    // QT_QPA_PLATFORMTHEME=gtk3, under which Qt opens GTK's chooser inside this process: a
    // tiled window with our class and no title. Nothing else here reads the platform theme
    // (colours come from Theme, the font from fontconfig, the tray is StatusNotifierItem), so
    // the switch changes only the pickers. OMASEED_PLATFORMTHEME picks another theme.
    const QByteArray platformTheme = qgetenv("OMASEED_PLATFORMTHEME");
    qputenv("QT_QPA_PLATFORMTHEME",
            platformTheme.isEmpty() ? QByteArrayLiteral("xdgdesktopportal") : platformTheme);

    // QSystemTrayIcon lives in QtWidgets, which needs a QApplication rather than a plain
    // QGuiApplication; nothing else in this app touches a QWidget.
    QApplication app(argc, argv);
    QGuiApplication::setApplicationName(omaseed::integration::AppIdentity::applicationName());
    QGuiApplication::setApplicationVersion(omaseed::core::version());
    QGuiApplication::setOrganizationDomain(omaseed::integration::AppIdentity::organizationDomain());
    QGuiApplication::setDesktopFileName(omaseed::integration::AppIdentity::desktopFileName());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("omaseed — torrents for Omarchy, powered by qBittorrent."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption demoOption(QStringList{QStringLiteral("demo")},
                                  QStringLiteral("Show a demo view with fictional data: %1.")
                                      .arg(reservedDemoNames().join(QStringLiteral(", "))),
                                  QStringLiteral("name"));
    parser.addOption(demoOption);

    QCommandLineOption rowsOption(
        QStringList{QStringLiteral("rows")},
        QStringLiteral("How many fictional torrents `--demo table` builds (default 16, the "
                       "16 rows of the spec's Fig. 1)."),
        QStringLiteral("n"),
        QStringLiteral("16"));
    parser.addOption(rowsOption);

    QCommandLineOption headlessOption(
        QStringList{QStringLiteral("headless")},
        QStringLiteral("Run without a window. Implemented for --demo table, which prints "
                       "what it loaded and exits."));
    parser.addOption(headlessOption);

    parser.addPositionalArgument(QStringLiteral("uris"),
                                 QStringLiteral("magnet: links or .torrent files to add."),
                                 QStringLiteral("[uris...]"));

    parser.process(app);

    qInfo().noquote() << omaseed::core::versionLine() << QStringLiteral("as") << applicationId();

    const QString demo = parser.isSet(demoOption) ? parser.value(demoOption) : QString();
    const bool headless = parser.isSet(headlessOption);

    bool rowsOk = true;
    const int demoRows = parser.value(rowsOption).toInt(&rowsOk);
    if (!rowsOk || demoRows < 1) {
        qWarning().noquote() << QStringLiteral("--rows needs a positive number, not %1")
                                    .arg(parser.value(rowsOption));
        return 2;
    }

    // The demos that never open a window: each one owns the event loop and ends the process.
    if (demo == QStringLiteral("notify"))
        return runNotifyDemo();
    if (demo == QStringLiteral("tray"))
        return runTrayDemo();
    if (demo == QStringLiteral("sync"))
        return runSyncDemo();
    if (demo == QStringLiteral("table") && headless)
        return runHeadlessTableDemo(demoRows);

    // What is left goes on to the window. `gallery` swaps the QML root for the theme gallery.
    // Every other name fills the models with fictional data (`--rows`) and then looks for a
    // host view `Demo<Name>` in Omaseed.Views (`--demo rail` -> DemoRail, `--demo details` ->
    // DemoDetails); when there is none, Main.qml is the host (`--demo table`).
    const bool windowDemo = !demo.isEmpty() && demo != QStringLiteral("gallery");
    QString demoHost;
    if (windowDemo && demo != QStringLiteral("table"))
        demoHost = QStringLiteral("Demo") + demo.at(0).toUpper() + demo.mid(1);

    if (headless)
        qInfo().noquote() << QStringLiteral("headless is implemented for --demo table only");

    const QStringList uris = parser.positionalArguments();

    // SingleInstance: one omaseed per session. A second launch forwards its URIs (or a plain
    // activation, if none) to the running process over D-Bus (org.freedesktop.Application) —
    // or a QLocalServer when the session bus is unreachable — and exits here, before any QML
    // engine or window is created. The URIs this very process was started with travel the
    // same openRequested() path, so there is exactly one place that logs a URI and raises the
    // window (wired up below, once that window exists).
    auto *singleInstance = new omaseed::integration::SingleInstance(&app);
    QWindow *window = nullptr;
    auto raiseWindow = [&window](omaseed::integration::SingleInstance *instance) {
        if (!window)
            return;
        // Best-effort Wayland focus hand-off: Qt 6.11 has no public API to apply an XDG
        // activation token to an already-mapped window, so this only helps on platforms
        // where requestActivate() alone is enough (notably X11); on Wayland compositors
        // that require a fresh token per activation, the window still raises but may not
        // receive focus until the user clicks it.
        const QString token = instance->takeLastActivationToken();
        if (!token.isEmpty())
            qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
        window->show();
        window->raise();
        window->requestActivate();
    };
    QObject::connect(singleInstance,
                     &omaseed::integration::SingleInstance::activateRequested,
                     &app,
                     [singleInstance, raiseWindow]() { raiseWindow(singleInstance); });
    QObject::connect(singleInstance,
                     &omaseed::integration::SingleInstance::openRequested,
                     &app,
                     [singleInstance, raiseWindow](const QStringList &openedUris) {
                         for (const QString &uri : openedUris)
                             qInfo().noquote() << QStringLiteral("uri: %1").arg(uri);
                         raiseWindow(singleInstance);
                     });
    // A `--demo` run never receives URIs and must never take the name: two agents' demos
    // (and a demo beside a real omaseed) have to coexist on one session bus.
    if (demo.isEmpty()) {
        if (!singleInstance->start(uris))
            return 0;
    } else {
        qInfo().noquote() << QStringLiteral("demo: single-instance skipped");
    }

    // Exposed to QML as the `Xdg` singleton (XdgForeign.hpp); the DropArea in Main.qml is its
    // only caller for now.
    omaseed::integration::Xdg xdg;
    omaseed::integration::Xdg::setDefaultInstance(&xdg);

    // The theme is read before the engine exists, so the first frame is already themed. The
    // Theme singleton the QML engine creates binds to this reader.
    omaseed::integration::ThemeReader theme;
    omaseed::integration::Theme::setDefaultReader(&theme);
    theme.exportToDBus();
    qInfo().noquote() << QStringLiteral("theme: %1 (%2) from %3")
                             .arg(theme.themeName(), theme.mode(), theme.source());

    // The composition root. Everything QML reads hangs off it, and it is the `App` singleton
    // of module `Omaseed` (src/ui/qml/AppForeign.hpp) from here on.
    omaseed::app::App omaseedApp;
    omaseed::app::App::setDefaultInstance(&omaseedApp);
    // The Appearance pane's "follow the Omarchy theme" and base-size override live in
    // state.json; App applies them to the reader here, before the first frame (T4.5).
    omaseedApp.attachTheme(&theme);
    if (windowDemo)
        omaseedApp.startDemo(demoRows);
    else
        omaseedApp.start();

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    if (demo == QStringLiteral("gallery"))
        engine.loadFromModule("Omaseed.Views", "ThemeGallery");
    else if (!demoHost.isEmpty() &&
             QQmlComponent(&engine, "Omaseed.Views", demoHost).status() == QQmlComponent::Ready)
        engine.loadFromModule("Omaseed.Views", demoHost);
    else {
        if (!demoHost.isEmpty())
            qInfo().noquote() << QStringLiteral(
                                     "demo %1: no Omaseed.Views/%2 host yet, showing Main")
                                     .arg(demo, demoHost);
        engine.loadFromModule("Omaseed", "Main");
    }
    if (engine.rootObjects().isEmpty())
        return 1;
    window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());

    // The tray, created once the window exists so toggleWindowRequested has something to
    // show. Its own signals become window control (toggle, raise), a real quit, or -- for
    // everything Phase 3 wires up (add, alt-speed, stop/start all, settings) -- a log line.
    omaseed::integration::Tray tray(&theme);

    QQuickWindow *rootWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!rootWindow)
        qWarning().noquote() << QStringLiteral("tray: root object is not a window; window "
                                               "control from the tray is disabled");

    QObject::connect(
        &tray, &omaseed::integration::Tray::toggleWindowRequested, &app, [rootWindow]() {
            if (!rootWindow)
                return;
            if (rootWindow->isVisible()) {
                rootWindow->hide();
            } else {
                rootWindow->show();
                rootWindow->raise();
                rootWindow->requestActivate();
            }
        });
    QObject::connect(&tray, &omaseed::integration::Tray::showRequested, &app, [rootWindow]() {
        if (!rootWindow)
            return;
        rootWindow->show();
        rootWindow->raise();
        rootWindow->requestActivate();
    });
    QObject::connect(
        &tray, &omaseed::integration::Tray::quitRequested, &app, &QCoreApplication::quit);

    // Everything else in the menu is an App action now: alt-speed, stop all and start all go
    // out as real requests, the icon state, tooltip and stats follow GlobalState and
    // FilterCounts, and the alt-speed checkbox follows the server rather than the last click.
    omaseedApp.attachTray(&tray);
    QObject::connect(&omaseedApp, &omaseed::app::App::openSettings, &app, []() {
        qInfo().noquote() << QStringLiteral("settings requested"); // T4.4 opens the modal
    });

    // Notifications (spec §6): the tray icon already reflects the same events through
    // App::updateTray; this is the desktop toast side. Xdg is attached so "open folder" can
    // reveal a completed torrent's content path; MainView has no `Connections { target: App }`
    // block yet (only `target: App.torrents`), so `showTorrentRequested` is only logged here
    // until that block exists.
    omaseed::integration::Notifier notifier;
    omaseedApp.attachXdg(&xdg);
    omaseedApp.attachNotifier(&notifier);
    QObject::connect(
        &omaseedApp, &omaseed::app::App::showTorrentRequested, &app, [](const QString &hash) {
            qInfo().noquote() << QStringLiteral("show torrent requested: %1").arg(hash);
        });

    // The window's visibility drives the poll cadence (1.5 s shown, 5 s hidden). Main.qml
    // forwards its own `visible`; this catches the tray's hide/show of the same window, and
    // the first call states where the window started (its `onVisibleChanged` fired while the
    // component was still being built).
    if (rootWindow) {
        omaseedApp.setVisible(rootWindow->isVisible());
        QObject::connect(rootWindow,
                         &QWindow::visibleChanged,
                         &omaseedApp,
                         [&omaseedApp](bool shown) { omaseedApp.setVisible(shown); });
    }

    // Everything spec §7 Fig. 26's "act" column promises but Phase 4 implements. MainView
    // already logs the key and the row count; this is the one place that will grow into the
    // real calls.
    QObject::connect(
        &omaseedApp,
        &omaseed::app::App::torrentActionRequested,
        &app,
        [](const QString &action, const QStringList &hashes) {
            qInfo().noquote()
                << QStringLiteral("torrent action: %1 on %2").arg(action).arg(hashes.size());
        });

    // --- T4.2: add flow ---
    // The URI hand-off. `SingleInstance` emits openRequested for the URIs this process was
    // started with as well as for every one a second launch forwards, always through a queued
    // singleShot, so connecting here — after the App exists — still catches our own argv. QML
    // picks them up as `App.addUrisRequested` and queues them into the add sheet.
    QObject::connect(
        singleInstance,
        &omaseed::integration::SingleInstance::openRequested,
        &omaseedApp,
        [&omaseedApp](const QStringList &openedUris) { omaseedApp.openUris(openedUris); });
    // `App::addRequested` (the tray, the hero's `+`, `a`) no longer needs a log line here:
    // MainView opens `Views/AddSheet.qml` on it.
    // --- end T4.2 ---

    const int code = QApplication::exec();
    omaseedApp.engine()->stop();
    return code;
}
