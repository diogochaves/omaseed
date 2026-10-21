// T4.7 — App wired to Notifier: the notice-worthy SyncEngine events become the right desktop
// notices, the per-event toggles suppress them, and a flapping server produces exactly one
// "lost" and one "back". T5.5 (Q16): the low-disk threshold is a setting that reaches the
// engine and survives a restart, and a tracker error is no longer a notice at all.
// SyncEngine itself (when to fire) is covered by tests/core/tst_syncengine_fixtures.cpp; this
// file only checks what App does with those events once they arrive.

#include "core/Format.hpp"
#include "core/LogFilterModel.hpp"
#include "core/ProblemLog.hpp"
#include "core/SeedLedger.hpp"
#include "core/ServerLog.hpp"
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
#include <QJsonDocument>
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
    void anErrorSendsOneNoticeWithItsReason();
    void disabledToggleSuppressesTheNotice();
    void flappingServerFiresOneLostAndOneBack();
    void aHiddenStartWaitsQuietlyForItsServer();
    void aHiddenStartSaysSoWhenTheServerStaysAway();
    void lowDiskThresholdDefaultsToTwoGiBInTheEngine();
    void lowDiskThresholdPersistsReachesTheEngineAndClamps();
    void anOldStateFileWithTheTrackerKeyStillLoads();
    void aMoveFailureIsNeverANotice();
    void aDismissedFailureStaysDismissedAfterARestart();
    void theLogFaceKeepsItsLevelsPerServer();
    void theDemoHasAWholeLog();
    void theWeeklyRecapIsOffByDefault();
    void theWeeklyRecapComesOnceASundayEveningAndOpensTheConsole();

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
                          QStringLiteral("Open folder"),
                          QStringLiteral("default"),
                          QStringLiteral("Show")}));
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
             QStringList({QStringLiteral("default"), QStringLiteral("Show")}));
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
    QCOMPARE(call.actions, QStringList({QStringLiteral("default"), QStringLiteral("Settings")}));
}

// docs/28 N1: a torrent that goes into error while watched gets one notice, once the log has
// been read, with the reason in it; a click shows it. The "errored" toggle turns it off.
void TestAppNotifications::anErrorSendsOneNoticeWithItsReason()
{
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);

    const QJsonObject seed{
        {QStringLiteral("full_update"), true},
        {QStringLiteral("rid"), 1},
        {QStringLiteral("torrents"),
         QJsonObject{
             {kCompletedHash,
              torrentRow(QStringLiteral("fixture-one"), QStringLiteral("linux"), 0.5, 1000)}}}};
    app.applyDemoData(seed);

    const QJsonObject errored{
        {QStringLiteral("rid"), 2},
        {QStringLiteral("torrents"),
         QJsonObject{
             {kCompletedHash, QJsonObject{{QStringLiteral("state"), QStringLiteral("error")}}}}}};
    app.applyDemoData(errored);
    QCOMPARE(m_bus.server()->calls.size(), 0); // waits for the log

    const QJsonArray log{QJsonObject{
        {QStringLiteral("id"), 7},
        {QStringLiteral("timestamp"), 1791928475},
        {QStringLiteral("type"), 4},
        {QStringLiteral("message"),
         QStringLiteral(
             "File error alert. Torrent: \"fixture-one\". File: \"/d/fixture-one\". "
             "Reason: \"4a4e3b file_open (/d/fixture-one) error: Permission denied\"")}}};
    app.engine()->problems()->applyEntries(log);
    QCOMPARE(m_bus.server()->calls.size(), 1);
    const auto &call = m_bus.server()->calls.first();
    QCOMPARE(call.summary, QStringLiteral("fixture-one stopped with an error"));
    QCOMPARE(
        call.body,
        QStringLiteral("Can't write to its save folder: Permission denied, opening fixture-one"));
    QCOMPARE(call.actions, QStringList({QStringLiteral("default"), QStringLiteral("Show")}));

    // Another read is not another notice.
    app.engine()->problems()->applyEntries(log);
    QCOMPARE(m_bus.server()->calls.size(), 1);

    // Off: back to work, then into error again, and nothing is sent.
    app.setNotificationEnabled(QStringLiteral("errored"), false);
    app.applyDemoData(QJsonObject{
        {QStringLiteral("rid"), 3},
        {QStringLiteral("torrents"),
         QJsonObject{{kCompletedHash,
                      QJsonObject{{QStringLiteral("state"), QStringLiteral("downloading")}}}}}});
    app.applyDemoData(errored);
    app.engine()->problems()->applyEntries({});
    QCOMPARE(m_bus.server()->calls.size(), 1);
    app.setNotificationEnabled(QStringLiteral("errored"), true);
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
    // docs/33 D23: the window's words, the server named, one retry figure.
    QCOMPARE(lostCall.summary, QStringLiteral("fake didn't answer"));
    QVERIFY(lostCall.body.startsWith(
        QStringLiteral("Nothing is listening at 127.0.0.1:%1.").arg(port)));
    QVERIFY(lostCall.body.contains(QStringLiteral("omaseed tries again")));
    // Plain words: neither the request that carried it nor Qt's text.
    QVERIFY(!lostCall.body.contains(QStringLiteral("GET")));
    QVERIFY(!lostCall.body.contains(QStringLiteral("sync/maindata")));
    // docs/35 N6: the window shows the loss once the engine declared it, in the same words.
    QVERIFY(app.property("serverLost").toBool());
    const QVariantMap problem =
        app.serverProblem(app.globalState()->lastError(), app.globalState()->retryInSeconds());
    QCOMPARE(problem.value(QStringLiteral("headline")).toString(),
             QStringLiteral("fake didn't answer"));
    QCOMPARE(problem.value(QStringLiteral("kind")).toString(), QStringLiteral("down"));

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
    QVERIFY(waitFor([&] { return !app.property("serverLost").toBool(); }));

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
    QCOMPARE(m_bus.server()->calls.first().summary, QStringLiteral("fake didn't answer"));
    QVERIFY(m_bus.server()->calls.first().body.contains(QStringLiteral("omaseed tries again")));

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

namespace {

const QString kMovedHash = QStringLiteral("37a783e4a8f6f3a92c4226d38ba1d69bdbfa6164");

QByteArray movedMaindata()
{
    return QByteArrayLiteral(
        "{\"full_update\":true,\"rid\":1,\"torrents\":{"
        "\"37a783e4a8f6f3a92c4226d38ba1d69bdbfa6164\":"
        "{\"name\":\"log55-move.bin\",\"state\":\"stalledUP\",\"save_path\":\"/d/src\","
        "\"added_on\":1792075500}}}");
}

QJsonArray moveFailedLog()
{
    return {QJsonObject{
        {QStringLiteral("id"), 220},
        {QStringLiteral("timestamp"), 1792075569},
        {QStringLiteral("type"), 4},
        {QStringLiteral("message"),
         QStringLiteral("Failed to move torrent. Torrent: \"log55-move.bin\". Source: \"/d/src\". "
                        "Destination: \"/d/dest\". Reason: \"37a783 storage move failed. "
                        "file_rename (/d/src/log55-move.bin): Permission denied\"")}}};
}

} // namespace

// docs/32 D8 (#55): a move that failed is never a desktop notice; the block stays on the
// torrent. DECISIONS C16 L9: the job tells of it (its band, or its footer line), not the log's
// own D2 note.
void TestAppNotifications::aMoveFailureIsNeverANotice()
{
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);
    app.applyDemoData(QJsonDocument::fromJson(movedMaindata()).object());
    QSignalSpy told(&app, &App::loggedFailure);

    app.engine()->problems()->watchJob({kMovedHash}, QDateTime());
    app.engine()->problems()->applyEntries(moveFailedLog());
    QCOMPARE(told.size(), 0);
    QCOMPARE(app.torrentRow(kMovedHash)
                 .value(QStringLiteral("failure"))
                 .toMap()
                 .value(QStringLiteral("headline"))
                 .toString(),
             QStringLiteral("The move to d/dest didn't happen"));
    QCOMPARE(m_bus.server()->calls.size(), 0);
}

// docs/32 D4: `x` on the block is kept in state.json under the server, and the next omaseed
// reading the same log keeps it hidden.
void TestAppNotifications::aDismissedFailureStaysDismissedAfterARestart()
{
    FakeQbtServer server;
    QVERIFY(server.start());
    server.setMainData({movedMaindata()});
    qputenv("OMASEED_SERVER_URL", server.url().toString().toUtf8());
    qputenv("OMASEED_SERVER_NAME", "fake");

    const auto failureShown = [](App &app) {
        return !app.torrentRow(kMovedHash).value(QStringLiteral("failure")).toMap().isEmpty();
    };
    {
        App app;
        app.start();
        QVERIFY(waitFor([&] { return app.engine()->torrents()->count() == 1; }));
        app.engine()->problems()->applyEntries(moveFailedLog());
        QVERIFY(failureShown(app));
        app.dismissFailure(kMovedHash);
        QVERIFY(!failureShown(app));
        app.engine()->stop();
    }
    const QJsonObject saved = omaseed::core::StateFile::readSection(QStringLiteral("failures"));
    QCOMPARE(saved.value(QStringLiteral("environment"))
                 .toObject()
                 .value(QStringLiteral("dismissed"))
                 .toArray()
                 .size(),
             1);

    App next;
    next.start();
    QVERIFY(waitFor([&] { return next.engine()->torrents()->count() == 1; }));
    next.engine()->problems()->applyEntries(moveFailedLog());
    QVERIFY(!failureShown(next));
    next.engine()->stop();
}

// docs/32 D10 (#56): the log face's levels are remembered per server, in state.json's "log".
void TestAppNotifications::theLogFaceKeepsItsLevelsPerServer()
{
    using omaseed::core::ServerLog;
    FakeQbtServer server;
    QVERIFY(server.start());
    server.setMainData({movedMaindata()});
    qputenv("OMASEED_SERVER_URL", server.url().toString().toUtf8());
    qputenv("OMASEED_SERVER_NAME", "fake");
    const auto savedLevels = []() {
        return omaseed::core::StateFile::readSection(QStringLiteral("log"))
            .value(QStringLiteral("environment"))
            .toObject()
            .value(QStringLiteral("levels"));
    };
    {
        App app;
        app.start();
        QVERIFY(waitFor([&] { return app.engine()->torrents()->count() == 1; }));
        QCOMPARE(app.logLines()->levels(), 15);
        app.logLines()->toggleLevel(ServerLog::Normal);
        app.engine()->stop();
    }
    QCOMPARE(savedLevels().toInt(), 14);

    App next;
    next.start();
    QVERIFY(waitFor([&] { return next.engine()->torrents()->count() == 1; }));
    QCOMPARE(next.logLines()->levels(), 14);
    // All four again is the default: nothing kept.
    next.logLines()->toggleLevel(ServerLog::Normal);
    QVERIFY(savedLevels().isUndefined());
    next.engine()->stop();
}

// docs/32 §6: the demo's log face has lines from qBittorrent's start, its failures among them.
void TestAppNotifications::theDemoHasAWholeLog()
{
    using omaseed::core::ServerLog;
    App app;
    app.loadDemo(16);
    ServerLog *log = app.serverLog();
    QVERIFY(log->total() > 3);
    QVERIFY(log->fromStart());
    QVERIFY(log->levelCounts().at(0) > 0); // normal
    QVERIFY(log->levelCounts().at(3) > 0); // critical
    // Not a level the demo turns off: a demo never writes state.json.
    app.logLines()->toggleLevel(ServerLog::Info);
    QVERIFY(omaseed::core::StateFile::readSection(QStringLiteral("log")).isEmpty());
    app.logLines()->toggleLevel(ServerLog::Info);
}

// #24 T8 (docs/DECISIONS.md C6): a setting, off by default, kept with the other switches.
void TestAppNotifications::theWeeklyRecapIsOffByDefault()
{
    App app;
    QCOMPARE(app.notificationsEnabled().value(QStringLiteral("weekly")).toBool(), false);
    app.setNotificationEnabled(QStringLiteral("weekly"), true);
    QCOMPARE(omaseed::core::StateFile::readSection(QStringLiteral("notifications"))
                 .value(QStringLiteral("weekly"))
                 .toBool(),
             true);
    App second;
    QCOMPARE(second.notificationsEnabled().value(QStringLiteral("weekly")).toBool(), true);
}

void TestAppNotifications::theWeeklyRecapComesOnceASundayEveningAndOpensTheConsole()
{
    using omaseed::core::SeedLedger;
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);
    QDateTime now(QDate(2026, 10, 18), QTime(19, 0)); // a Sunday evening
    app.setRecapClock([&now]() { return now; });

    // Thursday, 5 GiB given; the poll before it a few seconds earlier, so it is no gap.
    const qint64 thursday = QDateTime(QDate(2026, 10, 15), QTime(12, 0)).toSecsSinceEpoch();
    const QString hash = QStringLiteral("a");
    app.ledger()->observe(thursday - 2, 0, 0, {SeedLedger::TorrentTotals{hash, 0, 0, 1}});
    app.ledger()->observe(thursday, 5 * kGiB, 0, {SeedLedger::TorrentTotals{hash, 5 * kGiB, 0, 1}});

    app.checkWeeklyRecap();
    QCOMPARE(m_bus.server()->calls.size(), 0); // off by default

    app.setNotificationEnabled(QStringLiteral("weekly"), true);
    now = QDateTime(QDate(2026, 10, 17), QTime(20, 0)); // Saturday: not yet
    app.checkWeeklyRecap();
    QCOMPARE(m_bus.server()->calls.size(), 0);

    now = QDateTime(QDate(2026, 10, 18), QTime(19, 0));
    app.checkWeeklyRecap();
    QCOMPARE(m_bus.server()->calls.size(), 1);
    const auto &call = m_bus.server()->calls.first();
    QCOMPARE(call.summary, QStringLiteral("Your week"));
    // The server's counters were never polled here, so there is no ratio to print.
    QCOMPARE(call.body,
             QStringLiteral("%1 given this week\nbest day Thursday")
                 .arg(omaseed::core::Format::size(5 * kGiB)));
    QCOMPARE(call.actions,
             QStringList({QStringLiteral("default"), QStringLiteral("Open the console")}));

    // Once a Sunday, and the day it went is kept.
    now = now.addSecs(3600);
    app.checkWeeklyRecap();
    QCOMPARE(m_bus.server()->calls.size(), 1);
    QCOMPARE(omaseed::core::StateFile::readSection(QStringLiteral("notifications"))
                 .value(QStringLiteral("weeklyRecapSent"))
                 .toString(),
             QStringLiteral("2026-10-18"));

    // Its button (or a click on it) shows the console on 7d.
    QSignalSpy shown(&app, &App::showConsoleRequested);
    Q_EMIT notifier.actionInvoked(QStringLiteral("recap"), QString(), QStringLiteral("console"));
    QCOMPARE(shown.size(), 1);
    QCOMPARE(shown.first().first().toString(), QStringLiteral("7d"));

    // A week that gave nothing sends nothing.
    now = now.addDays(7);
    app.checkWeeklyRecap();
    QCOMPARE(m_bus.server()->calls.size(), 1);
}

QTEST_GUILESS_MAIN(TestAppNotifications)

#include "tst_app_notifications.moc"
