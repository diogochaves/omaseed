// EngineClient::pointAt (#294, DECISIONS C50 GT2): re-point a torrent at a folder that already
// holds its files, moving nothing. What each engine sends, against the fakes: qBittorrent's
// torrents/setLocation, Transmission's torrent_set_location with `move: false` (answered at once,
// never counted as a move in flight), Deluge's core.move_storage; none for no torrents; and the
// default for an engine that can't. That each call really moves nothing is proved on the
// playgrounds (scripts/dev/research/294-locate, tst_pointat_live).

#include "FakeDeluge.hpp"
#include "FakeTransmission.hpp"
#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/deluge/DelugeRpc.hpp"
#include "core/engines/qbt/QbtEngine.hpp"
#include "core/engines/transmission/TransmissionEngine.hpp"
#include "core/engines/transmission/TransmissionRpc.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>
#include <QUrlQuery>

using namespace fakehttp;
using namespace omaseed::core;

namespace {

const QString kOne = QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91");
const QString kTwo = QStringLiteral("2b86112ae20c6ca831cbfb3e4333c8acc6d21088");
const QString kFolder = QStringLiteral("/srv/found here");

QString field(const Recorded &request, const char *name)
{
    return QUrlQuery(QString::fromUtf8(request.body))
        .queryItemValue(QString::fromLatin1(name), QUrl::FullyDecoded);
}

} // namespace

class TestPointAt : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void qbittorrentSetsTheLocation();
    void transmissionSetsTheFolderWithoutMoving();
    void transmissionNoTorrentsNeverReachTheWire();
    void delugeMovesStorageOverTheFiles();
    void delugeNoTorrentsNeverReachTheWire();
};

void TestPointAt::qbittorrentSetsTheLocation()
{
    FakeServer server;
    QbtEngine engine;
    engine.setBaseUrl(server.url());
    QVERIFY(!await(this, engine.pointAt({kOne, kTwo}, kFolder)).isError());
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/setLocation"));
    QCOMPARE(field(server.requests.at(0), "hashes"), kOne + QLatin1Char('|') + kTwo);
    QCOMPARE(field(server.requests.at(0), "location"), kFolder);
}

void TestPointAt::transmissionSetsTheFolderWithoutMoving()
{
    FakeServer server;
    faketransmission::FakeTransmission daemon;
    server.setHandler(daemon.handler());
    TransmissionEngine engine;
    engine.setBaseUrl(server.url());
    engine.setTimeout(std::chrono::seconds(5));
    Reply<Done> *reply = engine.pointAt({kOne}, kFolder);
    // Not a move: nothing holds the poll while it is on its way (K22 is the copy's).
    QVERIFY(engine.busyMoving().isEmpty());
    QVERIFY(!await(this, reply).isError());
    QCOMPARE(daemon.methods(), QStringList({QStringLiteral("torrent_set_location")}));
    const QJsonObject params = daemon.last().params;
    QCOMPARE(params.value(QStringLiteral("ids")).toArray(), QJsonArray({kOne}));
    QCOMPARE(params.value(QStringLiteral("location")).toString(), kFolder);
    QCOMPARE(params.value(QStringLiteral("move")), QJsonValue(false));
}

void TestPointAt::transmissionNoTorrentsNeverReachTheWire()
{
    // An empty `ids` is every torrent to Transmission.
    FakeServer server;
    faketransmission::FakeTransmission daemon;
    server.setHandler(daemon.handler());
    TransmissionEngine engine;
    engine.setBaseUrl(server.url());
    QVERIFY(!await(this, engine.pointAt({}, kFolder)).isError());
    QCOMPARE(server.requests.size(), 0);
}

void TestPointAt::delugeMovesStorageOverTheFiles()
{
    FakeServer server;
    fakedeluge::FakeDeluge web;
    fakedeluge::RecordedDeluge recorded;
    web.answer = [&recorded](const QString &method, const QJsonArray &params) {
        return recorded.answer(method, params);
    };
    server.setHandler(web.handler());
    DelugeEngine engine;
    engine.setBaseUrl(server.url());
    engine.setCredentials(QString(), web.password);
    engine.setTimeout(std::chrono::seconds(5));
    QVERIFY(await(this, engine.probe()).isValid());
    web.calls.clear();
    QVERIFY(!await(this, engine.pointAt({kOne, kTwo}, kFolder)).isError());
    QCOMPARE(web.methods(), QStringList({QStringLiteral("core.move_storage")}));
    QCOMPARE(web.calls.at(0).params, QJsonArray({QJsonArray({kOne, kTwo}), kFolder}));
}

void TestPointAt::delugeNoTorrentsNeverReachTheWire()
{
    FakeServer server;
    fakedeluge::FakeDeluge web;
    server.setHandler(web.handler());
    DelugeEngine engine;
    engine.setBaseUrl(server.url());
    QVERIFY(!await(this, engine.pointAt({}, kFolder)).isError());
    QCOMPARE(server.requests.size(), 0);
}

QTEST_GUILESS_MAIN(TestPointAt)
#include "tst_pointat.moc"
