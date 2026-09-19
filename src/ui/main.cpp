#include "core/Version.hpp"
#include "integration/AppIdentity.hpp"
#include "integration/Notifier.hpp"
#include "integration/SingleInstance.hpp"
#include "integration/Theme.hpp"
#include "integration/ThemeReader.hpp"
#include "integration/Xdg.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QWindow>
#include <QtGlobal>

using omaseed::integration::AppIdentity::applicationId;

namespace {

/// Demo names the plan reserves. Unknown names are accepted too: every one of them logs and
/// continues until the views behind them exist.
const QStringList &reservedDemoNames()
{
    static const QStringList names{
        QStringLiteral("gallery"),
        QStringLiteral("notify"),
        QStringLiteral("sync"),
        QStringLiteral("table"),
    };
    return names;
}

/// `--demo notify`: sends the three notices of spec §6 through the real Notifier and keeps
/// the event loop alive (no window) until NotificationClosed arrived for all three or 30 s
/// passed. Every ActionInvoked is logged as "action: <class> <subject> <key>".
int runNotifyDemo()
{
    using omaseed::integration::Notifier;

    Notifier notifier;

    const QString hash = QStringLiteral("2f1e9c7a4b6d8e0f1a2b3c4d5e6f7089abcdef01");
    const QString trackerHost = QStringLiteral("tracker.opentrackr.org");
    const QString serverId = QStringLiteral("home");

    QSet<QString> pending{
        QStringLiteral("completed|%1").arg(hash),
        QStringLiteral("tracker-error|%1").arg(trackerHost),
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

    const quint32 trackerId = notifier.notifyTrackerError(trackerHost, 3);
    qInfo().noquote() << QStringLiteral("notified tracker-error id=%1").arg(trackerId);

    const quint32 serverLostId = notifier.notifyServerLost(serverId,
                                                           QStringLiteral("home"),
                                                           QStringLiteral("nas.local:9090"),
                                                           QStringLiteral("connection refused"),
                                                           10);
    qInfo().noquote() << QStringLiteral("notified server id=%1").arg(serverLostId);

    QTimer::singleShot(30000, &notifier, []() { QCoreApplication::quit(); });

    return QCoreApplication::exec();
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
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

    QCommandLineOption headlessOption(
        QStringList{QStringLiteral("headless")},
        QStringLiteral("Run without a window (reserved; not implemented yet)."));
    parser.addOption(headlessOption);

    parser.addPositionalArgument(QStringLiteral("uris"),
                                 QStringLiteral("magnet: links or .torrent files to add."),
                                 QStringLiteral("[uris...]"));

    parser.process(app);

    qInfo().noquote() << omaseed::core::versionLine() << QStringLiteral("as") << applicationId();

    const QString demo = parser.isSet(demoOption) ? parser.value(demoOption) : QString();
    if (demo == QStringLiteral("notify"))
        return runNotifyDemo();
    if (!demo.isEmpty() && demo != QStringLiteral("gallery")) {
        qInfo().noquote() << QStringLiteral("demo %1 not implemented yet").arg(demo);
        if (!reservedDemoNames().contains(demo)) {
            qInfo().noquote() << QStringLiteral("(%1 is not one of the reserved names: %2)")
                                     .arg(demo, reservedDemoNames().join(QStringLiteral(", ")));
        }
    }

    if (parser.isSet(headlessOption))
        qInfo().noquote() << QStringLiteral("headless requested; not implemented yet");

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
    if (!singleInstance->start(uris))
        return 0;

    // Exposed to QML as a plain context property until a proper bootstrap owns it (T1.7);
    // the DropArea in Main.qml is its only caller for now.
    omaseed::integration::Xdg xdg;

    // The theme is read before the engine exists, so the first frame is already themed. The
    // Theme singleton the QML engine creates binds to this reader.
    omaseed::integration::ThemeReader theme;
    omaseed::integration::Theme::setDefaultReader(&theme);
    theme.exportToDBus();
    qInfo().noquote() << QStringLiteral("theme: %1 (%2) from %3")
                             .arg(theme.themeName(), theme.mode(), theme.source());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Xdg"), &xdg);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    if (demo == QStringLiteral("gallery"))
        engine.loadFromModule("Omaseed.Views", "ThemeGallery");
    else
        engine.loadFromModule("Omaseed", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());

    return QGuiApplication::exec();
}
