// QbtEngine (#82, docs/DECISIONS.md C14 E2 a): qBittorrent behind EngineClient. Against a
// minimal HTTP server of our own: omaseed's values leave in qBittorrent's spelling (file
// priorities, -2 and -1 share limits, the add's form fields, the share-limit action's names)
// and its answers come back as omaseed's types. The transport itself is tst_qbtclient_*'s.

#include "FakeHttp.hpp"

#include "core/engines/qbt/QbtEngine.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

using namespace fakehttp;
using omaseed::core::AddOptions;
using omaseed::core::ContentLayout;
using omaseed::core::Done;
using omaseed::core::EngineClient;
using omaseed::core::FilePriority;
using omaseed::core::GlobalLimits;
using omaseed::core::MinutesLimit;
using omaseed::core::QbtEngine;
using omaseed::core::RatioLimit;
using omaseed::core::ServerInfo;
using omaseed::core::StopCondition;
using omaseed::core::TorrentField;

namespace {

QUrlQuery form(const Recorded &request)
{
    return QUrlQuery(QString::fromUtf8(request.body));
}

QString field(const Recorded &request, const char *name)
{
    return form(request).queryItemValue(QString::fromLatin1(name), QUrl::FullyDecoded);
}

} // namespace

class TestQbtEngine : public QObject
{
    Q_OBJECT

private slots:
    void filePrioritiesLeaveAsQbittorrentsNumbers_data();
    void filePrioritiesLeaveAsQbittorrentsNumbers();
    void shareLimitsLeaveAsCodes();
    void shareLimitActionIsLeftOutWhenAbsent();
    void addOptionsBecomeTheForm();
    void addAnswers();
    void startAndStopAll();
    void syncIsADelta();
    void filesTorrentsAndTagsComeBackTyped();
    void fetchMetadataLooksThenHasIt();
    void logAsksForProblemsOrEverything();
    void globalLimitsReadAndSetOnlyWhatIsGiven();
    void errorsPassThrough();
    void abortingCancelsTheRequest();
    void reachableUrlIsTheVersionEndpoint();
    void featuresFollowTheApiVersion();
};

void TestQbtEngine::filePrioritiesLeaveAsQbittorrentsNumbers_data()
{
    QTest::addColumn<int>("priority");
    QTest::addColumn<QString>("wire");
    QTest::newRow("skip") << int(FilePriority::Skip) << QStringLiteral("0");
    QTest::newRow("normal") << int(FilePriority::Normal) << QStringLiteral("1");
    QTest::newRow("high") << int(FilePriority::High) << QStringLiteral("6");
    QTest::newRow("maximal") << int(FilePriority::Maximal) << QStringLiteral("7");
}

void TestQbtEngine::filePrioritiesLeaveAsQbittorrentsNumbers()
{
    QFETCH(int, priority);
    QFETCH(QString, wire);
    FakeServer server;
    QbtEngine engine;
    engine.setBaseUrl(server.url());
    const auto result =
        await(this, engine.setFilePriority(QStringLiteral("abc"), {0, 3}, FilePriority(priority)));
    QVERIFY(!result.isError());
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/filePrio"));
    QCOMPARE(field(server.requests.at(0), "hash"), QStringLiteral("abc"));
    QCOMPARE(field(server.requests.at(0), "id"), QStringLiteral("0|3"));
    QCOMPARE(field(server.requests.at(0), "priority"), wire);
}

// qBittorrent spells "the server's default" -2 and "none" -1 (docs/36 §4.1).
void TestQbtEngine::shareLimitsLeaveAsCodes()
{
    FakeServer server;
    QbtEngine engine;
    engine.setBaseUrl(server.url());
    QVERIFY(!await(this,
                   engine.setShareLimits({QStringLiteral("a"), QStringLiteral("b")},
                                         RatioLimit::of(1.5),
                                         MinutesLimit::global(),
                                         MinutesLimit::unlimited(),
                                         QStringLiteral("RemoveWithContent")))
                 .isError());
    QVERIFY(!await(this,
                   engine.setShareLimits({QStringLiteral("a")},
                                         RatioLimit::unlimited(),
                                         MinutesLimit::of(90),
                                         MinutesLimit::global(),
                                         QStringLiteral("Default")))
                 .isError());
    QCOMPARE(server.requests.size(), 2);
    const Recorded &first = server.requests.at(0);
    QCOMPARE(first.target, QByteArrayLiteral("/api/v2/torrents/setShareLimits"));
    QCOMPARE(field(first, "hashes"), QStringLiteral("a|b"));
    QCOMPARE(field(first, "ratioLimit"), QStringLiteral("1.5"));
    QCOMPARE(field(first, "seedingTimeLimit"), QStringLiteral("-2"));
    QCOMPARE(field(first, "inactiveSeedingTimeLimit"), QStringLiteral("-1"));
    QCOMPARE(field(first, "shareLimitAction"), QStringLiteral("RemoveWithContent"));
    const Recorded &second = server.requests.at(1);
    QCOMPARE(field(second, "ratioLimit"), QStringLiteral("-1"));
    QCOMPARE(field(second, "seedingTimeLimit"), QStringLiteral("90"));
    QCOMPARE(field(second, "inactiveSeedingTimeLimit"), QStringLiteral("-2"));
    QCOMPARE(field(second, "shareLimitAction"), QStringLiteral("Default"));
}

// A server before Web API 2.15.1 refuses a parameter it does not know.
void TestQbtEngine::shareLimitActionIsLeftOutWhenAbsent()
{
    FakeServer server;
    QbtEngine engine;
    engine.setBaseUrl(server.url());
    QVERIFY(!await(this,
                   engine.setShareLimits({QStringLiteral("a")},
                                         RatioLimit::global(),
                                         MinutesLimit::global(),
                                         MinutesLimit::global(),
                                         std::nullopt))
                 .isError());
    QVERIFY(!form(server.requests.at(0)).hasQueryItem(QStringLiteral("shareLimitAction")));
    QCOMPARE(field(server.requests.at(0), "ratioLimit"), QStringLiteral("-2"));
}

void TestQbtEngine::addOptionsBecomeTheForm()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", "{}"); });
    QbtEngine engine;
    engine.setBaseUrl(server.url());

    AddOptions options;
    options.savePath = QStringLiteral("/data/x");
    options.category = QStringLiteral("films");
    options.tags = {QStringLiteral("a"), QStringLiteral("b")};
    options.stopped = false;
    options.skipChecking = true;
    options.contentLayout = ContentLayout::NoSubfolder;
    options.rename = QStringLiteral("New name");
    options.upLimit = 2048;
    options.dlLimit = 4096;
    options.autoManagement = true;
    options.sequentialDownload = true;
    options.firstLastPiece = true;
    options.filePriorities = {FilePriority::Normal, FilePriority::Skip, FilePriority::Maximal};
    options.stopCondition = StopCondition::MetadataReceived;
    QVERIFY(!await(this, engine.addUrls({QStringLiteral("magnet:?xt=urn:btih:abc")}, options))
                 .isError());

    const Recorded &add = server.requests.at(0);
    QCOMPARE(add.target, QByteArrayLiteral("/api/v2/torrents/add"));
    QCOMPARE(field(add, "urls"), QStringLiteral("magnet:?xt=urn:btih:abc"));
    QCOMPARE(field(add, "savepath"), QStringLiteral("/data/x"));
    QCOMPARE(field(add, "category"), QStringLiteral("films"));
    QCOMPARE(field(add, "tags"), QStringLiteral("a,b"));
    QCOMPARE(field(add, "stopped"), QStringLiteral("false"));
    QCOMPARE(field(add, "paused"), QStringLiteral("false"));
    QCOMPARE(field(add, "skip_checking"), QStringLiteral("true"));
    QCOMPARE(field(add, "contentLayout"), QStringLiteral("NoSubfolder"));
    QCOMPARE(field(add, "rename"), QStringLiteral("New name"));
    QCOMPARE(field(add, "upLimit"), QStringLiteral("2048"));
    QCOMPARE(field(add, "dlLimit"), QStringLiteral("4096"));
    QCOMPARE(field(add, "autoTMM"), QStringLiteral("true"));
    QCOMPARE(field(add, "sequentialDownload"), QStringLiteral("true"));
    QCOMPARE(field(add, "firstLastPiecePrio"), QStringLiteral("true"));
    QCOMPARE(field(add, "filePriorities"), QStringLiteral("1,0,7"));
    QCOMPARE(field(add, "stopCondition"), QStringLiteral("MetadataReceived"));

    // Nothing set, nothing sent: the server's (or the category's) own defaults apply.
    QVERIFY(
        !await(this, engine.addUrls({QStringLiteral("magnet:?xt=urn:btih:def")}, {})).isError());
    const QUrlQuery bare = form(server.requests.at(1));
    QCOMPARE(bare.queryItems().size(), 1);
}

void TestQbtEngine::addAnswers()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int index) {
        switch (index) {
        case 0:
            return httpResponse(200, "OK", R"({"added_torrent_ids":["aa","bb"]})");
        case 1:
            return httpResponse(200, "OK", "Fails.");
        case 2:
            return httpResponse(200, "OK", "Ok.");
        default:
            return httpResponse(409, "Conflict", "Torrent already exists");
        }
    });
    QbtEngine engine;
    engine.setBaseUrl(server.url());

    const auto ids = await(this, engine.addUrls({QStringLiteral("magnet:?a")}, {}));
    QVERIFY(!ids.isError());
    QVERIFY(!ids.value().refused);
    QCOMPARE(ids.value().addedIds, QStringList({QStringLiteral("aa"), QStringLiteral("bb")}));

    const auto refused =
        await(this, engine.addFile(QStringLiteral("x.torrent"), QByteArrayLiteral("d4:infoe"), {}));
    QVERIFY(!refused.isError());
    QVERIFY(refused.value().refused);

    const auto older = await(this, engine.addMany({}, {QStringLiteral("magnet:?c")}, {}));
    QVERIFY(!older.isError());
    QVERIFY(!older.value().refused);
    QVERIFY(older.value().addedIds.isEmpty());

    const auto conflict = await(this, engine.addUrls({QStringLiteral("magnet:?a")}, {}));
    QVERIFY(conflict.isError());
    QCOMPARE(conflict.error().kind(), ApiError::Kind::Conflict);
}

void TestQbtEngine::startAndStopAll()
{
    FakeServer server;
    QbtEngine engine;
    engine.setBaseUrl(server.url());
    QVERIFY(!await(this, engine.startAll()).isError());
    QVERIFY(!await(this, engine.stopAll()).isError());
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/start"));
    QCOMPARE(field(server.requests.at(0), "hashes"), QStringLiteral("all"));
    QCOMPARE(server.requests.at(1).target, QByteArrayLiteral("/api/v2/torrents/stop"));
    QCOMPARE(field(server.requests.at(1), "hashes"), QStringLiteral("all"));
}

void TestQbtEngine::syncIsADelta()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(200,
                            "OK",
                            R"({"rid":7,"full_update":true,"torrents":{"abc":{"name":"Big Buck",)"
                            R"("state":"stalledUP","eta":8640000}},"tags":["t"],)"
                            R"("server_state":{"dl_info_speed":42}})");
    });
    QbtEngine engine;
    engine.setBaseUrl(server.url());
    const auto delta = await(this, engine.sync(3));
    QVERIFY(!delta.isError());
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/sync/maindata?rid=3"));
    QVERIFY(delta.value().full);
    QCOMPARE(delta.value().cursor, std::optional<int>(7));
    QCOMPARE(delta.value().torrents.size(), 1);
    QCOMPARE(delta.value().torrents.at(0).hash, QStringLiteral("abc"));
    QCOMPARE(delta.value().torrents.at(0).values.name, QStringLiteral("Big Buck"));
    QVERIFY(delta.value().torrents.at(0).has(TorrentField::Status));
    QCOMPARE(delta.value().tags, QStringList{QStringLiteral("t")});
    QVERIFY(delta.value().serverState.has_value());
    QCOMPARE(delta.value().serverState->dlSpeed, std::optional<qint64>(42));
}

void TestQbtEngine::filesTorrentsAndTagsComeBackTyped()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        if (request.target.startsWith("/api/v2/torrents/files"))
            return httpResponse(200,
                                "OK",
                                R"([{"index":0,"name":"A/one.mkv","size":10,"progress":1,)"
                                R"("priority":6},{"index":1,"name":"A/two.nfo","size":2,)"
                                R"("progress":0,"priority":0}])");
        if (request.target.startsWith("/api/v2/torrents/info"))
            return httpResponse(200,
                                "OK",
                                R"([{"hash":"aa","name":"One","infohash_v1":"aa"},)"
                                R"({"hash":"bb","name":"Two"}])");
        if (request.target.startsWith("/api/v2/torrents/categories"))
            return httpResponse(200, "OK", R"({"films":{"name":"films","savePath":"/f"}})");
        return httpResponse(200, "OK", R"(["x","y"])");
    });
    QbtEngine engine;
    engine.setBaseUrl(server.url());

    const auto files = await(this, engine.files(QStringLiteral("aa")));
    QVERIFY(!files.isError());
    QCOMPARE(files.value().size(), 2);
    QCOMPARE(files.value().at(0).path, QStringLiteral("A/one.mkv"));
    QCOMPARE(files.value().at(0).priority, std::optional<int>(int(FilePriority::High)));
    QCOMPARE(files.value().at(1).index, std::optional<int>(1));
    QCOMPARE(files.value().at(1).priority, std::optional<int>(int(FilePriority::Skip)));

    const auto torrents =
        await(this, engine.torrents({QStringLiteral("aa"), QStringLiteral("bb")}));
    QVERIFY(!torrents.isError());
    QCOMPARE(server.requests.at(1).target,
             QByteArrayLiteral("/api/v2/torrents/info?hashes=aa%7Cbb"));
    QCOMPARE(torrents.value().size(), 2);
    QCOMPARE(torrents.value().at(0).hash, QStringLiteral("aa"));
    QCOMPARE(torrents.value().at(0).values.infohashV1, QStringLiteral("aa"));
    QCOMPARE(torrents.value().at(1).values.name, QStringLiteral("Two"));

    const auto categories = await(this, engine.categories());
    QVERIFY(!categories.isError());
    QCOMPARE(categories.value().size(), 1);
    QCOMPARE(categories.value().at(0).name, QStringLiteral("films"));
    QCOMPARE(categories.value().at(0).savePath, std::optional<QString>(QStringLiteral("/f")));

    const auto tags = await(this, engine.tags());
    QVERIFY(!tags.isError());
    QCOMPARE(tags.value(), QStringList({QStringLiteral("x"), QStringLiteral("y")}));
}

void TestQbtEngine::fetchMetadataLooksThenHasIt()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int index) {
        if (index == 0)
            return httpResponse(202, "Accepted", R"({"hash":"1f85"})");
        return httpResponse(
            200,
            "OK",
            R"({"hash":"1F85B9E8A0D259478EE98344B2B0816F899E59B8",)"
            R"("info":{"files":[{"length":812,"path":"Public Domain Sampler/README.txt"},)"
            R"({"length":20,"path":"Public Domain Sampler/b.txt"}],)"
            R"("name":"Public Domain Sampler","piece_length":65536,"private":1},)"
            R"("trackers":[{"url":"udp://t.example:1"}]})");
    });
    QbtEngine engine;
    engine.setBaseUrl(server.url());

    const auto looking = await(this, engine.fetchMetadata(QStringLiteral("magnet:?xt=x")));
    QVERIFY(!looking.isError());
    QVERIFY(!looking.value().ready);
    QVERIFY(!looking.value().metadata.has_value());

    const auto done = await(this, engine.fetchMetadata(QStringLiteral("magnet:?xt=x")));
    QVERIFY(!done.isError());
    QVERIFY(done.value().ready);
    QVERIFY(done.value().metadata.has_value());
    const auto &metadata = *done.value().metadata;
    QCOMPARE(metadata.torrentId, QStringLiteral("1f85b9e8a0d259478ee98344b2b0816f899e59b8"));
    QCOMPARE(metadata.name, QStringLiteral("Public Domain Sampler"));
    QVERIFY(metadata.multiFile);
    QVERIFY(metadata.isPrivate);
    QCOMPARE(metadata.totalSize, 832);
    QCOMPARE(metadata.files.at(0).path, QStringLiteral("README.txt"));
    QCOMPARE(metadata.trackers, QStringList{QStringLiteral("udp://t.example:1")});
}

void TestQbtEngine::logAsksForProblemsOrEverything()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(
            200, "OK", R"([{"id":5,"message":"File error","timestamp":1700000000,"type":4}])");
    });
    QbtEngine engine;
    engine.setBaseUrl(server.url());

    const auto problems = await(this, engine.log(4, true));
    QVERIFY(!problems.isError());
    QCOMPARE(problems.value().size(), 1);
    QCOMPARE(problems.value().at(0).id, 5);
    QCOMPARE(problems.value().at(0).type, 4);
    const QUrlQuery first(QUrl(QString::fromUtf8(server.requests.at(0).target)).query());
    QCOMPARE(first.queryItemValue(QStringLiteral("normal")), QStringLiteral("false"));
    QCOMPARE(first.queryItemValue(QStringLiteral("warning")), QStringLiteral("true"));
    QCOMPARE(first.queryItemValue(QStringLiteral("last_known_id")), QStringLiteral("4"));

    QVERIFY(!await(this, engine.log(-1, false)).isError());
    const QUrlQuery second(QUrl(QString::fromUtf8(server.requests.at(1).target)).query());
    QCOMPARE(second.queryItemValue(QStringLiteral("normal")), QStringLiteral("true"));
    QCOMPARE(second.queryItemValue(QStringLiteral("last_known_id")), QStringLiteral("-1"));
}

void TestQbtEngine::globalLimitsReadAndSetOnlyWhatIsGiven()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int index) {
        if (index == 0)
            return httpResponse(200,
                                "OK",
                                R"({"dl_limit":1024,"up_limit":0,"alt_dl_limit":2048,)"
                                R"("alt_up_limit":512,"save_path":"/x"})");
        return httpResponse(200, "OK", "");
    });
    QbtEngine engine;
    engine.setBaseUrl(server.url());

    const auto limits = await(this, engine.globalLimits());
    QVERIFY(!limits.isError());
    QCOMPARE(limits.value(), (GlobalLimits{1024, 0, 2048, 512}));

    GlobalLimits change;
    change.upload = 300;
    change.altDownload = 0;
    QVERIFY(!await(this, engine.setGlobalLimits(change)).isError());
    const Recorded &set = server.requests.at(1);
    QCOMPARE(set.target, QByteArrayLiteral("/api/v2/app/setPreferences"));
    const QJsonObject json = QJsonDocument::fromJson(field(set, "json").toUtf8()).object();
    QCOMPARE(json,
             (QJsonObject{{QStringLiteral("up_limit"), 300}, {QStringLiteral("alt_dl_limit"), 0}}));
}

void TestQbtEngine::errorsPassThrough()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(404, "Not Found", "Torrent hash was not found");
    });
    QbtEngine engine;
    engine.setBaseUrl(server.url());
    const auto properties = await(this, engine.properties(QStringLiteral("nope")));
    QVERIFY(properties.isError());
    QCOMPARE(properties.error().kind(), ApiError::Kind::NotFound);
    const auto renamed = await(this, engine.rename(QStringLiteral("nope"), QStringLiteral("x")));
    QVERIFY(renamed.isError());
    QCOMPARE(renamed.error().kind(), ApiError::Kind::NotFound);
}

void TestQbtEngine::abortingCancelsTheRequest()
{
    // A server that never answers: only the abort can settle the reply.
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost));
    QbtEngine engine;
    engine.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(silent.serverPort())));
    auto *reply = engine.sync(0);
    QTimer::singleShot(50, reply, [reply]() { reply->abort(); });
    const auto result = await(this, reply, 5000);
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Cancelled);
}

void TestQbtEngine::reachableUrlIsTheVersionEndpoint()
{
    QbtEngine engine;
    QCOMPARE(engine.reachableUrl(QUrl(QStringLiteral("https://nas.lan:8443"))),
             QUrl(QStringLiteral("https://nas.lan:8443/api/v2/app/version")));
    QCOMPARE(engine.reachableUrl(QUrl(QStringLiteral("http://host/qbt/"))),
             QUrl(QStringLiteral("http://host/qbt/api/v2/app/version")));
}

void TestQbtEngine::featuresFollowTheApiVersion()
{
    QbtEngine engine;
    const auto current =
        engine.features(ServerInfo{QStringLiteral("v5.2.3"), QStringLiteral("2.15.1")});
    QVERIFY(current.categories && current.tags && current.queue && current.engineLog);
    QVERIFY(current.qbtSettings && current.torrentsBackup && current.rss);
    QVERIFY(current.fetchMetadata);
    QVERIFY(current.shareLimitAction());
    const auto older =
        engine.features(ServerInfo{QStringLiteral("v5.0.0"), QStringLiteral("2.11.2")});
    QVERIFY(!older.fetchMetadata);
    QVERIFY(!older.shareLimitAction());
    QVERIFY(older.categories);
}

QTEST_GUILESS_MAIN(TestQbtEngine)
#include "tst_qbtengine.moc"
