// T4.7 — App wired to Notifier: the notice-worthy SyncEngine events become the right desktop
// notices, the per-event toggles suppress them, and a flapping server produces exactly one
// "lost" and one "back". T5.5 (Q16): the low-disk threshold is a setting that reaches the
// engine and survives a restart, and a tracker error is no longer a notice at all.
// SyncEngine itself (when to fire) is covered by tests/core/tst_syncengine_fixtures.cpp; this
// file only checks what App does with those events once they arrive.

#include "core/Format.hpp"
#include "core/StateFile.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "integration/Notifier.hpp"
#include "ui/app/App.hpp"

#include "../integration/FakeNotificationServer.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <chrono>
#include <functional>

using omaseed::app::App;
using omaseed::integration::Notifier;

namespace {

const QString kCompletedHash = QStringLiteral("4a4e3b1d2d0c1a8d1e6a9f0b3c2d4e5f6a7b8c9d");
const QString kTrackerHash = QStringLiteral("d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1");
constexpr qint64 kGiB = 1LL * 1024 * 1024 * 1024;

/// A maindata body that only carries the server's free space.
QJsonObject freeSpaceUpdate(int rid, qint64 freeBytes, bool full)
{
    QJsonObject data{{QStringLiteral("rid"), rid},
                     {QStringLiteral("server_state"),
                      QJsonObject{{QStringLiteral("free_space_on_disk"), freeBytes}}}};
    if (full)
        data.insert(QStringLiteral("full_update"), true);
    return data;
}

QByteArray httpResponse(int code, const QByteArray &reason, const QByteArray &body)
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

/// The three GET endpoints SyncEngine uses, for the flapping-server test.
class FakeQbtServer : public QTcpServer
{
public:
    explicit FakeQbtServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {}

    bool start(quint16 port = 0) { return listen(QHostAddress::LocalHost, port); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

    /// One maindata body per poll; the last one repeats.
    void setMainData(const QList<QByteArray> &bodies) { m_bodies = bodies; }

    int mainDataRequests = 0;

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(socketDescriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            const QByteArray head = buffer->left(headerEnd);
            const QByteArray requestLine = head.split('\n').value(0);
            const QByteArray target = requestLine.split(' ').value(1);

            qsizetype contentLength = 0;
            for (const QByteArray &line : head.split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    contentLength = line.mid(15).trimmed().toLongLong();
            }
            const qsizetype bodyStart = headerEnd + 4;
            if (buffer->size() < bodyStart + contentLength)
                return; // wait for the rest of the body
            buffer->clear();

            QByteArray response;
            if (target.startsWith("/api/v2/app/version")) {
                response = httpResponse(200, "OK", "v5.2.3");
            } else if (target.startsWith("/api/v2/app/webapiVersion")) {
                response = httpResponse(200, "OK", "2.15.1");
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                const int index = mainDataRequests++;
                const QByteArray responseBody =
                    m_bodies.isEmpty()
                        ? QByteArray("{}")
                        : m_bodies.at(std::min(qsizetype(index), m_bodies.size() - 1));
                response = httpResponse(200, "OK", responseBody);
            } else {
                response = httpResponse(404, "Not Found", "no");
            }

            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    }

private:
    QList<QByteArray> m_bodies;
};

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

QJsonObject torrentRow(const QString &name, const QString &category, double progress, qint64 size)
{
    QJsonObject row;
    row.insert(QStringLiteral("name"), name);
    row.insert(QStringLiteral("category"), category);
    row.insert(QStringLiteral("progress"), progress);
    row.insert(QStringLiteral("size"), size);
    row.insert(QStringLiteral("total_size"), size);
    row.insert(QStringLiteral("state"),
               progress >= 1.0 ? QStringLiteral("uploading") : QStringLiteral("downloading"));
    row.insert(QStringLiteral("has_tracker_error"), false);
    row.insert(QStringLiteral("tracker"), QString());
    return row;
}

} // namespace

class TestAppNotifications : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void completedNoticeMatchesSpecBodyAndOffersOpenFolderWhenTheFolderExists();
    void completedNoticeOmitsOpenFolderWhenTheFolderIsUnknown();
    void trackerErrorSendsNoNotice();
    void diskAlmostFullMatchesQ2Wording();
    void disabledToggleSuppressesTheNotice();
    void flappingServerFiresOneLostAndOneBack();
    void aHiddenStartWaitsQuietlyForItsServer();
    void aHiddenStartSaysSoWhenTheServerStaysAway();
    void lowDiskThresholdDefaultsToTwoGiBInTheEngine();
    void lowDiskThresholdPersistsReachesTheEngineAndClamps();
    void anOldStateFileWithTheTrackerKeyStillLoads();

private:
    FakeNotificationBus m_bus;
    QTemporaryDir m_configDir;
    QByteArray m_previousConfigHome;
};

void TestAppNotifications::initTestCase()
{
    QVERIFY2(m_bus.start(), "could not start a private session dbus-daemon for the test");
    qRegisterMetaType<omaseed::core::ApiError>("omaseed::core::ApiError");
}

void TestAppNotifications::cleanupTestCase()
{
    m_bus.stop();
}

void TestAppNotifications::init()
{
    m_bus.server()->calls.clear();
    m_bus.server()->nextId = 1;

    QVERIFY(m_configDir.isValid());
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_configDir.path().toUtf8());
    QFile::remove(omaseed::core::StateFile::path()); // every test starts from the defaults
    qunsetenv("OMASEED_SERVER_URL");
    qunsetenv("OMASEED_SERVER_NAME");
}

void TestAppNotifications::cleanup()
{
    if (m_previousConfigHome.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
}

void TestAppNotifications::completedNoticeMatchesSpecBodyAndOffersOpenFolderWhenTheFolderExists()
{
    QTemporaryDir contentDir;
    QVERIFY(contentDir.isValid());
    const QString contentPath = contentDir.filePath(QStringLiteral("fixture-one"));
    QVERIFY(QDir().mkpath(contentPath));

    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);

    QJsonObject seedTorrents;
    seedTorrents.insert(
        kCompletedHash,
        torrentRow(QStringLiteral("fixture-one"), QStringLiteral("linux"), 0.5, 1404304589));
    QJsonObject seed;
    seed.insert(QStringLiteral("full_update"), true);
    seed.insert(QStringLiteral("rid"), 1);
    seed.insert(QStringLiteral("torrents"), seedTorrents);
    app.applyDemoData(seed);

    QJsonObject completion;
    completion.insert(QStringLiteral("rid"), 2);
    QJsonObject diff{{QStringLiteral("progress"), 1.0},
                     {QStringLiteral("completion_on"), 1789834338},
                     {QStringLiteral("content_path"), contentPath}};
    completion.insert(QStringLiteral("torrents"), QJsonObject{{kCompletedHash, diff}});
    app.applyDemoData(completion);

    QCOMPARE(m_bus.server()->calls.size(), 1);
    const auto &call = m_bus.server()->calls.first();
    QCOMPARE(call.summary, QStringLiteral("Download complete"));
    const QString expectedBody = QStringLiteral("%1 · %2 · %3")
                                     .arg(QStringLiteral("fixture-one"),
                                          omaseed::core::Format::size(1404304589),
                                          QStringLiteral("linux"));
    QCOMPARE(call.body, expectedBody);
    QCOMPARE(call.actions,
             QStringList({QStringLiteral("open-folder"),
                          QStringLiteral("open folder"),
                          QStringLiteral("show"),
                          QStringLiteral("show"),
                          QStringLiteral("default"),
                          QStringLiteral("show")}));
}

void TestAppNotifications::completedNoticeOmitsOpenFolderWhenTheFolderIsUnknown()
{
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);

    QJsonObject seed{
        {QStringLiteral("full_update"), true},
        {QStringLiteral("rid"), 1},
        {QStringLiteral("torrents"),
         QJsonObject{
             {kCompletedHash,
              torrentRow(QStringLiteral("fixture-one"), QStringLiteral("linux"), 0.5, 1000)}}}};
    app.applyDemoData(seed);

    QJsonObject completion{
        {QStringLiteral("rid"), 2},
        {QStringLiteral("torrents"),
         QJsonObject{{kCompletedHash,
                      QJsonObject{{QStringLiteral("progress"), 1.0},
                                  {QStringLiteral("completion_on"), 1789834338}}}}}};
    app.applyDemoData(completion);

    QCOMPARE(m_bus.server()->calls.size(), 1);
    QCOMPARE(m_bus.server()->calls.first().actions,
             QStringList({QStringLiteral("show"),
                          QStringLiteral("show"),
                          QStringLiteral("default"),
                          QStringLiteral("show")}));
}

// Q16: tracker health is the Trackers tab badge and the rail filter, never a desktop notice.
void TestAppNotifications::trackerErrorSendsNoNotice()
{
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);

    QJsonObject seed{
        {QStringLiteral("full_update"), true},
        {QStringLiteral("rid"), 1},
        {QStringLiteral("torrents"),
         QJsonObject{
             {kTrackerHash, torrentRow(QStringLiteral("fixture-two"), QString(), 0.3, 1000)}}},
        {QStringLiteral("trackers"),
         QJsonObject{{QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
                      QJsonArray{kTrackerHash}}}}};
    app.applyDemoData(seed);

    QJsonObject diff{
        {QStringLiteral("rid"), 2},
        {QStringLiteral("torrents"),
         QJsonObject{{kTrackerHash, QJsonObject{{QStringLiteral("has_tracker_error"), true}}}}}};
    app.applyDemoData(diff);

    QVERIFY(app.torrents()->torrent(kTrackerHash).hasTrackerError);
    QCOMPARE(m_bus.server()->calls.size(), 0);
}

void TestAppNotifications::diskAlmostFullMatchesQ2Wording()
{
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);

    app.applyDemoData(freeSpaceUpdate(1, kGiB, /*full=*/true));

    QCOMPARE(m_bus.server()->calls.size(), 1);
    const auto &call = m_bus.server()->calls.first();
    QCOMPARE(call.summary, QStringLiteral("Disk almost full"));
    QCOMPARE(call.body, QStringLiteral("%1 free").arg(omaseed::core::Format::size(kGiB)));
    QCOMPARE(call.actions,
             QStringList({QStringLiteral("settings"),
                          QStringLiteral("settings"),
                          QStringLiteral("default"),
                          QStringLiteral("settings")}));
}

void TestAppNotifications::disabledToggleSuppressesTheNotice()
{
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);

    app.setNotificationEnabled(QStringLiteral("completed"), false);
    QCOMPARE(app.notificationsEnabled().value(QStringLiteral("completed")).toBool(), false);

    QJsonObject seed{
        {QStringLiteral("full_update"), true},
        {QStringLiteral("rid"), 1},
        {QStringLiteral("torrents"),
         QJsonObject{
             {kCompletedHash,
              torrentRow(QStringLiteral("fixture-one"), QStringLiteral("linux"), 0.5, 1000)}}}};
    app.applyDemoData(seed);

    QJsonObject completion{
        {QStringLiteral("rid"), 2},
        {QStringLiteral("torrents"),
         QJsonObject{{kCompletedHash,
                      QJsonObject{{QStringLiteral("progress"), 1.0},
                                  {QStringLiteral("completion_on"), 1789834338}}}}}};
    app.applyDemoData(completion);

    QCOMPARE(m_bus.server()->calls.size(), 0);

    // Persisted: a fresh App picks the toggle back up from state.json.
    App second;
    Notifier notifier2(m_bus.connection());
    second.attachNotifier(&notifier2);
    QCOMPARE(second.notificationsEnabled().value(QStringLiteral("completed")).toBool(), false);
}

void TestAppNotifications::flappingServerFiresOneLostAndOneBack()
{
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        port = probe.serverPort();
    } // nothing listens there now: the first poll gets connection refused

    qputenv("OMASEED_SERVER_URL", QStringLiteral("http://127.0.0.1:%1").arg(port).toUtf8());
    qputenv("OMASEED_SERVER_NAME", "fake");

    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);
    app.engine()->setIntervals(std::chrono::milliseconds(10),
                               std::chrono::milliseconds(10),
                               std::chrono::milliseconds(10),
                               std::chrono::milliseconds(30));

    app.start();
    QVERIFY(waitFor([&] { return m_bus.server()->calls.size() >= 1; }));
    QCOMPARE(m_bus.server()->calls.size(), 1);
    const auto &lostCall = m_bus.server()->calls.first();
    QCOMPARE(lostCall.summary, QStringLiteral("fake is unreachable"));
    QVERIFY(lostCall.body.startsWith(QStringLiteral("127.0.0.1:%1 · ").arg(port)));
    QVERIFY(lostCall.body.contains(QStringLiteral("retrying every")));
    // The reason is the underlying network error, not the request that carried it (spec §6
    // Fig. 21: "connection refused", not "GET sync/maindata: Connection refused").
    QVERIFY(!lostCall.body.contains(QStringLiteral("GET")));
    QVERIFY(!lostCall.body.contains(QStringLiteral("sync/maindata")));

    // Several failed retries must not spam a second "lost" notice.
    QTest::qWait(100);
    QCOMPARE(m_bus.server()->calls.size(), 1);

    FakeQbtServer server;
    QVERIFY(server.start(port));
    server.setMainData({QByteArrayLiteral("{\"full_update\":true,\"rid\":1,\"torrents\":{}}")});

    QVERIFY(waitFor([&] { return m_bus.server()->calls.size() >= 2; }));
    QCOMPARE(m_bus.server()->calls.size(), 2);
    QCOMPARE(m_bus.server()->calls.at(1).summary, QStringLiteral("fake is back"));
    QCOMPARE(m_bus.server()->calls.at(1).body, QStringLiteral("connected again"));

    app.engine()->stop();
}

// Start at login, in the tray: omaseed often runs before qBittorrent is up. A server that
// answers within the quiet start gets neither "unreachable" nor "back".
void TestAppNotifications::aHiddenStartWaitsQuietlyForItsServer()
{
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        port = probe.serverPort();
    }
    qputenv("OMASEED_SERVER_URL", QStringLiteral("http://127.0.0.1:%1").arg(port).toUtf8());
    qputenv("OMASEED_SERVER_NAME", "fake");

    App app;
    app.setQuietStartGrace(std::chrono::seconds(30));
    app.setLaunchHidden(true);
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);
    app.engine()->setIntervals(std::chrono::milliseconds(10),
                               std::chrono::milliseconds(10),
                               std::chrono::milliseconds(10),
                               std::chrono::milliseconds(30));
    QSignalSpy lost(app.engine(), &omaseed::core::SyncEngine::serverLost);
    QSignalSpy back(app.engine(), &omaseed::core::SyncEngine::serverBack);

    app.start();
    QVERIFY(waitFor([&] { return lost.size() >= 1; }));
    QTest::qWait(100);

    FakeQbtServer server;
    QVERIFY(server.start(port));
    server.setMainData({QByteArrayLiteral("{\"full_update\":true,\"rid\":1,\"torrents\":{}}")});
    QVERIFY(waitFor([&] { return back.size() >= 1; }));
    QTest::qWait(50);
    QCOMPARE(m_bus.server()->calls.size(), 0);

    app.engine()->stop();
}

// ...and one that is still away when the quiet start runs out is announced then, once, and
// its return as usual.
void TestAppNotifications::aHiddenStartSaysSoWhenTheServerStaysAway()
{
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        port = probe.serverPort();
    }
    qputenv("OMASEED_SERVER_URL", QStringLiteral("http://127.0.0.1:%1").arg(port).toUtf8());
    qputenv("OMASEED_SERVER_NAME", "fake");

    App app;
    // A grace no runner outlasts, so the loss is surely held; it is cut short below. A 300 ms
    // grace failed on a slow CI runner: the first loss came after it had run out.
    app.setQuietStartGrace(std::chrono::seconds(30));
    app.setLaunchHidden(true);
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);
    app.engine()->setIntervals(std::chrono::milliseconds(10),
                               std::chrono::milliseconds(10),
                               std::chrono::milliseconds(10),
                               std::chrono::milliseconds(30));
    QSignalSpy lost(app.engine(), &omaseed::core::SyncEngine::serverLost);

    app.start();
    QVERIFY(waitFor([&] { return lost.size() >= 1; }));
    QCOMPARE(m_bus.server()->calls.size(), 0);
    // Let the quiet start run out now: the held loss is announced then.
    app.setQuietStartGrace(std::chrono::milliseconds(10));
    app.setLaunchHidden(true);
    QVERIFY(waitFor([&] { return m_bus.server()->calls.size() >= 1; }));
    QCOMPARE(m_bus.server()->calls.first().summary, QStringLiteral("fake is unreachable"));
    QVERIFY(m_bus.server()->calls.first().body.contains(QStringLiteral("retrying every")));

    FakeQbtServer server;
    QVERIFY(server.start(port));
    server.setMainData({QByteArrayLiteral("{\"full_update\":true,\"rid\":1,\"torrents\":{}}")});
    QVERIFY(waitFor([&] { return m_bus.server()->calls.size() >= 2; }));
    QCOMPARE(m_bus.server()->calls.at(1).summary, QStringLiteral("fake is back"));

    app.engine()->stop();
}

// T5.5 (Q16): the threshold is a setting. Nothing persisted means 2 GiB, in App and in the
// engine that does the comparing.
void TestAppNotifications::lowDiskThresholdDefaultsToTwoGiBInTheEngine()
{
    App app;
    QCOMPARE(app.lowDiskThresholdGiB(), 2);
    QCOMPARE(app.engine()->lowDiskThreshold(), 2 * kGiB);
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);
    QCOMPARE(app.engine()->lowDiskThreshold(), 2 * kGiB);

    // 3 GiB free is above the line: no notice.
    app.applyDemoData(freeSpaceUpdate(1, 3 * kGiB, /*full=*/true));
    QCOMPARE(m_bus.server()->calls.size(), 0);
}

void TestAppNotifications::lowDiskThresholdPersistsReachesTheEngineAndClamps()
{
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);
    QSignalSpy changed(&app, &App::lowDiskThresholdGiBChanged);

    app.setLowDiskThresholdGiB(5);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(app.lowDiskThresholdGiB(), 5);
    QCOMPARE(app.engine()->lowDiskThreshold(), 5 * kGiB);
    QCOMPARE(omaseed::core::StateFile::readSection(QStringLiteral("notifications"))
                 .value(QStringLiteral("lowDiskThresholdGiB"))
                 .toInt(),
             5);

    // The same 3 GiB that was fine at the default is now a notice.
    app.applyDemoData(freeSpaceUpdate(1, 3 * kGiB, /*full=*/true));
    QCOMPARE(m_bus.server()->calls.size(), 1);
    QCOMPARE(m_bus.server()->calls.first().summary, QStringLiteral("Disk almost full"));

    // Never below 1 GiB: the diskFull toggle is the off switch, not a zero here.
    app.setLowDiskThresholdGiB(0);
    QCOMPARE(app.lowDiskThresholdGiB(), 1);
    QCOMPARE(app.engine()->lowDiskThreshold(), kGiB);
    app.setLowDiskThresholdGiB(-4);
    QCOMPARE(app.lowDiskThresholdGiB(), 1);
    QCOMPARE(changed.count(), 2);

    // A fresh App reads the stored value back, and its engine gets it before any poll —
    // through the property alone, without a notifier attached. (Written by an App that is
    // not in demo mode: `app` is one since applyDemoData, and a demo never writes state.json.)
    {
        App writer;
        writer.setLowDiskThresholdGiB(7);
    }
    App reopened;
    QCOMPARE(reopened.lowDiskThresholdGiB(), 7);
    QCOMPARE(reopened.engine()->lowDiskThreshold(), 7 * kGiB);
}

// Q16 removed the "trackerError" toggle; a state.json written before that still loads, the
// stale key is simply not reported.
void TestAppNotifications::anOldStateFileWithTheTrackerKeyStillLoads()
{
    omaseed::core::StateFile::writeSection(QStringLiteral("notifications"),
                                           QJsonObject{{QStringLiteral("completed"), false},
                                                       {QStringLiteral("trackerError"), false},
                                                       {QStringLiteral("diskFull"), true}});

    App app;
    const QVariantMap enabled = app.notificationsEnabled();
    QCOMPARE(enabled.value(QStringLiteral("completed")).toBool(), false);
    QCOMPARE(enabled.value(QStringLiteral("diskFull")).toBool(), true);
    QCOMPARE(enabled.value(QStringLiteral("serverLost")).toBool(), true);
    QVERIFY(!enabled.contains(QStringLiteral("trackerError")));
    QCOMPARE(app.lowDiskThresholdGiB(), 2);

    // Saving from here drops the stale key for good.
    app.setNotificationEnabled(QStringLiteral("serverLost"), false);
    const QJsonObject section =
        omaseed::core::StateFile::readSection(QStringLiteral("notifications"));
    QVERIFY(!section.contains(QStringLiteral("trackerError")));
    QCOMPARE(section.value(QStringLiteral("lowDiskThresholdGiB")).toInt(), 2);
}

QTEST_GUILESS_MAIN(TestAppNotifications)

#include "tst_app_notifications.moc"
