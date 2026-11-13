// The renames the story tells (board 56, DECISIONS C38, #236), through App against a small fake
// qBittorrent: omaseed's own renames of a torrent (R1), of a file, a folder and a run of the
// rename sheet (R3) are written to the server's record as the server answers OK, kept across a
// restart in state.json beside the move history and dropped when the torrent leaves (R9). A
// name changed outside is a line of its own (R4), omaseed's own rename seen in a poll is not
// (its echo), and a file rename logged failed takes its line back (R3, C2 D2).

#include "core/ProblemLog.hpp"
#include "core/StoryRecord.hpp"
#include "core/SyncEngine.hpp"
#include "ui/app/App.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <memory>

using omaseed::app::App;
using omaseed::core::StoryEntry;

namespace {

/// "Pack" (with files in two folders and one at its top) and "Single". Every maindata answer is
/// a full update of `hashes`, named by `names`, sized by `totalSizes` (-1: no metadata yet);
/// every POST to torrents/ answers "Ok." but those in `refuse` ("torrents/rename"), which answer
/// 409. A torrent rename it takes changes the name it lists: at once, with its OK held back
/// `renameAnswerDelayMs`, or `renameLandsAfterMs` after an OK sent at once; a file's or
/// folder's OK waits `fileAnswerDelayMs`. log/main answers
/// `logLines` after the id asked for.
class RenameServer : public QTcpServer
{
public:
    QString pack = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    QString single = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    QStringList hashes{pack, single};
    QHash<QString, QString> names{{pack, QStringLiteral("Pack")},
                                  {single, QStringLiteral("Single")}};
    QHash<QString, qint64> totalSizes;
    QStringList refuse;
    QStringList posts;
    QJsonArray logLines;
    int renameAnswerDelayMs = 0;
    int renameLandsAfterMs = 0;
    int fileAnswerDelayMs = 0; ///< renameFile's and renameFolder's OK held back
    int polls = 0;

    /// qBittorrent's line for a file rename that failed after its OK (C2 D2).
    void logRenameFailure(const QString &torrent, const QString &file)
    {
        logLines.append(QJsonObject{
            {QStringLiteral("id"), int(logLines.size())},
            {QStringLiteral("type"), 4},
            {QStringLiteral("timestamp"), QDateTime::currentSecsSinceEpoch()},
            {QStringLiteral("message"),
             QStringLiteral("File rename failed. Torrent: \"%1\", file: \"%2\", reason: "
                            "\"Permission denied\"")
                 .arg(torrent, file)}});
    }

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
            QByteArray body = buffer->mid(headerEnd + 4, length);
            body.replace('+', ' ');
            buffer->clear();
            const QUrl url(QString::fromUtf8(target));
            answer(socket,
                   url.path().mid(8), // after /api/v2/
                   QUrlQuery(url.query()),
                   QUrlQuery(QString::fromUtf8(body)));
        });
    }

private:
    static QByteArray json(const QJsonValue &value)
    {
        return value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                               : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    }

    void
    answer(QTcpSocket *socket, const QString &path, const QUrlQuery &query, const QUrlQuery &form)
    {
        int delayMs = 0;
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
                const qint64 totalSize = totalSizes.value(hash, 4096);
                torrents.insert(
                    hash,
                    QJsonObject{{QStringLiteral("name"), names.value(hash, hash)},
                                {QStringLiteral("state"), QStringLiteral("uploading")},
                                {QStringLiteral("progress"), 1.0},
                                {QStringLiteral("size"), std::max<qint64>(totalSize, 0)},
                                {QStringLiteral("total_size"), totalSize},
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
            } else if (path == QLatin1String("torrents/rename")) {
                const QString hash =
                    form.queryItemValue(QStringLiteral("hash"), QUrl::FullyDecoded);
                const QString name =
                    form.queryItemValue(QStringLiteral("name"), QUrl::FullyDecoded);
                if (renameLandsAfterMs > 0)
                    QTimer::singleShot(renameLandsAfterMs, this, [this, hash, name]() {
                        names.insert(hash, name);
                    });
                else
                    names.insert(hash, name);
                delayMs = renameAnswerDelayMs;
            } else {
                delayMs = fileAnswerDelayMs;
            }
        } else if (path == QLatin1String("log/main")) {
            const int after = query.queryItemValue(QStringLiteral("last_known_id")).toInt();
            QJsonArray lines;
            for (const QJsonValue &line : std::as_const(logLines))
                if (line.toObject().value(QStringLiteral("id")).toInt() > after)
                    lines.append(line);
            body = json(lines);
        } else if (path.startsWith(QLatin1String("log/"))) {
            body = "[]";
        } else if (path.startsWith(QLatin1String("torrents/"))) {
            body = "[]"; // trackers, web seeds, piece states
        }
        const QByteArray reply = "HTTP/1.1 " + status +
                                 "\r\nContent-Length: " + QByteArray::number(body.size()) +
                                 "\r\nConnection: close\r\n\r\n" + body;
        const QPointer<QTcpSocket> to(socket);
        const auto send = [to, reply]() {
            if (to.isNull())
                return;
            to->write(reply);
            to->disconnectFromHost();
        };
        if (delayMs > 0)
            QTimer::singleShot(delayMs, this, send);
        else
            send();
    }
};

QList<StoryEntry> recordsOf(const App &app, const QString &hash)
{
    return app.storyRecord().recordsFor(hash);
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
    void aNameChangedOutsideIsAnElsewhereLine();
    void outsideRenamesFoldAndNeverIntoOmaseedsOwn();
    void aMagnetNamedByItsMetadataIsNoLine();
    void aRenameWhileOmaseedWasntRunningIsNoLine();
    void omaseedsOwnRenameSeenBeforeItsOkIsOneLine();
    void omaseedsOwnRenameSeenAfterItsOkIsOneLine();
    void aFileRenameLoggedFailedTakesItsLineBack();
    void aRunLoggedFailedBeforeItsOkLeavesNoLine();

private:
    bool startOn(App &app, RenameServer &server);
    static bool waitPolls(RenameServer &server, int count);
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

bool TestAppRenames::waitPolls(RenameServer &server, int count)
{
    const int until = server.polls + count;
    return QTest::qWaitFor([&server, until]() { return server.polls >= until; }, 5000);
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
    QSignalSpy changed(&app, &App::storyChanged);
    app.rename(server.single, QStringLiteral("Single (2026)"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    QVERIFY(changed.count() >= 1);
    const StoryEntry record = recordsOf(app, server.single).constFirst();
    QCOMPARE(record.what, StoryEntry::What::Torrent);
    QCOMPARE(record.was, QStringLiteral("Single"));
    QCOMPARE(record.now, QStringLiteral("Single (2026)"));
    QCOMPARE(record.by, StoryEntry::By::Omaseed);
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
    StoryEntry file = recordsOf(app, server.pack).constFirst();
    QCOMPARE(file.what, StoryEntry::What::File);
    QCOMPARE(file.was, QStringLiteral("c.mkv"));
    QCOMPARE(file.now, QStringLiteral("d.mkv"));
    QCOMPARE(file.in, QStringLiteral("Pack/Disc 2"));
    QCOMPARE(file.count, 1);

    app.renameTorrentFile(server.pack, QStringLiteral("Pack/Disc 1"), QStringLiteral("CD 1"), true);
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 2, 3000);
    const StoryEntry folder = recordsOf(app, server.pack).constFirst();
    QCOMPARE(folder.what, StoryEntry::What::Folder);
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
    const StoryEntry run = recordsOf(app, server.pack).constFirst();
    QCOMPARE(run.what, StoryEntry::What::Files);
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
    const StoryEntry two = recordsOf(app, server.pack).constFirst();
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
    QSignalSpy changed(&app, &App::storyChanged);
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

// R4: a name changed on the server between two polls, not by omaseed, is a line timed at the
// poll that saw it; R7: qBittorrent's reads `was OLD` with no disk.
void TestAppRenames::aNameChangedOutsideIsAnElsewhereLine()
{
    RenameServer server;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 2));
    QVERIFY(recordsOf(app, server.single).isEmpty());

    const QDateTime before = QDateTime::currentDateTimeUtc().addSecs(-1);
    server.names.insert(server.single, QStringLiteral("Single (outside)"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    const StoryEntry record = recordsOf(app, server.single).constFirst();
    QCOMPARE(record.what, StoryEntry::What::Torrent);
    QCOMPARE(record.by, StoryEntry::By::Elsewhere);
    QCOMPARE(record.was, QStringLiteral("Single"));
    QCOMPARE(record.now, QStringLiteral("Single (outside)"));
    QVERIFY(!record.disk);
    QVERIFY(record.at >= before);
    // Not omaseed's: the take-back's seam is untouched.
    QCOMPARE(app.lastRenameLineOf(server.single), 0);

    const QVariantList story = app.torrentStory(server.single);
    const auto line = std::find_if(story.cbegin(), story.cend(), [](const QVariant &event) {
        return event.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("rename");
    });
    QVERIFY(line != story.cend());
    QCOMPARE(line->toMap().value(QStringLiteral("detail")).toString(),
             QStringLiteral("was Single · renamed outside omaseed"));

    // Seen once: more polls of the same name add nothing.
    QVERIFY(waitPolls(server, 3));
    QCOMPARE(recordsOf(app, server.single).size(), 1);
    app.engine()->stop();
}

// R6: two renames seen in polls fold into one line (the first's old name, the last's new); one
// seen right after omaseed's own is a line of its own.
void TestAppRenames::outsideRenamesFoldAndNeverIntoOmaseedsOwn()
{
    RenameServer server;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));

    server.names.insert(server.single, QStringLiteral("Single A"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    server.names.insert(server.single, QStringLiteral("Single B"));
    QTRY_COMPARE_WITH_TIMEOUT(
        recordsOf(app, server.single).constFirst().now, QStringLiteral("Single B"), 3000);
    QCOMPARE(recordsOf(app, server.single).size(), 1);
    QCOMPARE(recordsOf(app, server.single).constFirst().was, QStringLiteral("Single"));

    // omaseed's own on the pack, then one from outside.
    app.rename(server.pack, QStringLiteral("Pack Mine"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(
        app.torrents()->torrent(server.pack).name, QStringLiteral("Pack Mine"), 3000);
    server.names.insert(server.pack, QStringLiteral("Pack Theirs"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 2, 3000);
    const QList<StoryEntry> lines = recordsOf(app, server.pack);
    QCOMPARE(lines.at(0).by, StoryEntry::By::Elsewhere);
    QCOMPARE(lines.at(0).was, QStringLiteral("Pack Mine"));
    QCOMPARE(lines.at(0).now, QStringLiteral("Pack Theirs"));
    QCOMPARE(lines.at(1).by, StoryEntry::By::Omaseed);
    QCOMPARE(lines.at(1).was, QStringLiteral("Pack"));
    app.engine()->stop();
}

// R4: a magnet's name changes when its metadata arrives; that is no rename.
void TestAppRenames::aMagnetNamedByItsMetadataIsNoLine()
{
    RenameServer server;
    const QString magnet = QStringLiteral("cccccccccccccccccccccccccccccccccccccccc");
    server.hashes << magnet;
    server.totalSizes.insert(magnet, -1);
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));

    server.names.insert(magnet, QStringLiteral("Some Linux ISO"));
    server.totalSizes.insert(magnet, 8192);
    QTRY_COMPARE_WITH_TIMEOUT(
        app.torrents()->torrent(magnet).name, QStringLiteral("Some Linux ISO"), 3000);
    QVERIFY(waitPolls(server, 2));
    QVERIFY(recordsOf(app, magnet).isEmpty());

    // With its metadata, the next change is one.
    server.names.insert(magnet, QStringLiteral("Some Linux ISO (renamed)"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, magnet).size(), 1, 3000);
    QCOMPARE(recordsOf(app, magnet).constFirst().was, QStringLiteral("Some Linux ISO"));
    app.engine()->stop();
}

// R5: a name changed while omaseed wasn't running is no line: the first sync has no "before".
void TestAppRenames::aRenameWhileOmaseedWasntRunningIsNoLine()
{
    RenameServer server;
    {
        App app;
        QVERIFY(startOn(app, server));
        QVERIFY(waitPolls(server, 1));
        app.engine()->stop();
    }
    server.names.insert(server.single, QStringLiteral("Single (while away)"));
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 3));
    QCOMPARE(app.torrents()->torrent(server.single).name, QStringLiteral("Single (while away)"));
    QVERIFY(recordsOf(app, server.single).isEmpty());
    app.engine()->stop();
}

// R4's echo: omaseed's rename shows up in the polls while its OK is still out; it stays one
// Omaseed line.
void TestAppRenames::omaseedsOwnRenameSeenBeforeItsOkIsOneLine()
{
    RenameServer server;
    server.renameAnswerDelayMs = 800;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    app.rename(server.single, QStringLiteral("Single Mine"));
    QTRY_COMPARE_WITH_TIMEOUT(
        app.torrents()->torrent(server.single).name, QStringLiteral("Single Mine"), 3000);
    QVERIFY(recordsOf(app, server.single).isEmpty()); // the OK hasn't landed
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    QVERIFY(waitPolls(server, 3));
    QCOMPARE(recordsOf(app, server.single).size(), 1);
    QCOMPARE(recordsOf(app, server.single).constFirst().by, StoryEntry::By::Omaseed);
    QCOMPARE(recordsOf(app, server.single).constFirst().was, QStringLiteral("Single"));
    app.engine()->stop();
}

// The other order: the OK first, the poll that shows the name after.
void TestAppRenames::omaseedsOwnRenameSeenAfterItsOkIsOneLine()
{
    RenameServer server;
    server.renameLandsAfterMs = 600;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(waitPolls(server, 1));
    app.rename(server.single, QStringLiteral("Single Mine"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.single).size(), 1, 3000);
    QCOMPARE(app.torrents()->torrent(server.single).name, QStringLiteral("Single"));
    QTRY_COMPARE_WITH_TIMEOUT(
        app.torrents()->torrent(server.single).name, QStringLiteral("Single Mine"), 3000);
    QVERIFY(waitPolls(server, 3));
    QCOMPARE(recordsOf(app, server.single).size(), 1);
    QCOMPARE(recordsOf(app, server.single).constFirst().by, StoryEntry::By::Omaseed);
    app.engine()->stop();
}

// R3, C2 D2: qBittorrent answered the file rename OK, then logged it failed: its line goes (the
// failure line tells it). Another line of the torrent stays.
void TestAppRenames::aFileRenameLoggedFailedTakesItsLineBack()
{
    RenameServer server;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(openFiles(app, server));

    app.renameTorrentFile(server.pack, QStringLiteral("Pack/Disc 1"), QStringLiteral("CD 1"), true);
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 3000);
    app.renameTorrentFile(
        server.pack, QStringLiteral("Pack/Disc 2/c.mkv"), QStringLiteral("d.mkv"), false);
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 2, 3000);
    const int fileLine = recordsOf(app, server.pack).constFirst().id;

    QSignalSpy changed(&app, &App::storyChanged);
    // qBittorrent names the file by the path it kept. The folder's watch hears it too (same
    // torrent, within 30 s) but the file isn't the folder's: the folder's line stays.
    server.logRenameFailure(QStringLiteral("Pack"), QStringLiteral("Pack/Disc 2/c.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(recordsOf(app, server.pack).size(), 1, 5000);
    QVERIFY(changed.count() >= 1);
    QCOMPARE(recordsOf(app, server.pack).constFirst().what, StoryEntry::What::Folder);
    QVERIFY(recordsOf(app, server.pack).constFirst().id != fileLine);
    app.engine()->stop();
}

// The log read before the OK landed (slow answers): the run's line goes as soon as it is
// written.
void TestAppRenames::aRunLoggedFailedBeforeItsOkLeavesNoLine()
{
    RenameServer server;
    server.fileAnswerDelayMs = 400;
    App app;
    QVERIFY(startOn(app, server));
    QVERIFY(openFiles(app, server));
    QSignalSpy told(app.engine()->problems(), &omaseed::core::ProblemLog::watchedFailure);
    QSignalSpy done(&app, &App::torrentFilesRenamed);
    app.renameTorrentFiles(server.pack,
                           RenameServer::packFiles(),
                           {{QStringLiteral("find"), QStringLiteral("^")},
                            {QStringLiteral("replace"), QStringLiteral("x-")},
                            {QStringLiteral("regex"), true}});
    server.logRenameFailure(QStringLiteral("Pack"), QStringLiteral("Pack/Disc 1/a.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(told.count(), 1, 3000);
    QCOMPARE(done.count(), 0); // four answers, 400 ms each: the run is still out
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    QCOMPARE(done.at(0).at(1).toInt(), 4);
    QVERIFY(recordsOf(app, server.pack).isEmpty());
    app.engine()->stop();
}

QTEST_MAIN(TestAppRenames)
#include "tst_app_renames.moc"
