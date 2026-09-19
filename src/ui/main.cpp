#include "core/ApiError.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/QbtClient.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "core/Version.hpp"
#include "integration/AppIdentity.hpp"
#include "integration/Notifier.hpp"
#include "integration/Xdg.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
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
        if (demo == QStringLiteral("notify"))
            return runNotifyDemo();
        if (demo == QStringLiteral("sync"))
            return runSyncDemo();

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

    // Exposed to QML as a plain context property until a proper bootstrap owns it (T1.7);
    // the DropArea in Main.qml is its only caller for now.
    omaseed::integration::Xdg xdg;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Xdg"), &xdg);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Omaseed", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    return QGuiApplication::exec();
}
