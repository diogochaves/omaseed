// MoveController (board 42, DECISIONS C15, #97): the rail's folders, the sheet's words and the
// job's band, against a SyncEngine whose rows the test writes itself (no server: sends get no
// answer, and the test lands the torrents by moving their save paths, as a poll would).

#include "core/FreeSpace.hpp"
#include "core/MoveJob.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/MoveController.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
        m_move->setHooks(std::move(hooks));
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

private slots:
    void initTestCase()
    {
        QVERIFY(m_config.isValid());
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    }

    void init()
    {
        m_notices.clear();
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
                 QStringLiteral(
                     "games · /mnt/archive/games · 3 torrents · 600 B on disk · 2 save paths"));
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
        QCOMPARE(
            ids,
            QStringList(
                {QStringLiteral("moveRest"), QStringLiteral("show"), QStringLiteral("close")}));
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
