// #146 (board 49, DECISIONS C27): App's half of creating a torrent, against a small fake
// qBittorrent. The sheet's lists from the library (the passkey only as dots), the folder's
// check through the server, a public creation that ends in the strip with its magnet copied, a
// private one saved as "NAME [HOST].torrent" with its made-here record, the band after its
// delay and a cancel, a task taken up again by a new App, and nothing of a passkey in state.json
// or the band. Trackers, keys, names and folders are invented.

#include "core/ActionOutcome.hpp"
#include "core/Bencode.hpp"
#include "core/MadeHere.hpp"
#include "core/StateFile.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"

#include <QClipboard>
#include <QGuiApplication>
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
namespace bencode = omaseed::core::bencode;

namespace {

const QString kKey = QStringLiteral("k3y5SECRETk3y5");
const QString kPrivateAnnounce = QStringLiteral("https://tracker-a.example/announce/") + kKey;

QByteArray torrentBytes(const QString &name, const QString &announce, bool isPrivate)
{
    bencode::Dict info{{"length", qint64(3)},
                       {"name", name.toUtf8()},
                       {"piece length", qint64(16384)},
                       {"pieces", QByteArray(20, 'a')}};
    if (isPrivate) {
        info.append({"private", qint64(1)});
        info.append({"source", QByteArrayLiteral("TA")});
    }
    bencode::Dict top{{"announce", announce.toUtf8()},
                      {"created by", QByteArrayLiteral("qBittorrent v5.3.0")},
                      {"info", bencode::Value(info)}};
    return bencode::encode(bencode::Value(top));
}

struct Request
{
    QByteArray target;
    QByteArray body;
};

/// qBittorrent as far as creating goes: a library of three torrents (two public, one private),
/// the creator's four calls, the add, the folder's list and torrents/export.
class CreateServer : public QTcpServer
{
public:
    QString apiVersion = QStringLiteral("2.16.2");
    /// How many status polls answer Running before Finished; -1 runs for ever.
    int runningPolls = 1;
    /// The .torrent the task makes.
    QByteArray made;
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
            for (const QByteArray &line : buffer->left(headerEnd).split('\n'))
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toLongLong();
            if (buffer->size() < headerEnd + 4 + length)
                return;
            Request request;
            request.target = buffer->left(buffer->indexOf('\n')).split(' ').value(1);
            request.body = buffer->mid(headerEnd + 4, length);
            buffer->clear();
            requests.append(request);

            QByteArray status = "200 OK";
            QByteArray body = "Ok.";
            const QByteArray &target = request.target;
            if (target.startsWith("/api/v2/app/version")) {
                body = "v5.3.0";
            } else if (target.startsWith("/api/v2/app/webapiVersion")) {
                body = apiVersion.toUtf8();
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                const auto torrent = [](const QString &name, bool isPrivate, qint64 added) {
                    return QJsonObject{
                        {QStringLiteral("name"), name},
                        {QStringLiteral("state"), QStringLiteral("stalledUP")},
                        {QStringLiteral("private"), isPrivate},
                        {QStringLiteral("added_on"), double(added)},
                        {QStringLiteral("save_path"), QStringLiteral("/mnt/media/library")}};
                };
                const QJsonObject torrents{
                    {QString(40, QLatin1Char('1')), torrent(QStringLiteral("one"), false, 100)},
                    {QString(40, QLatin1Char('2')), torrent(QStringLiteral("two"), false, 200)},
                    {QString(40, QLatin1Char('3')), torrent(QStringLiteral("rec"), true, 300)}};
                const QJsonObject trackers{
                    {QStringLiteral("udp://open.tracker-c.example:6969/announce"),
                     QJsonArray{QString(40, QLatin1Char('1')), QString(40, QLatin1Char('2'))}},
                    {QStringLiteral("https://tracker-d.example/announce"),
                     QJsonArray{QString(40, QLatin1Char('2'))}},
                    {kPrivateAnnounce, QJsonArray{QString(40, QLatin1Char('3'))}}};
                body = QJsonDocument(QJsonObject{{QStringLiteral("rid"), 1},
                                                 {QStringLiteral("full_update"), true},
                                                 {QStringLiteral("torrents"), torrents},
                                                 {QStringLiteral("trackers"), trackers}})
                           .toJson(QJsonDocument::Compact);
            } else if (target.startsWith("/api/v2/torrentcreator/addTask")) {
                body = R"({"taskID":"t1"})";
            } else if (target.startsWith("/api/v2/torrentcreator/status")) {
                const bool running = runningPolls != 0;
                if (runningPolls > 0)
                    --runningPolls;
                body = running ? R"([{"taskID":"t1","status":"Running","progress":42}])"
                               : R"([{"taskID":"t1","status":"Finished","progress":100,)"
                                 R"("pieceSize":16384}])";
            } else if (target.startsWith("/api/v2/torrentcreator/torrentFile")) {
                body = made;
            } else if (target.startsWith("/api/v2/torrents/add")) {
                body = R"({"added_torrent_ids":["x"],"failure_count":0,"pending_count":0,)"
                       R"("success_count":1})";
            } else if (target.startsWith("/api/v2/app/getDirectoryContent")) {
                const QString dir =
                    QUrlQuery(QUrl(QString::fromUtf8(target)).query())
                        .queryItemValue(QStringLiteral("dirPath"), QUrl::FullyDecoded);
                if (dir == QLatin1String("/srv/share/photo-walks")) {
                    body = R"(["/srv/share/photo-walks/a.jpg","/srv/share/photo-walks/b.jpg"])";
                } else if (dir == QLatin1String("/srv/share")) {
                    body = R"(["/srv/share/photo-walks","/srv/share/talk.mkv"])";
                } else {
                    status = "404 Not Found";
                    body = "Directory does not exist";
                }
            } else if (target.startsWith("/api/v2/torrents/export")) {
                body = torrentBytes(QStringLiteral("rec"), kPrivateAnnounce, true);
            }
            QByteArray response = "HTTP/1.1 " + status +
                                  "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
            response += "Connection: close\r\n\r\n" + body;
            socket->write(response);
            socket->disconnectFromHost();
        });
    }
};

} // namespace

class TestAppCreate : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void offeredFromWebApi2104();
    void theSheetsListsFromTheLibrary();
    void theFolderIsCheckedThroughTheServer();
    void aQuickPublicOneIsTheStrip();
    void aPrivateOneIsSavedAndRemembered();
    void theBandAndACancel();
    void aRunningTaskIsTakenUpByTheNextRun();

private:
    bool startOn(App &app, CreateServer &server);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppCreate::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppCreate::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

bool TestAppCreate::startOn(App &app, CreateServer &server)
{
    if (!server.isListening() && !server.listen(QHostAddress::LocalHost))
        return false;
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.setDownloadsFolder(m_home->filePath(QStringLiteral("Downloads")));
    app.setCreateTimings(10, 60000);
    app.start();
    return QTest::qWaitFor(
        [&app]() { return app.engine()->rid() > 0 && app.torrents()->count() == 3; }, 5000);
}

void TestAppCreate::offeredFromWebApi2104()
{
    CreateServer old;
    old.apiVersion = QStringLiteral("2.10.3");
    App app;
    QVERIFY(startOn(app, old));
    QVERIFY(!app.createOffered());
    QVERIFY(!app.startCreate({{QStringLiteral("folder"), QStringLiteral("/srv/share/x")}}));
    app.engine()->stop();

    CreateServer server;
    App fresh;
    QVERIFY(startOn(fresh, server));
    QVERIFY(fresh.createOffered());
    fresh.engine()->stop();
}

// CR4, CR5: from the library; the passkey only as dots.
void TestAppCreate::theSheetsListsFromTheLibrary()
{
    CreateServer server;
    App app;
    QVERIFY(startOn(app, server));
    const QVariantMap sheet = app.createSheet();
    const QVariantList top = sheet.value(QStringLiteral("publicTrackers")).toList();
    QCOMPARE(top.size(), 2);
    QCOMPARE(top.first().toMap().value(QStringLiteral("url")).toString(),
             QStringLiteral("udp://open.tracker-c.example:6969/announce"));
    QCOMPARE(top.first().toMap().value(QStringLiteral("count")).toInt(), 2);
    const QVariantList hosts = sheet.value(QStringLiteral("privateHosts")).toList();
    QCOMPARE(hosts.size(), 1);
    const QVariantMap host = hosts.first().toMap();
    QCOMPARE(host.value(QStringLiteral("host")).toString(), QStringLiteral("tracker-a.example"));
    QCOMPARE(host.value(QStringLiteral("newestName")).toString(), QStringLiteral("rec"));
    QCOMPARE(host.value(QStringLiteral("masked")).toString(),
             QStringLiteral("https://tracker-a.example/announce/") + QString(8, QChar(0x2022)));
    QVERIFY(!QJsonDocument(QJsonObject::fromVariantMap(sheet)).toJson().contains(kKey.toUtf8()));
    QCOMPARE(sheet.value(QStringLiteral("forWhom")).toString(), QStringLiteral("anyone"));

    // CR6: the newest torrent's source tag, read from its .torrent.
    QSignalSpy source(&app, &App::createSourceLoaded);
    app.loadCreateSource(QStringLiteral("tracker-a.example"));
    QVERIFY(source.wait(5000));
    QCOMPARE(source.first().at(1).toString(), QStringLiteral("TA"));
    app.engine()->stop();
}

// CR17 on a server whose disks this machine can't see: its own list, a file told by its
// parent's, a missing folder.
void TestAppCreate::theFolderIsCheckedThroughTheServer()
{
    CreateServer server;
    App app;
    QVERIFY(startOn(app, server));
    QSignalSpy checked(&app, &App::createFolderChecked);
    app.checkCreateFolder(QStringLiteral("/srv/share/photo-walks"), true);
    QVERIFY(checked.wait(5000));
    QVariantMap check = checked.takeFirst().at(1).toMap();
    QCOMPARE(check.value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
    QVERIFY(!check.value(QStringLiteral("sized")).toBool());
    QCOMPARE(check.value(QStringLiteral("files")).toInt(), 2);
    QVERIFY(!check.value(QStringLiteral("blocking")).toBool());
    QVERIFY(check.value(QStringLiteral("line")).toString().startsWith(QStringLiteral("2 names")));

    app.checkCreateFolder(QStringLiteral("/srv/share/talk.mkv"), true);
    QVERIFY(checked.wait(5000));
    check = checked.takeFirst().at(1).toMap();
    QCOMPARE(check.value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
    QVERIFY(check.value(QStringLiteral("isFile")).toBool());

    app.checkCreateFolder(QStringLiteral("/srv/share/gone"), true);
    QVERIFY(checked.wait(5000));
    check = checked.takeFirst().at(1).toMap();
    QCOMPARE(check.value(QStringLiteral("state")).toString(), QStringLiteral("missing"));
    QVERIFY(check.value(QStringLiteral("blocking")).toBool());
    QVERIFY(check.value(QStringLiteral("line")).toString().contains(QStringLiteral("isn't there")));
    app.engine()->stop();
}

// CR12–CR14 and C16 L6: under the band's delay, the outcome strip; the magnet on the
// clipboard; the choices kept for next time.
void TestAppCreate::aQuickPublicOneIsTheStrip()
{
    CreateServer server;
    server.made = torrentBytes(QStringLiteral("photo-walks"),
                               QStringLiteral("udp://open.tracker-c.example:6969/announce"),
                               false);
    App app;
    QVERIFY(startOn(app, server));
    QSignalSpy changed(&app, &App::createChanged);
    QVERIFY(app.startCreate(
        {{QStringLiteral("folder"), QStringLiteral("/srv/share/photo-walks")},
         {QStringLiteral("forWhom"), QStringLiteral("anyone")},
         {QStringLiteral("trackers"),
          QStringList{QStringLiteral("udp://open.tracker-c.example:6969/announce")}},
         {QStringLiteral("format"), QStringLiteral("v1")},
         {QStringLiteral("category"), QStringLiteral("photos")}}));
    QCOMPARE(app.createRunning(), QStringLiteral("photo-walks"));
    // One at a time.
    QVERIFY(!app.startCreate({{QStringLiteral("folder"), QStringLiteral("/srv/share/x")}}));
    QTRY_VERIFY_WITH_TIMEOUT(app.createRunning().isEmpty(), 5000);
    QVERIFY(!app.createShown());
    const QVariantMap strip = app.outcomes()->current();
    QCOMPARE(strip.value(QStringLiteral("kind")).toString(), QStringLiteral("done"));
    QCOMPARE(strip.value(QStringLiteral("headline")).toString(),
             QStringLiteral("photo-walks is made and seeds from /srv/share"));
    const QString magnet = QGuiApplication::clipboard()->text();
    QVERIFY(magnet.startsWith(QStringLiteral("magnet:?xt=urn:btih:")));
    // The add: seed mode at the folder's parent.
    const QByteArray add = server.last("torrents/add").body;
    QVERIFY(add.contains("name=\"savepath\"\r\n\r\n/srv/share\r\n"));
    QVERIFY(add.contains("name=\"seedMode\"\r\n\r\ntrue\r\n"));
    QVERIFY(add.contains("name=\"category\"\r\n\r\nphotos\r\n"));
    QCOMPARE(server.count("torrentcreator/deleteTask"), 1);
    // Next time, last time's trackers.
    QCOMPARE(app.createSheet().value(QStringLiteral("lastTrackers")).toStringList(),
             QStringList{QStringLiteral("udp://open.tracker-c.example:6969/announce")});
    // Nothing left to take up.
    QVERIFY(!omaseed::core::StateFile::readSection(QStringLiteral("create"))
                 .value(QStringLiteral("default"))
                 .toObject()
                 .contains(QStringLiteral("task")));
    app.engine()->stop();
}

// CR5–CR7, CR15: never added, saved as "NAME [HOST].torrent", remembered without the passkey.
void TestAppCreate::aPrivateOneIsSavedAndRemembered()
{
    CreateServer server;
    server.made = torrentBytes(QStringLiteral("field-recordings"), kPrivateAnnounce, true);
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(
        app.startCreate({{QStringLiteral("folder"), QStringLiteral("/srv/share/field-recordings")},
                         {QStringLiteral("forWhom"), QStringLiteral("private")},
                         {QStringLiteral("host"), QStringLiteral("tracker-a.example")},
                         {QStringLiteral("source"), QStringLiteral("TA")},
                         {QStringLiteral("format"), QStringLiteral("v1")}}));
    QTRY_VERIFY_WITH_TIMEOUT(app.createRunning().isEmpty(), 5000);
    // The announce went to addTask, from the library, and nowhere else.
    const QUrlQuery form(QString::fromUtf8(server.last("torrentcreator/addTask").body));
    QCOMPARE(form.queryItemValue(QStringLiteral("trackers"), QUrl::FullyDecoded), kPrivateAnnounce);
    QCOMPARE(form.queryItemValue(QStringLiteral("private")), QStringLiteral("true"));
    QCOMPARE(form.queryItemValue(QStringLiteral("source")), QStringLiteral("TA"));
    QCOMPARE(server.count("torrents/add"), 0);

    QVERIFY(app.createShown()); // the private summary is a band: it carries the file
    const QVariantMap band = app.createBand();
    QCOMPARE(band.value(QStringLiteral("kind")).toString(), QStringLiteral("green"));
    QCOMPARE(band.value(QStringLiteral("headline")).toString(),
             QStringLiteral("field-recordings [tracker-a.example].torrent is ready to upload"));
    const QString saved =
        band.value(QStringLiteral("file")).toMap().value(QStringLiteral("path")).toString();
    QCOMPARE(
        saved,
        m_home->filePath(QStringLiteral("Downloads/field-recordings [tracker-a.example].torrent")));
    QFile file(saved);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), server.made);
    QVERIFY(!QJsonDocument(QJsonObject::fromVariantMap(band)).toJson().contains(kKey.toUtf8()));

    // The made-here record, and no passkey anywhere in state.json.
    QFile state(omaseed::core::StateFile::path());
    QVERIFY(state.open(QIODevice::ReadOnly));
    const QByteArray stateBytes = state.readAll();
    QVERIFY(!stateBytes.contains(kKey.toUtf8()));
    QVERIFY(!stateBytes.contains("tracker-a.example/announce"));
    const QJsonObject section =
        omaseed::core::StateFile::readSection(QStringLiteral("create")).begin()->toObject();
    const auto made =
        omaseed::core::MadeHere::fromJson(section.value(QStringLiteral("made")).toArray());
    QCOMPARE(made.records().size(), 1);
    QCOMPARE(made.records().first().folder, QStringLiteral("/srv/share/field-recordings"));
    QCOMPARE(made.records().first().host, QStringLiteral("tracker-a.example"));
    QCOMPARE(made.records().first().digest, omaseed::core::MadeHere::digestOf(server.made));
    QCOMPARE(section.value(QStringLiteral("for")).toString(), QStringLiteral("private"));

    // Made again: "NAME [HOST] (2).torrent".
    QVERIFY(
        app.startCreate({{QStringLiteral("folder"), QStringLiteral("/srv/share/field-recordings")},
                         {QStringLiteral("forWhom"), QStringLiteral("private")},
                         {QStringLiteral("host"), QStringLiteral("tracker-a.example")}}));
    QTRY_VERIFY_WITH_TIMEOUT(app.createRunning().isEmpty(), 5000);
    QVERIFY(app.createBand()
                .value(QStringLiteral("file"))
                .toMap()
                .value(QStringLiteral("name"))
                .toString()
                .endsWith(QStringLiteral("[tracker-a.example] (2).torrent")));
    app.engine()->stop();
}

// CR13: the band after its delay while it hashes; a cancel is the dim strip.
void TestAppCreate::theBandAndACancel()
{
    CreateServer server;
    server.runningPolls = -1;
    App app;
    QVERIFY(startOn(app, server));
    app.setCreateTimings(10, 50);
    QVERIFY(app.startCreate({{QStringLiteral("folder"), QStringLiteral("/srv/share/photo-walks")},
                             {QStringLiteral("forWhom"), QStringLiteral("anyone")},
                             {QStringLiteral("totalBytes"), double(1000000000)}}));
    QTRY_VERIFY_WITH_TIMEOUT(app.createShown(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
        app.createBand().value(QStringLiteral("progress")).toDouble(), 0.42, 5000);
    const QVariantMap band = app.createBand();
    QCOMPARE(band.value(QStringLiteral("kind")).toString(), QStringLiteral("accent"));
    QCOMPARE(band.value(QStringLiteral("headline")).toString(),
             QStringLiteral("Creating photo-walks"));
    QVERIFY(band.value(QStringLiteral("line")).toString().startsWith(QStringLiteral("hashing ")));
    QVERIFY(
        band.value(QStringLiteral("line")).toString().contains(QStringLiteral("512 KiB pieces")));
    // While it runs, its id is kept for the next run.
    QTRY_VERIFY_WITH_TIMEOUT(omaseed::core::StateFile::readSection(QStringLiteral("create"))
                                     .begin()
                                     ->toObject()
                                     .value(QStringLiteral("task"))
                                     .toObject()
                                     .value(QStringLiteral("taskId"))
                                     .toString() == QLatin1String("t1"),
                             5000);
    app.createAction(QStringLiteral("cancel"));
    QVERIFY(!app.createShown());
    const QVariantMap strip = app.outcomes()->current();
    QCOMPARE(strip.value(QStringLiteral("kind")).toString(), QStringLiteral("dim"));
    QCOMPARE(strip.value(QStringLiteral("headline")).toString(),
             QStringLiteral("photo-walks wasn't made: cancelled at 42 %"));
    QTRY_COMPARE_WITH_TIMEOUT(server.count("torrentcreator/deleteTask"), 1, 5000);
    // "create again…" hands the choices back.
    QSignalSpy again(&app, &App::createAgainRequested);
    app.createAction(QStringLiteral("createAgain"));
    QCOMPARE(again.count(), 1);
    QCOMPARE(again.first().first().toMap().value(QStringLiteral("folder")).toString(),
             QStringLiteral("/srv/share/photo-walks"));
    app.engine()->stop();
}

// CR13: omaseed quit while it hashed; the next run takes the task up and ends it.
void TestAppCreate::aRunningTaskIsTakenUpByTheNextRun()
{
    CreateServer server;
    server.runningPolls = -1;
    server.made = torrentBytes(QStringLiteral("photo-walks"),
                               QStringLiteral("udp://open.tracker-c.example:6969/announce"),
                               false);
    {
        App app;
        QVERIFY(startOn(app, server));
        QVERIFY(
            app.startCreate({{QStringLiteral("folder"), QStringLiteral("/srv/share/photo-walks")},
                             {QStringLiteral("forWhom"), QStringLiteral("anyone")}}));
        QTRY_VERIFY_WITH_TIMEOUT(server.count("torrentcreator/status") > 0, 5000);
        app.engine()->stop();
    }
    server.runningPolls = 0;
    App next;
    QVERIFY(startOn(next, server));
    QTRY_VERIFY_WITH_TIMEOUT(server.count("torrents/add") == 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(next.createShown(), 5000);
    QCOMPARE(next.createBand().value(QStringLiteral("kind")).toString(), QStringLiteral("green"));
    QCOMPARE(server.count("torrentcreator/addTask"), 1);
    next.engine()->stop();
}

QTEST_MAIN(TestAppCreate)
#include "tst_app_create.moc"
