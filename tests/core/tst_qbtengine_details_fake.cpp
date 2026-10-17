// The GETs the details pane lives on, through the qBittorrent engine (#82), against a minimal
// HTTP/1.1 server of our own: the exact query it sends and the typed value it hands back.
// `sync/torrentPeers` is the one that matters most here, because the playground never has a peer to
// answer with (tst_qbtclient_live only proves it answers at all).

#include "QbtFeed.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentModel.hpp"
#include "core/engines/qbt/QbtEngine.hpp"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <memory>

using omaseed::core::ApiError;
using omaseed::core::ContentFile;
using omaseed::core::Done;
using omaseed::core::FilePriority;
using omaseed::core::PeersDelta;
using omaseed::core::QbtEngine;
using omaseed::core::Reply;
using omaseed::core::Result;
using omaseed::core::TorrentDetails;
using omaseed::core::TorrentModel;
using omaseed::core::TorrentProperties;
using omaseed::core::TrackerEntry;
using omaseed::core::TrackerSource;

namespace {

QByteArray jsonResponse(const QByteArray &body)
{
    QByteArray response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

QByteArray notFoundResponse()
{
    return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
}

/// Speaks just enough HTTP to answer QNetworkAccessManager, and remembers the targets.
class FakeServer : public QTcpServer
{
public:
    using Handler = std::function<QByteArray(const QByteArray &target)>;

    explicit FakeServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {
        [this]() { QVERIFY(listen(QHostAddress::LocalHost)); }();
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
    void setHandler(Handler handler) { m_handler = std::move(handler); }

    QList<QByteArray> targets;

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

            const QByteArray requestLine = buffer->left(buffer->indexOf('\n')).trimmed();
            const QByteArray target = requestLine.split(' ').value(1);
            targets.append(target);
            buffer->clear();

            socket->write(m_handler ? m_handler(target) : jsonResponse("{}"));
            socket->flush();
            socket->disconnectFromHost();
        });
    }

private:
    Handler m_handler;
};

template<class T>
Result<T> await(QObject *context, Reply<T> *reply, int timeoutMs = 10000)
{
    Result<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("test never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const Result<T> &finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return result;
}

} // namespace

class TestQbtEngineDetailsFake : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void syncTorrentPeersSendsHashAndRid();
    void torrentPeersParsesTheDiffShape();
    void filesTrackersAndPropertiesSendTheHash();
    void filePrioPostsTheIdList();
    void pieceStatesSendsTheHashAndParsesTheArray();
    void aDeletedTorrentStopsThePollQuietly();
    void aHashTheModelNoLongerHoldsIsNotAsked();
    void webSeedsSendsTheHashAndParsesTheList();
    void webSeedsAreAskedOnSelectionThenOneTickInTen();
};

void TestQbtEngineDetailsFake::syncTorrentPeersSendsHashAndRid()
{
    FakeServer server;
    server.setHandler([](const QByteArray &) {
        return jsonResponse(R"({"full_update":true,"rid":3,"peers":{}})");
    });

    QbtEngine client;
    client.setBaseUrl(server.url());
    const Result<PeersDelta> result = await(this, client.peers(QStringLiteral("abc123"), 12));

    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.targets.size(), 1);

    const QUrlQuery query(QUrl(QString::fromLatin1(server.targets.first())).query());
    QVERIFY(server.targets.first().startsWith("/api/v2/sync/torrentPeers?"));
    QCOMPARE(query.queryItemValue(QStringLiteral("hash")), QStringLiteral("abc123"));
    QCOMPARE(query.queryItemValue(QStringLiteral("rid")), QStringLiteral("12"));
    QCOMPARE(result.value().cursor, std::optional<int>(3));
    QVERIFY(result.value().full);
}

void TestQbtEngineDetailsFake::torrentPeersParsesTheDiffShape()
{
    FakeServer server;
    server.setHandler([](const QByteArray &) {
        return jsonResponse(R"({"rid":4,"peers":{"1.2.3.4:6881":{"client":"qBittorrent 5.2.3",)"
                            R"("dl_speed":1069547}},"peers_removed":["5.6.7.8:6881"]})");
    });

    QbtEngine client;
    client.setBaseUrl(server.url());
    const Result<PeersDelta> result = await(this, client.peers(QStringLiteral("abc123"), 3));

    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    const PeersDelta &answer = result.value();
    QCOMPARE(answer.cursor, std::optional<int>(4));
    QVERIFY(!answer.full);
    QCOMPARE(answer.removed, QStringList{QStringLiteral("5.6.7.8:6881")});
    QCOMPARE(answer.peers.size(), 1);
    QCOMPARE(answer.peers.at(0).address, QStringLiteral("1.2.3.4:6881"));
    QCOMPARE(answer.peers.at(0).dlSpeed, std::optional<qint64>(1069547));
}

void TestQbtEngineDetailsFake::filesTrackersAndPropertiesSendTheHash()
{
    FakeServer server;
    server.setHandler([](const QByteArray &target) {
        if (target.contains("torrents/properties"))
            return jsonResponse(R"({"name":"Fixture","total_size":4226})");
        if (target.contains("torrents/files"))
            return jsonResponse(R"([{"index":0,"name":"a/b.bin","size":10,"progress":1}])");
        return jsonResponse(R"([{"url":"** [DHT] **","status":2}])");
    });

    QbtEngine client;
    client.setBaseUrl(server.url());

    const Result<TorrentProperties> properties =
        await(this, client.properties(QStringLiteral("abc123")));
    QVERIFY2(!properties.isError(), qPrintable(properties.error().toString()));
    QCOMPARE(properties.value().name, QStringLiteral("Fixture"));
    QCOMPARE(properties.value().totalSize, 4226);

    const Result<QList<ContentFile>> files = await(this, client.files(QStringLiteral("abc123")));
    QVERIFY2(!files.isError(), qPrintable(files.error().toString()));
    QCOMPARE(files.value().size(), 1);
    QCOMPARE(files.value().at(0).path, QStringLiteral("a/b.bin"));

    const Result<QList<TrackerEntry>> trackers =
        await(this, client.trackers(QStringLiteral("abc123")));
    QVERIFY2(!trackers.isError(), qPrintable(trackers.error().toString()));
    QCOMPARE(trackers.value().size(), 1);
    QCOMPARE(trackers.value().at(0).source, TrackerSource::Dht);

    QCOMPARE(server.targets.size(), 3);
    for (const QByteArray &target : server.targets) {
        const QUrlQuery query(QUrl(QString::fromLatin1(target)).query());
        QCOMPARE(query.queryItemValue(QStringLiteral("hash")), QStringLiteral("abc123"));
    }
}

void TestQbtEngineDetailsFake::pieceStatesSendsTheHashAndParsesTheArray()
{
    // docs/12 G3's bar: one number per piece.
    FakeServer server;
    server.setHandler([](const QByteArray &) { return jsonResponse("[2,2,1,0]"); });

    QbtEngine client;
    client.setBaseUrl(server.url());

    const Result<QByteArray> states = await(this, client.pieceStates(QStringLiteral("abc123")));
    QVERIFY2(!states.isError(), qPrintable(states.error().toString()));
    QCOMPARE(states.value().size(), 4);
    QCOMPARE(int(states.value().at(2)), 1);

    QCOMPARE(server.targets.size(), 1);
    const QUrl target(QString::fromLatin1(server.targets.constFirst()));
    QVERIFY(target.path().endsWith(QStringLiteral("/torrents/pieceStates")));
    QCOMPARE(QUrlQuery(target.query()).queryItemValue(QStringLiteral("hash")),
             QStringLiteral("abc123"));
}

void TestQbtEngineDetailsFake::filePrioPostsTheIdList()
{
    // The one action the details pane needs in Phase 3 (spec §2 Fig. 2's `+ - x`).
    FakeServer server;
    server.setHandler([](const QByteArray &) { return jsonResponse("ok"); });

    QbtEngine client;
    client.setBaseUrl(server.url());
    const Result<Done> result =
        await(this, client.setFilePriority(QStringLiteral("abc123"), {2, 3}, FilePriority::Skip));

    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.targets.size(), 1);
    QVERIFY(server.targets.first().endsWith("/api/v2/torrents/filePrio"));
}

QTEST_MAIN(TestQbtEngineDetailsFake)
// Review RUN-10: after `D` the pane kept asking about the deleted torrent (four 404s a tick)
// and put the 404 up as its error line.
void TestQbtEngineDetailsFake::aDeletedTorrentStopsThePollQuietly()
{
    FakeServer server;
    server.setHandler([](const QByteArray &) { return notFoundResponse(); });
    QbtEngine client;
    client.setBaseUrl(server.url());

    TorrentDetails details;
    details.setPollInterval(30);
    details.setClient(&client);
    details.setHash(QStringLiteral("deleted"));

    QTRY_VERIFY(server.targets.size() >= 3);
    QTest::qWait(250); // several poll intervals
    const qsizetype asked = server.targets.size();
    // One poll's worth (properties, files, trackers, webseeds, pieceStates), then nothing.
    QVERIFY2(asked <= 5, qPrintable(QStringLiteral("%1 requests for a gone torrent").arg(asked)));
    QCOMPARE(details.lastError(), QString());

    // Another torrent is asked about again.
    details.setHash(QStringLiteral("another"));
    QTRY_VERIFY(server.targets.size() > asked);
}

void TestQbtEngineDetailsFake::aHashTheModelNoLongerHoldsIsNotAsked()
{
    FakeServer server;
    server.setHandler([](const QByteArray &) { return jsonResponse("{}"); });
    QbtEngine client;
    client.setBaseUrl(server.url());

    TorrentModel torrents;
    QJsonObject map;
    map.insert(QStringLiteral("kept"), QJsonObject{{QStringLiteral("name"), QStringLiteral("k")}});
    qbtfeed::full(torrents, map);

    TorrentDetails details;
    details.setPollInterval(30);
    details.setTorrentModel(&torrents);
    details.setClient(&client);
    details.setHash(QStringLiteral("removed"));
    QTest::qWait(150);
    QCOMPARE(server.targets.size(), 0);

    details.setHash(QStringLiteral("kept"));
    QTRY_VERIFY(!server.targets.isEmpty());
}

void TestQbtEngineDetailsFake::webSeedsSendsTheHashAndParsesTheList()
{
    // #10's HTTP sources tab.
    FakeServer server;
    server.setHandler([](const QByteArray &) {
        return jsonResponse(
            R"([{"url":"https://mirror.example/pub/"},{"url":"http://b.example/x"}])");
    });

    QbtEngine client;
    client.setBaseUrl(server.url());
    const Result<QStringList> seeds = await(this, client.webSeeds(QStringLiteral("abc123")));
    QVERIFY2(!seeds.isError(), qPrintable(seeds.error().toString()));
    QCOMPARE(seeds.value().size(), 2);
    QCOMPARE(seeds.value().at(1), QStringLiteral("http://b.example/x"));

    QCOMPARE(server.targets.size(), 1);
    const QUrl target(QString::fromLatin1(server.targets.constFirst()));
    QVERIFY(target.path().endsWith(QStringLiteral("/torrents/webseeds")));
    QCOMPARE(QUrlQuery(target.query()).queryItemValue(QStringLiteral("hash")),
             QStringLiteral("abc123"));
}

// #10: no other call says whether a torrent has web seeds, so the list is asked for when the
// selection changes, then on one poll in ten, and on every poll while its tab is on show.
// The timer is parked and the polls are run by hand, so the count is exact.
void TestQbtEngineDetailsFake::webSeedsAreAskedOnSelectionThenOneTickInTen()
{
    FakeServer server;
    server.setHandler([](const QByteArray &target) {
        if (target.contains("/torrents/webseeds"))
            return jsonResponse(R"([{"url":"https://mirror.example/pub/"}])");
        if (target.contains("/torrents/properties"))
            return jsonResponse("{}");
        return jsonResponse("[]");
    });
    QbtEngine client;
    client.setBaseUrl(server.url());

    TorrentDetails details;
    details.setPollInterval(3600 * 1000);
    details.setClient(&client);
    const auto asked = [&server]() {
        qsizetype count = 0;
        for (const QByteArray &target : std::as_const(server.targets))
            count += target.contains("/torrents/webseeds") ? 1 : 0;
        return count;
    };
    // Each poll sends properties, files and trackers at least; wait for those to land.
    const auto pollOnce = [&]() {
        const qsizetype before = server.targets.size();
        details.refreshNow();
        QTRY_VERIFY(server.targets.size() >= before + 3);
    };

    details.setHash(QStringLiteral("abc123"));
    QTRY_COMPARE(asked(), 1);
    QTRY_COMPARE(details.webSeeds()->count(), 1);
    QVERIFY(details.webSeeds()->loaded());

    for (int tick = 1; tick < 10; ++tick)
        pollOnce();
    QTest::qWait(50);
    QCOMPARE(asked(), 1);
    pollOnce(); // the tenth
    QTRY_COMPARE(asked(), 2);

    details.setWebSeedsActive(true); // asks at once
    QTRY_COMPARE(asked(), 3);
    pollOnce();
    QTRY_COMPARE(asked(), 4);
    pollOnce();
    QTRY_COMPARE(asked(), 5);

    // Another torrent: the list is cleared and asked for again.
    details.setWebSeedsActive(false);
    details.setHash(QStringLiteral("def456"));
    QCOMPARE(details.webSeeds()->loaded(), false);
    QTRY_COMPARE(asked(), 6);
    QTRY_VERIFY(details.webSeeds()->loaded());
}

#include "tst_qbtengine_details_fake.moc"
