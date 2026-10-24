// QbtClient's search calls (#145, DECISIONS C26) against a fake server: the paths, the verbs
// and the form bodies, the job id, and qBittorrent's 409 words kept. The bodies are the 5.3.0
// playground's, answering invented local plugins (scripts/dev/search-plugins.py).

#include "FakeHttp.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>
#include <QUrlQuery>

using namespace fakehttp;
using omaseed::core::QbtClient;

namespace {

QUrlQuery formOf(const Recorded &request)
{
    return QUrlQuery(QString::fromUtf8(request.body));
}

QString field(const Recorded &request, const QString &name)
{
    return formOf(request).queryItemValue(name, QUrl::FullyDecoded);
}

} // namespace

class TestQbtClientSearchFake : public QObject
{
    Q_OBJECT

private slots:
    void startSendsPatternCategoryAndPlugins();
    void startKeepsQBittorrentsConflictWords();
    void statusAndResults();
    void stopAndDeleteSendTheId();
    void pluginsAndTheirWrites();
    void fetchMetadataAndAddSendTheDownloader();
};

void TestQbtClientSearchFake::startSendsPatternCategoryAndPlugins()
{
    FakeServer server;
    server.setHandler(
        [](const Recorded &, int) { return httpResponse(200, "OK", R"({"id":1801977308})"); });
    QbtClient client;
    client.setBaseUrl(server.url());
    const auto started =
        await(this,
              client.searchStart(QStringLiteral("open movie & night"),
                                 QStringLiteral("all"),
                                 {QStringLiteral("plugin_a"), QStringLiteral("plugin_c")}));
    QVERIFY(!started.isError());
    QCOMPARE(started.value(), 1801977308);
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).method, QByteArrayLiteral("POST"));
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/search/start"));
    QCOMPARE(field(server.requests.at(0), QStringLiteral("pattern")),
             QStringLiteral("open movie & night"));
    QCOMPARE(field(server.requests.at(0), QStringLiteral("category")), QStringLiteral("all"));
    QCOMPARE(field(server.requests.at(0), QStringLiteral("plugins")),
             QStringLiteral("plugin_a|plugin_c"));
}

// SR13: no Python on the server's machine, and the five-search cap, both 409: the words say which.
void TestQbtClientSearchFake::startKeepsQBittorrentsConflictWords()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int index) {
        return index == 0
                   ? httpResponse(
                         409, "Conflict", "Python must be installed to use the Search Engine.")
                   : httpResponse(
                         409, "Conflict", "Unable to create more than 5 concurrent searches.");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    const auto noPython = await(
        this,
        client.searchStart(QStringLiteral("x"), QStringLiteral("all"), {QStringLiteral("a")}));
    QVERIFY(noPython.isError());
    QCOMPARE(noPython.error().kind(), ApiError::Kind::Conflict);
    QVERIFY(noPython.error().message().contains(QStringLiteral("Python must be installed")));
    const auto busy = await(
        this,
        client.searchStart(QStringLiteral("x"), QStringLiteral("all"), {QStringLiteral("a")}));
    QCOMPARE(busy.error().kind(), ApiError::Kind::Conflict);
    QVERIFY(busy.error().message().contains(QStringLiteral("concurrent")));
    // A POST is never sent twice.
    QCOMPARE(server.requests.size(), 2);
}

void TestQbtClientSearchFake::statusAndResults()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        if (request.target.startsWith("/api/v2/search/status"))
            return httpResponse(
                200,
                "OK",
                R"([{"category":"all","id":7,"pattern":"sampler","plugins":["plugin_a"],)"
                R"("status":"Stopped","total":2}])");
        return httpResponse(
            200,
            "OK",
            R"({"results":[{"descrLink":"http://plugin-a.invalid/t/3","engineName":"plugin_a",)"
            R"("fileName":"Public Domain Sampler","fileSize":25296896,)"
            R"("fileUrl":"magnet:?xt=urn:btih:f43d3b0a927b9662dbb640d24793a1726aaa4c61",)"
            R"("nbLeechers":3,"nbSeeders":12,"pubDate":1792070661,)"
            R"("siteUrl":"http://plugin-a.invalid"}],"status":"Stopped","total":2})");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    const auto status = await(this, client.searchStatus(7));
    QVERIFY(!status.isError());
    QCOMPARE(status.value().at(0).toObject().value(QStringLiteral("status")).toString(),
             QStringLiteral("Stopped"));
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/search/status?id=7"));

    const auto results = await(this, client.searchResults(7, 50, 1));
    QVERIFY(!results.isError());
    QCOMPARE(results.value().value(QStringLiteral("total")).toInt(), 2);
    QCOMPARE(results.value().value(QStringLiteral("results")).toArray().size(), 1);
    QCOMPARE(server.requests.at(1).method, QByteArrayLiteral("GET"));
    QCOMPARE(server.requests.at(1).target,
             QByteArrayLiteral("/api/v2/search/results?id=7&limit=50&offset=1"));

    // Every job, and the first page with no offset.
    await(this, client.searchStatus());
    QCOMPARE(server.requests.at(2).target, QByteArrayLiteral("/api/v2/search/status"));
    await(this, client.searchResults(7));
    QCOMPARE(server.requests.at(3).target, QByteArrayLiteral("/api/v2/search/results?id=7"));
}

void TestQbtClientSearchFake::stopAndDeleteSendTheId()
{
    FakeServer server;
    QbtClient client;
    client.setBaseUrl(server.url());
    QVERIFY(!await(this, client.searchStop(12)).isError());
    QVERIFY(!await(this, client.searchDelete(12)).isError());
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/search/stop"));
    QCOMPARE(field(server.requests.at(0), QStringLiteral("id")), QStringLiteral("12"));
    QCOMPARE(server.requests.at(1).target, QByteArrayLiteral("/api/v2/search/delete"));
    QCOMPARE(server.requests.at(1).method, QByteArrayLiteral("POST"));
}

void TestQbtClientSearchFake::pluginsAndTheirWrites()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        if (request.target == "/api/v2/search/plugins")
            return httpResponse(
                200,
                "OK",
                R"([{"enabled":true,"fullName":"plugin a","name":"plugin_a",)"
                R"("supportedCategories":[{"id":"all","name":"All categories"},)"
                R"({"id":"movies","name":"Movies"}],"url":"http://plugin-a.invalid","version":"1.4"}])");
        return httpResponse(200, "OK", "");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    const auto plugins = await(this, client.searchPlugins());
    QVERIFY(!plugins.isError());
    QCOMPARE(plugins.value().at(0).toObject().value(QStringLiteral("fullName")).toString(),
             QStringLiteral("plugin a"));

    await(this,
          client.searchInstallPlugin(
              {QStringLiteral("https://plugins.example/a.py"), QStringLiteral("/srv/b.py")}));
    await(this,
          client.searchUninstallPlugin({QStringLiteral("plugin_a"), QStringLiteral("plugin_b")}));
    await(this, client.searchEnablePlugin({QStringLiteral("plugin_c")}, false));
    await(this, client.searchUpdatePlugins());
    QCOMPARE(server.requests.at(1).target, QByteArrayLiteral("/api/v2/search/installPlugin"));
    QCOMPARE(field(server.requests.at(1), QStringLiteral("sources")),
             QStringLiteral("https://plugins.example/a.py|/srv/b.py"));
    QCOMPARE(server.requests.at(2).target, QByteArrayLiteral("/api/v2/search/uninstallPlugin"));
    QCOMPARE(field(server.requests.at(2), QStringLiteral("names")),
             QStringLiteral("plugin_a|plugin_b"));
    QCOMPARE(server.requests.at(3).target, QByteArrayLiteral("/api/v2/search/enablePlugin"));
    QCOMPARE(field(server.requests.at(3), QStringLiteral("names")), QStringLiteral("plugin_c"));
    QCOMPARE(field(server.requests.at(3), QStringLiteral("enable")), QStringLiteral("false"));
    QCOMPARE(server.requests.at(4).target, QByteArrayLiteral("/api/v2/search/updatePlugins"));
    QCOMPARE(server.requests.at(4).method, QByteArrayLiteral("POST"));
}

// SR9: a result's link to a .torrent is fetched through its plugin (Web API 2.13.1 `downloader`).
void TestQbtClientSearchFake::fetchMetadataAndAddSendTheDownloader()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        return request.target.endsWith("fetchMetadata") ? httpResponse(202, "Accepted", "{}")
                                                        : httpResponse(200, "OK", "Ok.");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    const QString link = QStringLiteral("http://plugin-a.invalid/get/1.torrent");
    await(this, client.torrentsFetchMetadata(link, QStringLiteral("plugin_a")));
    await(this, client.torrentsFetchMetadata(link));
    QbtClient::AddOptions options;
    options.downloader = QStringLiteral("plugin_a");
    await(this, client.torrentsAddUrls({link}, options));
    QCOMPARE(field(server.requests.at(0), QStringLiteral("downloader")),
             QStringLiteral("plugin_a"));
    QVERIFY(!formOf(server.requests.at(1)).hasQueryItem(QStringLiteral("downloader")));
    QCOMPARE(server.requests.at(2).target, QByteArrayLiteral("/api/v2/torrents/add"));
    QCOMPARE(field(server.requests.at(2), QStringLiteral("downloader")),
             QStringLiteral("plugin_a"));
    QCOMPARE(field(server.requests.at(2), QStringLiteral("urls")), link);
}

QTEST_GUILESS_MAIN(TestQbtClientSearchFake)
#include "tst_qbtclient_search_fake.moc"
