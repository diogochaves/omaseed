#include "core/Version.hpp"
#include "integration/AppIdentity.hpp"
#include "integration/Theme.hpp"
#include "integration/ThemeReader.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QStringList>
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

    const QString demo = parser.isSet(demoOption) ? parser.value(demoOption) : QString();
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
    for (const QString &uri : uris)
        qInfo().noquote() << QStringLiteral("uri: %1").arg(uri);

    // The theme is read before the engine exists, so the first frame is already themed. The
    // Theme singleton the QML engine creates binds to this reader.
    omaseed::integration::ThemeReader theme;
    omaseed::integration::Theme::setDefaultReader(&theme);
    theme.exportToDBus();
    qInfo().noquote() << QStringLiteral("theme: %1 (%2) from %3")
                             .arg(theme.themeName(), theme.mode(), theme.source());

    QQmlApplicationEngine engine;
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

    return QGuiApplication::exec();
}
