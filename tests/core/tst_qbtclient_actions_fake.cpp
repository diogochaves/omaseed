// QbtClient torrent actions (T2.5) against a minimal HTTP/1.1 server of our own: asserts the
// exact form bodies the client sends (parameter names and percent-encoding) and that a
// mutating POST is never retried on a transient failure. Behaviour against a real
// qBittorrent is covered by tst_qbtclient_actions_live.

#include "core/QbtClient.hpp"
#include "core/ServerFolder.hpp"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <memory>

using omaseed::core::ApiError;
using omaseed::core::QbtClient;
using omaseed::core::QbtReply;
using omaseed::core::QbtResult;
using omaseed::core::ServerFolder;

namespace {

QByteArray httpResponse(int code,
                        const QByteArray &reason,
                        const QByteArray &body,
                        const QList<QByteArray> &extraHeaders = {})
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    response += "Content-Type: text/plain; charset=UTF-8\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    for (const QByteArray &header : extraHeaders)
        response += header + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

/// One HTTP request as the fixture saw it.
struct Recorded
{
    QByteArray method;
    QByteArray target;
    QList<QByteArray> headers;
    QByteArray body;

    QByteArray header(const QByteArray &name) const
    {
        for (const QByteArray &line : headers) {
            const qsizetype colon = line.indexOf(':');
            if (colon > 0 && line.left(colon).toLower() == name.toLower())
                return line.mid(colon + 1).trimmed();
        }
        return {};
    }
};

/// Speaks just enough HTTP to answer QNetworkAccessManager.
class FakeServer : public QTcpServer
{
public:
    using Handler = std::function<QByteArray(const Recorded &, int index)>;

    explicit FakeServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {
        [this]() { QVERIFY(listen(QHostAddress::LocalHost)); }();
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
    void setHandler(Handler handler) { m_handler = std::move(handler); }

    QList<Recorded> requests;

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

            const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
            Recorded recorded;
            const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
            recorded.method = requestLine.value(0);
            recorded.target = requestLine.value(1);
            for (qsizetype i = 1; i < lines.size(); ++i)
                recorded.headers.append(lines.at(i).trimmed());

            const QByteArray lengthHeader = recorded.header("Content-Length");
            const qsizetype bodyLength = lengthHeader.isEmpty() ? 0 : lengthHeader.toLongLong();
            if (buffer->size() - (headerEnd + 4) < bodyLength)
                return; // wait for the rest of the body
            recorded.body = buffer->mid(headerEnd + 4, bodyLength);
            buffer->clear();

            const int index = int(requests.size());
            requests.append(recorded);

            const QByteArray response =
                m_handler ? m_handler(recorded, index) : httpResponse(200, "OK", "ok");
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    }

private:
    Handler m_handler;
};

template<class T>
QbtResult<T> await(QObject *context, QbtReply<T> *reply, int timeoutMs = 10000)
{
    QbtResult<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("test never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const QbtResult<T> &finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return result;
}

} // namespace

class TestQbtClientActionsFake : public QObject
{
    Q_OBJECT

private slots:
    void hashesAreJoinedWithPipe();
    void tagsArePercentEncoded();
    void deleteFilesFlag();
    void shareLimitsSendsAllFiveFields();
    void shareLimitsKeepRatioPrecision();
    void shareLimitActionNames_data();
    void shareLimitActionNames();
    void shareLimitActionIsOmittedForAnOldApi();
    void shareLimitActionSupportFollowsTheApiVersion();
    void addUrlsSendsStoppedAndPausedAndOptions();
    void a500OnActionIsNotRetried();
    void a409OnActionMapsToConflict();
    void setLocationSendsEveryHashInOneBody();
    void setLocationFolder403IsNotASignIn();
    void setLocationBare403StillSignsIn();
    void setLocation409IsAConflict();
    void directoryContentListsAFolder();
    void directoryContentTellsMissingFromUnsupported();
    void setPreferencesSendsOnlyTheChangedKeysAsJson();
    void removeTrackersSendsTheHashAndPipedUrls();
    void addTrackersSendsOneUrlPerLine();
    void addPeersSendsPipedHashesAndPeers();
    void banPeersSendsPipedPeers();
    void setSpeedLimitsModeSendsTheMode();
    void webSeedWritesSendTheHashAndTheUrls();
    void fetchMetadataTellsLookingFromDone();
    void fetchMetadataOfALinkStillDownloading();
    void fetchMetadataNotFoundIsAnError();
    void fetchMetadataSupportFollowsTheApiVersion();
    void addUrlsSendsFilePrioritiesAndStopCondition();
    void skipCheckingFollowsTheApiVersion_data();
    void skipCheckingFollowsTheApiVersion();
    void seedModeSupportFollowsTheApiVersion();
};

// #44 (docs/29 §2): 202 is "still looking" with the hashes, 200 the metadata. Bodies are the
// research's, from qBittorrent 5.2.3.
void TestQbtClientActionsFake::fetchMetadataTellsLookingFromDone()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int index) {
        if (index == 0)
            return httpResponse(202,
                                "Accepted",
                                R"({"hash":"1f85b9e8a0d259478ee98344b2b0816f899e59b8",)"
                                R"("infohash_v1":"f43d3b0a927b9662dbb640d24793a1726aaa4c61",)"
                                R"("infohash_v2":""})");
        return httpResponse(
            200,
            "OK",
            R"({"hash":"1f85b9e8a0d259478ee98344b2b0816f899e59b8",)"
            R"("info":{"files":[{"length":812,"path":"Public Domain Sampler/README.txt"}],)"
            R"("length":812,"name":"Public Domain Sampler","piece_length":65536,)"
            R"("pieces_num":1,"private":false},"trackers":[],"webseeds":[]})");
    });
    QbtClient client;
    client.setBaseUrl(server.url());

    const QString magnet = QStringLiteral(
        "magnet:?xt=urn:btih:f43d3b0a927b9662dbb640d24793a1726aaa4c61&dn=A%20B&tr=x");
    const auto looking = await(this, client.torrentsFetchMetadata(magnet));
    QVERIFY(!looking.isError());
    QVERIFY(!looking.value().ready);
    QCOMPARE(looking.value().body.value(QStringLiteral("infohash_v1")).toString(),
             QStringLiteral("f43d3b0a927b9662dbb640d24793a1726aaa4c61"));

    const auto done = await(this, client.torrentsFetchMetadata(magnet));
    QVERIFY(!done.isError());
    QVERIFY(done.value().ready);
    QCOMPARE(done.value()
                 .body.value(QStringLiteral("info"))
                 .toObject()
                 .value(QStringLiteral("name"))
                 .toString(),
             QStringLiteral("Public Domain Sampler"));

    QCOMPARE(server.requests.size(), 2);
    QCOMPARE(server.requests.at(0).method, QByteArrayLiteral("POST"));
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/fetchMetadata"));
    // The magnet's own '&' and '=' stay inside `source`.
    const QUrlQuery form(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(form.queryItemValue(QStringLiteral("source"), QUrl::FullyDecoded), magnet);
}

// A link qBittorrent is still downloading answers 202 `{}`, forever if the link is dead.
void TestQbtClientActionsFake::fetchMetadataOfALinkStillDownloading()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int index) {
        return index == 0 ? httpResponse(202, "Accepted", "{}") : httpResponse(202, "Accepted", "");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    const QString link = QStringLiteral("https://releases.example.org/distro.torrent");
    const auto braces = await(this, client.torrentsFetchMetadata(link));
    QVERIFY(!braces.isError());
    QVERIFY(!braces.value().ready);
    QVERIFY(braces.value().body.isEmpty());
    const auto empty = await(this, client.torrentsFetchMetadata(link));
    QVERIFY(!empty.isError());
    QVERIFY(!empty.value().ready);
}

// D11: a server without the endpoint answers 404; it is an error the lookup reads as "not
// here", and the client never sends it twice.
void TestQbtClientActionsFake::fetchMetadataNotFoundIsAnError()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(404, "Not Found", "Endpoint does not exist");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    const auto result = await(this, client.torrentsFetchMetadata(QStringLiteral("magnet:?xt=x")));
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::NotFound);
    QCOMPARE(server.requests.size(), 1);
}

void TestQbtClientActionsFake::fetchMetadataSupportFollowsTheApiVersion()
{
    QVERIFY(QbtClient::supportsFetchMetadata(QStringLiteral("2.15.1")));
    QVERIFY(QbtClient::supportsFetchMetadata(QStringLiteral("2.11.9")));
    QVERIFY(QbtClient::supportsFetchMetadata(QStringLiteral("2.12")));
    QVERIFY(!QbtClient::supportsFetchMetadata(QStringLiteral("2.11.4"))); // qBittorrent 5.1.4
    QVERIFY(!QbtClient::supportsFetchMetadata(QStringLiteral("2.9.3")));
    QVERIFY(!QbtClient::supportsFetchMetadata(QString()));
    QVERIFY(!QbtClient::supportsFetchMetadata(QStringLiteral("nonsense")));
}

// docs/29 §2 and D5: the chosen files travel with the magnet in one add, comma-separated in
// file-id order; "add stopped" before the list is a started add with a stop condition.
void TestQbtClientActionsFake::addUrlsSendsFilePrioritiesAndStopCondition()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", "{}"); });
    QbtClient client;
    client.setBaseUrl(server.url());

    QbtClient::AddOptions withFiles;
    withFiles.filePriorities = {1, 0, 7};
    QVERIFY(!await(this, client.torrentsAddUrls({QStringLiteral("magnet:?xt=a")}, withFiles))
                 .isError());
    QbtClient::AddOptions untilMetadata;
    untilMetadata.stopped = false;
    untilMetadata.stopCondition = QStringLiteral("MetadataReceived");
    QVERIFY(!await(this, client.torrentsAddUrls({QStringLiteral("magnet:?xt=b")}, untilMetadata))
                 .isError());
    QVERIFY(!await(this, client.torrentsAddUrls({QStringLiteral("magnet:?xt=c")})).isError());
    QCOMPARE(server.requests.size(), 3);

    const QUrlQuery first(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(first.queryItemValue(QStringLiteral("filePriorities"), QUrl::FullyDecoded),
             QStringLiteral("1,0,7"));
    QVERIFY(!first.hasQueryItem(QStringLiteral("stopCondition")));

    const QUrlQuery second(QString::fromUtf8(server.requests.at(1).body));
    QCOMPARE(second.queryItemValue(QStringLiteral("stopCondition")),
             QStringLiteral("MetadataReceived"));
    QCOMPARE(second.queryItemValue(QStringLiteral("stopped")), QStringLiteral("false"));
    QVERIFY(!second.hasQueryItem(QStringLiteral("filePriorities")));

    const QUrlQuery third(QString::fromUtf8(server.requests.at(2).body));
    QVERIFY(!third.hasQueryItem(QStringLiteral("filePriorities")));
    QVERIFY(!third.hasQueryItem(QStringLiteral("stopCondition")));
}

// #10: the HTTP sources tab's add and remove, and the edit the client offers besides.
// #154: Web API 2.16.0 (qBittorrent 5.3) reads `seedMode` and no longer `skip_checking`;
// 5.2 (2.15.1) reads only `skip_checking`. A server whose version is unknown gets both.
void TestQbtClientActionsFake::skipCheckingFollowsTheApiVersion_data()
{
    QTest::addColumn<QString>("apiVersion");
    QTest::addColumn<bool>("seedMode");
    QTest::addColumn<bool>("skipChecking");

    QTest::newRow("5.2 (2.15.1)") << QStringLiteral("2.15.1") << false << true;
    QTest::newRow("2.15.4") << QStringLiteral("2.15.4") << false << true;
    QTest::newRow("5.1 (2.11.4)") << QStringLiteral("2.11.4") << false << true;
    QTest::newRow("2.16.0") << QStringLiteral("2.16.0") << true << false;
    QTest::newRow("5.3 (2.16.2)") << QStringLiteral("2.16.2") << true << false;
    QTest::newRow("2.17.0") << QStringLiteral("2.17.0") << true << false;
    QTest::newRow("unknown") << QString() << true << true;
    QTest::newRow("unreadable") << QStringLiteral("nonsense") << true << true;
}

void TestQbtClientActionsFake::skipCheckingFollowsTheApiVersion()
{
    QFETCH(QString, apiVersion);
    QFETCH(bool, seedMode);
    QFETCH(bool, skipChecking);

    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", "{}"); });
    QbtClient client;
    client.setBaseUrl(server.url());

    QbtClient::AddOptions skip;
    skip.skipChecking = true;
    skip.apiVersion = apiVersion;
    QVERIFY(!await(this, client.torrentsAddUrls({QStringLiteral("magnet:?xt=a")}, skip)).isError());
    // Without the choice neither key travels, whatever the version.
    QbtClient::AddOptions check;
    check.apiVersion = apiVersion;
    QVERIFY(
        !await(this, client.torrentsAddUrls({QStringLiteral("magnet:?xt=b")}, check)).isError());
    QCOMPARE(server.requests.size(), 2);

    const QUrlQuery first(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(first.hasQueryItem(QStringLiteral("seedMode")), seedMode);
    QCOMPARE(first.hasQueryItem(QStringLiteral("skip_checking")), skipChecking);
    if (seedMode)
        QCOMPARE(first.queryItemValue(QStringLiteral("seedMode")), QStringLiteral("true"));
    if (skipChecking)
        QCOMPARE(first.queryItemValue(QStringLiteral("skip_checking")), QStringLiteral("true"));
    // The version is not a parameter of its own.
    QVERIFY(!first.hasQueryItem(QStringLiteral("apiVersion")));

    const QUrlQuery second(QString::fromUtf8(server.requests.at(1).body));
    QVERIFY(!second.hasQueryItem(QStringLiteral("seedMode")));
    QVERIFY(!second.hasQueryItem(QStringLiteral("skip_checking")));
}

void TestQbtClientActionsFake::seedModeSupportFollowsTheApiVersion()
{
    QVERIFY(QbtClient::supportsSeedMode(QStringLiteral("2.16.0")));
    QVERIFY(QbtClient::supportsSeedMode(QStringLiteral("2.16.2"))); // qBittorrent 5.3.0
    QVERIFY(QbtClient::supportsSeedMode(QStringLiteral("2.17")));
    QVERIFY(QbtClient::supportsSeedMode(QStringLiteral("3.0")));
    QVERIFY(!QbtClient::supportsSeedMode(QStringLiteral("2.15.4")));
    QVERIFY(!QbtClient::supportsSeedMode(QStringLiteral("2.15.1"))); // every 5.2.x
    QVERIFY(!QbtClient::supportsSeedMode(QString()));
    QVERIFY(!QbtClient::supportsSeedMode(QStringLiteral("nonsense")));
}

void TestQbtClientActionsFake::webSeedWritesSendTheHashAndTheUrls()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QString first = QStringLiteral("https://mirror.example/pub/");
    const QString second = QStringLiteral("http://b.example/x?k=1&p=2");
    QVERIFY(
        !await(this, client.torrentsAddWebSeeds(QStringLiteral("h1"), {first, second})).isError());
    QVERIFY(
        !await(this, client.torrentsEditWebSeed(QStringLiteral("h1"), first, second)).isError());
    QVERIFY(!await(this, client.torrentsRemoveWebSeeds(QStringLiteral("h1"), {first, second}))
                 .isError());
    QCOMPARE(server.requests.size(), 3);

    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/addWebSeeds"));
    const QUrlQuery add(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(add.queryItemValue(QStringLiteral("hash"), QUrl::FullyDecoded), QStringLiteral("h1"));
    // The second URL's own '&' and '=' stay inside `urls`.
    QCOMPARE(add.queryItemValue(QStringLiteral("urls"), QUrl::FullyDecoded),
             first + QLatin1Char('|') + second);

    QCOMPARE(server.requests.at(1).target, QByteArrayLiteral("/api/v2/torrents/editWebSeed"));
    const QUrlQuery edit(QString::fromUtf8(server.requests.at(1).body));
    QCOMPARE(edit.queryItemValue(QStringLiteral("origUrl"), QUrl::FullyDecoded), first);
    QCOMPARE(edit.queryItemValue(QStringLiteral("newUrl"), QUrl::FullyDecoded), second);

    QCOMPARE(server.requests.at(2).target, QByteArrayLiteral("/api/v2/torrents/removeWebSeeds"));
    const QUrlQuery remove(QString::fromUtf8(server.requests.at(2).body));
    QCOMPARE(remove.queryItemValue(QStringLiteral("urls"), QUrl::FullyDecoded),
             first + QLatin1Char('|') + second);
}

void TestQbtClientActionsFake::addTrackersSendsOneUrlPerLine()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result = await(
        this,
        client.torrentsAddTrackers(QStringLiteral("h1"),
                                   QStringList{QStringLiteral("udp://a.example:1337/announce"),
                                               QStringLiteral("https://b.example/a?k=1&p=2")}));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/addTrackers"));
    // The newline goes out as %0A, and the second URL's own '&' and '=' stay inside `urls`.
    QVERIFY(server.requests.at(0).body.contains("announce%0Ahttps"));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("hash"), QUrl::FullyDecoded), QStringLiteral("h1"));
    QCOMPARE(body.queryItemValue(QStringLiteral("urls"), QUrl::FullyDecoded),
             QStringLiteral("udp://a.example:1337/announce\nhttps://b.example/a?k=1&p=2"));
}

void TestQbtClientActionsFake::addPeersSendsPipedHashesAndPeers()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this,
              client.torrentsAddPeers(QStringList{QStringLiteral("h1"), QStringLiteral("h2")},
                                      QStringList{QStringLiteral("10.0.0.2:6881"),
                                                  QStringLiteral("[2001:db8::1]:51413")}));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/addPeers"));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("hashes"), QUrl::FullyDecoded),
             QStringLiteral("h1|h2"));
    QCOMPARE(body.queryItemValue(QStringLiteral("peers"), QUrl::FullyDecoded),
             QStringLiteral("10.0.0.2:6881|[2001:db8::1]:51413"));
}

// #13: the widget's SetAltSpeed says which mode it wants rather than toggling.
void TestQbtClientActionsFake::setSpeedLimitsModeSendsTheMode()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    QVERIFY(!await(this, client.transferSetSpeedLimitsMode(true)).isError());
    QVERIFY(!await(this, client.transferSetSpeedLimitsMode(false)).isError());
    QCOMPARE(server.requests.size(), 2);
    QCOMPARE(server.requests.at(0).method, QByteArray("POST"));
    QCOMPARE(server.requests.at(0).target,
             QByteArrayLiteral("/api/v2/transfer/setSpeedLimitsMode"));
    QCOMPARE(server.requests.at(0).body, QByteArrayLiteral("mode=1"));
    QCOMPARE(server.requests.at(1).body, QByteArrayLiteral("mode=0"));
}

void TestQbtClientActionsFake::banPeersSendsPipedPeers()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this, client.transferBanPeers(QStringList{QStringLiteral("10.0.0.2:6881")}));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.at(0).method, QByteArray("POST"));
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/transfer/banPeers"));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("peers"), QUrl::FullyDecoded),
             QStringLiteral("10.0.0.2:6881"));
}

void TestQbtClientActionsFake::removeTrackersSendsTheHashAndPipedUrls()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result = await(
        this,
        client.torrentsRemoveTrackers(QStringLiteral("h1"),
                                      QStringList{QStringLiteral("udp://a.example:1337/announce"),
                                                  QStringLiteral("https://b.example/announce")}));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/removeTrackers"));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("hash"), QUrl::FullyDecoded), QStringLiteral("h1"));
    QCOMPARE(body.queryItemValue(QStringLiteral("urls"), QUrl::FullyDecoded),
             QStringLiteral("udp://a.example:1337/announce|https://b.example/announce"));
}

void TestQbtClientActionsFake::setPreferencesSendsOnlyTheChangedKeysAsJson()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this,
              client.appSetPreferences(QJsonObject{{QStringLiteral("dl_limit"), 51200},
                                                   {QStringLiteral("alt_up_limit"), 0}}));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.at(0).method, QByteArray("POST"));
    QVERIFY(server.requests.at(0).target.endsWith("/api/v2/app/setPreferences"));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    const QJsonObject sent =
        QJsonDocument::fromJson(
            body.queryItemValue(QStringLiteral("json"), QUrl::FullyDecoded).toUtf8())
            .object();
    QCOMPARE(sent.size(), 2);
    QCOMPARE(sent.value(QStringLiteral("dl_limit")).toInteger(), 51200);
    QCOMPARE(sent.value(QStringLiteral("alt_up_limit")).toInteger(), 0);
}

void TestQbtClientActionsFake::hashesAreJoinedWithPipe()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this, client.torrentsStart(QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/start"));
    QCOMPARE(server.requests.at(0).header("Content-Type"),
             QByteArrayLiteral("application/x-www-form-urlencoded"));
    QCOMPARE(server.requests.at(0).body, QByteArrayLiteral("hashes=a%7Cb"));
}

void TestQbtClientActionsFake::tagsArePercentEncoded()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this,
              client.torrentsAddTags(QStringList{QStringLiteral("h1")},
                                     QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/addTags"));
    // qBittorrent expects a comma-separated tag list; the comma is percent-encoded in an
    // x-www-form-urlencoded body.
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("hashes"), QUrl::FullyDecoded),
             QStringLiteral("h1"));
    QCOMPARE(body.queryItemValue(QStringLiteral("tags"), QUrl::FullyDecoded),
             QStringLiteral("a,b"));
    QVERIFY(server.requests.at(0).body.contains("tags=a%2Cb"));
}

void TestQbtClientActionsFake::deleteFilesFlag()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this, client.torrentsDelete(QStringList{QStringLiteral("h1")}, true));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/delete"));
    QCOMPARE(server.requests.at(0).body, QByteArrayLiteral("hashes=h1&deleteFiles=true"));
}

void TestQbtClientActionsFake::shareLimitsSendsAllFiveFields()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this, client.torrentsSetShareLimits(QStringList{QStringLiteral("h1")}, -2, -2, -2));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("hashes"), QUrl::FullyDecoded),
             QStringLiteral("h1"));
    QCOMPARE(body.queryItemValue(QStringLiteral("ratioLimit"), QUrl::FullyDecoded),
             QStringLiteral("-2"));
    QCOMPARE(body.queryItemValue(QStringLiteral("seedingTimeLimit"), QUrl::FullyDecoded),
             QStringLiteral("-2"));
    QCOMPARE(body.queryItemValue(QStringLiteral("inactiveSeedingTimeLimit"), QUrl::FullyDecoded),
             QStringLiteral("-2"));
    // 5.2.3 answers 400 without this field, so the default argument is Default, not "omit".
    QCOMPARE(body.queryItemValue(QStringLiteral("shareLimitAction"), QUrl::FullyDecoded),
             QStringLiteral("Default"));
    // qBittorrent 5.3 answers 400 without the mode.
    QCOMPARE(body.queryItemValue(QStringLiteral("shareLimitsMode"), QUrl::FullyDecoded),
             QStringLiteral("Default"));
}

void TestQbtClientActionsFake::shareLimitsKeepRatioPrecision()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });
    QbtClient client;
    client.setBaseUrl(server.url());

    QVERIFY(
        !await(this,
               client.torrentsSetShareLimits(QStringList{QStringLiteral("h1")}, 1234567.5, -2, -2))
             .isError());
    QVERIFY(
        !await(this, client.torrentsSetShareLimits(QStringList{QStringLiteral("h1")}, 1.25, -2, -2))
             .isError());
    const auto ratioOf = [&server](int index) {
        return QUrlQuery(QString::fromUtf8(server.requests.at(index).body))
            .queryItemValue(QStringLiteral("ratioLimit"), QUrl::FullyDecoded);
    };
    // The default QString::number would send 1.23457e+06.
    QCOMPARE(ratioOf(0), QStringLiteral("1234567.5"));
    QCOMPARE(ratioOf(1), QStringLiteral("1.25"));
}

void TestQbtClientActionsFake::shareLimitActionNames_data()
{
    QTest::addColumn<QbtClient::ShareLimitAction>("action");
    QTest::addColumn<QString>("name");

    // The qBittorrent 5.2 meta-enum names (T5.5 Q14), which torrents/info echoes back as
    // `share_limit_action`.
    QTest::newRow("default") << QbtClient::ShareLimitAction::Default << QStringLiteral("Default");
    QTest::newRow("stop") << QbtClient::ShareLimitAction::Stop << QStringLiteral("Stop");
    QTest::newRow("remove") << QbtClient::ShareLimitAction::Remove << QStringLiteral("Remove");
    QTest::newRow("remove with content")
        << QbtClient::ShareLimitAction::RemoveWithContent << QStringLiteral("RemoveWithContent");
    QTest::newRow("super seeding")
        << QbtClient::ShareLimitAction::EnableSuperSeeding << QStringLiteral("EnableSuperSeeding");
}

void TestQbtClientActionsFake::shareLimitActionNames()
{
    QFETCH(QbtClient::ShareLimitAction, action);
    QFETCH(QString, name);

    QCOMPARE(QbtClient::shareLimitActionName(action), name);
    QCOMPARE(QbtClient::shareLimitActionFromName(name), action);

    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });
    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result = await(
        this, client.torrentsSetShareLimits(QStringList{QStringLiteral("h1")}, 2, 60, -1, action));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("shareLimitAction"), QUrl::FullyDecoded), name);
    QCOMPARE(body.queryItemValue(QStringLiteral("ratioLimit"), QUrl::FullyDecoded),
             QStringLiteral("2"));
    QCOMPARE(body.queryItemValue(QStringLiteral("seedingTimeLimit"), QUrl::FullyDecoded),
             QStringLiteral("60"));
    QCOMPARE(body.queryItemValue(QStringLiteral("inactiveSeedingTimeLimit"), QUrl::FullyDecoded),
             QStringLiteral("-1"));
}

void TestQbtClientActionsFake::shareLimitActionIsOmittedForAnOldApi()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });
    QbtClient client;
    client.setBaseUrl(server.url());

    // A caller talking to a pre-2.15.1 server passes nullopt: the field is left off
    // entirely (not sent empty), the four limits still travel.
    const QbtResult<QByteArray> result = await(
        this,
        client.torrentsSetShareLimits(QStringList{QStringLiteral("h1")}, -2, -2, -2, std::nullopt));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QVERIFY(!body.hasQueryItem(QStringLiteral("shareLimitAction")));
    QVERIFY(!body.hasQueryItem(QStringLiteral("shareLimitsMode")));
    QCOMPARE(body.queryItemValue(QStringLiteral("hashes"), QUrl::FullyDecoded),
             QStringLiteral("h1"));
    QCOMPARE(body.queryItemValue(QStringLiteral("inactiveSeedingTimeLimit"), QUrl::FullyDecoded),
             QStringLiteral("-2"));

    // Unknown or empty names read as Default rather than failing.
    QCOMPARE(QbtClient::shareLimitActionFromName(QString()), QbtClient::ShareLimitAction::Default);
    QCOMPARE(QbtClient::shareLimitActionFromName(QStringLiteral("DeleteFiles")),
             QbtClient::ShareLimitAction::Default);
}

void TestQbtClientActionsFake::shareLimitActionSupportFollowsTheApiVersion()
{
    QVERIFY(QbtClient::supportsShareLimitAction(QStringLiteral("2.15.1")));
    QVERIFY(QbtClient::supportsShareLimitAction(QStringLiteral("2.16.0")));
    QVERIFY(QbtClient::supportsShareLimitAction(QStringLiteral("3.0")));
    QVERIFY(!QbtClient::supportsShareLimitAction(QStringLiteral("2.15.0")));
    QVERIFY(!QbtClient::supportsShareLimitAction(QStringLiteral("2.11.4")));
    QVERIFY(!QbtClient::supportsShareLimitAction(QString()));
    QVERIFY(!QbtClient::supportsShareLimitAction(QStringLiteral("unknown")));
}

void TestQbtClientActionsFake::addUrlsSendsStoppedAndPausedAndOptions()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(200, "OK", R"({"added_torrent_ids":[]})");
    });

    QbtClient client;
    client.setBaseUrl(server.url());

    QbtClient::AddOptions options;
    options.category = QStringLiteral("t25");
    options.tags = {QStringLiteral("a"), QStringLiteral("b")};
    options.savepath = QStringLiteral("/tmp/t25");
    options.stopped = true;
    options.sequentialDownload = true;
    options.rename = QStringLiteral("renamed");
    options.contentLayout = QbtClient::ContentLayout::Subfolder;

    const QbtResult<QByteArray> result = await(
        this,
        client.torrentsAddUrls(QStringList{QStringLiteral("magnet:?xt=urn:btih:abc")}, options));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/add"));

    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("urls"), QUrl::FullyDecoded),
             QStringLiteral("magnet:?xt=urn:btih:abc"));
    QCOMPARE(body.queryItemValue(QStringLiteral("category"), QUrl::FullyDecoded),
             QStringLiteral("t25"));
    QCOMPARE(body.queryItemValue(QStringLiteral("tags"), QUrl::FullyDecoded),
             QStringLiteral("a,b"));
    QCOMPARE(body.queryItemValue(QStringLiteral("savepath"), QUrl::FullyDecoded),
             QStringLiteral("/tmp/t25"));
    QCOMPARE(body.queryItemValue(QStringLiteral("stopped"), QUrl::FullyDecoded),
             QStringLiteral("true"));
    // Sent alongside `stopped` so a pre-5.0 server (which only knows `paused`) also works.
    QCOMPARE(body.queryItemValue(QStringLiteral("paused"), QUrl::FullyDecoded),
             QStringLiteral("true"));
    QCOMPARE(body.queryItemValue(QStringLiteral("sequentialDownload"), QUrl::FullyDecoded),
             QStringLiteral("true"));
    QCOMPARE(body.queryItemValue(QStringLiteral("rename"), QUrl::FullyDecoded),
             QStringLiteral("renamed"));
    QCOMPARE(body.queryItemValue(QStringLiteral("contentLayout"), QUrl::FullyDecoded),
             QStringLiteral("Subfolder"));
}

void TestQbtClientActionsFake::a500OnActionIsNotRetried()
{
    FakeServer server;
    server.setHandler(
        [](const Recorded &, int) { return httpResponse(500, "Internal Server Error", "boom"); });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setRetryBackoff(std::chrono::milliseconds(20));

    const QbtResult<QByteArray> result =
        await(this, client.torrentsStop(QStringList{QStringLiteral("h1")}));
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Server);
    QCOMPARE(server.requests.size(), 1); // a mutating POST is never retried
}

void TestQbtClientActionsFake::a409OnActionMapsToConflict()
{
    FakeServer server;
    server.setHandler(
        [](const Recorded &, int) { return httpResponse(409, "Conflict", "Conflict"); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this, client.torrentsRecheck(QStringList{QStringLiteral("h1")}));
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Conflict);
    QCOMPARE(result.error().httpStatus(), 409);
    QCOMPARE(server.requests.size(), 1);
}

// C15 (board 42): a folder's move sends one setLocation per target with every hash, piped,
// in one form body; 430 hashes are ~19 KB.
void TestQbtClientActionsFake::setLocationSendsEveryHashInOneBody()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    QStringList hashes;
    for (int i = 0; i < 430; ++i)
        hashes << QStringLiteral("%1").arg(i, 40, 16, QLatin1Char('0'));
    const QString location = QStringLiteral("/mnt/fast/games/Hand Held & Ünïcødé");
    const QbtResult<QByteArray> result = await(this, client.torrentsSetLocation(hashes, location));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/setLocation"));
    const QUrlQuery form(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(form.queryItemValue(QStringLiteral("hashes"), QUrl::FullyDecoded),
             hashes.join(QLatin1Char('|')));
    QCOMPARE(form.queryItemValue(QStringLiteral("location"), QUrl::FullyDecoded), location);
    QVERIFY(server.requests.at(0).body.size() < 20 * 1024);
}

// C15 M19: the API documents 403 "User does not have write access to directory" for
// setLocation. That refuses the folder, not the sign-in: no re-login, no auth state.
void TestQbtClientActionsFake::setLocationFolder403IsNotASignIn()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(403, "Forbidden", "Cannot write to directory");
    });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setCredentials(QStringLiteral("user"), QStringLiteral("pass"));

    const QbtResult<QByteArray> result = await(
        this, client.torrentsSetLocation({QStringLiteral("h1")}, QStringLiteral("/mnt/readonly")));
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Forbidden);
    QCOMPARE(result.error().httpStatus(), 403);
    QVERIFY2(result.error().message().contains(QStringLiteral("Cannot write to directory")),
             qPrintable(result.error().message()));
    QCOMPARE(server.requests.size(), 1); // no auth/login
    QVERIFY(client.authState() != QbtClient::AuthState::Forbidden);
}

// The bare "Forbidden" is a session qBittorrent doesn't know: that one still signs in again.
void TestQbtClientActionsFake::setLocationBare403StillSignsIn()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        if (request.target.startsWith("/api/v2/auth/login"))
            return httpResponse(200, "OK", "Ok.", {"Set-Cookie: SID=abc; path=/"});
        return httpResponse(403, "Forbidden", "Forbidden");
    });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setCredentials(QStringLiteral("user"), QStringLiteral("pass"));

    const QbtResult<QByteArray> result =
        await(this, client.torrentsSetLocation({QStringLiteral("h1")}, QStringLiteral("/mnt/x")));
    QVERIFY(result.isError());
    bool loggedIn = false;
    for (const Recorded &request : std::as_const(server.requests))
        loggedIn = loggedIn || request.target.startsWith("/api/v2/auth/login");
    QVERIFY(loggedIn);
}

void TestQbtClientActionsFake::setLocation409IsAConflict()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(409, "Conflict", "Cannot make save path");
    });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result = await(
        this, client.torrentsSetLocation({QStringLiteral("h1")}, QStringLiteral("/proc/nope")));
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Conflict);
    QCOMPARE(server.requests.size(), 1);
}

// #102 (C16 L13): app/getDirectoryContent, with qBittorrent 5.2.3's own answers.
void TestQbtClientActionsFake::directoryContentListsAFolder()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(
            200, "OK", R"(["/srv/media/films/Short Film 01","/srv/media/films/poster.jpg"])");
    });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QJsonArray> result =
        await(this, client.appDirectoryContent(QStringLiteral("/srv/media/films")));
    QVERIFY(!result.isError());
    QCOMPARE(server.requests.size(), 1);
    const QUrlQuery query(QUrl(QString::fromUtf8(server.requests.at(0).target)));
    QCOMPARE(QUrl(QString::fromUtf8(server.requests.at(0).target)).path(),
             QStringLiteral("/api/v2/app/getDirectoryContent"));
    QCOMPARE(query.queryItemValue(QStringLiteral("dirPath"), QUrl::FullyDecoded),
             QStringLiteral("/srv/media/films"));
    QCOMPARE(query.queryItemValue(QStringLiteral("mode")), QStringLiteral("all"));

    const ServerFolder folder = ServerFolder::fromReply(result);
    QCOMPARE(folder.state, ServerFolder::State::Listed);
    QVERIFY(folder.has(QStringLiteral("Short Film 01")));
    QVERIFY(folder.has(QStringLiteral("poster.jpg")));
    QVERIFY(!folder.has(QStringLiteral("films")));
}

void TestQbtClientActionsFake::directoryContentTellsMissingFromUnsupported()
{
    const QList<std::pair<QByteArray, ServerFolder::State>> cases{
        {"404 Directory does not exist", ServerFolder::State::Missing},
        {"400 Invalid directory path", ServerFolder::State::Missing},
        {"404 Endpoint does not exist", ServerFolder::State::Unsupported},
        {"404 Not Found", ServerFolder::State::Unsupported},
        {"500 Internal Server Error", ServerFolder::State::Failed},
    };
    for (const auto &[answer, state] : cases) {
        const int code = answer.left(3).toInt();
        const QByteArray text = answer.mid(4);
        FakeServer server;
        server.setHandler([code, text](const Recorded &, int) {
            return httpResponse(code, code == 404 ? "Not Found" : "Error", text);
        });
        QbtClient client;
        client.setBaseUrl(server.url());
        const QbtResult<QJsonArray> result =
            await(this, client.appDirectoryContent(QStringLiteral("/srv/media/nope")));
        QVERIFY2(result.isError(), answer.constData());
        QCOMPARE(ServerFolder::fromReply(result).state, state);
        // Not a sign-in matter: no login asked.
        for (const Recorded &request : std::as_const(server.requests))
            QVERIFY(!request.target.startsWith("/api/v2/auth/login"));
    }
}

QTEST_MAIN(TestQbtClientActionsFake)

#include "tst_qbtclient_actions_fake.moc"
