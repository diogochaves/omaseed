// #44 (docs/29): App's half of a magnet's files before the add, against a small fake
// qBittorrent. The lookup goes through App's own client (its session cookie rides along), the
// 200 becomes the files a parsed .torrent would give, the add carries the choices in one call,
// "add stopped" before the list uses the stop condition, a private tracker's magnet asks for
// nothing, an old server is left alone, and a replaced session is asked again.

#include "core/AddFilesModel.hpp"
#include "core/MetadataLookup.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"

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
using omaseed::core::MetadataLookup;

namespace {

const char *kHash = "1f85b9e8a0d259478ee98344b2b0816f899e59b8";
const char *kV1 = "f43d3b0a927b9662dbb640d24793a1726aaa4c61";
const char *kPrivateHash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

QString samplerMagnet(const QString &tracker = QStringLiteral("http://127.0.0.1:26996/announce"))
{
    return QStringLiteral("magnet:?xt=urn:btih:%1&dn=Public%20Domain%20Sampler&tr=%2")
        .arg(QString::fromLatin1(kV1), QString::fromLatin1(QUrl::toPercentEncoding(tracker)));
}

const char *kSampler = R"({"hash":"1f85b9e8a0d259478ee98344b2b0816f899e59b8",
 "infohash_v1":"f43d3b0a927b9662dbb640d24793a1726aaa4c61","infohash_v2":"",
 "info":{"files":[{"length":812,"path":"Public Domain Sampler/README.txt"},
                  {"length":25165824,"path":"Public Domain Sampler/sampler.mkv"},
                  {"length":43008,"path":"Public Domain Sampler/sampler.srt"}],
         "length":25209644,"name":"Public Domain Sampler","piece_length":65536,"private":false},
 "trackers":[{"tier":0,"url":"http://127.0.0.1:26996/announce"}],"webseeds":[]})";

struct Request
{
    QByteArray target;
    QUrlQuery form;
    QByteArray cookie;
};

/// qBittorrent as far as the add sheet's magnets go. Hands out a session cookie on the first
/// answer, and answers fetchMetadata 202 `lookingFor` times, then 200.
class MagnetServer : public QTcpServer
{
public:
    QString apiVersion = QStringLiteral("2.15.1");
    int lookingFor = 2;
    /// The next add that carries filePriorities answers 400, as a replaced session does.
    bool refuseNextPriorities = false;
    bool withPrivateTorrent = false;
    QList<Request> requests;

    int count(const QByteArray &endpoint) const
    {
        int n = 0;
        for (const Request &request : requests)
            n += request.target.startsWith("/api/v2/" + endpoint) ? 1 : 0;
        return n;
    }
    Request last(const QByteArray &endpoint) const
    {
        for (qsizetype i = requests.size() - 1; i >= 0; --i)
            if (requests.at(i).target.startsWith("/api/v2/" + endpoint))
                return requests.at(i);
        return {};
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
            QByteArray cookie;
            for (const QByteArray &line : buffer->left(headerEnd).split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toLongLong();
                if (line.toLower().startsWith("cookie:"))
                    cookie = line.mid(7).trimmed();
            }
            if (buffer->size() < headerEnd + 4 + length)
                return;
            Request request;
            request.target = buffer->left(buffer->indexOf('\n')).split(' ').value(1);
            request.form = QUrlQuery(QString::fromUtf8(buffer->mid(headerEnd + 4, length)));
            request.cookie = cookie;
            buffer->clear();
            requests.append(request);

            QByteArray status = "200 OK";
            QByteArray body = "Ok.";
            const QByteArray &target = request.target;
            if (target.startsWith("/api/v2/app/version")) {
                body = "v5.2.3";
            } else if (target.startsWith("/api/v2/app/webapiVersion")) {
                body = apiVersion.toUtf8();
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                QJsonObject torrents;
                QJsonObject trackers;
                if (withPrivateTorrent) {
                    torrents.insert(
                        QString::fromLatin1(kPrivateHash),
                        QJsonObject{{QStringLiteral("name"), QStringLiteral("p")},
                                    {QStringLiteral("state"), QStringLiteral("stalledUP")},
                                    {QStringLiteral("private"), true},
                                    {QStringLiteral("tracker"), QString()}});
                    trackers.insert(QStringLiteral("http://127.0.0.1:26996/announce"),
                                    QJsonArray{QString::fromLatin1(kPrivateHash)});
                }
                body = QJsonDocument(QJsonObject{{QStringLiteral("rid"), 1},
                                                 {QStringLiteral("full_update"), true},
                                                 {QStringLiteral("torrents"), torrents},
                                                 {QStringLiteral("trackers"), trackers}})
                           .toJson(QJsonDocument::Compact);
            } else if (target.startsWith("/api/v2/torrents/fetchMetadata")) {
                if (lookingFor > 0) {
                    --lookingFor;
                    status = "202 Accepted";
                    body = R"({"hash":"1f85b9e8a0d259478ee98344b2b0816f899e59b8"})";
                } else {
                    body = kSampler;
                }
            } else if (target.startsWith("/api/v2/torrents/add")) {
                if (refuseNextPriorities &&
                    request.form.hasQueryItem(QStringLiteral("filePriorities"))) {
                    refuseNextPriorities = false;
                    status = "400 Bad Request";
                    body = "`filePriorities` may only be specified when metadata has already been "
                           "fetched";
                } else {
                    body = R"({"added_torrent_ids":["1f85b9e8a0d259478ee98344b2b0816f899e59b8"],)"
                           R"("failure_count":0,"pending_count":0,"success_count":1})";
                }
            } else if (target.startsWith("/api/v2/torrents/files")) {
                body = R"([{"index":0,"name":"Public Domain Sampler/README.txt","size":812},)"
                       R"({"index":1,"name":"Public Domain Sampler/sampler.mkv","size":25165824},)"
                       R"({"index":2,"name":"Public Domain Sampler/sampler.srt","size":43008}])";
            }
            QByteArray response = "HTTP/1.1 " + status +
                                  "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
            if (requests.size() == 1)
                response += "Set-Cookie: QBT_SID_8081=session-one; path=/\r\n";
            response += "Connection: close\r\n\r\n" + body;
            socket->write(response);
            socket->disconnectFromHost();
        });
    }
};

} // namespace

class TestAppMagnet : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aMagnetsFilesArriveAsATorrentsWould();
    void theChoicesTravelWithTheMagnet();
    void addStoppedBeforeTheListStopsOnceItIsIn();
    void anUntouchedListIsAPlainAdd();
    void renamesAreReplayedAfterTheAdd();
    void aPrivateTrackersMagnetIsNotLookedUp();
    void anOldServerKeepsTodaysSheet();
    void aReplacedSessionIsAskedAgain();

private:
    bool startOn(App &app, MagnetServer &server);
    /// Looks the Sampler's magnet up and waits for its 200; the addable map with its token.
    QVariantMap fetched(App &app);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppMagnet::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppMagnet::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

bool TestAppMagnet::startOn(App &app, MagnetServer &server)
{
    if (!server.listen(QHostAddress::LocalHost))
        return false;
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    return QTest::qWaitFor([&app]() { return app.engine()->globalState()->connected(); }, 5000);
}

QVariantMap TestAppMagnet::fetched(App &app)
{
    QSignalSpy spy(&app, &App::metadataFetched);
    app.lookUpMetadata(app.parseAddable(samplerMagnet()));
    if (!spy.wait(5000) && spy.isEmpty())
        return {};
    return spy.first().first().toMap();
}

// F2: the 200's files, root stripped, as the add sheet's model takes a .torrent's; every poll
// in the session the first answer opened.
void TestAppMagnet::aMagnetsFilesArriveAsATorrentsWould()
{
    MagnetServer server;
    App app;
    QVERIFY(startOn(app, server));
    const QVariantMap map = fetched(app);
    QVERIFY(!map.isEmpty());
    QCOMPARE(app.addLookup()->phase(), MetadataLookup::Phase::Done);
    QCOMPARE(server.count("torrents/fetchMetadata"), 3); // 202, 202, 200
    for (const Request &request : server.requests) {
        if (request.target.startsWith("/api/v2/torrents/fetchMetadata")) {
            QCOMPARE(request.cookie, QByteArrayLiteral("QBT_SID_8081=session-one"));
            QCOMPARE(request.form.queryItemValue(QStringLiteral("source"), QUrl::FullyDecoded),
                     samplerMagnet());
        }
    }
    QCOMPARE(map.value(QStringLiteral("kind")).toString(), QStringLiteral("magnet"));
    QCOMPARE(map.value(QStringLiteral("value")).toString(), samplerMagnet());
    QCOMPARE(map.value(QStringLiteral("fileCount")).toInt(), 3);
    QCOMPARE(map.value(QStringLiteral("totalSize")).toLongLong(), 25209644);
    QCOMPARE(map.value(QStringLiteral("sizeText")).toString(), QStringLiteral("24.0 MB"));
    QCOMPARE(map.value(QStringLiteral("name")).toString(), QStringLiteral("Public Domain Sampler"));
    const QString token = map.value(QStringLiteral("token")).toString();
    QVERIFY(token.startsWith(QStringLiteral("meta-")));

    app.loadAddFiles(token);
    QCOMPARE(app.addFiles()->fileCount(), 3);
    QCOMPARE(app.addFiles()->pathOf(app.addFiles()->rowOfPath(QStringLiteral("sampler.mkv"))),
             QStringLiteral("sampler.mkv"));

    // The same magnet again (the field read once more): the same token, nothing asked.
    QSignalSpy again(&app, &App::metadataFetched);
    app.lookUpMetadata(app.parseAddable(samplerMagnet()));
    QVERIFY(again.wait(2000));
    QCOMPARE(again.first().first().toMap().value(QStringLiteral("token")).toString(), token);
    QCOMPARE(server.count("torrents/fetchMetadata"), 3);
    app.engine()->stop();
}

// docs/29 §2: one add with urls and filePriorities, no stop, filePrio, start.
void TestAppMagnet::theChoicesTravelWithTheMagnet()
{
    MagnetServer server;
    server.lookingFor = 0;
    App app;
    QVERIFY(startOn(app, server));
    QVariantMap request = fetched(app);
    request[QStringLiteral("filePriorities")] = QVariantList{1, 0, 1};
    request[QStringLiteral("stopped")] = false;
    QSignalSpy finished(&app, &App::addFinished);
    app.addTorrent(request);
    QVERIFY(finished.wait(5000));
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
    QCOMPARE(finished.first().at(2).toString(), QString::fromLatin1(kHash));
    QCOMPARE(server.count("torrents/add"), 1);
    const Request add = server.last("torrents/add");
    QCOMPARE(add.form.queryItemValue(QStringLiteral("urls"), QUrl::FullyDecoded), samplerMagnet());
    QCOMPARE(add.form.queryItemValue(QStringLiteral("filePriorities"), QUrl::FullyDecoded),
             QStringLiteral("1,0,1"));
    QVERIFY(!add.form.hasQueryItem(QStringLiteral("stopped")));
    QCOMPARE(add.cookie, QByteArrayLiteral("QBT_SID_8081=session-one"));
    QCOMPARE(server.count("torrents/filePrio"), 0);
    QCOMPARE(server.count("torrents/start"), 0);
    app.engine()->stop();
}

// D5 a: "add stopped" before the list: started, with the stop condition, so it stops as soon
// as its list arrives.
void TestAppMagnet::addStoppedBeforeTheListStopsOnceItIsIn()
{
    MagnetServer server;
    App app;
    QVERIFY(startOn(app, server));
    QVariantMap request = app.parseAddable(samplerMagnet());
    request[QStringLiteral("stopped")] = true;
    request[QStringLiteral("untilMetadata")] = true;
    QSignalSpy finished(&app, &App::addFinished);
    app.addTorrent(request);
    QVERIFY(finished.wait(5000));
    QVERIFY(finished.first().at(0).toBool());
    const Request add = server.last("torrents/add");
    QCOMPARE(add.form.queryItemValue(QStringLiteral("stopped")), QStringLiteral("false"));
    QCOMPARE(add.form.queryItemValue(QStringLiteral("stopCondition")),
             QStringLiteral("MetadataReceived"));
    QVERIFY(!add.form.hasQueryItem(QStringLiteral("filePriorities")));

    // Without the lookup (an old server, D11) it is today's plain stopped add.
    request[QStringLiteral("untilMetadata")] = false;
    app.addTorrent(request);
    QVERIFY(finished.wait(5000));
    const Request plain = server.last("torrents/add");
    QCOMPARE(plain.form.queryItemValue(QStringLiteral("stopped")), QStringLiteral("true"));
    QVERIFY(!plain.form.hasQueryItem(QStringLiteral("stopCondition")));
    app.engine()->stop();
}

// Nothing changed in the tree: a plain add, as today.
void TestAppMagnet::anUntouchedListIsAPlainAdd()
{
    MagnetServer server;
    server.lookingFor = 0;
    App app;
    QVERIFY(startOn(app, server));
    QVariantMap request = fetched(app);
    request[QStringLiteral("filePriorities")] = QVariantList{};
    QSignalSpy finished(&app, &App::addFinished);
    app.addTorrent(request);
    QVERIFY(finished.wait(5000));
    QVERIFY(finished.first().at(0).toBool());
    QVERIFY(!server.last("torrents/add").form.hasQueryItem(QStringLiteral("filePriorities")));
    app.engine()->stop();
}

// D9 a (M18): a magnet's renames go on after the add, as a .torrent's do: added stopped with
// its choices, renamed under its root, then started.
void TestAppMagnet::renamesAreReplayedAfterTheAdd()
{
    MagnetServer server;
    server.lookingFor = 0;
    App app;
    QVERIFY(startOn(app, server));
    QVariantMap request = fetched(app);
    app.loadAddFiles(request.value(QStringLiteral("token")).toString());
    QVERIFY(app.addFiles()->rename(app.addFiles()->rowOfPath(QStringLiteral("README.txt")),
                                   QStringLiteral("READ ME.txt")));
    request[QStringLiteral("filePriorities")] = QVariantList{1, 0, 1};
    request[QStringLiteral("fileRenames")] = app.addFiles()->renameList();
    request[QStringLiteral("renameAnchor")] = app.addFiles()->renameAnchor();
    request[QStringLiteral("stopped")] = false;
    QSignalSpy finished(&app, &App::addFinished);
    app.addTorrent(request);
    QVERIFY(finished.wait(5000));
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
    const Request add = server.last("torrents/add");
    QCOMPARE(add.form.queryItemValue(QStringLiteral("stopped")), QStringLiteral("true"));
    QCOMPARE(add.form.queryItemValue(QStringLiteral("filePriorities"), QUrl::FullyDecoded),
             QStringLiteral("1,0,1"));
    const Request rename = server.last("torrents/renameFile");
    QCOMPARE(rename.form.queryItemValue(QStringLiteral("oldPath"), QUrl::FullyDecoded),
             QStringLiteral("Public Domain Sampler/README.txt"));
    QCOMPARE(rename.form.queryItemValue(QStringLiteral("newPath"), QUrl::FullyDecoded),
             QStringLiteral("Public Domain Sampler/READ ME.txt"));
    QCOMPARE(server.count("torrents/start"), 1);
    QCOMPARE(server.count("torrents/filePrio"), 0);
    app.engine()->stop();
}

// D7 a: the magnet's tracker is one the library's private torrents announce to.
void TestAppMagnet::aPrivateTrackersMagnetIsNotLookedUp()
{
    MagnetServer server;
    server.withPrivateTorrent = true;
    App app;
    QVERIFY(startOn(app, server));
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 1, 5000);
    app.lookUpMetadata(app.parseAddable(samplerMagnet()));
    QCOMPARE(app.addLookup()->phase(), MetadataLookup::Phase::Private);
    QCOMPARE(app.addLookup()->privateHost(), QStringLiteral("127.0.0.1"));
    QTest::qWait(300);
    QCOMPARE(server.count("torrents/fetchMetadata"), 0);

    // A magnet of another tracker is looked up as usual.
    app.lookUpMetadata(
        app.parseAddable(samplerMagnet(QStringLiteral("udp://open.example:1337/announce"))));
    QVERIFY(app.addLookup()->isActive());
    QTRY_VERIFY_WITH_TIMEOUT(server.count("torrents/fetchMetadata") > 0, 3000);
    app.stopMetadataLookup();
    QCOMPARE(app.addLookup()->phase(), MetadataLookup::Phase::Idle);
    app.engine()->stop();
}

// D11 a: below Web API 2.11.9 the sheet is today's; nothing is asked.
void TestAppMagnet::anOldServerKeepsTodaysSheet()
{
    MagnetServer server;
    server.apiVersion = QStringLiteral("2.11.4");
    App app;
    QVERIFY(startOn(app, server));
    QTRY_VERIFY_WITH_TIMEOUT(!app.engine()->globalState()->apiVersion().isEmpty(), 5000);
    app.lookUpMetadata(app.parseAddable(samplerMagnet()));
    QCOMPARE(app.addLookup()->phase(), MetadataLookup::Phase::Unsupported);
    QTest::qWait(300);
    QCOMPARE(server.count("torrents/fetchMetadata"), 0);
    app.engine()->stop();
}

// docs/29 §6, "Session swapped": the add's priorities refused with 400; the metadata is asked
// for once more and the add goes again with them.
void TestAppMagnet::aReplacedSessionIsAskedAgain()
{
    MagnetServer server;
    server.lookingFor = 0;
    App app;
    QVERIFY(startOn(app, server));
    QVariantMap request = fetched(app);
    request[QStringLiteral("filePriorities")] = QVariantList{1, 0, 1};
    server.refuseNextPriorities = true;
    server.lookingFor = 1;
    const int asked = server.count("torrents/fetchMetadata");
    QSignalSpy finished(&app, &App::addFinished);
    app.addTorrent(request);
    QVERIFY(finished.wait(8000));
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
    QCOMPARE(server.count("torrents/add"), 2);
    QCOMPARE(server.count("torrents/fetchMetadata"), asked + 2); // a 202, then the 200
    QCOMPARE(server.last("torrents/add")
                 .form.queryItemValue(QStringLiteral("filePriorities"), QUrl::FullyDecoded),
             QStringLiteral("1,0,1"));
    app.engine()->stop();
}

QTEST_MAIN(TestAppMagnet)
#include "tst_app_magnet.moc"
