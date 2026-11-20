// The room of one add through the server's temp folder (#275, board 64; DECISIONS C47 I8–I12, on
// #271's facts): App::roomFor against a small fake qBittorrent and a pretend machine's drives,
// and the add's `useTemp` as qBittorrent's useDownloadPath.

#include "core/FreeSpace.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"

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
using omaseed::core::StorageReading;

namespace {

constexpr qint64 kGiB = 1024LL * 1024 * 1024;

const QString kOnHome = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
const QString kInTemp = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

/// qBittorrent as far as the room asks: its preferences, a poll with its free space, its
/// categories and two downloads, and the add's form, kept.
class RoomServer : public QTcpServer
{
public:
    QJsonObject prefs{{QStringLiteral("save_path"), QStringLiteral("/home/d/Downloads")},
                      {QStringLiteral("temp_path_enabled"), true},
                      {QStringLiteral("temp_path"), QStringLiteral("/fast/temp")},
                      {QStringLiteral("preallocate_all"), false}};
    QList<QUrlQuery> adds;

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
            const QByteArray target = buffer->left(buffer->indexOf('\n')).split(' ').value(1);
            const QByteArray requestBody = buffer->mid(headerEnd + 4, length);
            buffer->clear();

            QByteArray body = "Ok.";
            if (target.startsWith("/api/v2/app/version"))
                body = "v5.3.0";
            else if (target.startsWith("/api/v2/app/webapiVersion"))
                body = "2.16.2";
            else if (target.startsWith("/api/v2/app/preferences"))
                body = QJsonDocument(prefs).toJson(QJsonDocument::Compact);
            else if (target.startsWith("/api/v2/sync/maindata"))
                body = QJsonDocument(maindata()).toJson(QJsonDocument::Compact);
            else if (target.startsWith("/api/v2/torrents/add"))
                adds.append(QUrlQuery(QString::fromUtf8(requestBody)));
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                          "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }

private:
    static QJsonObject maindata()
    {
        const auto category = [](const QString &name, const QJsonValue &downloadPath) {
            return QJsonObject{{QStringLiteral("name"), name},
                               {QStringLiteral("savePath"), QStringLiteral("/home/d/") + name},
                               {QStringLiteral("download_path"), downloadPath}};
        };
        const auto torrent = [](qint64 left, const QString &downloadPath) {
            return QJsonObject{{QStringLiteral("name"), QStringLiteral("t")},
                               {QStringLiteral("state"), QStringLiteral("downloading")},
                               {QStringLiteral("amount_left"), left},
                               {QStringLiteral("save_path"), QStringLiteral("/home/d/Downloads")},
                               {QStringLiteral("download_path"), downloadPath}};
        };
        return {
            {QStringLiteral("rid"), 1},
            {QStringLiteral("full_update"), true},
            {QStringLiteral("server_state"),
             QJsonObject{{QStringLiteral("free_space_on_disk"), 1500 * kGiB},
                         {QStringLiteral("connection_status"), QStringLiteral("connected")}}},
            {QStringLiteral("categories"),
             QJsonObject{{QStringLiteral("films"),
                          category(QStringLiteral("films"), QStringLiteral("/fast/films"))},
                         {QStringLiteral("music"), category(QStringLiteral("music"), false)},
                         {QStringLiteral("tv"), category(QStringLiteral("tv"), QJsonValue())}}},
            {QStringLiteral("torrents"),
             QJsonObject{{kOnHome, torrent(100 * kGiB, QString())},
                         {kInTemp, torrent(5 * kGiB, QStringLiteral("/fast/temp"))}}},
        };
    }
};

/// The pretend machine: /home (drive 1, 1500 GiB free) and /fast (drive 2, 200 GiB free).
/// Anything else is not seen.
StorageReading readDisk(const QString &path)
{
    StorageReading reading;
    const auto drive = [&](const QString &root, qint64 free, quint64 device) {
        reading.valid = true;
        reading.exists = true; // the calibration needs the default folder there
        reading.rootPath = root;
        reading.bytesFree = free;
        reading.bytesTotal = free * 2;
        reading.device = device;
        reading.fileSystem = QStringLiteral("ext4");
    };
    if (path.startsWith(QLatin1String("/home")))
        drive(QStringLiteral("/home"), 1500 * kGiB, 1);
    else if (path.startsWith(QLatin1String("/fast")))
        drive(QStringLiteral("/fast"), 200 * kGiB, 2);
    return reading;
}

QVariantMap lineOf(const QVariantMap &room, const char *name)
{
    return room.value(QString::fromLatin1(name)).toMap();
}

} // namespace

class TestAppRoom : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void roomForTwoDrives();
    void roomForOneDriveIsSameDrive();
    void roomForACategoryOnlyUnderAutoTmm();
    void roomForUnknownTempDrive();
    void promisedLeftOutWhenPreallocating();
    void addSendsUseTempOnlyWhenOff();

private:
    /// Starts `app` on `server` and waits for its preferences, poll and features.
    bool startOn(App &app, RoomServer &server);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppRoom::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppRoom::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

bool TestAppRoom::startOn(App &app, RoomServer &server)
{
    if (!server.listen(QHostAddress::LocalHost))
        return false;
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    app.setStorageReaderForTests(readDisk);
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    return QTest::qWaitFor(
        [&app]() {
            return app.torrents()->count() == 2 && app.categories()->count() == 3 &&
                   app.features().tempFolderPerAdd &&
                   app.serverPrefs()->has(QStringLiteral("temp_path")) &&
                   app.freeSpaceFor(QStringLiteral("/fast/x")).value(QStringLiteral("source")) ==
                       QStringLiteral("local");
        },
        5000);
}

void TestAppRoom::roomForTwoDrives()
{
    RoomServer server;
    App app;
    QVERIFY(startOn(app, server));
    const QVariantMap room =
        app.roomFor(QStringLiteral("/home/d/Downloads/films"), QString(), false, true);
    QCOMPARE(room.value(QStringLiteral("engine")).toString(), QStringLiteral("qbittorrent"));
    QVERIFY(room.value(QStringLiteral("hasTemp")).toBool());
    QVERIFY(room.value(QStringLiteral("switchable")).toBool());
    QVERIFY(room.value(QStringLiteral("useTemp")).toBool());
    QVERIFY(!room.value(QStringLiteral("sameDrive")).toBool());
    QCOMPARE(room.value(QStringLiteral("tempPath")).toString(), QStringLiteral("/fast/temp"));
    QCOMPARE(room.value(QStringLiteral("savePath")).toString(),
             QStringLiteral("/home/d/Downloads/films"));

    const QVariantMap temp = lineOf(room, "temp");
    QCOMPARE(temp.value(QStringLiteral("source")).toString(), QStringLiteral("local"));
    QCOMPARE(temp.value(QStringLiteral("bytesFree")).toLongLong(), 200 * kGiB);
    QCOMPARE(temp.value(QStringLiteral("where")).toString(), QStringLiteral("/fast"));
    // I10: the download writing into the temp folder promises its 5 GiB there.
    QCOMPARE(temp.value(QStringLiteral("promised")).toLongLong(), 5 * kGiB);
    QCOMPARE(temp.value(QStringLiteral("promisedCount")).toInt(), 1);

    const QVariantMap save = lineOf(room, "save");
    QCOMPARE(save.value(QStringLiteral("source")).toString(), QStringLiteral("local"));
    QCOMPARE(save.value(QStringLiteral("bytesFree")).toLongLong(), 1500 * kGiB);
    QCOMPARE(save.value(QStringLiteral("promised")).toLongLong(), 100 * kGiB);
    QCOMPARE(save.value(QStringLiteral("promisedCount")).toInt(), 1);

    // Off: still a temp folder to name ("This torrent skips FOLDER"), not used.
    QVERIFY(!app.roomFor(QStringLiteral("/home/d/Downloads/films"), QString(), false, false)
                 .value(QStringLiteral("useTemp"))
                 .toBool());
    // An empty Save to is the server's default save path, read on its drive too.
    const QVariantMap byDefault = app.roomFor(QString(), QString(), false, true);
    QCOMPARE(byDefault.value(QStringLiteral("savePath")).toString(),
             QStringLiteral("/home/d/Downloads"));
    QCOMPARE(lineOf(byDefault, "save").value(QStringLiteral("promised")).toLongLong(), 100 * kGiB);
    app.engine()->stop();
}

void TestAppRoom::roomForOneDriveIsSameDrive()
{
    RoomServer server;
    server.prefs.insert(QStringLiteral("temp_path"), QStringLiteral("/home/d/temp"));
    App app;
    QVERIFY(startOn(app, server));
    const QVariantMap room =
        app.roomFor(QStringLiteral("/home/d/Downloads/films"), QString(), false, true);
    QCOMPARE(room.value(QStringLiteral("tempPath")).toString(), QStringLiteral("/home/d/temp"));
    QVERIFY(room.value(QStringLiteral("sameDrive")).toBool());
    // Both lines on drive 1: the same promise on each.
    QCOMPARE(lineOf(room, "temp").value(QStringLiteral("promised")).toLongLong(), 100 * kGiB);

    // No temp folder at all: one line, today's.
    server.prefs.insert(QStringLiteral("temp_path_enabled"), false);
    app.serverPrefs()->reload();
    QTRY_VERIFY_WITH_TIMEOUT(
        !app.roomFor(QString(), QString(), false, true).value(QStringLiteral("hasTemp")).toBool(),
        3000);
    const QVariantMap none = app.roomFor(QString(), QString(), false, true);
    QVERIFY(!none.value(QStringLiteral("switchable")).toBool());
    QVERIFY(lineOf(none, "temp").isEmpty());
    app.engine()->stop();
}

void TestAppRoom::roomForACategoryOnlyUnderAutoTmm()
{
    RoomServer server;
    App app;
    QVERIFY(startOn(app, server));
    const auto room = [&app](const QString &save, const QString &category, bool autoTMM) {
        return app.roomFor(save, category, autoTMM, true);
    };
    // I12, #271 (2): its own folder.
    QVariantMap films = room(QString(), QStringLiteral("films"), true);
    QCOMPARE(films.value(QStringLiteral("tempRule")).toString(), QStringLiteral("own"));
    QCOMPARE(films.value(QStringLiteral("tempPath")).toString(), QStringLiteral("/fast/films"));
    QCOMPARE(films.value(QStringLiteral("savePath")).toString(), QStringLiteral("/home/d/films"));
    // None: one line though the session's temp folder is on.
    QVERIFY(
        !room(QString(), QStringLiteral("music"), true).value(QStringLiteral("hasTemp")).toBool());
    // The session's rule: temp_path/<category>.
    QCOMPARE(
        room(QString(), QStringLiteral("tv"), true).value(QStringLiteral("tempPath")).toString(),
        QStringLiteral("/fast/temp/tv"));
    // A typed Save to (autoTMM false) keeps the session's temp_path whatever the category says.
    films = room(QStringLiteral("/home/d/elsewhere"), QStringLiteral("films"), false);
    QCOMPARE(films.value(QStringLiteral("tempRule")).toString(), QStringLiteral("session"));
    QCOMPARE(films.value(QStringLiteral("tempPath")).toString(), QStringLiteral("/fast/temp"));
    QVERIFY(room(QStringLiteral("/home/d/elsewhere"), QStringLiteral("music"), false)
                .value(QStringLiteral("hasTemp"))
                .toBool());
    app.engine()->stop();
}

void TestAppRoom::roomForUnknownTempDrive()
{
    // #271 (3): free_space_on_disk is the default save path's drive: a temp folder this machine
    // doesn't see is unknown, never the server's figure.
    RoomServer server;
    server.prefs.insert(QStringLiteral("temp_path"), QStringLiteral("/nowhere/temp"));
    App app;
    QVERIFY(startOn(app, server));
    const QVariantMap room = app.roomFor(QString(), QString(), false, true);
    QVERIFY(room.value(QStringLiteral("hasTemp")).toBool());
    const QVariantMap temp = lineOf(room, "temp");
    QCOMPARE(temp.value(QStringLiteral("source")).toString(), QStringLiteral("unknown"));
    QCOMPARE(temp.value(QStringLiteral("bytesFree")).toLongLong(), 0);
    QCOMPARE(temp.value(QStringLiteral("promised")).toLongLong(), 0);
    QVERIFY(!room.value(QStringLiteral("sameDrive")).toBool());
    // The second line keeps today's answer: the server's own figure for its default folder.
    QCOMPARE(lineOf(room, "save").value(QStringLiteral("source")).toString(),
             QStringLiteral("default"));
    app.engine()->stop();
}

void TestAppRoom::promisedLeftOutWhenPreallocating()
{
    RoomServer server;
    server.prefs.insert(QStringLiteral("preallocate_all"), true);
    App app;
    QVERIFY(startOn(app, server));
    const QVariantMap room =
        app.roomFor(QStringLiteral("/home/d/Downloads/films"), QString(), false, true);
    QVERIFY(room.value(QStringLiteral("preallocates")).toBool());
    QCOMPARE(lineOf(room, "temp").value(QStringLiteral("promised")).toLongLong(), 0);
    QCOMPARE(lineOf(room, "save").value(QStringLiteral("promised")).toLongLong(), 0);
    QCOMPARE(lineOf(room, "save").value(QStringLiteral("promisedCount")).toInt(), 0);
    app.engine()->stop();
}

void TestAppRoom::addSendsUseTempOnlyWhenOff()
{
    // C47 I9, #271 (1): the switch off is useDownloadPath=false; on (or not said) sends nothing.
    RoomServer server;
    App app;
    QVERIFY(startOn(app, server));
    QSignalSpy finished(&app, &App::addFinished);
    const QVariantMap magnet{
        {QStringLiteral("kind"), QStringLiteral("magnet")},
        {QStringLiteral("value"),
         QStringLiteral("magnet:?xt=urn:btih:cccccccccccccccccccccccccccccccccccccccc")},
        {QStringLiteral("infoHashV1"), QStringLiteral("cccccccccccccccccccccccccccccccccccccccc")},
        {QStringLiteral("name"), QStringLiteral("c")},
        {QStringLiteral("savePath"), QStringLiteral("/home/d/Downloads/films")},
    };
    QVariantMap off = magnet;
    off.insert(QStringLiteral("useTemp"), false);
    app.addTorrent(off);
    QTRY_COMPARE_WITH_TIMEOUT(server.adds.size(), 1, 3000);
    QCOMPARE(server.adds.at(0).queryItemValue(QStringLiteral("useDownloadPath")),
             QStringLiteral("false"));
    QVERIFY(!server.adds.at(0).hasQueryItem(QStringLiteral("downloadPath")));

    QVariantMap on = magnet;
    on.insert(QStringLiteral("useTemp"), true);
    app.addTorrent(on);
    QTRY_COMPARE_WITH_TIMEOUT(server.adds.size(), 2, 3000);
    QVERIFY(!server.adds.at(1).hasQueryItem(QStringLiteral("useDownloadPath")));
    QVERIFY(!server.adds.at(1).hasQueryItem(QStringLiteral("downloadPath")));
    app.engine()->stop();
}

QTEST_MAIN(TestAppRoom)
#include "tst_app_room.moc"
