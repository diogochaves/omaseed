#include "core/Version.hpp"
#include "integration/AppIdentity.hpp"
#include "integration/SingleInstance.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QStringList>
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

    if (parser.isSet(demoOption)) {
        const QString demo = parser.value(demoOption);
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

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Omaseed", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());

    return QGuiApplication::exec();
}
