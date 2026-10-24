// MoveController (board 42, DECISIONS C15, #97): the rail's folders, the sheet's words and the
// job's band, against a SyncEngine whose rows the test writes itself (no server: sends get no
// answer, and the test lands the torrents by moving their save paths, as a poll would).

#include "core/CategoryModel.hpp"
#include "core/FreeSpace.hpp"
#include "core/MoveJob.hpp"
#include "core/QbtClient.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/MoveController.hpp"
#include "ui/app/MoveWords.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include <memory>

using omaseed::app::MoveController;
using omaseed::core::MoveJob;
using omaseed::core::SyncEngine;

namespace {

QJsonObject torrent(const QString &name, const QString &savePath, qint64 bytes)
{
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("state"), QStringLiteral("stalledUP")},
                       {QStringLiteral("progress"), 1.0},
                       {QStringLiteral("save_path"), savePath},
                       {QStringLiteral("content_path"), savePath + QLatin1Char('/') + name},
                       {QStringLiteral("size"), bytes},
                       {QStringLiteral("total_size"), bytes},
                       {QStringLiteral("completed"), bytes}};
}

QJsonObject library()
{
    return QJsonObject{
        {QStringLiteral("a1"),
         torrent(QStringLiteral("Open Arena"), QStringLiteral("/mnt/archive/games"), 300)},
        {QStringLiteral("b2"),
         torrent(QStringLiteral("Demo Disc"), QStringLiteral("/mnt/archive/games"), 200)},
        {QStringLiteral("c3"),
         torrent(
             QStringLiteral("Homebrew Pack"), QStringLiteral("/mnt/archive/games/handheld"), 100)},
        {QStringLiteral("d4"),
         torrent(QStringLiteral("Big Film"), QStringLiteral("/srv/media/films"), 900)},
    };
}

/// A qBittorrent that only answers app/getDirectoryContent, from `folders` (a folder's entries
/// as full paths); any other folder is "Directory does not exist". `unsupported` answers as a
/// qBittorrent without the endpoint.
class ListingServer : public QTcpServer
{
public:
    QHash<QString, QStringList> folders;
    bool unsupported = false;
    QStringList asked;

    ListingServer()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    const QByteArray request = socket->readAll();
                    const QByteArray target = request.split(' ').value(1);
                    const QUrl url(QString::fromUtf8(target));
                    const QString folder = QUrlQuery(url).queryItemValue(QStringLiteral("dirPath"),
                                                                         QUrl::FullyDecoded);
                    asked << folder;
                    QByteArray status = "200 OK";
                    QByteArray body;
                    if (unsupported) {
                        status = "404 Not Found";
                        body = "Endpoint does not exist";
                    } else if (folders.contains(folder)) {
                        QJsonArray entries;
                        for (const QString &entry : folders.value(folder))
                            entries << entry;
                        body = QJsonDocument(entries).toJson(QJsonDocument::Compact);
                    } else {
                        status = "404 Not Found";
                        body = "Directory does not exist";
                    }
                    socket->write(
                        "HTTP/1.1 " + status + "\r\nContent-Type: text/plain\r\nContent-Length: " +
                        QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
};

/// A qBittorrent that answers every POST "Ok." and notes it as "path hashes" in `calls`
/// (the recheck's `torrents/recheck` and `torrents/start`).
class ActionServer : public QTcpServer
{
public:
    QStringList calls;

    ActionServer()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
                    buffer->append(socket->readAll());
                    const qsizetype end = buffer->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const QByteArray head = buffer->left(end);
                    qsizetype length = 0;
                    for (const QByteArray &line : head.split('\n')) {
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(15).trimmed().toLongLong();
                    }
                    if (buffer->size() < end + 4 + length)
                        return;
                    const QByteArray body = buffer->mid(end + 4, length);
                    const QString path = QUrl(QString::fromUtf8(head.split(' ').value(1))).path();
                    const QString hashes =
                        QUrlQuery(QString::fromUtf8(body)).queryItemValue(QStringLiteral("hashes"));
                    calls << path.section(QLatin1Char('/'), -1) + QLatin1Char(' ') + hashes;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                                  "Content-Length: 3\r\nConnection: close\r\n\r\nOk.");
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
};

QStringList checkTexts(const QVariantMap &plan)
{
    QStringList out;
    for (const QVariant &line : plan.value(QStringLiteral("checks")).toList())
        out << line.toMap().value(QStringLiteral("text")).toString();
    return out;
}

} // namespace

class TestMoveController : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_config;
    SyncEngine *m_engine = nullptr;
    MoveController *m_move = nullptr;
    QStringList m_notices;
    QHash<QString, QStringList> m_recents; ///< state.json's, by server
    QHash<QString, QJsonArray> m_history;  ///< state.json's finished moves, by server
    const omaseed::core::FreeSpaceProbe *m_probe = nullptr;

    void make()
    {
        delete m_move;
        m_move = new MoveController(this);
        MoveController::Hooks hooks;
        hooks.engine = m_engine;
        hooks.lowDiskBytes = []() { return qint64(0); };
        hooks.serverName = []() { return QStringLiteral("home"); };
        hooks.notify = [this](const QString &summary, const QString &body) {
            m_notices << summary + QStringLiteral(" | ") + body;
        };
        hooks.loadRecents = [this](const QString &server) { return m_recents.value(server); };
        hooks.saveRecents = [this](const QString &server, const QStringList &paths) {
            m_recents.insert(server, paths);
        };
        hooks.loadHistory = [this](const QString &server) { return m_history.value(server); };
        hooks.saveHistory = [this](const QString &server, const QJsonArray &moves) {
            m_history.insert(server, moves);
        };
        hooks.probe = m_probe;
        m_move->setHooks(std::move(hooks));
    }

    /// The `checks` line that names a later move to undo first, or {}.
    static QVariantMap backFirstLine(const QVariantMap &plan)
    {
        for (const QVariant &line : plan.value(QStringLiteral("checks")).toList()) {
            if (line.toMap().contains(QStringLiteral("backFirst")))
                return line.toMap();
        }
        return {};
    }

    /// One poll that moves every torrent sent so far to its target.
    void landSent()
    {
        QJsonObject partial;
        for (const MoveJob::Entry &entry : m_move->job()->entries()) {
            if (entry.step == MoveJob::Step::Queued || entry.step == MoveJob::Step::Moving)
                partial.insert(entry.hash, QJsonObject{{QStringLiteral("save_path"), entry.to}});
        }
        m_engine->torrents()->applyPartial(partial, {});
        Q_EMIT m_engine->synced(false);
    }

    /// Demo Disc (b2) is hybrid, both hashes; Open Arena (a1) is v1 only.
    void markHybrid()
    {
        m_engine->torrents()->applyPartial(
            QJsonObject{{QStringLiteral("b2"),
                         QJsonObject{{QStringLiteral("infohash_v1"), QStringLiteral("b2v1")},
                                     {QStringLiteral("infohash_v2"), QStringLiteral("b2v2")}}},
                        {QStringLiteral("a1"),
                         QJsonObject{{QStringLiteral("infohash_v1"), QStringLiteral("a1v1")},
                                     {QStringLiteral("infohash_v2"), QString()}}}},
            {});
    }

    /// DECISIONS C21 (#144): games moved, Homebrew Pack (c3) lands, Demo Disc (b2, hybrid) and
    /// Open Arena (a1, v1 only) fail.
    void failAHybridAndAV1()
    {
        markHybrid();
        QVERIFY(m_engine->torrents()->torrent(QStringLiteral("b2")).isHybrid());
        QVERIFY(!m_engine->torrents()->torrent(QStringLiteral("a1")).isHybrid());
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        QVERIFY(m_move->start(
            QStringLiteral("/mnt/archive/games"), QStringLiteral("/mnt/fast/games"), false));
        landSent(); // the first (c3) lands; b2 and a1 go
        m_move->job()->onSendResult(
            {QStringLiteral("b2"), QStringLiteral("a1")},
            omaseed::core::ApiError(omaseed::core::ApiError::Kind::Conflict,
                                    QStringLiteral("Cannot make save path")));
        QCOMPARE(m_move->stateName(), QStringLiteral("done"));
        QCOMPARE(m_move->failedHashes(), QStringList({QStringLiteral("b2"), QStringLiteral("a1")}));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_config.isValid());
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    }

    void init()
    {
        m_notices.clear();
        m_recents.clear();
        m_history.clear();
        m_probe = nullptr;
        delete m_engine;
        m_engine = new SyncEngine(this);
        m_engine->torrents()->applyFullUpdate(library());
        make();
        QFile::remove(MoveJob::defaultFilePath(QStringLiteral("home")));
    }

    void cleanup()
    {
        delete m_move;
        m_move = nullptr;
    }

    void foldersFollowTheList()
    {
        // F1: the rows come with a server, and go without one.
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        const QVariantMap games = m_move->folders()->figures(QStringLiteral("/mnt/archive/games"));
        QCOMPARE(games.value(QStringLiteral("count")).toInt(), 3);
        QCOMPARE(m_move->folderLine(QStringLiteral("/mnt/archive/games")),
                 QStringLiteral("3 torrents · 600 B on disk · 2 save paths · /mnt/archive/games"));
        QCOMPARE(m_move->folderOf(QStringLiteral("c3")),
                 QStringLiteral("/mnt/archive/games/handheld"));
        // Without a server (a demo) the rows stay, and there is no job to start.
        m_move->setServer(QString());
        QVERIFY(m_move->job() == nullptr);
        QVERIFY(!m_move->start(
            QStringLiteral("/mnt/archive/games"), QStringLiteral("/mnt/fast/games"), false));
        m_engine->torrents()->applyFullUpdate({});
        QTRY_COMPARE(m_move->folders()->total(), 0);
    }

    void theSheetSaysWhatBlocks()
    {
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        const QString games = QStringLiteral("/mnt/archive/games");
        QVariantMap plan = m_move->plan(games, games, false);
        QCOMPARE(plan.value(QStringLiteral("canMove")).toBool(), false);
        QCOMPARE(plan.value(QStringLiteral("blocker")).toString(),
                 QStringLiteral("games is already there."));
        plan = m_move->plan(games, games + QStringLiteral("/new"), false);
        QCOMPARE(plan.value(QStringLiteral("blocker")).toString(),
                 QStringLiteral("That is inside games."));
        plan = m_move->plan(games, QString(), false);
        QCOMPARE(plan.value(QStringLiteral("blocker")).toString(),
                 QStringLiteral("Type the folder's new path."));

        plan = m_move->plan(games, QStringLiteral("/mnt/fast/games"), true);
        QVERIFY(plan.value(QStringLiteral("canMove")).toBool());
        QCOMPARE(plan.value(QStringLiteral("title")).toString(), QStringLiteral("Move games"));
        QCOMPARE(plan.value(QStringLiteral("header")).toString(),
                 QStringLiteral("3 torrents · 600 B on disk · on home"));
        QCOMPARE(plan.value(QStringLiteral("explain")).toString(),
                 QStringLiteral(
                     "games goes inside /mnt/fast. Each torrent keeps its place below games."));
        const QVariantList rows = plan.value(QStringLiteral("pathRows")).toList();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("to")).toString(),
                 QStringLiteral("/mnt/fast/games/handheld"));
        QCOMPARE(plan.value(QStringLiteral("moveText")).toString(),
                 QStringLiteral("move 3 torrents"));
        // S3: this machine can't see the server's disks here.
        const QStringList texts = checkTexts(plan);
        QVERIFY(texts.contains(QStringLiteral(
            "Writable: can't tell here. The first torrent goes alone and answers it.")));
        QVERIFY(texts.contains(
            QStringLiteral("Order: smallest first. Homebrew Pack (100 B) goes alone; the rest "
                           "follow, 3 at a time in qBittorrent's queue.")));
        const QVariantMap loose = plan.value(QStringLiteral("loose")).toMap();
        QCOMPARE(loose.value(QStringLiteral("state")).toString(), QStringLiteral("cantSee"));
        QCOMPARE(plan.value(QStringLiteral("carryOffered")).toBool(), false);
    }

    // #102, C16 L13: a server whose disks this machine can't see answers for them.
    void aRemoteServerListsWhatIsThere()
    {
        ListingServer server;
        server.folders.insert(QStringLiteral("/volume2/games"),
                              {QStringLiteral("/volume2/games/Open Arena"),
                               QStringLiteral("/volume2/games/notes.txt")});
        server.folders.insert(QStringLiteral("/mnt/archive/games"),
                              {QStringLiteral("/mnt/archive/games/Open Arena"),
                               QStringLiteral("/mnt/archive/games/Demo Disc"),
                               QStringLiteral("/mnt/archive/games/handheld"),
                               QStringLiteral("/mnt/archive/games/manual.pdf"),
                               QStringLiteral("/mnt/archive/games/Thumbs.db")});
        server.folders.insert(QStringLiteral("/mnt/archive/games/handheld"),
                              {QStringLiteral("/mnt/archive/games/handheld/Homebrew Pack"),
                               QStringLiteral("/mnt/archive/games/handheld/config")});
        omaseed::core::QbtClient client;
        client.setBaseUrl(server.url());
        m_engine->setClient(&client); // a new client empties the library: fill it again
        m_engine->torrents()->applyFullUpdate(library());
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);

        const QString games = QStringLiteral("/mnt/archive/games");
        const QString target = QStringLiteral("/volume2/games");
        QVariantMap plan = m_move->plan(games, target, false);
        // Asked, and nothing goes before the answer.
        QCOMPARE(plan.value(QStringLiteral("canMove")).toBool(), false);
        QVERIFY(checkTexts(plan).contains(
            QStringLiteral("Asking home whether files with these torrents' names are already "
                           "where they go…")));
        QVERIFY(!m_move->start(games, target, false));
        QCOMPARE(plan.value(QStringLiteral("loose")).toMap().value(QStringLiteral("state")),
                 QVariant(QStringLiteral("reading")));

        QTRY_VERIFY(m_move->plan(games, target, false).value(QStringLiteral("canMove")).toBool());
        plan = m_move->plan(games, target, false);
        const QStringList texts = checkTexts(plan);
        QVERIFY(texts.constFirst().startsWith(QStringLiteral("omaseed can't see home's disks.")));
        QVERIFY(texts.contains(QStringLiteral(
            "1 left out: /volume2/games already has a file with their file's name (no torrent "
            "owns it). qBittorrent would keep that file and report the move as done.")));
        QCOMPARE(plan.value(QStringLiteral("moveHashes")).toStringList(),
                 QStringList({QStringLiteral("c3"), QStringLiteral("b2")}));
        QVERIFY(server.asked.contains(QStringLiteral("/volume2/games/handheld")));

        const QVariantMap loose = plan.value(QStringLiteral("loose")).toMap();
        QCOMPARE(loose.value(QStringLiteral("state")).toString(), QStringLiteral("onServer"));
        QCOMPARE(loose.value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(plan.value(QStringLiteral("carryOffered")).toBool(), false);
        const QVariantList rows = m_move->looseRows(games);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("path")).toString(),
                 QStringLiteral("games/manual.pdf"));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("path")).toString(),
                 QStringLiteral("games/handheld/config"));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("what")).toString(),
                 QStringLiteral("stays: omaseed can't see home's disks"));

        // Without a clash the server's check says so, once.
        plan = m_move->plan(games, QStringLiteral("/volume2/new-games"), false);
        QTRY_VERIFY(m_move->plan(games, QStringLiteral("/volume2/new-games"), false)
                        .value(QStringLiteral("canMove"))
                        .toBool());
        QVERIFY(checkTexts(m_move->plan(games, QStringLiteral("/volume2/new-games"), false))
                    .contains(QStringLiteral(
                        "Nothing already there. home listed where the torrents go.")));
        m_engine->setClient(nullptr);
    }

    void aServerThatCantListSaysSo()
    {
        ListingServer server;
        server.unsupported = true;
        omaseed::core::QbtClient client;
        client.setBaseUrl(server.url());
        m_engine->setClient(&client); // a new client empties the library: fill it again
        m_engine->torrents()->applyFullUpdate(library());
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);

        const QString games = QStringLiteral("/mnt/archive/games");
        const QString target = QStringLiteral("/volume2/games");
        m_move->plan(games, target, false);
        QTRY_VERIFY(m_move->plan(games, target, false).value(QStringLiteral("canMove")).toBool());
        const QVariantMap plan = m_move->plan(games, target, false);
        QVERIFY(checkTexts(plan).contains(
            QStringLiteral("Files already there: can't tell. home's qBittorrent can't list its "
                           "folders to omaseed.")));
        QCOMPARE(plan.value(QStringLiteral("loose")).toMap().value(QStringLiteral("state")),
                 QVariant(QStringLiteral("cantSee")));
        // Asked once; the server's answer stands for the rest of the session.
        const int asked = int(server.asked.size());
        m_move->plan(games, QStringLiteral("/volume2/other"), false);
        QTest::qWait(200);
        QCOMPARE(int(server.asked.size()), asked);
        m_engine->setClient(nullptr);
    }

    void aJobFromStartToSummary()
    {
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        QSignalSpy changed(m_move, &MoveController::changed);
        QVERIFY(m_move->start(
            QStringLiteral("/mnt/archive/games"), QStringLiteral("/mnt/fast/games"), false));
        QVERIFY(changed.count() > 0);
        QVERIFY(m_move->active());
        QCOMPARE(m_move->stateName(), QStringLiteral("running"));
        // M12: smallest first.
        QCOMPARE(m_move->jobHashes(),
                 QStringList({QStringLiteral("c3"), QStringLiteral("b2"), QStringLiteral("a1")}));
        QVariantMap band = m_move->band();
        QCOMPARE(band.value(QStringLiteral("kind")).toString(), QStringLiteral("accent"));
        QCOMPARE(band.value(QStringLiteral("headline")).toString(),
                 QStringLiteral("Moving games to /mnt/fast/games"));
        QVERIFY(band.value(QStringLiteral("line"))
                    .toString()
                    .startsWith(QStringLiteral("0 of 3 · 0 of 600 B · 0 %")));
        // M6: the first goes alone; it is "queued" until a poll reads it moving.
        QCOMPARE(m_move->stepFor(QStringLiteral("c3")).value(QStringLiteral("text")).toString(),
                 QStringLiteral("queued"));
        QCOMPARE(m_move->stepFor(QStringLiteral("a1")).value(QStringLiteral("text")).toString(),
                 QStringLiteral("waiting"));
        QVERIFY(m_move->stepFor(QStringLiteral("d4")).isEmpty());
        QCOMPARE(m_move->trayLine(), QStringLiteral("Moving games: 0 %"));
        QCOMPARE(
            m_move->quitLine(),
            QStringLiteral(
                "0 of 3 moved. Quitting pauses it: the 1 with qBittorrent finishes, and the rest "
                "wait until omaseed runs again."));
        // F3: the marks.
        QTRY_COMPARE(
            m_move->folders()->index(1).data(omaseed::core::FolderModel::MarkRole).toString(),
            QStringLiteral("0 %"));

        // The polls land them.
        for (int poll = 0; poll < 6 && m_move->stateName() != QStringLiteral("done"); ++poll)
            landSent();
        QCOMPARE(m_move->stateName(), QStringLiteral("done"));
        QVERIFY(!m_move->active());
        band = m_move->band();
        QCOMPARE(band.value(QStringLiteral("kind")).toString(), QStringLiteral("green"));
        QCOMPARE(band.value(QStringLiteral("headline")).toString(),
                 QStringLiteral("games moved to /mnt/fast/games"));
        QCOMPARE(m_move->trayLine(), QString());
        // E2: one notice.
        QCOMPARE(m_notices, QStringList({QStringLiteral("games moved | 3 moved")}));
        QVERIFY(m_move->detailsText().contains(
            QStringLiteral("moved · Homebrew Pack · /mnt/archive/games/handheld → "
                           "/mnt/fast/games/handheld")));

        // The summary stays across a restart until it is closed (M13).
        make();
        m_move->setServer(QStringLiteral("home"));
        QCOMPARE(m_move->stateName(), QStringLiteral("done"));
        m_move->closeSummary();
        QCOMPARE(m_move->stateName(), QStringLiteral("idle"));
        QVERIFY(m_move->band().isEmpty());
        make();
        m_move->setServer(QStringLiteral("home"));
        QVERIFY(!m_move->shown());
    }

    // #98, M17: after the torrents, the files no torrent owns follow; the summary says so, and
    // keeps saying so after a restart.
    void looseFilesFollowTheTorrents()
    {
        QTemporaryDir disks;
        const QString games = disks.filePath(QStringLiteral("archive/games"));
        const QString fast = disks.filePath(QStringLiteral("fast/games"));
        const auto put = [](const QString &path, const QByteArray &bytes, bool old) {
            QDir().mkpath(QFileInfo(path).path());
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(bytes);
            file.flush();
            if (old)
                file.setFileTime(QDateTime::currentDateTime().addDays(-2),
                                 QFileDevice::FileModificationTime);
        };
        put(games + u"/Open Arena/arena.bin", "torrent's own", true);
        put(games + u"/handheld/Homebrew Pack/pack.bin", "torrent's own", true);
        put(games + u"/manual.pdf", "loose", true);
        put(games + u"/handheld/saves/slot1.sav", "loose too", true);
        put(games + u"/still-writing.part.mkv", "recent", false);
        put(games + u"/Thumbs.db", "never counts", true);

        QJsonObject list{
            {QStringLiteral("a1"), torrent(QStringLiteral("Open Arena"), games, 300)},
            {QStringLiteral("c3"),
             torrent(QStringLiteral("Homebrew Pack"), games + u"/handheld", 100)},
        };
        m_engine->torrents()->applyFullUpdate(list);
        omaseed::core::FreeSpaceProbe probe([](const QString &) {
            omaseed::core::StorageReading reading;
            reading.valid = true;
            reading.exists = true;
            reading.bytesFree = 500LL << 30;
            reading.bytesTotal = 1000LL << 30;
            reading.rootPath = QStringLiteral("/");
            return reading;
        });
        probe.setServer(games, 500LL << 30, omaseed::core::PathMap(), false);
        QVERIFY(probe.seesServerDisks());
        const auto withProbe = [&]() {
            delete m_move;
            m_move = new MoveController(this);
            MoveController::Hooks hooks;
            hooks.engine = m_engine;
            hooks.probe = &probe;
            hooks.lowDiskBytes = []() { return qint64(0); };
            hooks.serverName = []() { return QStringLiteral("home"); };
            hooks.notify = [this](const QString &summary, const QString &body) {
                m_notices << summary + QStringLiteral(" | ") + body;
            };
            m_move->setHooks(std::move(hooks));
            m_move->setServer(QStringLiteral("home"));
        };
        withProbe();

        // The sheet's dry run.
        m_move->plan(games, fast, true);
        QTRY_COMPARE(m_move->looseRows(games).size(), 4);
        QVERIFY(m_move->start(games, fast, true));
        for (int poll = 0; poll < 6 && m_move->stateName() == QStringLiteral("running"); ++poll)
            landSent();
        QTRY_COMPARE(m_move->stateName(), QStringLiteral("done"));

        QCOMPARE(QFile(fast + u"/manual.pdf").exists(), true);
        QCOMPARE(QFile(fast + u"/handheld/saves/slot1.sav").exists(), true);
        QVERIFY(!QFile::exists(games + u"/manual.pdf"));
        QVERIFY(!QFileInfo::exists(games + u"/handheld/saves")); // left empty, so removed
        QVERIFY(QFile::exists(games + u"/still-writing.part.mkv"));
        QVERIFY(QFile::exists(games + u"/Thumbs.db"));

        QVariantMap band = m_move->band();
        QCOMPARE(band.value(QStringLiteral("headline")).toString(),
                 QStringLiteral("games moved, loose files too"));
        const QString line = band.value(QStringLiteral("line")).toString();
        QVERIFY2(line.startsWith(QStringLiteral("2 torrents and 2 files at ") + fast),
                 qPrintable(line));
        QVERIFY2(line.contains(QStringLiteral("stayed at %1: 1 recent file").arg(games)),
                 qPrintable(line));
        QVERIFY2(line.contains(QStringLiteral("1 empty folder removed there")), qPrintable(line));
        QStringList keys;
        for (const QVariant &action : band.value(QStringLiteral("actions")).toList())
            keys << action.toMap().value(QStringLiteral("id")).toString();
        QVERIFY2(keys.contains(QStringLiteral("stayed")), qPrintable(keys.join(u',')));
        QCOMPARE(m_notices.size(), 1);
        QVERIFY2(m_notices.first().contains(QStringLiteral("2 loose files carried")),
                 qPrintable(m_notices.first()));
        const QVariantList rows = m_move->carryRows();
        QCOMPARE(rows.size(), 3);
        QVERIFY(rows.first()
                    .toMap()
                    .value(QStringLiteral("what"))
                    .toString()
                    .startsWith(QStringLiteral("stays: ")));

        // The record survives a restart, and goes with the summary.
        withProbe();
        QCOMPARE(m_move->stateName(), QStringLiteral("done"));
        QCOMPARE(m_move->band().value(QStringLiteral("headline")).toString(),
                 QStringLiteral("games moved, loose files too"));
        m_move->closeSummary();
        QVERIFY(
            !QFile::exists(MoveJob::defaultFilePath(QStringLiteral("home"))
                               .replace(QStringLiteral(".json"), QStringLiteral(".carry.json"))));
        delete m_move;
        m_move = nullptr;
    }

    // T3, board 42 §8: qBittorrent's "No space left on device" reads "… is full".
    void aFullDiskSaysFull()
    {
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        QVERIFY(m_move->start(
            QStringLiteral("/mnt/archive/games"), QStringLiteral("/mnt/fast/games"), false));
        landSent(); // the probe lands; the next ones go
        QVERIFY(m_move->job()->moved() == 1);
        const QString next = m_move->jobHashes().at(1);
        m_move->job()->onFailed(next,
                                QDateTime::currentDateTime(),
                                QStringLiteral("No space left on device"),
                                QStringLiteral("diskFull"),
                                QStringLiteral("Failed to move torrent. …"));
        QTRY_COMPARE(m_move->job()->pauseReason(), MoveJob::PauseReason::NoSpace);
        landSent(); // what was already with qBittorrent finishes; then it is paused
        QCOMPARE(m_move->stateName(), QStringLiteral("paused"));
        QCOMPARE(m_move->band().value(QStringLiteral("headline")).toString(),
                 QStringLiteral("The move of games is paused: /mnt/fast/games is full"));
    }

    // ---- board 43, DECISIONS C16 (#103): every move is a job -------------------------------

    void aQuickSelectionIsANoteNotABand()
    {
        m_move->setServer(QStringLiteral("home"));
        QSignalSpy notes(m_move, &MoveController::note);
        // L1: set location's torrents go through the job, smallest first, the first alone.
        QVERIFY(m_move->startSelection({QStringLiteral("a1")},
                                       QStringLiteral("/mnt/fast/shooters"),
                                       QStringLiteral("oneFolder")));
        QCOMPARE(m_move->job()->kind(), QStringLiteral("selection"));
        QCOMPARE(m_move->title(), QStringLiteral("Open Arena"));
        QVERIFY(!m_move->shown());
        QVERIFY(!m_move->active()); // a quit asks nothing (board 43 §9)
        QVERIFY(m_move->band().isEmpty());
        landSent();
        // L7: it landed before the band was due: a note, no summary, no notice.
        QCOMPARE(notes.count(), 1);
        QCOMPARE(notes.at(0).at(0).toString(),
                 QStringLiteral("Open Arena moved to /mnt/fast/shooters"));
        QCOMPARE(notes.at(0).at(2).toBool(), false);
        QTRY_COMPARE(m_move->stateName(), QStringLiteral("idle"));
        QVERIFY(m_notices.isEmpty());
        // L4: the destination is remembered for this server.
        QCOMPARE(m_recents.value(QStringLiteral("home")),
                 QStringList({QStringLiteral("/mnt/fast/shooters")}));
    }

    void aQuickFailureIsAFailedNote()
    {
        m_move->setServer(QStringLiteral("home"));
        QSignalSpy notes(m_move, &MoveController::note);
        QVERIFY(m_move->startSelection(
            {QStringLiteral("a1")}, QStringLiteral("/nope/x"), QStringLiteral("oneFolder")));
        m_move->job()->onSendResult(
            {QStringLiteral("a1")},
            omaseed::core::ApiError(omaseed::core::ApiError::Kind::Conflict,
                                    QStringLiteral("Cannot make save path")));
        QCOMPARE(m_move->stateName(), QStringLiteral("done"));
        QVERIFY(!m_move->shown());
        QCOMPARE(notes.count(), 1);
        QCOMPARE(notes.at(0).at(0).toString(),
                 QStringLiteral("Open Arena didn't move: qBittorrent can't create /nope/x"));
        QCOMPARE(notes.at(0).at(1).toStringList(), QStringList({QStringLiteral("a1")}));
        QCOMPARE(notes.at(0).at(2).toBool(), true);
    }

    void aSlowSelectionShowsItsBandThenMovesBack()
    {
        m_move->setServer(QStringLiteral("home"));
        m_move->job()->setShowDelay(std::chrono::milliseconds(20));
        QVERIFY(m_move->startSelection({QStringLiteral("b2"), QStringLiteral("c3")},
                                       QStringLiteral("/mnt/fast/all"),
                                       QStringLiteral("oneFolder")));
        QCOMPARE(m_move->title(), QStringLiteral("2 torrents"));
        QVERIFY(!m_move->shown());
        QTRY_VERIFY(m_move->shown()); // L6: it ran past the delay
        QVERIFY(m_move->active());
        QCOMPARE(m_move->band().value(QStringLiteral("headline")).toString(),
                 QStringLiteral("Moving 2 torrents to /mnt/fast/all"));
        landSent();
        landSent();
        QCOMPARE(m_move->stateName(), QStringLiteral("done"));
        QVariantMap band = m_move->band();
        QCOMPARE(band.value(QStringLiteral("headline")).toString(),
                 QStringLiteral("2 torrents moved to /mnt/fast/all"));
        QStringList ids;
        for (const QVariant &item : band.value(QStringLiteral("actions")).toList())
            ids << item.toMap().value(QStringLiteral("id")).toString();
        QVERIFY(ids.contains(QStringLiteral("moveBack")));
        // L8: watched land, in under a minute: no notice.
        QVERIFY(m_notices.isEmpty());

        // L11: each back to where it was.
        const QVariantMap back = m_move->planBack();
        QCOMPARE(back.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Move 2 torrents back"));
        QCOMPARE(back.value(QStringLiteral("moveText")).toString(), QStringLiteral("move 2 back"));
        QCOMPARE(back.value(QStringLiteral("pathRows")).toList().size(), 2);
        QVERIFY(m_move->startBack());
        QCOMPARE(m_move->job()->kind(), QStringLiteral("back"));
        const MoveJob::Entry *c3 = m_move->job()->entry(QStringLiteral("c3"));
        QCOMPARE(c3->to, QStringLiteral("/mnt/archive/games/handheld"));
        landSent();
        landSent();
        QCOMPARE(m_engine->torrents()->torrent(QStringLiteral("b2")).savePath,
                 QStringLiteral("/mnt/archive/games"));
    }

    void moveBackOutlivesTheSummaryAndUndoesInOrder()
    {
        // Board 45 D7–D8 (C18, #133): Diogo's case. Two torrents moved, one of them moved
        // again; the first move's way back names the second, which goes back first.
        const QString b2 = QStringLiteral("b2");
        const QString c3 = QStringLiteral("c3");
        m_move->setServer(QStringLiteral("home"));
        QVERIFY(m_move->startSelection(
            {b2, c3}, QStringLiteral("/mnt/fast/all"), QStringLiteral("oneFolder")));
        landSent();
        landSent();
        // A quick move: no summary to close, and it is in the history all the same.
        QTRY_COMPARE(m_move->stateName(), QStringLiteral("idle"));
        const QVariantMap first = m_move->newestMove();
        const QString firstId = first.value(QStringLiteral("id")).toString();
        QVERIFY(!firstId.isEmpty());
        QCOMPARE(first.value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(first.value(QStringLiteral("where")).toString(), QStringLiteral("/mnt/fast/all"));
        QCOMPARE(m_history.value(QStringLiteral("home")).size(), 1);

        QVERIFY(m_move->startSelection(
            {b2}, QStringLiteral("/mnt/elsewhere"), QStringLiteral("oneFolder")));
        landSent();
        QTRY_COMPARE(m_move->stateName(), QStringLiteral("idle"));
        const QString secondId = m_move->newestMove().value(QStringLiteral("id")).toString();
        QVERIFY(secondId != firstId);
        // The details: each torrent's last move.
        QCOMPARE(m_move->lastMoveOf(b2).value(QStringLiteral("id")).toString(), secondId);
        const QVariantMap c3Move = m_move->lastMoveOf(c3);
        QCOMPARE(c3Move.value(QStringLiteral("id")).toString(), firstId);
        QCOMPARE(c3Move.value(QStringLiteral("from")).toString(),
                 QStringLiteral("/mnt/archive/games/handheld"));
        QCOMPARE(c3Move.value(QStringLiteral("others")).toInt(), 1);
        QVERIFY(c3Move.value(QStringLiteral("canBackThis")).toBool());
        // The log: one line per move, oldest first.
        const QVariantList lines = m_move->moveLines();
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).toMap().value(QStringLiteral("message")).toString(),
                 QStringLiteral("omaseed moved 2 torrents to /mnt/fast/all"));
        QCOMPARE(lines.at(1).toMap().value(QStringLiteral("message")).toString(),
                 QStringLiteral("omaseed moved \"Demo Disc\" to /mnt/elsewhere"));

        // The first move back: c3 goes, b2 stays, named with the move that took it.
        QVariantMap sheet = m_move->planBackOf(firstId);
        QCOMPARE(sheet.value(QStringLiteral("moveText")).toString(),
                 QStringLiteral("move 1 back · 1 stay"));
        const QVariantMap inTheWay = backFirstLine(sheet);
        QCOMPARE(inTheWay.value(QStringLiteral("backFirst")).toString(), secondId);
        QCOMPARE(inTheWay.value(QStringLiteral("link")).toString(),
                 QStringLiteral("move that one back first"));
        QVERIFY(inTheWay.value(QStringLiteral("text"))
                    .toString()
                    .startsWith(QStringLiteral(
                        "1 was moved again since, by the move to /mnt/elsewhere (")));
        QCOMPARE(inTheWay.value(QStringLiteral("hashes")).toStringList(), QStringList{b2});

        // Taken up again after a restart.
        make();
        m_move->setServer(QStringLiteral("home"));
        QCOMPARE(m_move->newestMove().value(QStringLiteral("id")).toString(), secondId);

        // "Move that one back first": b2 lands where the first move left it, and the second
        // move leaves the history.
        QVERIFY(m_move->startBackOf(secondId));
        landSent();
        QTRY_COMPARE(m_move->stateName(), QStringLiteral("idle"));
        QCOMPARE(m_engine->torrents()->torrent(b2).savePath, QStringLiteral("/mnt/fast/all"));
        QCOMPARE(m_move->newestMove().value(QStringLiteral("id")).toString(), firstId);
        // The first is whole again.
        sheet = m_move->planBackOf(firstId);
        QCOMPARE(sheet.value(QStringLiteral("moveText")).toString(), QStringLiteral("move 2 back"));
        QVERIFY(backFirstLine(sheet).isEmpty());
        QVERIFY(m_move->startBackOf(firstId));
        landSent();
        landSent();
        QTRY_COMPARE(m_move->stateName(), QStringLiteral("idle"));
        QCOMPARE(m_engine->torrents()->torrent(c3).savePath,
                 QStringLiteral("/mnt/archive/games/handheld"));
        QVERIFY(m_move->newestMove().isEmpty());
        QCOMPARE(m_history.value(QStringLiteral("home")).size(), 0);
    }

    void aTorrentMovedOutsideOmaseedStays()
    {
        // C18 D7: not where the move put it, and no later move here did it.
        m_move->setServer(QStringLiteral("home"));
        QVERIFY(m_move->startSelection({QStringLiteral("b2"), QStringLiteral("c3")},
                                       QStringLiteral("/mnt/fast/all"),
                                       QStringLiteral("oneFolder")));
        landSent();
        landSent();
        QTRY_COMPARE(m_move->stateName(), QStringLiteral("idle"));
        const QString id = m_move->newestMove().value(QStringLiteral("id")).toString();
        m_engine->torrents()->applyPartial(
            {{QStringLiteral("c3"),
              QJsonObject{{QStringLiteral("save_path"), QStringLiteral("/mnt/by/hand")}}}},
            {});
        Q_EMIT m_engine->synced(false);
        const QVariantMap sheet = m_move->planBackOf(id);
        QCOMPARE(sheet.value(QStringLiteral("moveText")).toString(),
                 QStringLiteral("move 1 back · 1 stay"));
        QVERIFY(checkTexts(sheet).contains(
            QStringLiteral("1 was moved since outside omaseed: it stays where it is now.")));
        // "this one only", on the one that moved away: nothing to move.
        QCOMPARE(
            m_move->planBackOf(id, QStringLiteral("c3")).value(QStringLiteral("canMove")).toBool(),
            false);
        QVERIFY(!m_move->lastMoveOf(QStringLiteral("c3"))
                     .value(QStringLiteral("canBackThis"))
                     .toBool());
    }

    void oneBigTorrentSaysWhatItCanTell()
    {
        // L10 (B2): without a view of the disks, no progress and no time left, rather than a
        // guess.
        m_move->setServer(QStringLiteral("home"));
        m_move->job()->setShowDelay(std::chrono::milliseconds(0));
        QVERIFY(m_move->startSelection(
            {QStringLiteral("d4")}, QStringLiteral("/mnt/usb/films"), QStringLiteral("oneFolder")));
        m_engine->torrents()->applyPartial(
            {{QStringLiteral("d4"),
              QJsonObject{{QStringLiteral("state"), QStringLiteral("moving")}}}},
            {});
        Q_EMIT m_engine->synced(false);
        QTRY_VERIFY(m_move->shown());
        const QVariantMap band = m_move->band();
        QCOMPARE(band.value(QStringLiteral("headline")).toString(),
                 QStringLiteral("Moving Big Film to /mnt/usb/films"));
        QVERIFY(band.value(QStringLiteral("line"))
                    .toString()
                    .contains(QStringLiteral("qBittorrent doesn't report a single move's "
                                             "progress, and omaseed can't see home's disks")));
        QCOMPARE(band.value(QStringLiteral("progress")).toDouble(), -1.0);
        QStringList ids;
        for (const QVariant &item : band.value(QStringLiteral("actions")).toList())
            ids << item.toMap().value(QStringLiteral("id")).toString();
        QVERIFY(!ids.contains(QStringLiteral("pause"))); // it is with qBittorrent already
        QCOMPARE(m_move->stepFor(QStringLiteral("d4")).value(QStringLiteral("progress")).toInt(),
                 -1);
    }

    void aSelectionWaitsBehindAFolder()
    {
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        QVERIFY(m_move->start(
            QStringLiteral("/mnt/archive/games"), QStringLiteral("/mnt/fast/games"), false));
        // Q4: checked now, sent when the folder's move ends.
        const QVariantMap sheet = m_move->planSelection(
            {QStringLiteral("d4")}, QStringLiteral("/mnt/fast/films"), QStringLiteral("oneFolder"));
        QCOMPARE(sheet.value(QStringLiteral("moveText")).toString(),
                 QStringLiteral("move after games"));
        QVERIFY(m_move->startSelection({QStringLiteral("d4")},
                                       QStringLiteral("/mnt/fast/films"),
                                       QStringLiteral("oneFolder")));
        QCOMPARE(m_move->queued(), QStringList({QStringLiteral("Big Film")}));
        QVERIFY(m_move->band()
                    .value(QStringLiteral("line"))
                    .toString()
                    .endsWith(QStringLiteral("next: Big Film to /mnt/fast/films")));
        for (int i = 0; i < 3; ++i)
            landSent();
        QCOMPARE(m_move->stateName(), QStringLiteral("done"));
        m_move->closeSummary();
        QCOMPARE(m_move->job()->kind(), QStringLiteral("selection"));
        QCOMPARE(m_move->title(), QStringLiteral("Big Film"));
        QCOMPARE(m_move->job()->entry(QStringLiteral("d4"))->to, QStringLiteral("/mnt/fast/films"));
    }

    void aSummaryStaysWhileTheMoveQueuedBehindItRuns()
    {
        // L12 (Q4): the queued move goes when the folder's ends, under its summary, which stays
        // until it is closed (#105).
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        QVERIFY(m_move->start(
            QStringLiteral("/mnt/archive/games"), QStringLiteral("/mnt/fast/games"), false));
        QVERIFY(m_move->startSelection({QStringLiteral("d4")},
                                       QStringLiteral("/mnt/fast/films"),
                                       QStringLiteral("oneFolder")));
        QSignalSpy notes(m_move, &MoveController::note);
        for (int i = 0; i < 3; ++i)
            landSent();
        QTRY_COMPARE(m_move->job()->kind(), QStringLiteral("selection"));
        QVERIFY(m_move->shown());
        QCOMPARE(m_move->stateName(), QStringLiteral("done"));
        QCOMPARE(m_move->kind(), QStringLiteral("folder"));
        QCOMPARE(m_move->band().value(QStringLiteral("headline")).toString(),
                 QStringLiteral("games moved to /mnt/fast/games"));
        QVERIFY(m_move->queued().isEmpty());
        // The quick one lands unseen: its note, and the summary still there.
        landSent();
        QTRY_COMPARE(notes.count(), 1);
        QCOMPARE(notes.at(0).at(0).toString(), QStringLiteral("Big Film moved to /mnt/fast/films"));
        QCOMPARE(m_engine->torrents()->torrent(QStringLiteral("d4")).savePath,
                 QStringLiteral("/mnt/fast/films"));
        QVERIFY(m_move->shown());
        QCOMPARE(m_move->band().value(QStringLiteral("headline")).toString(),
                 QStringLiteral("games moved to /mnt/fast/games"));
        QCOMPARE(m_move->jobHashes().size(), 3);
        QCOMPARE(m_move->stepFor(QStringLiteral("a1")).value(QStringLiteral("text")).toString(),
                 m_move->stepFor(QStringLiteral("b2")).value(QStringLiteral("text")).toString());
        m_move->closeSummary();
        QVERIFY(!m_move->shown());
        QTRY_COMPARE(m_move->stateName(), QStringLiteral("idle"));
    }

    void aNewMoveRunsUnderTheSummaryAndMoveBackIsTheSummarys()
    {
        m_move->setServer(QStringLiteral("home"));
        m_move->job()->setShowDelay(std::chrono::milliseconds(0));
        QVERIFY(m_move->startSelection({QStringLiteral("b2"), QStringLiteral("c3")},
                                       QStringLiteral("/mnt/fast/all"),
                                       QStringLiteral("oneFolder")));
        QTRY_VERIFY(m_move->shown());
        landSent();
        landSent();
        QCOMPARE(m_move->stateName(), QStringLiteral("done"));
        // #105: the record names where they came from.
        QVERIFY(m_move->detailsText().contains(QStringLiteral("from 2 places to /mnt/fast/all")));

        m_move->job()->setShowDelay(std::chrono::hours(1));
        QSignalSpy notes(m_move, &MoveController::note);
        QVERIFY(m_move->startSelection({QStringLiteral("d4")},
                                       QStringLiteral("/mnt/fast/films"),
                                       QStringLiteral("oneFolder")));
        QVERIFY(m_move->shown());
        QCOMPARE(m_move->band().value(QStringLiteral("headline")).toString(),
                 QStringLiteral("2 torrents moved to /mnt/fast/all"));
        QVERIFY(m_move->detailsText().contains(QStringLiteral("from 2 places to /mnt/fast/all")));
        landSent();
        QTRY_COMPARE(notes.count(), 1);
        QCOMPARE(m_move->band().value(QStringLiteral("headline")).toString(),
                 QStringLiteral("2 torrents moved to /mnt/fast/all"));
        // Move back is the summary's: the two, not the film.
        const QVariantMap back = m_move->planBack();
        QCOMPARE(back.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Move 2 torrents back"));
        QVERIFY(m_move->startBack());
        QCOMPARE(m_move->job()->kind(), QStringLiteral("back"));
        QVERIFY(m_move->job()->entry(QStringLiteral("d4")) == nullptr);
        QCOMPARE(m_move->job()->entry(QStringLiteral("c3"))->to,
                 QStringLiteral("/mnt/archive/games/handheld"));
    }

    void timeLeftSaysWhatItCanTell()
    {
        using omaseed::app::movewords::timeLeft;
        QCOMPARE(timeLeft(-1), QString());
        QCOMPARE(timeLeft(12), QStringLiteral("less than a minute left"));
        QCOMPARE(timeLeft(59), QStringLiteral("less than a minute left"));
        QCOMPARE(timeLeft(80), QStringLiteral("about 1 min left"));
        QCOMPARE(timeLeft(100), QStringLiteral("about 2 min left"));
        QCOMPARE(timeLeft(3000), QStringLiteral("about 50 min left"));
        QCOMPARE(timeLeft(7800), QStringLiteral("about 2 h 10 min left"));
    }

    void placesAreRecentsFoldersAndCategories()
    {
        m_recents.insert(QStringLiteral("home"),
                         {QStringLiteral("/mnt/fast/films"), QStringLiteral("/srv/old")});
        m_engine->categories()->applyFullUpdate(QJsonObject{
            {QStringLiteral("incoming"),
             QJsonObject{{QStringLiteral("name"), QStringLiteral("incoming")},
                         {QStringLiteral("savePath"), QStringLiteral("/mnt/fast/incoming")}}},
            {QStringLiteral("films"),
             QJsonObject{{QStringLiteral("name"), QStringLiteral("films")},
                         {QStringLiteral("savePath"), QStringLiteral("/srv/media/films")}}}});
        make();
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        QStringList paths;
        QStringList kinds;
        for (const QVariant &place : m_move->places(QStringLiteral("fast"))) {
            paths << place.toMap().value(QStringLiteral("path")).toString();
            kinds << place.toMap().value(QStringLiteral("kind")).toString();
        }
        QCOMPARE(
            paths,
            QStringList({QStringLiteral("/mnt/fast/films"), QStringLiteral("/mnt/fast/incoming")}));
        QCOMPARE(kinds, QStringList({QStringLiteral("recent"), QStringLiteral("category")}));
        // Unfiltered: the library's save paths with their counts; a category already among
        // them once.
        const QVariantList all = m_move->places(QString());
        QStringList allPaths;
        for (const QVariant &place : all)
            allPaths << place.toMap().value(QStringLiteral("path")).toString();
        QCOMPARE(allPaths.count(QStringLiteral("/srv/media/films")), 1);
        QVERIFY(allPaths.contains(QStringLiteral("/mnt/archive/games/handheld")));
    }

    void theQuietSheetSaysOneLine()
    {
        // L3, with a view of the disks: every check passed, so one green line.
        QTemporaryDir disks;
        const QString archive = disks.filePath(QStringLiteral("archive"));
        const QString fast = disks.filePath(QStringLiteral("fast"));
        QDir().mkpath(archive + QStringLiteral("/games/handheld"));
        QDir().mkpath(fast);
        omaseed::core::FreeSpaceProbe probe([&](const QString &path) {
            omaseed::core::StorageReading reading;
            reading.valid = true;
            reading.exists = QFileInfo::exists(path);
            reading.rootPath = path.startsWith(fast) ? fast : archive;
            reading.bytesFree = 400LL * 1024 * 1024 * 1024;
            reading.bytesTotal = 1000LL * 1024 * 1024 * 1024;
            return reading;
        });
        probe.setServer(archive, 400LL * 1024 * 1024 * 1024, omaseed::core::PathMap(), false);
        QVERIFY(probe.seesServerDisks());
        m_probe = &probe;
        m_engine->torrents()->applyFullUpdate(QJsonObject{
            {QStringLiteral("e5"),
             torrent(QStringLiteral("Open Arena"), archive + QStringLiteral("/games"), 1000)},
            {QStringLiteral("f6"),
             torrent(QStringLiteral("Demo"), archive + QStringLiteral("/games/handheld"), 10)}});
        make();
        m_move->setServer(QStringLiteral("home"));

        QVariantMap sheet = m_move->planSelection(
            {QStringLiteral("e5")}, archive + QStringLiteral("/old"), QStringLiteral("oneFolder"));
        QCOMPARE(sheet.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Move Open Arena"));
        QCOMPARE(sheet.value(QStringLiteral("moveText")).toString(), QStringLiteral("move"));
        QCOMPARE(checkTexts(sheet),
                 QStringList({QStringLiteral(
                     "Same drive: a rename, it lands at once. Room and access are fine.")}));

        sheet = m_move->planSelection({QStringLiteral("e5"), QStringLiteral("f6")},
                                      fast + QStringLiteral("/library"),
                                      QStringLiteral("keepPlaces"));
        QVERIFY(sheet.value(QStringLiteral("ruleOffered")).toBool());
        QCOMPARE(sheet.value(QStringLiteral("commonRoot")).toString(),
                 archive + QStringLiteral("/games"));
        QCOMPARE(sheet.value(QStringLiteral("pathRows")).toList().size(), 2);
        const QStringList texts = checkTexts(sheet);
        QCOMPARE(texts.size(), 1);
        QVERIFY(texts.constFirst().startsWith(QStringLiteral("Room and access are fine on ")));

        // Something to say: the whole of S1's lines, word for word.
        QFile clash(fast + QStringLiteral("/library/Open Arena"));
        QDir().mkpath(fast + QStringLiteral("/library"));
        QVERIFY(clash.open(QIODevice::WriteOnly));
        clash.close();
        sheet = m_move->planSelection({QStringLiteral("e5"), QStringLiteral("f6")},
                                      fast + QStringLiteral("/library"),
                                      QStringLiteral("oneFolder"));
        QVERIFY(checkTexts(sheet).size() > 2);
        QCOMPARE(sheet.value(QStringLiteral("moveText")).toString(),
                 QStringLiteral("move 1 torrent"));
        // The note names the one that moved, not the selection ("2 torrents", #105).
        QSignalSpy notes(m_move, &MoveController::note);
        QVERIFY(m_move->startSelection({QStringLiteral("e5"), QStringLiteral("f6")},
                                       fast + QStringLiteral("/library"),
                                       QStringLiteral("oneFolder")));
        landSent();
        QTRY_COMPARE(notes.count(), 1);
        QCOMPARE(notes.at(0).at(0).toString(),
                 QStringLiteral("Demo moved to %1/library").arg(fast));
        m_probe = nullptr;
    }

    void anEmptyFileInTheWayIsReplaced()
    {
        // DECISIONS C28 (#176): the left-out line offers "replace it" for an empty file no
        // torrent owns; the next start deletes it, then moves, and the log's line says so.
        QTemporaryDir disks;
        const QString archive = disks.filePath(QStringLiteral("archive"));
        const QString fast = disks.filePath(QStringLiteral("fast"));
        QDir().mkpath(archive + QStringLiteral("/games"));
        QDir().mkpath(fast + QStringLiteral("/library"));
        omaseed::core::FreeSpaceProbe probe([&](const QString &path) {
            omaseed::core::StorageReading reading;
            reading.valid = true;
            reading.exists = QFileInfo::exists(path);
            reading.rootPath = path.startsWith(fast) ? fast : archive;
            reading.bytesFree = 400LL * 1024 * 1024 * 1024;
            reading.bytesTotal = 1000LL * 1024 * 1024 * 1024;
            return reading;
        });
        probe.setServer(archive, 400LL * 1024 * 1024 * 1024, omaseed::core::PathMap(), false);
        QVERIFY(probe.seesServerDisks());
        m_probe = &probe;
        m_engine->torrents()->applyFullUpdate(QJsonObject{
            {QStringLiteral("e5"),
             torrent(QStringLiteral("Open Arena"), archive + QStringLiteral("/games"), 1000)},
            {QStringLiteral("f6"),
             torrent(QStringLiteral("Demo"), archive + QStringLiteral("/games"), 10)}});
        make();
        m_move->setServer(QStringLiteral("home"));
        const QString to = fast + QStringLiteral("/library");
        const QString empty = to + QStringLiteral("/Open Arena");
        const QString full = to + QStringLiteral("/Demo");
        {
            QFile file(empty);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QFile other(full);
            QVERIFY(other.open(QIODevice::WriteOnly));
            other.write("x");
        }
        const auto leftOutLine = [](const QVariantMap &plan) {
            for (const QVariant &line : plan.value(QStringLiteral("checks")).toList()) {
                if (line.toMap()
                        .value(QStringLiteral("text"))
                        .toString()
                        .contains(QStringLiteral("left out")))
                    return line.toMap();
            }
            return QVariantMap();
        };

        // A file of a byte is someone's: no offer.
        QVariantMap sheet =
            m_move->planSelection({QStringLiteral("f6")}, to, QStringLiteral("oneFolder"));
        QVERIFY(!leftOutLine(sheet).isEmpty());
        QVERIFY(!leftOutLine(sheet).contains(QStringLiteral("replace")));

        sheet = m_move->planSelection({QStringLiteral("e5")}, to, QStringLiteral("oneFolder"));
        QVERIFY(!sheet.value(QStringLiteral("canMove")).toBool());
        const QVariantMap line = leftOutLine(sheet);
        QVERIFY(line.value(QStringLiteral("text"))
                    .toString()
                    .contains(QStringLiteral("(an empty file no torrent owns)")));
        QCOMPARE(line.value(QStringLiteral("link")).toString(), QStringLiteral("replace it"));
        QCOMPARE(line.value(QStringLiteral("replace")).toStringList(), QStringList{empty});
        QCOMPARE(line.value(QStringLiteral("note")).toString(),
                 QStringLiteral("replace it: the empty file is deleted, then the move goes"));

        // Asked for a path the plan doesn't offer (the byte-long file): kept, and no move.
        m_move->replaceNext({full});
        QVERIFY(!m_move->startSelection({QStringLiteral("f6")}, to, QStringLiteral("oneFolder")));
        QVERIFY(QFileInfo::exists(full));

        // The file filled in since the sheet showed it: not deleted, nothing moves.
        {
            QFile file(empty);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("late");
        }
        m_move->replaceNext({empty});
        QVERIFY(!m_move->startSelection({QStringLiteral("e5")}, to, QStringLiteral("oneFolder")));
        QCOMPARE(QFileInfo(empty).size(), qint64(4));
        QVERIFY(QFile::resize(empty, 0));

        // Replace it: deleted, then moved; the record and the log's line say so.
        m_move->replaceNext({empty});
        QVERIFY(m_move->startSelection({QStringLiteral("e5")}, to, QStringLiteral("oneFolder")));
        QVERIFY(!QFileInfo::exists(empty));
        QVERIFY(QFileInfo::exists(full));
        QCOMPARE(m_move->job()->spec().value(QStringLiteral("replaced")).toVariant().toStringList(),
                 QStringList{empty});
        landSent();
        QTRY_COMPARE(m_move->stateName(), QStringLiteral("idle"));
        const QVariantList lines = m_move->moveLines();
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.constFirst().toMap().value(QStringLiteral("message")).toString(),
                 QStringLiteral("omaseed moved \"Open Arena\" to %1 after deleting the empty file "
                                "no torrent owned at %2")
                     .arg(to, empty));
        m_probe = nullptr;
    }

    void pauseEndAndTheRest()
    {
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        QVERIFY(m_move->start(
            QStringLiteral("/mnt/archive/games"), QStringLiteral("/mnt/fast/games"), false));
        landSent(); // the first one lands, the other two go
        m_move->pause();
        QCOMPARE(m_move->stateName(), QStringLiteral("pausing"));
        QCOMPARE(m_move->band().value(QStringLiteral("headline")).toString(),
                 QStringLiteral("Pausing the move of games"));
        m_move->end();
        QCOMPARE(m_move->stateName(), QStringLiteral("ending"));
        landSent();
        QCOMPARE(m_move->stateName(), QStringLiteral("ended"));
        const QVariantMap band = m_move->band();
        QCOMPARE(band.value(QStringLiteral("headline")).toString(),
                 QStringLiteral("The move of games ended early"));
        QStringList ids;
        for (const QVariant &item : band.value(QStringLiteral("actions")).toList())
            ids << item.toMap().value(QStringLiteral("id")).toString();
        QCOMPARE(ids,
                 QStringList({QStringLiteral("moveRest"),
                              QStringLiteral("moveBack"),
                              QStringLiteral("show"),
                              QStringLiteral("close")}));
    }

    // #174: the record "details" unfolds starts where the band's own words end, and says
    // what became of a torrent later in the table's words, with what fixes it.
    void theRecordSaysLaterInTheTablesWords()
    {
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        QVERIFY(m_move->start(
            QStringLiteral("/mnt/archive/games"), QStringLiteral("/mnt/fast/games"), false));
        for (int poll = 0; poll < 6 && m_move->stateName() != QStringLiteral("done"); ++poll)
            landSent();
        QCOMPARE(m_move->stateName(), QStringLiteral("done"));
        m_engine->torrents()->applyPartial(
            QJsonObject{{QStringLiteral("a1"),
                         QJsonObject{{QStringLiteral("state"), QStringLiteral("missingFiles")}}}},
            {});
        Q_EMIT m_engine->synced(false);
        const QString record = m_move->detailsText();
        QVERIFY2(record.startsWith(QStringLiteral("started ")), qPrintable(record));
        QVERIFY(!record.contains(m_move->band().value(QStringLiteral("headline")).toString()));
        QVERIFY2(record.contains(
                     QStringLiteral("    later: missing files · set location… or recheck it")),
                 qPrintable(record));
        QVERIFY(!record.contains(QStringLiteral("missingFiles")));
    }

    // DECISIONS C20 (#138): the job notes which torrents were running when it sent them,
    // keeps it through a pause and quit, and its recheck starts only those once complete.
    void aRecheckStartsOnlyWhatWasRunning()
    {
        // Demo Disc (b2) was stopped before the move; Open Arena (a1) was seeding.
        m_engine->torrents()->applyPartial(
            QJsonObject{{QStringLiteral("b2"),
                         QJsonObject{{QStringLiteral("state"), QStringLiteral("stoppedUP")}}}},
            {});
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        QVERIFY(m_move->start(
            QStringLiteral("/mnt/archive/games"), QStringLiteral("/mnt/fast/games"), false));
        landSent(); // the first (c3) lands; b2 and a1 go
        const auto wasRunning = [this](const char *hash) {
            const MoveJob::Entry *entry = m_move->job()->entry(QString::fromLatin1(hash));
            return entry != nullptr && entry->wasRunning;
        };
        QVERIFY(wasRunning("a1"));
        QVERIFY(!wasRunning("b2"));
        QVERIFY(wasRunning("c3"));

        // Pause and quit: saved with the job, read back at the next start.
        m_move->pauseForQuit();
        make();
        m_move->setServer(QStringLiteral("home"));
        QCOMPARE(m_move->stateName(), QStringLiteral("running"));
        QVERIFY(wasRunning("a1"));
        QVERIFY(!wasRunning("b2"));
        for (int poll = 0; poll < 6 && m_move->stateName() != QStringLiteral("done"); ++poll)
            landSent();
        QCOMPARE(m_move->stateName(), QStringLiteral("done"));

        // A qBittorrent for the recheck's calls (a client clears the rows: the library again,
        // where the job put it, both stopped by now).
        ActionServer server;
        QVERIFY(server.isListening());
        omaseed::core::QbtClient client;
        client.setBaseUrl(server.url());
        m_engine->setClient(&client);
        QJsonObject moved = library();
        for (const char *hash : {"a1", "b2"}) {
            QJsonObject row = moved.value(QLatin1String(hash)).toObject();
            row.insert(QStringLiteral("save_path"), QStringLiteral("/mnt/fast/games"));
            row.insert(QStringLiteral("state"), QStringLiteral("stoppedUP"));
            moved.insert(QLatin1String(hash), row);
        }
        m_engine->torrents()->applyFullUpdate(moved);

        // The sheet says which start: smallest first, b2 (200) then a1 (300).
        const QStringList both{QStringLiteral("a1"), QStringLiteral("b2")};
        const QVariantList rows = m_move->recheckRows(both);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("hash")).toString(), QStringLiteral("b2"));
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("starts")).toBool(), false);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("then")).toString(),
                 QStringLiteral("stays stopped"));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("starts")).toBool(), true);
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("then")).toString(),
                 QStringLiteral("starts if complete"));

        // Both come out complete; only a1 starts.
        m_move->recheckSmallestFirst(both);
        const auto checkOne = [this](const char *hash, const char *after) {
            const QString id = QString::fromLatin1(hash);
            m_engine->torrents()->applyPartial(
                QJsonObject{
                    {id, QJsonObject{{QStringLiteral("state"), QStringLiteral("checkingUP")}}}},
                {});
            Q_EMIT m_engine->synced(false);
            m_engine->torrents()->applyPartial(
                QJsonObject{{id,
                             QJsonObject{{QStringLiteral("state"), QString::fromLatin1(after)},
                                         {QStringLiteral("progress"), 1.0}}}},
                {});
            Q_EMIT m_engine->synced(false);
        };
        QTRY_VERIFY(server.calls.contains(QStringLiteral("recheck b2")));
        checkOne("b2", "stoppedUP");
        QTRY_VERIFY(server.calls.contains(QStringLiteral("recheck a1")));
        checkOne("a1", "stoppedUP");
        QTRY_VERIFY(server.calls.contains(QStringLiteral("start a1")));
        QCOMPARE(m_move->rechecksLeft(), 0);
        QVERIFY2(!server.calls.contains(QStringLiteral("start b2")),
                 qPrintable(server.calls.join(QStringLiteral(", "))));
        m_engine->setClient(nullptr);
    }

    void aHybridIsNotRecheckedButMovedAgain()
    {
        failAHybridAndAV1();
        if (QTest::currentTestFailed())
            return;
        const QStringList failed = m_move->failedHashes();

        // The sheet's rows: the hybrid listed as not rechecked, with why.
        const QVariantList rows = m_move->recheckRows(failed);
        QCOMPARE(rows.size(), 2);
        const QVariantMap hybrid = rows.at(0).toMap();
        QCOMPARE(hybrid.value(QStringLiteral("hash")).toString(), QStringLiteral("b2"));
        QCOMPARE(hybrid.value(QStringLiteral("hybrid")).toBool(), true);
        QCOMPARE(hybrid.value(QStringLiteral("starts")).toBool(), false);
        QCOMPARE(hybrid.value(QStringLiteral("then")).toString(), QStringLiteral("not rechecked"));
        QCOMPARE(hybrid.value(QStringLiteral("why")).toString(),
                 QStringLiteral("Its files may be split between the two places, and on a hybrid "
                                "torrent (v1 and v2) a recheck would put it in an error that only "
                                "adding it again clears."));
        const QVariantMap v1 = rows.at(1).toMap();
        QCOMPARE(v1.value(QStringLiteral("hash")).toString(), QStringLiteral("a1"));
        QCOMPARE(v1.value(QStringLiteral("hybrid")).toBool(), false);
        QCOMPARE(v1.value(QStringLiteral("then")).toString(), QStringLiteral("starts if complete"));
        QVERIFY(v1.value(QStringLiteral("why")).toString().isEmpty());

        // The recheck sends only the v1 one, and starts it once complete.
        ActionServer server;
        QVERIFY(server.isListening());
        omaseed::core::QbtClient client;
        client.setBaseUrl(server.url());
        m_engine->setClient(&client);
        // A client clears the rows: the library again, the two that failed where they were.
        m_engine->torrents()->applyFullUpdate(library());
        markHybrid();
        m_move->recheckSmallestFirst(failed);
        QTRY_VERIFY(server.calls.contains(QStringLiteral("recheck a1")));
        QCOMPARE(m_move->rechecksLeft(), 1); // a1 only, being rechecked
        m_engine->torrents()->applyPartial(
            QJsonObject{{QStringLiteral("a1"),
                         QJsonObject{{QStringLiteral("state"), QStringLiteral("checkingUP")}}}},
            {});
        Q_EMIT m_engine->synced(false);
        m_engine->torrents()->applyPartial(
            QJsonObject{{QStringLiteral("a1"),
                         QJsonObject{{QStringLiteral("state"), QStringLiteral("stoppedUP")}}}},
            {});
        Q_EMIT m_engine->synced(false);
        QTRY_VERIFY(server.calls.contains(QStringLiteral("start a1")));
        QCOMPARE(m_move->rechecksLeft(), 0);
        QTest::qWait(100);
        for (const QString &call : std::as_const(server.calls))
            QVERIFY2(!call.endsWith(QStringLiteral(" b2")), qPrintable(call));

        // The sheet's rows as listed, a hybrid among them: still never rechecked.
        server.calls.clear();
        m_move->recheckListed(rows);
        QTRY_VERIFY(server.calls.contains(QStringLiteral("recheck a1")));
        QVERIFY(!server.calls.contains(QStringLiteral("recheck b2")));
        m_engine->setClient(nullptr);
        m_engine->torrents()->applyFullUpdate(library());
        markHybrid();

        // Only hybrids: nothing to recheck.
        m_move->recheckSmallestFirst({QStringLiteral("b2")});
        QCOMPARE(m_move->rechecksLeft(), 0);

        // Move again: the hybrid alone, to the job's target, as a retry.
        QVERIFY(m_move->moveAgain(QStringLiteral("b2")));
        QCOMPARE(m_move->job()->kind(), QStringLiteral("retry"));
        QCOMPARE(m_move->jobHashes(), QStringList({QStringLiteral("b2")}));
        QCOMPARE(m_move->title(), QStringLiteral("Demo Disc"));
        QCOMPARE(m_move->job()->entry(QStringLiteral("b2"))->to, QStringLiteral("/mnt/fast/games"));
        landSent();
        QCOMPARE(m_engine->torrents()->torrent(QStringLiteral("b2")).savePath,
                 QStringLiteral("/mnt/fast/games"));
        QVERIFY(!m_move->moveAgain(QStringLiteral("c3"))); // never failed
    }

    void aFailedHybridMovesBack()
    {
        failAHybridAndAV1();
        if (QTest::currentTestFailed())
            return;
        // qBittorrent still has it where it was: nothing to send, and the note says so.
        QSignalSpy notes(m_move, &MoveController::note);
        QVERIFY(!m_move->moveFailedBack(QStringLiteral("b2")));
        QCOMPARE(notes.count(), 1);
        QCOMPARE(notes.at(0).at(0).toString(),
                 QStringLiteral("Demo Disc wasn't moved: It is already at /mnt/archive/games."));
        QCOMPARE(m_move->stateName(), QStringLiteral("done")); // the summary stays

        // qBittorrent has it at the target (a move it took for done): back to where it was.
        m_engine->torrents()->applyPartial(
            QJsonObject{
                {QStringLiteral("b2"),
                 QJsonObject{{QStringLiteral("save_path"), QStringLiteral("/mnt/fast/games")}}}},
            {});
        QVERIFY(m_move->moveFailedBack(QStringLiteral("b2")));
        QCOMPARE(m_move->job()->kind(), QStringLiteral("back"));
        QCOMPARE(m_move->jobHashes(), QStringList({QStringLiteral("b2")}));
        QCOMPARE(m_move->job()->entry(QStringLiteral("b2"))->to,
                 QStringLiteral("/mnt/archive/games"));
        landSent();
        QCOMPARE(m_engine->torrents()->torrent(QStringLiteral("b2")).savePath,
                 QStringLiteral("/mnt/archive/games"));
    }

    void quitPausesAndTheNextStartRunsAgain()
    {
        // J5: "pause and quit" is taken up again, running, at the next start.
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        QVERIFY(m_move->start(
            QStringLiteral("/mnt/archive/games"), QStringLiteral("/mnt/fast/games"), false));
        m_move->pauseForQuit();
        make();
        m_move->setServer(QStringLiteral("home"));
        QCOMPARE(m_move->stateName(), QStringLiteral("running"));
        QVERIFY(m_move->active());
    }

    void anotherServerHasItsOwnJob()
    {
        m_move->setServer(QStringLiteral("home"));
        QTRY_COMPARE(m_move->folders()->total(), 5);
        QVERIFY(m_move->start(
            QStringLiteral("/mnt/archive/games"), QStringLiteral("/mnt/fast/games"), false));
        m_move->setServer(QStringLiteral("other"));
        QVERIFY(!m_move->shown());
        m_move->setServer(QStringLiteral("home"));
        QVERIFY(m_move->active());
        QCOMPARE(m_move->root(), QStringLiteral("/mnt/archive/games"));
    }
};

QTEST_GUILESS_MAIN(TestMoveController)
#include "tst_movecontroller.moc"
