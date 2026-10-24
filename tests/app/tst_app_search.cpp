// #145 (board 48, DECISIONS C26): App's half of search, against a small fake qBittorrent with
// invented plugins. The plugins that are on are each asked on their own; the rows carry the
// library's marks; add… hands the folded magnet, or the link and its plugin, to the add sheet,
// whose lookup and add send `downloader`; the jobs go on quit.

#include "core/SearchModel.hpp"
#include "core/SearchPlugins.hpp"
#include "core/SearchSession.hpp"
#include "core/SyncEngine.hpp"
#include "ui/app/App.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <memory>

using omaseed::app::App;
using omaseed::core::SearchModel;
using omaseed::core::SearchSession;

namespace {

const char *kMine = "f43d3b0a927b9662dbb640d24793a1726aaa4c61";
const char *kOther = "1111111111111111111111111111111111111111";

struct Request
{
    QByteArray target;
    QUrlQuery form;
};

/// qBittorrent as far as search goes: two plugins on, one off; plugin a answers a magnet of a
/// torrent the library has, another magnet and a link to a .torrent.
class SearchServer : public QTcpServer
{
public:
    QList<Request> requests;
    int nextId = 40;

    QList<Request> all(const QByteArray &endpoint) const
    {
        QList<Request> found;
        for (const Request &request : requests)
            if (request.target.startsWith("/api/v2/" + endpoint))
                found.append(request);
        return found;
    }

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
            Request request;
            request.target = buffer->left(buffer->indexOf('\n')).split(' ').value(1);
            request.form = QUrlQuery(QString::fromUtf8(buffer->mid(headerEnd + 4, length)));
            buffer->clear();
            requests.append(request);
            socket->write(answer(request));
            socket->disconnectFromHost();
        });
    }

private:
    QByteArray answer(const Request &request)
    {
        QByteArray body = "Ok.";
        const QByteArray &target = request.target;
        if (target.startsWith("/api/v2/app/version")) {
            body = "v5.3.0";
        } else if (target.startsWith("/api/v2/app/webapiVersion")) {
            body = "2.16.2";
        } else if (target.startsWith("/api/v2/sync/maindata")) {
            const QJsonObject torrents{
                {QString::fromLatin1(kMine),
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("Public Domain Sampler")},
                             {QStringLiteral("state"), QStringLiteral("downloading")},
                             {QStringLiteral("progress"), 0.5},
                             {QStringLiteral("total_size"), 25209644},
                             {QStringLiteral("size"), 25209644},
                             {QStringLiteral("infohash_v1"), QString::fromLatin1(kMine)}}}};
            body = QJsonDocument(QJsonObject{{QStringLiteral("rid"), 1},
                                             {QStringLiteral("full_update"), true},
                                             {QStringLiteral("torrents"), torrents}})
                       .toJson(QJsonDocument::Compact);
        } else if (target.startsWith("/api/v2/search/plugins")) {
            const auto plugin = [](const QString &name, bool enabled) {
                return QJsonObject{
                    {QStringLiteral("name"), name},
                    {QStringLiteral("fullName"), QStringLiteral("plugin ") + name.right(1)},
                    {QStringLiteral("version"), QStringLiteral("1.0")},
                    {QStringLiteral("enabled"), enabled},
                    {QStringLiteral("supportedCategories"),
                     QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("all")},
                                            {QStringLiteral("name"), QStringLiteral("All")}}}}};
            };
            body = QJsonDocument(QJsonArray{plugin(QStringLiteral("p_a"), true),
                                            plugin(QStringLiteral("p_b"), false),
                                            plugin(QStringLiteral("p_c"), true)})
                       .toJson(QJsonDocument::Compact);
        } else if (target.startsWith("/api/v2/search/start")) {
            body = "{\"id\":" + QByteArray::number(nextId++) + "}";
        } else if (target.startsWith("/api/v2/search/results")) {
            const bool a = target.contains("id=40");
            QJsonArray results;
            if (a && !target.contains("offset=")) {
                const auto row = [](const QString &name, const QString &url, int seeds) {
                    return QJsonObject{{QStringLiteral("fileName"), name},
                                       {QStringLiteral("fileSize"), 25209644},
                                       {QStringLiteral("fileUrl"), url},
                                       {QStringLiteral("nbSeeders"), seeds},
                                       {QStringLiteral("nbLeechers"), 1},
                                       {QStringLiteral("pubDate"), 1000},
                                       {QStringLiteral("engineName"), QStringLiteral("p_a")}};
                };
                results = {
                    row(QStringLiteral("Public Domain Sampler"),
                        QStringLiteral("magnet:?xt=urn:btih:%1&tr=udp%3A%2F%2Fone.invalid%3A1")
                            .arg(QString::fromLatin1(kMine)),
                        12),
                    row(QStringLiteral("Another Sampler"),
                        QStringLiteral("magnet:?xt=urn:btih:%1&tr=udp%3A%2F%2Fone.invalid%3A1&tr="
                                       "udp%3A%2F%2Ftwo.invalid%3A2")
                            .arg(QString::fromLatin1(kOther)),
                        30),
                    row(QStringLiteral("Sampler extras"),
                        QStringLiteral("http://plugin-a.invalid/get/1.torrent"),
                        4)};
            }
            body = QJsonDocument(QJsonObject{{QStringLiteral("results"), results},
                                             {QStringLiteral("status"), QStringLiteral("Stopped")},
                                             {QStringLiteral("total"), a ? 3 : 0}})
                       .toJson(QJsonDocument::Compact);
        } else if (target.startsWith("/api/v2/log/main")) {
            body = "[]";
        } else if (target.startsWith("/api/v2/torrents/fetchMetadata")) {
            return "HTTP/1.1 202 Accepted\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
        } else if (target.startsWith("/api/v2/torrents/add")) {
            body =
                R"({"added_torrent_ids":[],"failure_count":0,"pending_count":1,"success_count":0})";
        }
        return "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
               "\r\nConnection: close\r\n\r\n" + body;
    }
};

} // namespace

class TestAppSearch : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void eachPluginThatIsOnIsAskedAndTheRowsAreMarked();
    void addHandsOverTheMagnetOrTheLinkAndItsPlugin();
    void theJobsGoOnQuit();
    void theDemoSearch();

private:
    bool startOn(App &app, SearchServer &server);
    bool searched(App &app);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppSearch::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppSearch::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

bool TestAppSearch::startOn(App &app, SearchServer &server)
{
    if (!server.listen(QHostAddress::LocalHost))
        return false;
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.setSearchTimings(30, 5000, 4);
    app.start();
    return QTest::qWaitFor([&app]() { return app.engine()->globalState()->connected(); }, 5000);
}

bool TestAppSearch::searched(App &app)
{
    app.refreshSearchPlugins();
    if (!QTest::qWaitFor([&app]() { return app.searchPlugins()->loaded(); }, 5000))
        return false;
    app.startSearch(QStringLiteral("  sampler  "));
    return QTest::qWaitFor([&app]() { return app.search()->state() == SearchSession::State::Done; },
                           5000);
}

void TestAppSearch::eachPluginThatIsOnIsAskedAndTheRowsAreMarked()
{
    SearchServer server;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(app.searchOffered());
    QVERIFY(app.searchServerLocal());
    QVERIFY(searched(app));
    const QList<Request> starts = server.all("search/start");
    QCOMPARE(starts.size(), 2); // p_b is off
    QCOMPARE(starts.at(0).form.queryItemValue(QStringLiteral("pattern")),
             QStringLiteral("sampler"));
    QCOMPARE(starts.at(0).form.queryItemValue(QStringLiteral("plugins")), QStringLiteral("p_a"));
    QCOMPARE(starts.at(1).form.queryItemValue(QStringLiteral("plugins")), QStringLiteral("p_c"));
    SearchModel *rows = app.searchResults();
    QCOMPARE(rows->rowCount(), 3);
    // Most seeds first; the library's torrent is marked and its add… is trackers….
    QCOMPARE(rows->data(rows->index(0), SearchModel::NameRole).toString(),
             QStringLiteral("Another Sampler"));
    QCOMPARE(rows->data(rows->index(1), SearchModel::MarkRole).toString(),
             QStringLiteral("library"));
    QCOMPARE(rows->data(rows->index(1), SearchModel::AddWordRole).toString(),
             QStringLiteral("trackers…"));
    QVERIFY(rows->data(rows->index(1), SearchModel::SentenceRole)
                .toString()
                .contains(QStringLiteral("arriving at 50")));
    QCOMPARE(rows->data(rows->index(0), SearchModel::PluginsRole).toString(),
             QStringLiteral("plugin a"));
    QCOMPARE(app.search()->answered(), 2);
}

// SR9: the folded magnet goes as it is; a link goes with its plugin as `downloader`.
void TestAppSearch::addHandsOverTheMagnetOrTheLinkAndItsPlugin()
{
    SearchServer server;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(searched(app));
    SearchModel *rows = app.searchResults();
    const QVariantMap magnet =
        app.searchAddable(rows->data(rows->index(0), SearchModel::KeyRole).toString());
    QVERIFY(magnet.value(QStringLiteral("text"))
                .toString()
                .startsWith(QStringLiteral("magnet:?xt=urn:btih:1111")));
    QVERIFY(magnet.value(QStringLiteral("downloader")).toString().isEmpty());
    QCOMPARE(magnet.value(QStringLiteral("fromLine")).toString(),
             QStringLiteral("from search · plugin a · a magnet with 2 trackers · 24.0 MB"));

    const QVariantMap link =
        app.searchAddable(rows->data(rows->index(2), SearchModel::KeyRole).toString());
    QCOMPARE(link.value(QStringLiteral("text")).toString(),
             QStringLiteral("http://plugin-a.invalid/get/1.torrent"));
    QCOMPARE(link.value(QStringLiteral("downloader")).toString(), QStringLiteral("p_a"));
    QVERIFY(link.value(QStringLiteral("fromLine"))
                .toString()
                .contains(QStringLiteral("a link to a .torrent")));

    QVariantMap addable = app.parseAddable(link.value(QStringLiteral("text")).toString());
    addable.insert(QStringLiteral("downloader"), link.value(QStringLiteral("downloader")));
    app.lookUpMetadata(addable);
    QTRY_VERIFY(!server.all("torrents/fetchMetadata").isEmpty());
    QCOMPARE(server.all("torrents/fetchMetadata")
                 .first()
                 .form.queryItemValue(QStringLiteral("downloader")),
             QStringLiteral("p_a"));
    app.stopMetadataLookup();

    QVariantMap request = addable;
    request.insert(QStringLiteral("stopped"), true);
    app.addTorrent(request);
    QTRY_VERIFY(!server.all("torrents/add").isEmpty());
    QCOMPARE(server.all("torrents/add").first().form.queryItemValue(QStringLiteral("downloader")),
             QStringLiteral("p_a"));

    app.searchAdded(link.value(QStringLiteral("key")).toString());
    QCOMPARE(rows->data(rows->index(2), SearchModel::MarkRole).toString(), QStringLiteral("added"));
}

// SR12: omaseed deletes the jobs it started when it quits.
void TestAppSearch::theJobsGoOnQuit()
{
    SearchServer server;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(searched(app));
    QVERIFY(server.all("search/delete").isEmpty());
    QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::DirectConnection);
    QCOMPARE(server.all("search/delete").size(), 2);
}

void TestAppSearch::theDemoSearch()
{
    App app;
    app.loadDemo(16);
    QVERIFY(app.searchOffered());
    app.loadDemoSearch(QStringLiteral("done"));
    QCOMPARE(app.search()->state(), SearchSession::State::Done);
    QCOMPARE(app.search()->didntAnswer(), 1);
    QCOMPARE(app.searchPlugins()->rowCount(), 3);
    QVERIFY(app.searchResults()->rowCount() >= 9);
    QCOMPARE(
        app.searchResults()->data(app.searchResults()->index(0), SearchModel::CopiesRole).toInt(),
        2);
    app.loadDemoSearch(QStringLiteral("none"));
    QCOMPARE(app.searchPlugins()->rowCount(), 0);
    QCOMPARE(app.searchResults()->rowCount(), 0);
    app.loadDemoSearch(QStringLiteral("nopython"));
    QCOMPARE(app.search()->stateName(), QStringLiteral("nopython"));
}

QTEST_MAIN(TestAppSearch)
#include "tst_app_search.moc"
