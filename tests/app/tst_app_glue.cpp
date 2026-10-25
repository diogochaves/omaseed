// Regression tests for App glue that needs no server: the install runner's failure paths,
// and the other small App behaviours the 2026-09 review found broken.

#include "core/Server.hpp"
#include "core/ServerListModel.hpp"
#include "core/ServerStore.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentFile.hpp"
#include "core/TorrentModel.hpp"
#include "integration/Tray.hpp"
#include "integration/WidgetHeartbeat.hpp"
#include "ui/app/App.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUrlQuery>

#include <memory>

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
    // #14: the bar widget's heartbeat hides the icon under auto, and only then.
    void theBarWidgetHidesTheTrayIconUnderAuto();

    // APP-15
    void theDemoTorrentLivesInAPrivateDirectory();

    // APP-14
    void abandonedTorrentParsesAreBounded();

    // APP-12
    void aSecondPressBeforeTheNextSyncDoesNotUndoTheToggle();
    void limitsCarryTheTorrentsInactiveSeedingTime();

    // REG-2
    void retryAfterARefusedLoginLogsInOnceMore();

    // APP-11
    void removingTheLiveServerClearsItsWorld();

    // Review rulings Q9, Q10
    void anUnreadableServersFileIsNamedWhereItWasKept();
    void secretsInAPlainFileAreNamed();

    // DECISIONS C28 (#84)
    void aServerOfAnotherEngineIsNeverAsked();

    // #201 F4, F5
    void aSwitchReadsConnectingNeverDown();

    // Manual box M7
    void theDownloadsListMarksWhatTheLibraryHolds();
    void aDemoRunListsNoRealDownloads();

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
    QCOMPARE(omaseed::core::TorrentFile::torrentIdFor(kV1, kV2), kV2.left(40));
    QCOMPARE(omaseed::core::TorrentFile::torrentIdFor(QString(), kV2), kV2.left(40));
    QCOMPARE(omaseed::core::TorrentFile::torrentIdFor(kV1.toUpper(), QString()), kV1);
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

    // #41: browse opens at the local side and hands back the server's path.
    QCOMPARE(app.browseStartFor(QStringLiteral("/srv/downloads/iso")),
             QStringLiteral("/mnt/nas/iso"));
    QCOMPARE(app.browseStartFor(QStringLiteral("/elsewhere")), QStringLiteral("/elsewhere"));
    QVariantMap pick = app.serverPathForPick(QStringLiteral("/mnt/nas/films"));
    QCOMPARE(pick.value(QStringLiteral("path")).toString(), QStringLiteral("/srv/downloads/films"));
    QVERIFY(pick.value(QStringLiteral("mapped")).toBool());
    QVERIFY(!pick.value(QStringLiteral("outside")).toBool());
    pick = app.serverPathForPick(QStringLiteral("/home/me/Videos"));
    QCOMPARE(pick.value(QStringLiteral("path")).toString(), QStringLiteral("/home/me/Videos"));
    QVERIFY(pick.value(QStringLiteral("outside")).toBool());
    QCOMPARE(pick.value(QStringLiteral("mappings")).toString(),
             QStringLiteral("/mnt/nas → /srv/downloads"));
    app.setPathMapEnabled(false);
    QCOMPARE(app.browseStartFor(QStringLiteral("/srv/downloads/iso")),
             QStringLiteral("/srv/downloads/iso"));
    pick = app.serverPathForPick(QStringLiteral("/home/me/Videos"));
    QVERIFY(!pick.value(QStringLiteral("outside")).toBool());
    QVERIFY(!pick.value(QStringLiteral("mapped")).toBool());
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

void TestAppGlue::theBarWidgetHidesTheTrayIconUnderAuto()
{
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    omaseed::integration::WidgetHeartbeat heartbeat(nullptr, runtime.path());

    App app;
    app.setTrayIconMode(QStringLiteral("auto"));
    omaseed::integration::Tray tray(nullptr);
    app.attachTray(&tray);
    app.attachWidgetHeartbeat(&heartbeat);
    QVERIFY(!app.barWidgetRunning());
    QVERIFY(tray.iconWanted());

    // The widget's touch: hidden within one watcher tick.
    QSignalSpy running(&app, &App::barWidgetRunningChanged);
    {
        QFile alive(heartbeat.filePath());
        QVERIFY(alive.open(QIODevice::WriteOnly));
    }
    QTRY_VERIFY_WITH_TIMEOUT(!tray.iconWanted(), 3000);
    QVERIFY(app.barWidgetRunning());
    QVERIFY(running.count() >= 1);

    // always and never override the heartbeat both ways.
    app.setTrayIconMode(QStringLiteral("always"));
    QVERIFY(tray.iconWanted());
    app.setTrayIconMode(QStringLiteral("never"));
    QVERIFY(!tray.iconWanted());
    app.setTrayIconMode(QStringLiteral("auto"));
    QVERIFY(!tray.iconWanted());

    // A stale file (the widget gone without cleaning up) brings the icon back.
    {
        QFile alive(heartbeat.filePath());
        QVERIFY(alive.open(QIODevice::Append));
        QVERIFY(alive.setFileTime(QDateTime::currentDateTime().addSecs(-120),
                                  QFileDevice::FileModificationTime));
    }
    QTRY_VERIFY_WITH_TIMEOUT(tray.iconWanted(), 3000);
    QVERIFY(!app.barWidgetRunning());
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

namespace {

/// Just enough qBittorrent for a toggle: one torrent that stays `seq_dl: false` in every
/// maindata (as it would until the server's next answer shows the change), and a count of
/// toggle requests. It also keeps the last torrents/setShareLimits body.
class ToggleServer : public QTcpServer
{
public:
    int toggles = 0;
    QByteArray shareLimitsBody;
    QString hash = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    /// With requireLogin, everything but auth/login wants the SID cookie, and a login is
    /// refused while refuseLogin is set.
    bool requireLogin = false;
    bool refuseLogin = false;
    int logins = 0;
    int requests = 0;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            qsizetype length = 0;
            for (const QByteArray &line : buffer->left(headerEnd).split('\n'))
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toLongLong();
            if (buffer->size() < headerEnd + 4 + length)
                return;
            const QByteArray target = buffer->left(buffer->indexOf('\n')).split(' ').value(1);
            const QByteArray requestBody = buffer->mid(headerEnd + 4, length);
            const bool hasCookie = buffer->left(headerEnd).contains("SID=ok");
            buffer->clear();
            ++requests;
            QByteArray status = "200 OK";
            QByteArray extraHeaders;
            QByteArray body = "Ok.";
            if (target.startsWith("/api/v2/auth/login")) {
                ++logins;
                if (refuseLogin) {
                    status = "403 Forbidden";
                    body = "Forbidden";
                } else {
                    extraHeaders = "Set-Cookie: SID=ok; path=/\r\n";
                }
            } else if (requireLogin && !hasCookie) {
                status = "403 Forbidden";
                body = "Forbidden";
            } else if (target.startsWith("/api/v2/app/version"))
                body = "v5.2.3";
            else if (target.startsWith("/api/v2/app/webapiVersion"))
                body = "2.15.1";
            else if (target.startsWith("/api/v2/torrents/info"))
                body = QStringLiteral(R"([{"hash":"%1","name":"t"}])").arg(hash).toUtf8();
            else if (target.startsWith("/api/v2/sync/maindata"))
                body = QStringLiteral(R"({"rid":1,"full_update":true,"torrents":{"%1":)"
                                      R"({"name":"t","seq_dl":false,"state":"downloading",)"
                                      R"("inactive_seeding_time_limit":45}}})")
                           .arg(hash)
                           .toUtf8();
            else if (target.startsWith("/api/v2/torrents/toggleSequentialDownload"))
                ++toggles;
            else if (target.startsWith("/api/v2/torrents/setShareLimits"))
                shareLimitsBody = requestBody;
            socket->write("HTTP/1.1 " + status + "\r\n" + extraHeaders + "Content-Length: " +
                          QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }
};

} // namespace

void TestAppGlue::aSecondPressBeforeTheNextSyncDoesNotUndoTheToggle()
{
    ToggleServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());

    App app;
    app.start();
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 1, 5000);

    // "sequential" pressed twice before a sync shows the first: one toggle, not two.
    app.setSequentialDownload({server.hash}, true);
    app.setSequentialDownload({server.hash}, true);
    QTRY_COMPARE_WITH_TIMEOUT(server.toggles, 1, 3000);
    QTest::qWait(300);
    QCOMPARE(server.toggles, 1);

    // Turning it off again is a real change, still before the model caught up.
    app.setSequentialDownload({server.hash}, false);
    QTRY_COMPARE_WITH_TIMEOUT(server.toggles, 2, 3000);

    app.engine()->stop();
    qunsetenv("OMASEED_SERVER_URL");
}

void TestAppGlue::limitsCarryTheTorrentsInactiveSeedingTime()
{
    // Review VIEW-5 (d): the row carries `inactive_seeding_time_limit`, and setLimits sends
    // what it is given (the Limits sheet passes the row's own value) instead of -2.
    ToggleServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());

    App app;
    app.start();
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 1, 5000);
    const qint64 inactive =
        app.torrentRow(server.hash).value(QStringLiteral("inactiveSeedingTimeLimit")).toLongLong();
    QCOMPARE(inactive, qint64(45));

    app.setLimits({server.hash}, -1, -1, -2, -2, QStringLiteral("Default"), inactive);
    QTRY_VERIFY_WITH_TIMEOUT(!server.shareLimitsBody.isEmpty(), 3000);
    const QUrlQuery body(QString::fromUtf8(server.shareLimitsBody));
    QCOMPARE(body.queryItemValue(QStringLiteral("inactiveSeedingTimeLimit")), QStringLiteral("45"));
    QCOMPARE(body.queryItemValue(QStringLiteral("seedingTimeLimit")), QStringLiteral("-2"));

    // Called without it (the C++ default), the global sentinel still goes out.
    server.shareLimitsBody.clear();
    app.setLimits({server.hash}, -1, -1, -2, -2);
    QTRY_VERIFY_WITH_TIMEOUT(!server.shareLimitsBody.isEmpty(), 3000);
    QCOMPARE(QUrlQuery(QString::fromUtf8(server.shareLimitsBody))
                 .queryItemValue(QStringLiteral("inactiveSeedingTimeLimit")),
             QStringLiteral("-2"));

    app.engine()->stop();
    qunsetenv("OMASEED_SERVER_URL");
}

void TestAppGlue::retryAfterARefusedLoginLogsInOnceMore()
{
    // Review REG-2: after one refused login the client stops logging in by itself (IP bans),
    // but the lost-server "retry" is the user asking for one more attempt.
    ToggleServer server;
    server.requireLogin = true;
    server.refuseLogin = true;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    qputenv("OMASEED_USERNAME", "admin");
    qputenv("OMASEED_PASSWORD", "secret");

    App app;
    QSignalSpy authRequired(app.engine(), &omaseed::core::SyncEngine::authRequired);
    app.start();
    QTRY_COMPARE_WITH_TIMEOUT(authRequired.count(), 1, 5000);
    QCOMPARE(server.logins, 1);

    // The ban has expired: retry logs in again and the torrent arrives.
    server.refuseLogin = false;
    app.retryNow();
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 1, 5000);
    QCOMPARE(server.logins, 2);
    QCOMPARE(authRequired.count(), 1);

    app.engine()->stop();
    qunsetenv("OMASEED_SERVER_URL");
    qunsetenv("OMASEED_USERNAME");
    qunsetenv("OMASEED_PASSWORD");
}

void TestAppGlue::removingTheLiveServerClearsItsWorld()
{
    // Review APP-11: removing the only (live) server must not leave the tray, the global
    // state or the client on the removed server's world.
    ToggleServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qunsetenv("OMASEED_SERVER_URL");

    App app;
    omaseed::integration::Tray tray(nullptr);
    app.attachTray(&tray);
    app.start();
    const QString id = app.addServer(
        QVariantMap{
            {QStringLiteral("name"), QStringLiteral("gone-soon")},
            {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())},
            {QStringLiteral("authMode"), QStringLiteral("none")},
        },
        QString());
    QVERIFY(!id.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(app.globalState()->connected(), 5000);
    QCOMPARE(app.globalState()->serverName(), QStringLiteral("gone-soon"));
    QVERIFY(tray.tooltip().contains(QStringLiteral("gone-soon")));

    QVERIFY(app.removeServer(id));
    QVERIFY(!app.hasServer());
    QCOMPARE(app.torrents()->count(), 0);
    QVERIFY(!app.globalState()->connected());
    QVERIFY(app.globalState()->serverName().isEmpty());
    QVERIFY(app.client()->baseUrl().isEmpty());
    QVERIFY(!tray.tooltip().contains(QStringLiteral("gone-soon")));
    QCOMPARE(tray.state(), omaseed::integration::Tray::Disconnected);

    // Nothing polls the removed server afterwards. A request already on the wire when the
    // server was removed (the connect-time preferences read, say) may still land: let it,
    // then count.
    QTest::qWait(300);
    const int before = server.requests;
    QTest::qWait(2000);
    QCOMPARE(server.requests, before);
}

void TestAppGlue::anUnreadableServersFileIsNamedWhereItWasKept()
{
    // Review Q9: first run shows although the user had servers; App says why and where the
    // unreadable file went, before anything new is saved.
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qunsetenv("OMASEED_SERVER_URL");
    const QString configDir = home.path() + QStringLiteral("/omaseed");
    QVERIFY(QDir().mkpath(configDir));
    const QString filePath = configDir + QStringLiteral("/servers.json");
    const QByteArray corrupt = "{ \"version\": 1, \"servers\": [ oops";
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(corrupt);
    }

    App app;
    QCOMPARE(app.serversKeptPath(), QString());
    QSignalSpy changed(&app, &App::serverChanged);
    app.start();
    QVERIFY(changed.count() >= 1);
    QVERIFY(app.firstRunNeeded());
    QVERIFY(!app.serversLoadError().isEmpty());
    const QString kept = app.serversKeptPath();
    QVERIFY2(kept.startsWith(filePath + QStringLiteral(".unreadable-")), qPrintable(kept));
    QFile file(kept);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), corrupt);
}

void TestAppGlue::aSwitchReadsConnectingNeverDown()
{
    using omaseed::core::ServerListModel;
    ToggleServer first;
    ToggleServer second;
    QVERIFY(first.listen(QHostAddress::LocalHost));
    QVERIFY(second.listen(QHostAddress::LocalHost));
    QTcpServer closed; // a port nothing answers on once it is closed
    QVERIFY(closed.listen(QHostAddress::LocalHost));
    const quint16 deadPort = closed.serverPort();
    closed.close();
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qunsetenv("OMASEED_SERVER_URL");

    App app;
    app.start();
    ServerListModel *list = app.serverList();
    const auto field = [list](const QString &id, int role) {
        return list->data(list->index(list->indexOfId(id), 0), role).toString();
    };
    const auto fields = [](const QString &name, quint16 port) {
        return QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:%1").arg(port)},
            {QStringLiteral("authMode"), QStringLiteral("none")},
        };
    };

    // F4: before its first answer the server is on its way, not down.
    const QString one = app.addServer(fields(QStringLiteral("one"), first.serverPort()), QString());
    QVERIFY(app.serverConnecting());
    QCOMPARE(field(one, ServerListModel::StateRole), QStringLiteral("connecting"));
    QTRY_VERIFY_WITH_TIMEOUT(app.globalState()->connected(), 5000);
    QVERIFY(!app.serverConnecting());
    QCOMPARE(field(one, ServerListModel::StateRole), QStringLiteral("live"));

    // F5: a server added beside it is counted at once, before omaseed has been on it.
    const QString two = app.addServer(fields(QStringLiteral("two"), second.serverPort()), QString());
    QTRY_COMPARE_WITH_TIMEOUT(field(two, ServerListModel::CountTextRole), QStringLiteral("1"), 5000);
    QCOMPARE(field(two, ServerListModel::StateRole), QStringLiteral("seen"));

    // F4: every state the row goes through on a switch; "down" is never one of them.
    QStringList states;
    connect(list, &QAbstractItemModel::dataChanged, &app, [&]() {
        const QString state = field(two, ServerListModel::StateRole);
        if (states.isEmpty() || states.constLast() != state)
            states << state;
    });
    QVERIFY(app.switchServer(two));
    QTRY_VERIFY_WITH_TIMEOUT(app.globalState()->connected() && !app.serverConnecting(), 5000);
    QCOMPARE(field(two, ServerListModel::StateRole), QStringLiteral("live"));
    QVERIFY2(states.contains(QStringLiteral("connecting")), qPrintable(states.join(u',')));
    QVERIFY2(!states.contains(QStringLiteral("down")), qPrintable(states.join(u',')));

    // A real failure is red: nothing answers there.
    const QString dead = app.addServer(fields(QStringLiteral("dead"), deadPort), QString());
    QVERIFY(app.switchServer(dead));
    QCOMPARE(field(dead, ServerListModel::StateRole), QStringLiteral("connecting"));
    QTRY_COMPARE_WITH_TIMEOUT(field(dead, ServerListModel::StateRole), QStringLiteral("down"), 15000);
    QVERIFY(!app.serverConnecting());
    app.engine()->stop();
}

void TestAppGlue::aServerOfAnotherEngineIsNeverAsked()
{
    // DECISIONS C28: servers.json from an engines build (v2, #84) with a Transmission server
    // as the default. v0.4 lists it, says why it can't use it, and sends it nothing: not at
    // start, not on "retry now", not for a test from its sheet, not when the others are probed.
    QTcpServer transmission;
    QVERIFY(transmission.listen(QHostAddress::LocalHost));
    int connections = 0;
    connect(&transmission, &QTcpServer::newConnection, this, [&]() {
        while (QTcpSocket *socket = transmission.nextPendingConnection()) {
            ++connections;
            socket->abort();
            socket->deleteLater();
        }
    });

    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qunsetenv("OMASEED_SERVER_URL");
    const QString configDir = home.path() + QStringLiteral("/omaseed");
    QVERIFY(QDir().mkpath(configDir));
    const QString filePath = configDir + QStringLiteral("/servers.json");
    const QJsonObject seedbox{
        {QStringLiteral("id"), QStringLiteral("t-1")},
        {QStringLiteral("name"), QStringLiteral("seedbox")},
        {QStringLiteral("url"),
         QStringLiteral("http://127.0.0.1:%1").arg(transmission.serverPort())},
        {QStringLiteral("authMode"), QStringLiteral("none")},
        {QStringLiteral("engine"), QStringLiteral("transmission")},
        {QStringLiteral("engineVersion"), QStringLiteral("4.1.0")},
    };
    const QJsonObject other{
        {QStringLiteral("id"), QStringLiteral("q-1")},
        {QStringLiteral("name"), QStringLiteral("public")},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:9")}, // nobody listens
        {QStringLiteral("authMode"), QStringLiteral("none")},
        {QStringLiteral("engine"), QStringLiteral("qbittorrent")},
    };
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            QJsonDocument(QJsonObject{{QStringLiteral("version"), 2},
                                      {QStringLiteral("servers"), QJsonArray{seedbox, other}},
                                      {QStringLiteral("defaultId"), QStringLiteral("t-1")}})
                .toJson());
    }

    App app;
    QSignalSpy tested(&app, &App::serverTested);
    app.start();
    QVERIFY(!app.firstRunNeeded());
    QCOMPARE(app.serverList()->count(), 2);
    QCOMPARE(app.serverName(), QStringLiteral("seedbox"));
    QVERIFY(app.serverOtherEngine());
    QVERIFY(app.serverRefused());
    QVERIFY(app.isServerLost());
    QVERIFY(!app.engine()->isRunning());

    const QVariantMap problem = app.serverProblem(QString(), -1);
    QCOMPARE(problem.value(QStringLiteral("kind")).toString(), QStringLiteral("engine"));
    QCOMPARE(problem.value(QStringLiteral("headline")).toString(),
             QStringLiteral("seedbox is a Transmission server"));
    QCOMPARE(problem.value(QStringLiteral("retry")).toString(), QString());
    QCOMPARE(app.guardNote(QStringLiteral("p")),
             QStringLiteral("seedbox is a Transmission server: p does nothing here"));

    app.retryNow();
    QVERIFY(!app.engine()->isRunning());
    app.testServer(app.serverList()->serverMap(QStringLiteral("t-1")), QString());
    QVERIFY(tested.wait(2000));
    QCOMPARE(tested.constFirst().at(1).toBool(), false);
    QCOMPARE(tested.constFirst().at(6).toString(),
             QStringLiteral("seedbox is a Transmission server: this omaseed only talks to "
                            "qBittorrent. A newer omaseed can."));

    // The rows say it too, and the server is never rewritten as a qBittorrent.
    const QVariantList rows = app.serverRows();
    QCOMPARE(rows.constFirst().toMap().value(QStringLiteral("state")).toString(),
             QStringLiteral("refused"));
    QVERIFY(rows.constFirst()
                .toMap()
                .value(QStringLiteral("detail"))
                .toString()
                .contains(QStringLiteral("Transmission server")));

    // On the other server, the Transmission one is still listed and still not asked.
    QVERIFY(app.switchServer(QStringLiteral("q-1")));
    QVERIFY(!app.serverOtherEngine());
    QVERIFY(!app.serverRefused());
    const QVariantMap seedboxRow = app.serverRows().at(1).toMap();
    QCOMPARE(seedboxRow.value(QStringLiteral("key")).toString(), QStringLiteral("t-1"));
    QCOMPARE(seedboxRow.value(QStringLiteral("state")).toString(), QStringLiteral("refused"));
    QCOMPARE(seedboxRow.value(QStringLiteral("meta")).toString(),
             QStringLiteral("Transmission server"));
    QVERIFY(app.switchServer(QStringLiteral("t-1")));
    QVERIFY(app.serverOtherEngine());
    QVERIFY(!app.engine()->isRunning());

    QTest::qWait(300); // anything sent would have connected by now
    QCOMPARE(connections, 0);

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 2);
    const QJsonObject saved = root.value(QStringLiteral("servers")).toArray().at(0).toObject();
    QCOMPARE(saved.value(QStringLiteral("engine")).toString(), QStringLiteral("transmission"));
    QCOMPARE(saved.value(QStringLiteral("engineVersion")).toString(), QStringLiteral("4.1.0"));
}

void TestAppGlue::secretsInAPlainFileAreNamed()
{
    // Review Q10: with no keychain, the first secret settles the store on the file backend,
    // and App names the directory. The probe and the keychain are stand-ins: the bogus
    // DBUS_SESSION_BUS_ADDRESS above does NOT keep libsecret off the desktop's keyring.
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qunsetenv("OMASEED_SERVER_URL");

    App app;
    using omaseed::core::SecretStore;
    int probes = 0;
    app.secretStore()->setProbeRunner([&probes](std::function<void(bool)> done) {
        ++probes;
        done(false); // no keychain answered
    });
    const auto answerNothing = [](const QString &, SecretStore::KeychainAnswer answer) {
        answer(SecretStore::KeychainStatus::NotFound, QString(), QString());
    };
    app.secretStore()->setKeychainHooks(SecretStore::KeychainHooks{
        answerNothing,
        [](const QString &, const QString &, SecretStore::KeychainAnswer answer) {
            answer(SecretStore::KeychainStatus::Failed, QString(), QStringLiteral("no keychain"));
        },
        answerNothing,
    });

    QCOMPARE(app.plainSecretsPath(), QString()); // not known yet...
    QCOMPARE(probes, 0);                         // ...and asking did not probe
    QSignalSpy changed(&app, &App::plainSecretsPathChanged);
    const QString id = app.addServer(
        QVariantMap{
            {QStringLiteral("name"), QStringLiteral("keyed")},
            {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:9")},
            {QStringLiteral("authMode"), QStringLiteral("apiKey")},
        },
        QStringLiteral("not-a-real-key"));
    QVERIFY(!id.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);
    QCOMPARE(app.plainSecretsPath(), home.path() + QStringLiteral("/omaseed/secrets"));
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo::exists(app.plainSecretsPath() + QLatin1Char('/') + id + QStringLiteral(".json")),
        5000);
    app.engine()->stop();
}

namespace {

/// Copies a fixture into `dir` and dates it `minutesAgo` minutes back.
bool copyFixture(const QTemporaryDir &dir, const QString &name, int minutesAgo)
{
    const QString to = dir.filePath(name);
    if (!QFile::copy(QStringLiteral(OMASEED_FIXTURES_DIR "/") + name, to))
        return false;
    QFile file(to);
    return file.open(QIODevice::ReadWrite) &&
           file.setFileTime(QDateTime::currentDateTime().addSecs(-60LL * minutesAgo),
                            QFileDevice::FileModificationTime);
}

} // namespace

void TestAppGlue::theDownloadsListMarksWhatTheLibraryHolds()
{
    App app;
    app.startDemo(4);
    // single_file_v1.torrent's infohash, put into the library as a sync would.
    const QString held = QStringLiteral("857ddb9df19f876e5a5e3f5b9c4f3991cbce1b04");
    app.torrents()->applyPartial(
        QJsonObject{{held, QJsonObject{{QStringLiteral("name"), QStringLiteral("Single.bin")}}}},
        QJsonArray());
    QVERIFY(!app.findDuplicate(held, QString()).isEmpty());

    QTemporaryDir downloads;
    QVERIFY(downloads.isValid());
    QVERIFY(copyFixture(downloads, QStringLiteral("single_file_v1.torrent"), 5));
    QVERIFY(copyFixture(downloads, QStringLiteral("open_movies_v1.torrent"), 90));
    app.setDownloadsFolder(downloads.path());
    QCOMPARE(app.downloadsFolder(), downloads.path());

    QSignalSpy changed(&app, &App::downloadsTorrentsChanged);
    app.scanDownloads();
    QVERIFY(changed.wait(10000));
    const QVariantList list = app.downloadsTorrents();
    QCOMPARE(list.size(), 2);

    const QVariantMap newest = list[0].toMap();
    QCOMPARE(newest.value(QStringLiteral("path")).toString(),
             downloads.filePath(QStringLiteral("single_file_v1.torrent")));
    QCOMPARE(newest.value(QStringLiteral("name")).toString(), QStringLiteral("Single.bin"));
    QCOMPARE(newest.value(QStringLiteral("infoHashV1")).toString(), held);
    QCOMPARE(newest.value(QStringLiteral("inLibrary")).toBool(), true);
    QVERIFY(!newest.value(QStringLiteral("sizeText")).toString().isEmpty());
    QVERIFY(!newest.value(QStringLiteral("ageText")).toString().isEmpty());

    const QVariantMap older = list[1].toMap();
    QCOMPARE(older.value(QStringLiteral("name")).toString(), QStringLiteral("Blender Open Movies"));
    QCOMPARE(older.value(QStringLiteral("inLibrary")).toBool(), false);

    // Added elsewhere (the bar widget) after the scan: "in library" follows at the sync that
    // brings it, and goes when it is deleted, with no new scan (#20).
    const QString added = older.value(QStringLiteral("infoHashV1")).toString();
    changed.clear();
    app.torrents()->applyPartial(
        QJsonObject{{added, QJsonObject{{QStringLiteral("name"), QStringLiteral("Blender")}}}},
        QJsonArray());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(app.downloadsTorrents()[1].toMap().value(QStringLiteral("inLibrary")).toBool(), true);
    QCOMPARE(app.downloadsTorrents()[0].toMap().value(QStringLiteral("inLibrary")).toBool(), true);
    app.torrents()->applyPartial(QJsonObject(), QJsonArray{added});
    QCOMPARE(changed.count(), 2);
    QCOMPARE(app.downloadsTorrents()[1].toMap().value(QStringLiteral("inLibrary")).toBool(), false);
}

void TestAppGlue::aDemoRunListsNoRealDownloads()
{
    App plain;
    QCOMPARE(plain.downloadsFolder(),
             QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));

    App demo;
    demo.startDemo(4);
    QCOMPARE(demo.downloadsFolder(), QString());
    QSignalSpy changed(&demo, &App::downloadsTorrentsChanged);
    demo.scanDownloads();
    QVERIFY(changed.wait(10000));
    QVERIFY(demo.downloadsTorrents().isEmpty());
}

QTEST_MAIN(TestAppGlue)

#include "tst_app_glue.moc"
