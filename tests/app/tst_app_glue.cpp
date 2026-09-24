// Regression tests for App glue that needs no server: the install runner's failure paths,
// and the other small App behaviours the 2026-09 review found broken.

#include "core/Server.hpp"
#include "core/ServerStore.hpp"
#include "core/SyncEngine.hpp"
#include "integration/Tray.hpp"
#include "ui/app/App.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QThread>
#include <QTemporaryDir>
#include <QTest>

using omaseed::app::App;

class TestAppGlue : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // APP-4
    void installThatCrashesAnswersOnce();
    void installThatCannotStartAnswersOnce();
    void installThatFailsCarriesItsStderr();

    // APP-5
    void aTorrentFileIsReadAndHashedOffTheGuiThread();
    void anUnreadableTorrentFileAnswersWithItsError();

    // APP-6
    void torrentIdIsTheTruncatedV2HashWhenThereIsOne();
    void aHybridMagnetIsAddedUnderItsV2Id();

    // APP-9
    void pathMapFollowsItsSetting();

    // APP-10
    void aSavedNeverTrayModeHidesTheIconAtStartup();

    // APP-15
    void theDemoTorrentLivesInAPrivateDirectory();

    // APP-14
    void abandonedTorrentParsesAreBounded();

private:
    QTemporaryDir m_dir;
    QByteArray m_previousConfigHome;
};

void TestAppGlue::initTestCase()
{
    // Nothing here may reach the desktop's session bus (keychain, tray, notices).
    QVERIFY(m_dir.isValid());
    qputenv("DBUS_SESSION_BUS_ADDRESS",
            QStringLiteral("unix:path=%1/no-bus").arg(m_dir.path()).toUtf8());
}

void TestAppGlue::init()
{
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_dir.path().toUtf8());
}

void TestAppGlue::cleanup()
{
    if (m_previousConfigHome.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
}

void TestAppGlue::installThatCrashesAnswersOnce()
{
    App app;
    QSignalSpy finished(&app, &App::installFinished);
    // A process killed by a signal: errorOccurred(Crashed), then finished(CrashExit).
    app.runInstall(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("kill -9 $$")});
    QVERIFY(finished.wait(5000));
    QTest::qWait(200); // a second answer would arrive right behind the first
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), false);
    QVERIFY(!finished.constFirst().at(1).toString().isEmpty());

    // And the runner is free again.
    app.runInstall(QStringLiteral("true"), {});
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.count(), 2);
    QCOMPARE(finished.at(1).at(0).toBool(), true);
}

void TestAppGlue::installThatCannotStartAnswersOnce()
{
    App app;
    QSignalSpy finished(&app, &App::installFinished);
    app.runInstall(QStringLiteral("/nonexistent/omaseed-no-such-program"), {});
    QVERIFY(finished.count() > 0 || finished.wait(5000));
    QTest::qWait(200);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), false);
}

void TestAppGlue::installThatFailsCarriesItsStderr()
{
    App app;
    QSignalSpy finished(&app, &App::installFinished);
    app.runInstall(QStringLiteral("sh"),
                   {QStringLiteral("-c"), QStringLiteral("echo 'no polkit agent' >&2; exit 127")});
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), false);
    QCOMPARE(finished.constFirst().at(1).toString(), QStringLiteral("no polkit agent"));
}

void TestAppGlue::aTorrentFileIsReadAndHashedOffTheGuiThread()
{
    App app;
    QSignalSpy parsed(&app, &App::addableParsed);
    const QString path = QStringLiteral(OMASEED_FIXTURES_DIR "/single_file_v1.torrent");
    const QVariantMap first = app.parseAddable(path);
    QCOMPARE(first.value(QStringLiteral("kind")).toString(), QStringLiteral("file"));
    QVERIFY(first.value(QStringLiteral("pending")).toBool());

    QVERIFY(parsed.wait(5000));
    const QVariantMap map = parsed.constFirst().constFirst().toMap();
    QVERIFY2(map.value(QStringLiteral("valid")).toBool(),
             qPrintable(map.value(QStringLiteral("error")).toString()));
    QCOMPARE(map.value(QStringLiteral("token")), first.value(QStringLiteral("token")));
    QVERIFY(!map.value(QStringLiteral("infoHashV1")).toString().isEmpty());
    QVERIFY(App::lastTorrentParseThread() != nullptr);
    QVERIFY(App::lastTorrentParseThread() != QThread::currentThread());
}

void TestAppGlue::anUnreadableTorrentFileAnswersWithItsError()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/locked.torrent");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("d4:infod4:name1:xee");
    file.close();
    QVERIFY(QFile::setPermissions(path, QFileDevice::Permissions()));

    App app;
    QSignalSpy parsed(&app, &App::addableParsed);
    app.parseAddable(path);
    QVERIFY(parsed.wait(5000));
    const QVariantMap map = parsed.constFirst().constFirst().toMap();
    QVERIFY(!map.value(QStringLiteral("valid")).toBool());
    QVERIFY(!map.value(QStringLiteral("error")).toString().isEmpty());
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

namespace {
const QString kV1 = QStringLiteral("fdc2dabac7590000000000000000000000000001");
const QString kV2 =
    QStringLiteral("81f2da56846c00000000000000000000000000000000000000000000000000ff");
} // namespace

void TestAppGlue::torrentIdIsTheTruncatedV2HashWhenThereIsOne()
{
    // As qBittorrent keys them (checked live on :8081: a hybrid's `hash` is its v2 hash's
    // first 40 characters, `infohash_v1` differs).
    QCOMPARE(App::torrentIdFor(kV1, kV2), kV2.left(40));
    QCOMPARE(App::torrentIdFor(QString(), kV2), kV2.left(40));
    QCOMPARE(App::torrentIdFor(kV1.toUpper(), QString()), kV1);
}

void TestAppGlue::aHybridMagnetIsAddedUnderItsV2Id()
{
    App app;
    app.startDemo(4);
    QSignalSpy finished(&app, &App::addFinished);
    const QVariantMap map = app.parseAddable(
        QStringLiteral("magnet:?xt=urn:btih:%1&xt=urn:btmh:1220%2&dn=hybrid").arg(kV1, kV2));
    QVERIFY(map.value(QStringLiteral("valid")).toBool());
    app.addTorrent(map);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.constFirst().at(2).toString(), kV2.left(40));
}

void TestAppGlue::pathMapFollowsItsSetting()
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    {
        // A server nobody listens on (port 9 on loopback): start() may poll it, harmlessly.
        omaseed::core::Server server;
        server.id = QStringLiteral("nas");
        server.name = QStringLiteral("nas");
        server.url = QUrl(QStringLiteral("http://127.0.0.1:9"));
        server.pathMap.append({QStringLiteral("/srv/downloads"), QStringLiteral("/mnt/nas")});
        omaseed::core::ServerStore store;
        QVERIFY(store.add(server));
    }

    App app;
    app.start();
    const QString remote = QStringLiteral("/srv/downloads/iso/arch.iso");
    QCOMPARE(app.mapContentPathToLocal(remote), QStringLiteral("/mnt/nas/iso/arch.iso"));
    app.setPathMapEnabled(false);
    QCOMPARE(app.mapContentPathToLocal(remote), remote);
    app.setPathMapEnabled(true);
    QCOMPARE(app.mapContentPathToLocal(remote), QStringLiteral("/mnt/nas/iso/arch.iso"));
    app.engine()->stop();
}

void TestAppGlue::aSavedNeverTrayModeHidesTheIconAtStartup()
{
    {
        App before;
        before.setTrayIconMode(QStringLiteral("never"));
    }
    App app; // a restart: the setting comes from state.json
    omaseed::integration::Tray tray(nullptr);
    QVERIFY(tray.iconWanted());
    app.attachTray(&tray);
    QVERIFY(!tray.iconWanted());
    QVERIFY(!tray.systemTrayIcon()->isVisible());

    app.setTrayIconMode(QStringLiteral("bogus")); // unknown values fall back to auto
    QCOMPARE(app.trayIconMode(), QStringLiteral("auto"));
    QVERIFY(tray.iconWanted());
}

void TestAppGlue::theDemoTorrentLivesInAPrivateDirectory()
{
    App app;
    QVERIFY(app.demoTorrentPath().isEmpty()); // not a demo
    app.startDemo(4);
    const QString path = app.demoTorrentPath();
    QVERIFY(!path.isEmpty());
    QVERIFY(QFile::exists(path));
    // Not a fixed name straight in the shared temp dir, and written once.
    QVERIFY(QFileInfo(path).absolutePath() != QDir(QDir::tempPath()).absolutePath());
    QCOMPARE(app.demoTorrentPath(), path);
}

void TestAppGlue::abandonedTorrentParsesAreBounded()
{
    App app;
    QSignalSpy parsed(&app, &App::addableParsed);
    const QString path = QStringLiteral(OMASEED_FIXTURES_DIR "/single_file_v1.torrent");
    constexpr int kParses = 80;
    for (int i = 0; i < kParses; ++i)
        app.parseAddable(path);
    QTRY_COMPARE_WITH_TIMEOUT(parsed.count(), kParses, 10000);
    QVERIFY(app.pendingTorrentFileCount() > 0);
    QVERIFY(app.pendingTorrentFileCount() <= 64);
}

QTEST_MAIN(TestAppGlue)

#include "tst_app_glue.moc"
