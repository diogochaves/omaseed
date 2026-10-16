// SyncEngine against recorded sync/maindata sequences.
//
// QbtClient belongs to another task and must not grow a test seam, so the engine is driven
// the way tst_qbtclient_fake.cpp drives the client: a small HTTP server of our own replays
// tests/fixtures/maindata/*.json in order, one file per poll, and records the rid it was
// asked for.

#include "QbtFeed.hpp"
#include "core/ApiError.hpp"
#include "core/CategoryModel.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/QbtClient.hpp"
#include "core/SyncEngine.hpp"
#include "core/TagModel.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <memory>

using namespace omaseed::core;

namespace {

const QString kFixtureDir = QStringLiteral(OMASEED_FIXTURES_DIR "/maindata/");

QByteArray fixtureBytes(const QString &name)
{
    QFile file(kFixtureDir + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
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

/// Speaks the three endpoints the engine uses and replays a scripted list of maindata bodies.
class FixtureServer : public QTcpServer
{
public:
    explicit FixtureServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {}

    bool start(quint16 port = 0) { return listen(QHostAddress::LocalHost, port); }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

    /// One body per poll; the last one repeats forever.
    void setMainData(const QList<QByteArray> &bodies) { m_bodies = bodies; }
    void setStatus(int status) { m_status = status; }
    void setDelayMs(int delay) { m_delayMs = delay; }

    QList<int> seenRids;
    int mainDataRequests = 0;
    int maxConcurrentMainData = 0;

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
            const QByteArray target =
                buffer->left(headerEnd).split('\n').value(0).split(' ').value(1);
            buffer->clear();

            QByteArray response;
            bool isMainData = false;
            if (target.startsWith("/api/v2/app/version")) {
                response = httpResponse(200, "OK", "v5.2.3");
            } else if (target.startsWith("/api/v2/app/webapiVersion")) {
                response = httpResponse(200, "OK", "2.15.1");
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                isMainData = true;
                ++m_inFlight;
                maxConcurrentMainData = std::max(maxConcurrentMainData, m_inFlight);
                const qsizetype at = target.indexOf("rid=");
                seenRids.append(at < 0 ? -1 : target.mid(at + 4).toInt());
                const int index = mainDataRequests++;
                if (m_status != 200) {
                    response = httpResponse(m_status, "Forbidden", "Forbidden");
                } else {
                    const QByteArray body =
                        m_bodies.isEmpty()
                            ? QByteArray("{}")
                            : m_bodies.at(std::min(qsizetype(index), m_bodies.size() - 1));
                    response = httpResponse(200, "OK", body);
                }
            } else {
                response = httpResponse(404, "Not Found", "no");
            }

            const auto answer = [this, socket, response, isMainData]() {
                if (isMainData)
                    --m_inFlight;
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            };
            if (m_delayMs > 0)
                QTimer::singleShot(m_delayMs, socket, answer);
            else
                answer();
        });
    }

private:
    QList<QByteArray> m_bodies;
    int m_status = 200;
    int m_delayMs = 0;
    int m_inFlight = 0;
};

/// Runs the event loop until `predicate` holds or `timeoutMs` passes.
bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

/// Lets the event loop run for a while without asserting anything.
void spin(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

} // namespace

class TestSyncEngineFixtures : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void fullUpdateFillsEveryModel();
    void partialSequenceAppliesDiffs();
    void ridStartsAtZeroAndFollowsTheServer();
    void onlyOnePollInFlight();
    void cadenceFollowsVisibility();
    void completionIsAnnouncedOnceAndNeverOnTheFirstFullUpdate();
    void aCompletionSeenOnlyInALaterFullUpdateIsAnnounced();
    void aCompletionAfterARestartIsAnnounced();
    void aStalePollIsNotAppliedAfterAServerSwitch();
    void aTorrentAddedAndFinishedBetweenPollsIsAnnounced();
    void trackerErrorFlagReachesTheRowWithoutANotice();
    void diskAlmostFullFiresOncePerCrossing();
    void serverLostOnceThenBackWithAFullUpdate();
    void forbiddenStopsThePollLoop();
    void applyDemoDataFillsTheModelsWithNoClient();

private:
    QByteArray m_full;
    QByteArray m_tags;
    QByteArray m_removedCategoryAndTag;
    QByteArray m_removedTorrent;
    QByteArray m_speed;
    QByteArray m_completion;
    QByteArray m_trackerError;
};

void TestSyncEngineFixtures::initTestCase()
{
    qRegisterMetaType<ApiError>("omaseed::core::ApiError");

    m_full = fixtureBytes(QStringLiteral("01-full.json"));
    m_tags = fixtureBytes(QStringLiteral("02-partial-tags.json"));
    m_removedCategoryAndTag =
        fixtureBytes(QStringLiteral("03-partial-categories-tags-removed.json"));
    m_removedTorrent = fixtureBytes(QStringLiteral("04-partial-removed.json"));
    m_speed = fixtureBytes(QStringLiteral("05-partial-speed.json"));
    m_completion = fixtureBytes(QStringLiteral("06-partial-completion.json"));
    m_trackerError = fixtureBytes(QStringLiteral("07-partial-tracker-error.json"));

    QVERIFY2(!m_full.isEmpty() && !m_tags.isEmpty() && !m_removedTorrent.isEmpty() &&
                 !m_completion.isEmpty() && !m_trackerError.isEmpty(),
             "tests/fixtures/maindata is incomplete");
}

void TestSyncEngineFixtures::fullUpdateFillsEveryModel()
{
    FixtureServer server;
    QVERIFY(server.start());
    server.setMainData({m_full});

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10000),
                        std::chrono::milliseconds(10000),
                        std::chrono::milliseconds(10000),
                        std::chrono::milliseconds(10000));

    QSignalSpy firstSync(&engine, &SyncEngine::firstSyncFinished);
    engine.start();
    QVERIFY(waitFor([&] { return firstSync.count() == 1; }));

    QCOMPARE(engine.torrents()->count(), 4);
    QCOMPARE(engine.categories()->names(),
             QStringList({QStringLiteral("fixtures"), QStringLiteral("linux")}));
    QCOMPARE(engine.tags()->tags(),
             QStringList({QStringLiteral("keep"), QStringLiteral("private")}));
    QCOMPARE(engine.trackers()->hosts(),
             QStringList({QStringLiteral("tracker.archlinux.org"),
                          QStringLiteral("tracker.opentrackr.org"),
                          QStringLiteral("trackerless")}));
    QCOMPARE(engine.trackers()->hashesFor(QStringLiteral("trackerless")).size(), 3);
    // Every torrent of the recorded 5.2.3 maindata carries `share_limit_action` (T5.5 Q14).
    for (const QString &hash : engine.torrents()->hashes())
        QCOMPARE(engine.torrents()->torrent(hash).shareLimitAction, QStringLiteral("Default"));
    QCOMPARE(engine.counts()->total(), 4);
    QCOMPARE(engine.globalState()->serverVersion(), QStringLiteral("v5.2.3"));
    QCOMPARE(engine.globalState()->apiVersion(), QStringLiteral("2.15.1"));
    QCOMPARE(engine.globalState()->connectionStatus(), QStringLiteral("firewalled"));
    QCOMPARE(engine.globalState()->connected(), true);
    QCOMPARE(engine.globalState()->syncing(), false);
    QVERIFY(engine.globalState()->lastSyncTime().isValid());
    QCOMPARE(engine.rid(), 1);

    engine.stop();
}

void TestSyncEngineFixtures::partialSequenceAppliesDiffs()
{
    FixtureServer server;
    QVERIFY(server.start());
    server.setMainData({m_full, m_tags, m_removedCategoryAndTag, m_removedTorrent});

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10));

    engine.start();
    QVERIFY(waitFor([&] { return server.mainDataRequests >= 4 && engine.rid() == 4; }));
    engine.stop();

    // 02 added the tag 4k and gave b1c2… the tag private; 03 deleted 4k and the category
    // linux; 04 deleted the torrent c0ffee…
    QCOMPARE(engine.tags()->tags(),
             QStringList({QStringLiteral("keep"), QStringLiteral("private")}));
    QCOMPARE(engine.categories()->names(), QStringList({QStringLiteral("fixtures")}));
    QCOMPARE(engine.torrents()->count(), 3);
    QCOMPARE(
        engine.torrents()->indexOfHash(QStringLiteral("c0ffee00c0ffee00c0ffee00c0ffee00c0ffee00")),
        -1);
    QCOMPARE(
        engine.torrents()->torrent(QStringLiteral("b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0")).tags,
        QStringList({QStringLiteral("private")}));
    // 03 cleared d1d1…'s category when `linux` went away.
    QCOMPARE(engine.torrents()
                 ->torrent(QStringLiteral("d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1"))
                 .category,
             QString());
    QCOMPARE(engine.counts()->total(), 3);
}

void TestSyncEngineFixtures::ridStartsAtZeroAndFollowsTheServer()
{
    FixtureServer server;
    QVERIFY(server.start());
    server.setMainData({m_full, m_tags, m_removedCategoryAndTag, m_removedTorrent});

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10));

    engine.start();
    QVERIFY(waitFor([&] { return server.seenRids.size() >= 4; }));
    engine.stop();

    QCOMPARE(server.seenRids.mid(0, 4), QList<int>({0, 1, 2, 3}));
}

void TestSyncEngineFixtures::onlyOnePollInFlight()
{
    FixtureServer server;
    QVERIFY(server.start());
    server.setMainData({m_full, m_speed});
    server.setDelayMs(40);

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(1),
                        std::chrono::milliseconds(1),
                        std::chrono::milliseconds(1),
                        std::chrono::milliseconds(1));

    engine.start();
    QVERIFY(waitFor([&] { return server.mainDataRequests >= 4; }));
    engine.stop();

    QCOMPARE(server.maxConcurrentMainData, 1);
}

void TestSyncEngineFixtures::cadenceFollowsVisibility()
{
    FixtureServer server;
    QVERIFY(server.start());
    server.setMainData({m_full, m_speed});

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(20),  // visible
                        std::chrono::milliseconds(400), // hidden
                        std::chrono::milliseconds(20),
                        std::chrono::milliseconds(20));

    engine.start();
    QVERIFY(waitFor([&] { return server.mainDataRequests >= 5; }, 3000));

    engine.setVisible(false);
    spin(100); // let the in-flight poll settle
    const int afterHiding = server.mainDataRequests;
    spin(250); // less than one hidden interval
    QVERIFY2(server.mainDataRequests - afterHiding <= 1,
             "a hidden window must not keep polling at the visible rate");

    // Showing the window again polls at once instead of waiting out the long interval.
    engine.setVisible(true);
    QVERIFY(waitFor([&] { return server.mainDataRequests > afterHiding + 1; }, 1000));
    engine.stop();
}

void TestSyncEngineFixtures::completionIsAnnouncedOnceAndNeverOnTheFirstFullUpdate()
{
    FixtureServer server;
    QVERIFY(server.start());
    // full, completion, then the same completion again (the engine must stay quiet), then a
    // fresh full update where the torrent is incomplete again (nothing to announce).
    server.setMainData({m_full, m_completion, m_completion, m_full});

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10));

    QSignalSpy completed(&engine, &SyncEngine::torrentCompleted);
    engine.start();
    QVERIFY(waitFor([&] { return server.mainDataRequests >= 5; }));
    engine.stop();

    QCOMPARE(completed.count(), 1);
    const QList<QVariant> args = completed.first();
    QCOMPARE(args.at(0).toString(), QStringLiteral("4a4e3b1d2d0c1a8d1e6a9f0b3c2d4e5f6a7b8c9d"));
    QCOMPARE(args.at(1).toString(), QStringLiteral("fixture-one"));
    QCOMPARE(args.at(2).toLongLong(), 1404304589);
    QCOMPARE(args.at(3).toString(), QStringLiteral("fixtures"));
    QVERIFY(args.at(4).toString().endsWith(QStringLiteral("fixture-one")));
}

// NET-6: a lost server (or one that forgot our rid) answers with a full update. A torrent
// that finished during the gap must still be announced; only the very first full update
// is silent.
void TestSyncEngineFixtures::aCompletionSeenOnlyInALaterFullUpdateIsAnnounced()
{
    const QString hash = QStringLiteral("4a4e3b1d2d0c1a8d1e6a9f0b3c2d4e5f6a7b8c9d");
    QJsonObject later = QJsonDocument::fromJson(m_full).object();
    QJsonObject torrents = later.value(QStringLiteral("torrents")).toObject();
    QJsonObject row = torrents.value(hash).toObject();
    row.insert(QStringLiteral("progress"), 1);
    row.insert(QStringLiteral("completion_on"), 1789834338);
    row.insert(QStringLiteral("downloaded"), 1412693197);
    row.insert(QStringLiteral("state"), QStringLiteral("uploading"));
    torrents.insert(hash, row);
    later.insert(QStringLiteral("torrents"), torrents);
    later.insert(QStringLiteral("rid"), 9);

    FixtureServer server;
    QVERIFY(server.start());
    server.setMainData({m_full, QJsonDocument(later).toJson(QJsonDocument::Compact)});

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10));

    QSignalSpy completed(&engine, &SyncEngine::torrentCompleted);
    engine.start();
    QVERIFY(waitFor([&] { return server.mainDataRequests >= 4; }));
    engine.stop();

    QCOMPARE(completed.count(), 1); // once, not once per repeated full update
    QCOMPARE(completed.first().at(0).toString(), hash);
}

// REG-5: stop() and start() again (App::retryNow after authRequired) keep the rows, so the
// full update that follows still announces what finished while the engine was stopped.
void TestSyncEngineFixtures::aCompletionAfterARestartIsAnnounced()
{
    const QString hash = QStringLiteral("4a4e3b1d2d0c1a8d1e6a9f0b3c2d4e5f6a7b8c9d");
    QJsonObject later = QJsonDocument::fromJson(m_full).object();
    QJsonObject torrents = later.value(QStringLiteral("torrents")).toObject();
    QJsonObject row = torrents.value(hash).toObject();
    row.insert(QStringLiteral("progress"), 1);
    row.insert(QStringLiteral("completion_on"), 1789834338);
    row.insert(QStringLiteral("downloaded"), 1412693197);
    row.insert(QStringLiteral("state"), QStringLiteral("uploading"));
    torrents.insert(hash, row);
    later.insert(QStringLiteral("torrents"), torrents);
    later.insert(QStringLiteral("rid"), 9);

    FixtureServer server;
    QVERIFY(server.start());
    server.setMainData({m_full});

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10000),
                        std::chrono::milliseconds(10000),
                        std::chrono::milliseconds(10000),
                        std::chrono::milliseconds(10000));
    QSignalSpy completed(&engine, &SyncEngine::torrentCompleted);
    QSignalSpy firstSync(&engine, &SyncEngine::firstSyncFinished);

    engine.start();
    QVERIFY(firstSync.wait(5000));
    engine.stop();
    QCOMPARE(completed.count(), 0);

    // The download finishes while the engine is stopped; the restart polls from rid 0.
    server.setMainData({QJsonDocument(later).toJson(QJsonDocument::Compact)});
    const int before = server.mainDataRequests;
    engine.start();
    QVERIFY(waitFor([&] { return server.mainDataRequests > before && engine.rid() == 9; }));
    engine.stop();

    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.first().at(0).toString(), hash);
}

// NET-4 / APP-1: App switches servers on one client with stop(), setClient(nullptr),
// setClient(client), then a new base URL and start(). The old server's poll, still in
// flight, must never land in the new server's models.
void TestSyncEngineFixtures::aStalePollIsNotAppliedAfterAServerSwitch()
{
    FixtureServer oldServer;
    QVERIFY(oldServer.start());
    oldServer.setMainData({m_full});
    oldServer.setDelayMs(300);
    FixtureServer newServer;
    QVERIFY(newServer.start());
    newServer.setMainData(
        {QByteArrayLiteral(R"({"rid":7,"full_update":true,"torrents":{},"server_state":{}})")});

    QbtClient client;
    client.setBaseUrl(oldServer.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10000),
                        std::chrono::milliseconds(10000),
                        std::chrono::milliseconds(10000),
                        std::chrono::milliseconds(10000));
    QSignalSpy firstSync(&engine, &SyncEngine::firstSyncFinished);

    engine.start();
    QVERIFY(waitFor([&] { return oldServer.mainDataRequests == 1; }));

    engine.stop();
    engine.setClient(nullptr);
    engine.setClient(&client);
    client.clearAuth();
    client.setBaseUrl(newServer.url());
    engine.start();

    QVERIFY(waitFor([&] { return firstSync.count() == 1; }));
    spin(500); // past the old server's answer
    QCOMPARE(firstSync.count(), 1);
    QCOMPARE(engine.torrents()->count(), 0);
    QCOMPARE(engine.rid(), 7);
    QCOMPARE(newServer.seenRids, QList<int>({0}));
    engine.stop();
}

void TestSyncEngineFixtures::aTorrentAddedAndFinishedBetweenPollsIsAnnounced()
{
    // Three torrents the engine first meets in a partial update: one downloaded and already
    // done (a 4 KB torrent finishes inside one poll), one done with nothing downloaded (added
    // over data already on disk), one still downloading. Only the first is a completion.
    const auto added = [](const QString &hash, double progress, qint64 downloaded) {
        return QJsonObject{
            {QStringLiteral("name"), hash.left(4)},
            {QStringLiteral("state"),
             progress >= 1.0 ? QStringLiteral("uploading") : QStringLiteral("downloading")},
            {QStringLiteral("progress"), progress},
            {QStringLiteral("downloaded"), downloaded},
            {QStringLiteral("size"), 4096}};
    };
    const QJsonObject partial{
        {QStringLiteral("rid"), 2},
        {QStringLiteral("torrents"),
         QJsonObject{
             {QStringLiteral("aaaa000000000000000000000000000000000001"),
              added(QStringLiteral("aaaa000000000000000000000000000000000001"), 1.0, 4096)},
             {QStringLiteral("bbbb000000000000000000000000000000000002"),
              added(QStringLiteral("bbbb000000000000000000000000000000000002"), 1.0, 0)},
             {QStringLiteral("cccc000000000000000000000000000000000003"),
              added(QStringLiteral("cccc000000000000000000000000000000000003"), 0.5, 2048)}}}};

    FixtureServer server;
    QVERIFY(server.start());
    server.setMainData({m_full, QJsonDocument(partial).toJson(QJsonDocument::Compact)});

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10));

    QSignalSpy completed(&engine, &SyncEngine::torrentCompleted);
    engine.start();
    QVERIFY(waitFor([&] { return server.mainDataRequests >= 3; }));
    engine.stop();

    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.first().at(0).toString(),
             QStringLiteral("aaaa000000000000000000000000000000000001"));
}

// Q16 dropped the tracker-error notice: a torrent losing its trackers is a row flag (the
// Trackers tab badge and the rail filter read it), not an event the engine announces.
void TestSyncEngineFixtures::trackerErrorFlagReachesTheRowWithoutANotice()
{
    FixtureServer server;
    QVERIFY(server.start());
    server.setMainData({m_full, m_trackerError, m_trackerError});

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10));

    const QString hash = QStringLiteral("d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1");
    engine.start();
    QVERIFY(waitFor([&] { return engine.torrents()->count() == 4; }));
    QVERIFY(!engine.torrents()->torrent(hash).hasTrackerError);
    QVERIFY(waitFor([&] { return server.mainDataRequests >= 4; }));
    engine.stop();

    QVERIFY(engine.torrents()->torrent(hash).hasTrackerError);
    QCOMPARE(engine.torrents()->torrent(hash).tracker, QString());
}

void TestSyncEngineFixtures::diskAlmostFullFiresOncePerCrossing()
{
    const QByteArray low =
        QByteArrayLiteral(R"({"rid":9,"server_state":{"free_space_on_disk":536870912}})");
    const QByteArray plenty =
        QByteArrayLiteral(R"({"rid":10,"server_state":{"free_space_on_disk":723595538432}})");

    FixtureServer server;
    QVERIFY(server.start());
    server.setMainData({m_full, low, low, plenty, low});

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10));

    QSignalSpy lowDisk(&engine, &SyncEngine::diskAlmostFull);
    engine.start();
    QVERIFY(waitFor([&] { return server.mainDataRequests >= 6; }));
    engine.stop();

    QCOMPARE(lowDisk.count(), 2); // once per crossing, not once per poll
    QCOMPARE(lowDisk.first().at(0).toLongLong(), 536870912);
}

void TestSyncEngineFixtures::serverLostOnceThenBackWithAFullUpdate()
{
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        port = probe.serverPort();
    } // nothing listens there now

    QbtClient client;
    client.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port)));
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(30));

    QSignalSpy lost(&engine, &SyncEngine::serverLost);
    QSignalSpy back(&engine, &SyncEngine::serverBack);
    engine.start();

    QVERIFY(waitFor([&] { return lost.count() == 1; }));
    spin(200);                 // several failed retries
    QCOMPARE(lost.count(), 1); // reported once, not once per poll
    QCOMPARE(engine.globalState()->connected(), false);
    QVERIFY(!engine.globalState()->lastError().isEmpty());
    QCOMPARE(engine.rid(), 0); // a returning server must be asked for everything

    FixtureServer server;
    QVERIFY(server.start(port));
    server.setMainData({m_full});

    QVERIFY(waitFor([&] { return back.count() == 1; }));
    QCOMPARE(engine.torrents()->count(), 4);
    QCOMPARE(engine.globalState()->connected(), true);
    // NET-7: the probe failed at start; it is retried, so the versions are known now.
    QCOMPARE(engine.globalState()->serverVersion(), QStringLiteral("v5.2.3"));
    QCOMPARE(engine.globalState()->apiVersion(), QStringLiteral("2.15.1"));
    QCOMPARE(server.seenRids.first(), 0);
    engine.stop();
}

void TestSyncEngineFixtures::forbiddenStopsThePollLoop()
{
    FixtureServer server;
    QVERIFY(server.start());
    server.setStatus(403);

    QbtClient client;
    client.setBaseUrl(server.url());
    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10),
                        std::chrono::milliseconds(10));

    QSignalSpy auth(&engine, &SyncEngine::authRequired);
    engine.start();
    QVERIFY(waitFor([&] { return auth.count() == 1; }));

    const int after = server.mainDataRequests;
    spin(150);
    QCOMPARE(server.mainDataRequests, after); // stopped, not backing off
    QCOMPARE(engine.isRunning(), false);
    QCOMPARE(engine.globalState()->connected(), false);
    QCOMPARE(auth.first().at(0).value<ApiError>().kind(), ApiError::Kind::Forbidden);
}

void TestSyncEngineFixtures::applyDemoDataFillsTheModelsWithNoClient()
{
    // `--demo table` has no server at all: applyDemoData() must do everything a poll does to
    // the models, and nothing the poll loop does (no client, no timer, never running).
    SyncEngine engine;
    QSignalSpy firstSync(&engine, &SyncEngine::firstSyncFinished);
    QSignalSpy synced(&engine, &SyncEngine::synced);

    qbtfeed::demo(engine, QJsonDocument::fromJson(m_full).object());

    QVERIFY(!engine.isRunning());
    QCOMPARE(engine.client(), nullptr);
    QCOMPARE(firstSync.count(), 1);
    QCOMPARE(synced.count(), 1);
    QCOMPARE(synced.first().at(0).toBool(), true);
    QCOMPARE(engine.torrents()->count(), 4);
    QCOMPARE(engine.counts()->total(), 4);
    QCOMPARE(engine.categories()->names(),
             QStringList({QStringLiteral("fixtures"), QStringLiteral("linux")}));
    QVERIFY(engine.globalState()->freeSpaceOnDisk() > 0);
}

QTEST_MAIN(TestSyncEngineFixtures)
#include "tst_syncengine_fixtures.moc"
