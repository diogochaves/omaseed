// The renames the story tells (board 56, DECISIONS C38, #236), through App against a small fake
// qBittorrent: omaseed's own renames of a torrent (R1), of a file, a folder and a run of the
// rename sheet (R3) are written to the server's record as the server answers OK, kept across a
// restart in state.json beside the move history and dropped when the torrent leaves (R9).

#include "core/RenameHistory.hpp"
#include "ui/app/App.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>

using omaseed::app::App;
using omaseed::core::RenameRecord;

namespace {

/// "Pack" (with files in two folders and one at its top) and "Single". Every maindata answer is
/// a full update of `hashes`; every POST to torrents/ answers "Ok." but those in `refuse`
/// ("torrents/rename"), which answer 409.
class RenameServer : public QTcpServer
{
public:
    QString pack = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    QString single = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    QStringList hashes{pack, single};
    QStringList refuse;
    QStringList posts;
    int polls = 0;

    static QStringList packFiles()
    {
        return {QStringLiteral("Pack/Disc 1/a.mkv"),
                QStringLiteral("Pack/Disc 1/b.mkv"),
                QStringLiteral("Pack/Disc 2/c.mkv"),
                QStringLiteral("Pack/notes.txt")};
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
            const QByteArray target = buffer->left(buffer->indexOf('\n')).split(' ').value(1);
            buffer->clear();
            answer(socket, QUrl(QString::fromUtf8(target)).path().mid(8)); // after /api/v2/
        });
    }

private:
    static QByteArray json(const QJsonValue &value)
    {
        return value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                               : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    }

    void answer(QTcpSocket *socket, const QString &path)
    {
        QByteArray status = "200 OK";
        QByteArray body = "Ok.";
        if (path == QLatin1String("app/version")) {
            body = "v5.2.4";
        } else if (path == QLatin1String("app/webapiVersion")) {
            body = "2.15.1";
        } else if (path == QLatin1String("sync/maindata")) {
            ++polls;
            QJsonObject torrents;
            for (const QString &hash : std::as_const(hashes)) {
                const bool isPack = hash == pack;
                torrents.insert(
                    hash,
                    QJsonObject{{QStringLiteral("name"),
                                 isPack ? QStringLiteral("Pack") : QStringLiteral("Single")},
                                {QStringLiteral("state"), QStringLiteral("uploading")},
                                {QStringLiteral("progress"), 1.0},
                                {QStringLiteral("size"), 4096},
                                {QStringLiteral("total_size"), 4096},
                                {QStringLiteral("save_path"), QStringLiteral("/srv/nowhere")},
                                {QStringLiteral("content_path"),
                                 isPack ? QStringLiteral("/srv/nowhere/Pack")
                                        : QStringLiteral("/srv/nowhere/single.iso")}});
            }
            body = json(QJsonObject{{QStringLiteral("rid"), polls},
                                    {QStringLiteral("full_update"), true},
                                    {QStringLiteral("torrents"), torrents}});
        } else if (path == QLatin1String("torrents/properties")) {
            body = json(QJsonObject{{QStringLiteral("save_path"), QStringLiteral("/srv/nowhere")},
                                    {QStringLiteral("total_size"), 4096}});
        } else if (path == QLatin1String("torrents/files")) {
            QJsonArray files;
            const QStringList names = packFiles();
            for (int index = 0; index < names.size(); ++index)
                files.append(QJsonObject{{QStringLiteral("index"), index},
                                         {QStringLiteral("name"), names.at(index)},
                                         {QStringLiteral("size"), 1024},
                                         {QStringLiteral("progress"), 1.0},
                                         {QStringLiteral("priority"), 1},
                                         {QStringLiteral("availability"), 1.0}});
            body = json(files);
        } else if (path == QLatin1String("sync/torrentPeers")) {
            body = "{}";
        } else if (path == QLatin1String("torrents/info")) {
            body = "[]";
        } else if (path == QLatin1String("torrents/rename") ||
                   path == QLatin1String("torrents/renameFile") ||
                   path == QLatin1String("torrents/renameFolder")) {
            posts << path;
            if (refuse.contains(path)) {
                status = "409 Conflict";
                body = "Refused";
            }
        } else if (path.startsWith(QLatin1String("log/"))) {
            body = "[]";
        } else if (path.startsWith(QLatin1String("torrents/"))) {
            body = "[]"; // trackers, web seeds, piece states
        }
        socket->write("HTTP/1.1 " + status + "\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }
};

QList<RenameRecord> recordsOf(const App &app, const QString &hash)
{
    return app.renameHistory().recordsFor(hash);
}

} // namespace

class TestAppRenames : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aTorrentRenameIsALineWithItsOldName();
    void aRefusedRenameWritesNothing();
    void aFileAndAFolderAreALineEach();
    void aSheetRunIsOneLine();
    void theRecordOutlivesARestartAndLeavesWithTheTorrent();

private:
    bool startOn(App &app, RenameServer &server);
    bool openFiles(App &app, RenameServer &server);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppRenames::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppRenames::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

bool TestAppRenames::startOn(App &app, RenameServer &server)
{
    if (!server.isListening() && !server.listen(QHostAddress::LocalHost))
        return false;
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.details()->setPollInterval(150);
    app.start();
    return QTest::qWaitFor(
        [&app, &server]() { return app.torrents()->count() == int(server.hashes.size()); }, 5000);
}

bool TestAppRenames::openFiles(App &app, RenameServer &server)
{
    app.selectTorrent(server.pack);
    app.details()->setVisible(true);
    return QTest::qWaitFor(
        [&app]() {
            return app.details()->files()->fileCount() == int(RenameServer::packFiles().size());
        },
        5000);
}

// R1, R7: the name it had when the rename was sent, the new one, omaseed's, and not on disk
// on qBittorrent. The story reads it.
void TestAppRenames::aTorrentRenameIsALineWithItsOldName()
{
    RenameServer server;
    App app;
    QVERIFY(startOn(app, server));
    QSignalSpy changed(&app, &App::renamesChanged);
    app.rename(server.single, QStringLiteral("Single (2026)"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    QVERIFY(changed.count() >= 1);
    const RenameRecord record = recordsOf(app, server.single).constFirst();
    QCOMPARE(record.what, RenameRecord::What::Torrent);
    QCOMPARE(record.was, QStringLiteral("Single"));
    QCOMPARE(record.now, QStringLiteral("Single (2026)"));
    QCOMPARE(record.by, RenameRecord::By::Omaseed);
    QVERIFY(!record.disk);
    QVERIFY(record.at.isValid());
    QCOMPARE(app.lastRenameLineOf(server.single), record.id);
    QVERIFY(recordsOf(app, server.pack).isEmpty());

    const QVariantList story = app.torrentStory(server.single);
    const auto line = std::find_if(story.cbegin(), story.cend(), [](const QVariant &event) {
        return event.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("rename");
    });
    QVERIFY(line != story.cend());
    QCOMPARE(line->toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Renamed"));
    QCOMPARE(line->toMap().value(QStringLiteral("detail")).toString(),
             QStringLiteral("was Single"));
    app.engine()->stop();
}

void TestAppRenames::aRefusedRenameWritesNothing()
{
    RenameServer server;
    server.refuse << QStringLiteral("torrents/rename");
    App app;
    QVERIFY(startOn(app, server));
    app.rename(server.single, QStringLiteral("Single (2026)"));
    QTRY_COMPARE_WITH_TIMEOUT(app.outcomes()->current().value(QStringLiteral("kind")).toString(),
                              QStringLiteral("failed"),
                              3000);
    QVERIFY(recordsOf(app, server.single).isEmpty());
    QCOMPARE(app.lastRenameLineOf(server.single), 0);
    app.engine()->stop();
}

// R3: `r` in files. A file's own names and the folder it sits in, as the files tab shows it; a
// folder's own names.
void TestAppRenames::aFileAndAFolderAreALineEach()
{
    RenameServer server;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(openFiles(app, server));

    app.renameTorrentFile(
        server.pack, QStringLiteral("Pack/Disc 2/c.mkv"), QStringLiteral("d.mkv"), false);
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 3000);
    RenameRecord file = recordsOf(app, server.pack).constFirst();
    QCOMPARE(file.what, RenameRecord::What::File);
    QCOMPARE(file.was, QStringLiteral("c.mkv"));
    QCOMPARE(file.now, QStringLiteral("d.mkv"));
    QCOMPARE(file.in, QStringLiteral("Pack/Disc 2"));
    QCOMPARE(file.count, 1);

    app.renameTorrentFile(server.pack, QStringLiteral("Pack/Disc 1"), QStringLiteral("CD 1"), true);
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 2, 3000);
    const RenameRecord folder = recordsOf(app, server.pack).constFirst();
    QCOMPARE(folder.what, RenameRecord::What::Folder);
    QCOMPARE(folder.was, QStringLiteral("Disc 1"));
    QCOMPARE(folder.now, QStringLiteral("CD 1"));
    QCOMPARE(folder.in, QStringLiteral("Pack"));
    QCOMPARE(app.lastRenameLineOf(server.pack), folder.id);
    QCOMPARE(server.posts,
             QStringList(
                 {QStringLiteral("torrents/renameFile"), QStringLiteral("torrents/renameFolder")}));
    app.engine()->stop();
}

// R3: board 18's sheet over files in two folders and the top is one Files line: the count, the
// first file's names, how many folders.
void TestAppRenames::aSheetRunIsOneLine()
{
    RenameServer server;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(openFiles(app, server));
    const QVariantMap prefix{{QStringLiteral("find"), QStringLiteral("^")},
                             {QStringLiteral("replace"), QStringLiteral("x-")},
                             {QStringLiteral("regex"), true}};
    QSignalSpy done(&app, &App::torrentFilesRenamed);
    app.renameTorrentFiles(server.pack, RenameServer::packFiles(), prefix);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    QCOMPARE(done.at(0).at(1).toInt(), 4);
    QCOMPARE(recordsOf(app, server.pack).size(), 1);
    const RenameRecord run = recordsOf(app, server.pack).constFirst();
    QCOMPARE(run.what, RenameRecord::What::Files);
    QCOMPARE(run.count, 4);
    QCOMPARE(run.was, QStringLiteral("a.mkv"));
    QCOMPARE(run.now, QStringLiteral("x-a.mkv"));
    QCOMPARE(run.in, QString());
    QCOMPARE(run.folders, 3);

    // Two in one folder: that folder, one of it.
    app.renameTorrentFiles(
        server.pack,
        {QStringLiteral("Pack/Disc 1/a.mkv"), QStringLiteral("Pack/Disc 1/b.mkv")},
        prefix);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 5000);
    QCOMPARE(recordsOf(app, server.pack).size(), 2);
    const RenameRecord two = recordsOf(app, server.pack).constFirst();
    QCOMPARE(two.count, 2);
    QCOMPARE(two.in, QStringLiteral("Pack/Disc 1"));
    QCOMPARE(two.folders, 1);
    app.engine()->stop();
}

// R9: per server in state.json, back after a restart; a full update without the torrent drops
// its lines.
void TestAppRenames::theRecordOutlivesARestartAndLeavesWithTheTorrent()
{
    RenameServer server;
    {
        App app;
        QVERIFY(startOn(app, server));
        app.rename(server.pack, QStringLiteral("Pack Renamed"));
        app.rename(server.single, QStringLiteral("Single Renamed"));
        QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 3000);
        app.engine()->stop();
    }
    App app;
    QSignalSpy changed(&app, &App::renamesChanged);
    QVERIFY(startOn(app, server));
    QCOMPARE(recordsOf(app, server.pack).size(), 1);
    QCOMPARE(recordsOf(app, server.pack).constFirst().was, QStringLiteral("Pack"));
    QCOMPARE(recordsOf(app, server.single).size(), 1);

    server.hashes.removeAll(server.single);
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(recordsOf(app, server.single).isEmpty(), 3000);
    QCOMPARE(recordsOf(app, server.pack).size(), 1);
    app.engine()->stop();

    // And it is gone from state.json too.
    App again;
    QVERIFY(startOn(again, server));
    QVERIFY(recordsOf(again, server.single).isEmpty());
    QCOMPARE(recordsOf(again, server.pack).size(), 1);
    again.engine()->stop();
}

QTEST_MAIN(TestAppRenames)
#include "tst_app_renames.moc"
