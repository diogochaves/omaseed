// App — the composition root (src/ui/app/App.hpp) and the QML registration that goes with it.
//
// Five things are checked here: that a fresh install with no server and no environment
// override polls nothing; that the environment override reaches the :8081 playground and
// brings back a first sync (skipped when the playground is not running); that DemoData
// reproduces the spec's 16 rows and scales to hundreds of unique torrents; that a QML engine
// really sees `App`, `TorrentFilterModel` and the core enums through module `Omaseed`; and
// that the tray's icon state follows the world App shows.

#include "core/CategoryModel.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/Paths.hpp"
#include "core/ServerListModel.hpp"
#include "core/ServerStore.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"
#include "integration/ThemeReader.hpp"
#include "integration/Tray.hpp"
#include "ui/app/App.hpp"
#include "ui/app/DemoData.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSet>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <functional>

using namespace omaseed::core;
using omaseed::app::App;
using omaseed::integration::ThemeReader;
using omaseed::integration::Tray;

namespace {

QUrl playgroundUrl()
{
    const QString fromEnv =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("QBT_TEST_URL"));
    return QUrl(fromEnv.isEmpty() ? QStringLiteral("http://127.0.0.1:8081") : fromEnv);
}

bool answers(const QUrl &url)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), quint16(url.port(80)));
    return socket.waitForConnected(2000);
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

/// Every OMASEED_* key App reads, cleared so a developer's own shell cannot change a test.
void clearServerEnvironment()
{
    for (const char *name : {"OMASEED_SERVER_URL",
                             "OMASEED_SERVER_NAME",
                             "OMASEED_API_KEY",
                             "OMASEED_USERNAME",
                             "OMASEED_PASSWORD"})
        qunsetenv(name);
}

/// The value of `model.<role>` a QML delegate would see on row `row`.
QVariant roleOf(const TorrentModel *model, int row, const char *role)
{
    return model->data(model->index(row), model->roleNames().key(QByteArray(role)));
}

} // namespace

class TestApp : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void withoutAServerNothingPolls();
    void demoDataReproducesTheSpecsSixteenRows();
    void demoListsItsServerAndPersistsNothing();
    void demoDataScalesWithUniqueHashes();
    void qmlSeesAppAndTheCoreTypes();
    void trayStateFollowsTheWorld();
    void environmentOverrideReachesThePlayground();
    void filesTabActionsRunLocallyInDemo();

private:
    QTemporaryDir m_config;
    QByteArray m_savedConfigHome;
};

void TestApp::init()
{
    QVERIFY(m_config.isValid());
    m_savedConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    clearServerEnvironment();
}

void TestApp::cleanup()
{
    clearServerEnvironment();
    if (m_savedConfigHome.isEmpty())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_savedConfigHome);
}

void TestApp::withoutAServerNothingPolls()
{
    App app;
    app.start();

    QVERIFY(!app.hasServer());
    QVERIFY(!app.isDemoMode());
    QCOMPARE(app.serverCount(), 0);
    QVERIFY(app.serverName().isEmpty());
    QVERIFY(app.serverUrlText().isEmpty());
    QVERIFY(!app.engine()->isRunning());

    // Nothing may start behind our back either: give the event loop half a second.
    QTest::qWait(500);
    QVERIFY(!app.engine()->isRunning());
    QCOMPARE(app.torrents()->count(), 0);
    QVERIFY(!app.globalState()->connected());
}

// Batch-2 P5: the hero says HOME under --demo, so Settings › Servers lists "home" too; and a
// demo never writes the user's servers.json, whatever is done in its Settings.
void TestApp::demoListsItsServerAndPersistsNothing()
{
    const QString file = omaseed::core::paths::configDir() + QStringLiteral("/servers.json");
    QFile::remove(file); // the directory is shared with the cases before this one

    App app;
    app.startDemo(16);
    QCOMPARE(app.serverCount(), 1);
    QCOMPARE(app.serverList()->count(), 1);
    const std::optional<omaseed::core::Server> home = app.servers()->defaultServer();
    QVERIFY(home.has_value());
    QCOMPARE(home->name, QStringLiteral("home"));
    QCOMPARE(home->name, app.serverName());
    const QVariantMap row = app.serverList()->serverMap(home->id);
    QCOMPARE(row.value(QStringLiteral("torrentCount")).toInt(), 16);

    const QString added =
        app.addServer({{QStringLiteral("name"), QStringLiteral("second")},
                       {QStringLiteral("url"), QStringLiteral("http://nas.example:8080")},
                       {QStringLiteral("authMode"), QStringLiteral("none")}},
                      QString());
    QVERIFY(!added.isEmpty());
    QCOMPARE(app.serverCount(), 2);
    QVERIFY(app.removeServer(added));
    QVERIFY2(!QFile::exists(file), "a demo wrote servers.json");

    // A second demo in the same process does not list "home" twice.
    app.startDemo(16);
    QCOMPARE(app.serverCount(), 1);
}

void TestApp::demoDataReproducesTheSpecsSixteenRows()
{
    App app;
    app.startDemo(16);

    QVERIFY(app.isDemoMode());
    QVERIFY(!app.engine()->isRunning());
    QCOMPARE(app.torrents()->count(), 16);
    QCOMPARE(app.filtered()->count(), app.torrents()->count());
    QCOMPARE(app.counts()->total(), 16);

    const QStringList expected = omaseed::app::DemoData::specNames();
    QCOMPARE(expected.size(), 16);
    QStringList actual;
    for (int row = 0; row < app.torrents()->count(); ++row)
        actual.append(roleOf(app.torrents(), row, "name").toString());
    QCOMPARE(actual, expected);

    // The figure's own values, back out through the formatters the table will use.
    QCOMPARE(roleOf(app.torrents(), 0, "sizeText").toString(), QStringLiteral("1.31 GB"));
    QCOMPARE(roleOf(app.torrents(), 0, "progressText").toString(), QStringLiteral("48.9%"));
    QCOMPARE(roleOf(app.torrents(), 0, "stateText").toString(), QStringLiteral("downloading"));
    QCOMPARE(roleOf(app.torrents(), 0, "seedsPeersText").toString(), QStringLiteral("21 / 42"));
    QCOMPARE(roleOf(app.torrents(), 0, "dlspeedText").toString(), QStringLiteral("1.84 MB/s"));
    QCOMPARE(roleOf(app.torrents(), 0, "upspeedText").toString(), QStringLiteral("112 KB/s"));
    QCOMPARE(roleOf(app.torrents(), 0, "etaText").toString(), QStringLiteral("2m 40s"));
    QCOMPARE(roleOf(app.torrents(), 0, "category").toString(), QStringLiteral("linux"));
    QCOMPARE(roleOf(app.torrents(), 9, "sizeText").toString(), QStringLiteral("6.11 GB"));
    QCOMPARE(roleOf(app.torrents(), 13, "stateText").toString(), QStringLiteral("error"));
    QCOMPARE(roleOf(app.torrents(), 15, "stateText").toString(), QStringLiteral("completed"));

    // The rail and the hero, from the spec's own numbers.
    QCOMPARE(app.categories()->count(), 4);
    QCOMPARE(app.tags()->count(), 3);
    QVERIFY(app.trackers()->hosts().contains(QStringLiteral("tracker.example.org")));
    QVERIFY(app.trackers()->hosts().contains(QStringLiteral("trackerless")));
    QCOMPARE(app.globalState()->dlSpeedText(), QStringLiteral("4.21 MB/s"));
    QCOMPARE(app.globalState()->upSpeedText(), QStringLiteral("812 KB/s"));
    QVERIFY(app.globalState()->freeSpaceText().endsWith(QStringLiteral("TB")));
    QVERIFY(app.globalState()->connected());
    QCOMPARE(app.serverName(), QStringLiteral("home"));
    QCOMPARE(app.globalState()->serverVersion(), QStringLiteral("5.2.3"));

    // Nothing in the demo library names a real tracker.
    for (int row = 0; row < app.torrents()->count(); ++row) {
        const QString tracker = roleOf(app.torrents(), row, "tracker").toString();
        if (!tracker.isEmpty())
            QVERIFY2(tracker.contains(QStringLiteral("example.")),
                     qPrintable(QStringLiteral("real-looking tracker: %1").arg(tracker)));
    }
}

void TestApp::demoDataScalesWithUniqueHashes()
{
    const QJsonObject data = omaseed::app::DemoData::maindata(500);
    const QJsonObject torrents = data.value(QStringLiteral("torrents")).toObject();
    QCOMPARE(torrents.size(), 500);

    QSet<QString> hashes;
    for (auto it = torrents.constBegin(); it != torrents.constEnd(); ++it) {
        QCOMPARE(it.key().size(), 40);
        hashes.insert(it.key());
    }
    QCOMPARE(hashes.size(), 500);

    // The same rows twice must give the same library, or no screenshot is reproducible.
    QCOMPARE(omaseed::app::DemoData::maindata(500), data);

    App app;
    app.startDemo(500);
    QCOMPARE(app.torrents()->count(), 500);
    QCOMPARE(app.filtered()->count(), 500);
}

void TestApp::qmlSeesAppAndTheCoreTypes()
{
    App app;
    app.startDemo(16);
    App::setDefaultInstance(&app);

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQml
import Omaseed

QtObject {
    property int total: App.counts.total
    property int filteredCount: App.filtered.count
    property int torrentCount: App.torrents.count
    property bool demo: App.demoMode
    property string serverName: App.serverName
    property int downloadingFilter: TorrentFilterModel.Downloading
    property int seedingFilter: TorrentFilterModel.Seeding
    property int seedingGroup: Torrent.StatusGroups.Seeding
    property int erroredGroup: Torrent.StatusGroups.Errored
    property int stalledUpload: Torrent.TorrentState.StalledUpload
}
)",
                      QUrl(QStringLiteral("qrc:/tst_app/Probe.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> probe(component.create());
    QVERIFY(probe != nullptr);
    QCOMPARE(probe->property("total").toInt(), 16);
    QCOMPARE(probe->property("filteredCount").toInt(), 16);
    QCOMPARE(probe->property("torrentCount").toInt(), 16);
    QCOMPARE(probe->property("demo").toBool(), true);
    QCOMPARE(probe->property("serverName").toString(), QStringLiteral("home"));
    QCOMPARE(probe->property("downloadingFilter").toInt(), int(TorrentFilterModel::Downloading));
    QCOMPARE(probe->property("seedingFilter").toInt(), int(TorrentFilterModel::Seeding));
    QCOMPARE(probe->property("seedingGroup").toInt(), int(StatusGroup::Seeding));
    QCOMPARE(probe->property("erroredGroup").toInt(), int(StatusGroup::Errored));
    QCOMPARE(probe->property("stalledUpload").toInt(), int(TorrentState::StalledUpload));

    App::setDefaultInstance(nullptr);
}

void TestApp::trayStateFollowsTheWorld()
{
    // The mapping itself, exhaustively.
    QCOMPARE(App::trayStateFor(false, false, 0, 0, false), Tray::Disconnected);
    QCOMPARE(App::trayStateFor(true, true, 0, 0, false), Tray::Disconnected);
    QCOMPARE(App::trayStateFor(true, false, 2, 6, true), Tray::Error);
    QCOMPARE(App::trayStateFor(true, false, 0, 6, true), Tray::AltSpeed);
    QCOMPARE(App::trayStateFor(true, false, 0, 6, false), Tray::Downloading);
    QCOMPARE(App::trayStateFor(true, false, 0, 0, false), Tray::Idle);

    // And the same five states through a real Tray, driven by demo data.
    ThemeReader reader;
    Tray tray(&reader);
    App app;
    app.attachTray(&tray);

    // Nothing applied yet: GlobalState is not connected.
    QCOMPARE(tray.state(), Tray::Disconnected);

    // The spec's 16 rows carry one errored torrent (Elephants Dream).
    QJsonObject data = omaseed::app::DemoData::maindata(16);
    app.applyDemoData(data);
    QCOMPARE(app.counts()->errored(), 1);
    QCOMPARE(tray.state(), Tray::Error);

    // Drop the errored row: five torrents are still downloading.
    QJsonObject torrents = data.value(QStringLiteral("torrents")).toObject();
    QString erroredKey;
    for (auto it = torrents.constBegin(); it != torrents.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("state")).toString() ==
            QLatin1String("error"))
            erroredKey = it.key();
    }
    QVERIFY(!erroredKey.isEmpty());
    torrents.remove(erroredKey);
    data.insert(QStringLiteral("torrents"), torrents);
    app.applyDemoData(data);
    QCOMPARE(app.counts()->errored(), 0);
    QVERIFY(app.counts()->downloading() > 0);
    QCOMPARE(tray.state(), Tray::Downloading);
    QCOMPARE(tray.tooltip(),
             QStringLiteral("home · ↓ 4.21 MB/s ↑ 812 KB/s · %1 downloading · %2 seeding")
                 .arg(app.counts()->downloading())
                 .arg(app.counts()->seeding()));

    // Alternative speed limits on: the mode wins over the downloading count.
    QJsonObject state = data.value(QStringLiteral("server_state")).toObject();
    state.insert(QStringLiteral("use_alt_speed_limits"), true);
    data.insert(QStringLiteral("server_state"), state);
    app.applyDemoData(data);
    QCOMPARE(tray.state(), Tray::AltSpeed);
    QVERIFY(tray.altSpeedChecked());

    // Nothing downloading and no error: idle.
    QJsonObject quiet = omaseed::app::DemoData::maindata(16);
    QJsonObject onlySeeding;
    const QJsonObject allRows = quiet.value(QStringLiteral("torrents")).toObject();
    for (auto it = allRows.constBegin(); it != allRows.constEnd(); ++it) {
        QJsonObject row = it.value().toObject();
        row.insert(QStringLiteral("state"), QStringLiteral("uploading"));
        row.insert(QStringLiteral("progress"), 1.0);
        row.insert(QStringLiteral("has_tracker_error"), false);
        onlySeeding.insert(it.key(), row);
    }
    quiet.insert(QStringLiteral("torrents"), onlySeeding);
    app.applyDemoData(quiet);
    QCOMPARE(app.counts()->downloading(), 0);
    QCOMPARE(app.counts()->errored(), 0);
    QCOMPARE(tray.state(), Tray::Idle);
}

void TestApp::environmentOverrideReachesThePlayground()
{
    const QUrl url = playgroundUrl();
    if (!url.isValid() || !answers(url))
        QSKIP("the :8081 playground is not answering: scripts/dev/test-server.sh start");

    qputenv("OMASEED_SERVER_URL", url.toString().toUtf8());
    qputenv("OMASEED_SERVER_NAME", QByteArrayLiteral("playground"));

    App app;
    QSignalSpy firstSync(app.engine(), &SyncEngine::firstSyncFinished);
    app.start();

    QVERIFY(app.hasServer());
    QCOMPARE(app.serverName(), QStringLiteral("playground"));
    QCOMPARE(app.serverUrlText(), QStringLiteral("%1:%2").arg(url.host()).arg(url.port()));
    QCOMPARE(app.serverCount(), 0); // nothing was persisted

    QVERIFY2(waitFor([&firstSync]() { return firstSync.count() > 0; }, 10000),
             "no first sync from the playground within 10 s");

    QVERIFY(app.globalState()->connected());
    QCOMPARE(app.filtered()->count(), app.torrents()->count());
    QCOMPARE(app.counts()->total(), app.torrents()->count());

    app.engine()->stop();
}

// N1: with no server, a priority and a rename still change the Files tab's rows, so the demo
// shows them; a name with a slash is refused before anything is sent.
void TestApp::filesTabActionsRunLocallyInDemo()
{
    App app;
    app.startDemo(16);
    QString hash;
    for (int row = 0; row < app.torrents()->count(); ++row) {
        if (roleOf(app.torrents(), row, "name").toString() ==
            QStringLiteral("Big Buck Bunny 4K (CC-BY)"))
            hash = roleOf(app.torrents(), row, "hash").toString();
    }
    QVERIFY(!hash.isEmpty());
    app.selectTorrent(hash);
    TorrentFilesModel *files = app.details()->files();
    QCOMPARE(files->count(), 7);

    app.setFilePriority(hash, {2, 3}, int(FilePriority::Normal));
    QCOMPARE(files->priority(3), int(FilePriority::Normal));

    QSignalSpy renamed(&app, &App::torrentFileRenameRequested);
    QSignalSpy failed(&app, &App::actionFailed);
    app.renameTorrentFile(
        hash, QStringLiteral("Big Buck Bunny 4K/subs"), QStringLiteral("subtitles"), true);
    QCOMPARE(renamed.count(), 1);
    QCOMPARE(renamed.at(0).at(2).toString(), QStringLiteral("Big Buck Bunny 4K/subtitles"));
    QCOMPARE(files->nameAt(3), QStringLiteral("subtitles"));

    app.renameTorrentFile(
        hash, QStringLiteral("Big Buck Bunny 4K/LICENSE.txt"), QStringLiteral("a/b"), false);
    QCOMPARE(renamed.count(), 1);
    QCOMPARE(failed.count(), 1);

    // The demo's content path names the torrent, not its top folder, so the save path is
    // where the files are.
    const QString savePath = app.torrents()->torrent(hash).savePath;
    QCOMPARE(app.torrentFileLocalPath(hash, QStringLiteral("Big Buck Bunny 4K/LICENSE.txt")),
             savePath + QStringLiteral("/Big Buck Bunny 4K/LICENSE.txt"));
    QVERIFY(app.torrentFileLocalPath(QStringLiteral("nope"), QStringLiteral("a")).isEmpty());
}

QTEST_MAIN(TestApp)
#include "tst_app.moc"
