#include "core/MovePlan.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using omaseed::core::FreeSpaceProbe;
using omaseed::core::MoveDisk;
using omaseed::core::MovePlan;
using omaseed::core::PathMap;
using omaseed::core::ServerFolder;
using omaseed::core::StorageReading;
using omaseed::core::Torrent;

namespace {

constexpr qint64 kGiB = 1024LL * 1024 * 1024;

Torrent torrent(const QString &name,
                const QString &savePath,
                qint64 completed,
                const QString &content = QString(),
                qint64 size = 0)
{
    static int serial = 0;
    Torrent t;
    t.hash = QStringLiteral("%1").arg(++serial, 40, 16, QLatin1Char('0'));
    t.name = name;
    t.savePath = savePath;
    t.contentPath = content.isEmpty() ? savePath + QLatin1Char('/') + name : content;
    t.completed = completed;
    t.size = size > 0 ? size : completed;
    return t;
}

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
}

} // namespace

// Board 42 §3 and §9 (DECISIONS C15 M4–M8, M12): the preflight.
class TestMovePlan : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_archive; ///< the source drive
    QString m_fast;    ///< the destination drive
    qint64 m_fastFree = 100 * kGiB;
    FreeSpaceProbe m_probe;

    /// Two drives under the temporary folder, as this machine sees the server's disks.
    void setUpProbe()
    {
        m_probe = FreeSpaceProbe([this](const QString &path) {
            StorageReading reading;
            reading.valid = true;
            reading.exists = QFileInfo::exists(path);
            if (path.startsWith(m_fast)) {
                reading.bytesFree = m_fastFree;
                reading.rootPath = m_fast;
            } else {
                reading.bytesFree = 500 * kGiB;
                reading.rootPath = m_archive;
            }
            reading.bytesTotal = 1000 * kGiB;
            return reading;
        });
        m_probe.setServer(m_archive, 500 * kGiB, PathMap(), false);
        QVERIFY(m_probe.seesServerDisks());
    }

    MovePlan::Input input(const QList<Torrent> &torrents, const QString &newPath)
    {
        MovePlan::Input in;
        in.torrents = torrents;
        in.root = m_archive + QStringLiteral("/games");
        in.newPath = newPath;
        in.probe = &m_probe;
        return in;
    }

private slots:
    void init()
    {
        QVERIFY(m_tmp.isValid());
        m_archive = m_tmp.filePath(QStringLiteral("archive"));
        m_fast = m_tmp.filePath(QStringLiteral("fast"));
        QDir().mkpath(m_archive + QStringLiteral("/games/handheld"));
        QDir().mkpath(m_fast);
        m_fastFree = 100 * kGiB;
        setUpProbe();
    }

    void cleanup()
    {
        QDir(m_archive).removeRecursively();
        QDir(m_fast).removeRecursively();
    }

    void keepsTheStructureSmallestFirst()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent big = torrent(QStringLiteral("Retro Collection"), games, 40 * kGiB);
        const Torrent small =
            torrent(QStringLiteral("Tiny Platformers"), games + "/handheld", kGiB);
        const Torrent mid = torrent(QStringLiteral("Open Arena"), games, 2 * kGiB);
        const Torrent elsewhere = torrent(QStringLiteral("Film"), m_archive + "/films", kGiB);
        const MovePlan plan =
            MovePlan::make(input({big, small, mid, elsewhere}, m_fast + QStringLiteral("/games")));
        QVERIFY(plan.canMove());
        QCOMPARE(plan.toMove(), QStringList({small.hash, mid.hash, big.hash}));
        QCOMPARE(plan.item(small.hash)->to, m_fast + QStringLiteral("/games/handheld"));
        QCOMPARE(plan.item(big.hash)->to, m_fast + QStringLiteral("/games"));
        QVERIFY(plan.item(elsewhere.hash) == nullptr);
        QCOMPARE(plan.pathRows().size(), 2);
        QCOMPARE(plan.pathRows().at(0).from, games);
        QCOMPARE(plan.pathRows().at(0).count, 2);
        QCOMPARE(plan.pathRows().at(1).to, m_fast + QStringLiteral("/games/handheld"));
        QCOMPARE(plan.torrentBytes(), 43 * kGiB);
        QCOMPARE(plan.room(), MovePlan::Check::Ok);
        QCOMPARE(plan.bytesFree(), 100 * kGiB);
        QCOMPARE(plan.drive(), m_fast);
        QVERIFY(plan.freeReadHere());
        QVERIFY(!plan.sameDrive());
        QCOMPARE(plan.writable(), MovePlan::Check::Ok);
    }

    void bytesOnDiskNotTorrentSize()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent partial =
            torrent(QStringLiteral("Freeware Classics"), games, 4 * kGiB, QString(), 200 * kGiB);
        const MovePlan plan = MovePlan::make(input({partial}, m_fast + QStringLiteral("/games")));
        QCOMPARE(plan.torrentBytes(), 4 * kGiB);
        QCOMPARE(plan.sizeOfTorrents(), 200 * kGiB);
        QCOMPARE(plan.partial(), 1);
        QCOMPARE(plan.room(), MovePlan::Check::Ok);
    }

    void roomBlocksBelowTheThreshold()
    {
        m_fastFree = 12 * kGiB;
        const QString games = m_archive + QStringLiteral("/games");
        MovePlan::Input in =
            input({torrent(QStringLiteral("a"), games, 9 * kGiB)}, m_fast + QStringLiteral("/g"));
        in.looseBytes = 2 * kGiB; // 11 GiB to move leaves 1 GiB, under 2 GiB
        const MovePlan plan = MovePlan::make(in);
        QCOMPARE(plan.room(), MovePlan::Check::Block);
        QCOMPARE(plan.blocker(), MovePlan::Blocker::NoRoom);
        QCOMPARE(plan.bytesToMove(), 11 * kGiB);
        QCOMPARE(plan.bytesFreeAfter(), kGiB);
    }

    void roomUnknownWithoutAView()
    {
        const QString games = m_archive + QStringLiteral("/games");
        MovePlan::Input in =
            input({torrent(QStringLiteral("a"), games, kGiB)}, QStringLiteral("/volume2/games"));
        FreeSpaceProbe remote([](const QString &) { return StorageReading(); });
        remote.setServer(QStringLiteral("/downloads"), 50 * kGiB, PathMap(), false);
        in.probe = &remote;
        const MovePlan plan = MovePlan::make(in);
        QVERIFY(!plan.seesDisks());
        QCOMPARE(plan.room(), MovePlan::Check::Unknown);
        QCOMPARE(plan.writable(), MovePlan::Check::Unknown);
        QVERIFY(plan.canMove());
    }

    // #102, C16 L13: without a view of the disks, M8's check runs on what the server lists.
    void withoutAViewTheServerListsTheTargets()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent clash = torrent(QStringLiteral("Clash"), games, 5);
        const Torrent fine =
            torrent(QStringLiteral("Fine"), games + QStringLiteral("/handheld"), 6);
        MovePlan::Input in = input({clash, fine}, QStringLiteral("/volume2/games"));
        FreeSpaceProbe remote([](const QString &) { return StorageReading(); });
        remote.setServer(QStringLiteral("/downloads"), 50 * kGiB, PathMap(), false);
        in.probe = &remote;
        in.disk.exists = [](const QString &) {
            QTest::qFail("the local disk was asked", __FILE__, __LINE__);
            return true;
        };

        // Nothing listed yet: the plan asks for both targets' folders and waits.
        MovePlan plan = MovePlan::make(in);
        QCOMPARE(plan.namesCheck(), MovePlan::NamesCheck::Pending);
        QCOMPARE(plan.needListings(),
                 QStringList({QStringLiteral("/volume2/games"),
                              QStringLiteral("/volume2/games/handheld")}));
        QVERIFY(plan.leftOut().isEmpty());

        ServerFolder listed;
        listed.state = ServerFolder::State::Listed;
        listed.names = {QStringLiteral("Clash"), QStringLiteral("handheld")};
        ServerFolder missing;
        missing.state = ServerFolder::State::Missing;
        in.serverFolders.insert(QStringLiteral("/volume2/games"), listed);
        in.serverFolders.insert(QStringLiteral("/volume2/games/handheld"), missing);
        plan = MovePlan::make(in);
        QCOMPARE(plan.namesCheck(), MovePlan::NamesCheck::Server);
        QVERIFY(plan.needListings().isEmpty());
        QCOMPARE(plan.leftOut(), QStringList({clash.hash}));
        QCOMPARE(plan.item(clash.hash)->why, QStringLiteral("/volume2/games/Clash"));
        QCOMPARE(plan.toMove(), QStringList({fine.hash}));
    }

    void withoutAViewAServerThatCantListCantTell()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent one = torrent(QStringLiteral("One"), games, 5);
        MovePlan::Input in = input({one}, QStringLiteral("/volume2/games"));
        FreeSpaceProbe remote([](const QString &) { return StorageReading(); });
        remote.setServer(QStringLiteral("/downloads"), 50 * kGiB, PathMap(), false);
        in.probe = &remote;

        // A qBittorrent without the endpoint: said once, then nothing is asked.
        in.serverLists = false;
        MovePlan plan = MovePlan::make(in);
        QCOMPARE(plan.namesCheck(), MovePlan::NamesCheck::Cannot);
        QVERIFY(plan.needListings().isEmpty());
        QCOMPARE(plan.toMove(), QStringList({one.hash}));

        // A listing that didn't come back: can't tell, and the move is still allowed (M5's
        // "unknown" rule), with the line that says so.
        in.serverLists = true;
        in.serverFolders.insert(QStringLiteral("/volume2/games"), ServerFolder());
        plan = MovePlan::make(in);
        QCOMPARE(plan.namesCheck(), MovePlan::NamesCheck::Cannot);
        QVERIFY(plan.canMove());

        ServerFolder unsupported;
        unsupported.state = ServerFolder::State::Unsupported;
        in.serverFolders.insert(QStringLiteral("/volume2/games"), unsupported);
        QCOMPARE(MovePlan::make(in).namesCheck(), MovePlan::NamesCheck::Cannot);
    }

    void withAViewTheServerIsNotAsked()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const MovePlan plan = MovePlan::make(
            input({torrent(QStringLiteral("a"), games, 1)}, m_fast + QStringLiteral("/games")));
        QCOMPARE(plan.namesCheck(), MovePlan::NamesCheck::Here);
        QVERIFY(plan.needListings().isEmpty());
    }

    void sameDriveNeedsNoRoom()
    {
        m_fastFree = 0;
        const QString games = m_archive + QStringLiteral("/games");
        const MovePlan plan = MovePlan::make(
            input({torrent(QStringLiteral("a"), games, 900 * kGiB)}, m_archive + "/old-games"));
        QVERIFY(plan.sameDrive());
        QCOMPARE(plan.room(), MovePlan::Check::Ok);
        QVERIFY(plan.canMove());
    }

    void blockers()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const QList<Torrent> one{torrent(QStringLiteral("a"), games, 1)};
        QCOMPARE(MovePlan::make(input(one, QString())).blocker(), MovePlan::Blocker::NoPath);
        QCOMPARE(MovePlan::make(input(one, games + "/")).blocker(), MovePlan::Blocker::SameFolder);
        QCOMPARE(MovePlan::make(input(one, games + "/new")).blocker(),
                 MovePlan::Blocker::InsideTheFolder);
        QCOMPARE(MovePlan::make(input({}, m_fast + "/games")).blocker(),
                 MovePlan::Blocker::NothingToMove);
    }

    void aFileAtTheTargetLeavesTheTorrentOut()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent clash = torrent(QStringLiteral("Clash"), games, 5);
        const Torrent fine = torrent(QStringLiteral("Fine"), games, 6);
        touch(m_fast + QStringLiteral("/games/Clash"));
        const MovePlan plan =
            MovePlan::make(input({clash, fine}, m_fast + QStringLiteral("/games")));
        QCOMPARE(plan.leftOut(), QStringList({clash.hash}));
        QCOMPARE(plan.item(clash.hash)->why, m_fast + QStringLiteral("/games/Clash"));
        QCOMPARE(plan.toMove(), QStringList({fine.hash}));
        QCOMPARE(plan.torrentBytes(), qint64(6));
    }

    void aNoSubfolderTorrentIsCheckedFileByFile()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent loose = torrent(QStringLiteral("Manuals"), games, 5, games);
        MovePlan::Input in = input({loose}, m_fast + QStringLiteral("/games"));
        MovePlan plan = MovePlan::make(in);
        QCOMPARE(plan.needFiles(), QStringList({loose.hash}));
        QCOMPARE(plan.toMove(), QStringList({loose.hash}));

        touch(m_fast + QStringLiteral("/games/manual-2.pdf"));
        in.files.insert(loose.hash,
                        {QStringLiteral("manual-1.pdf"), QStringLiteral("manual-2.pdf")});
        plan = MovePlan::make(in);
        QVERIFY(plan.needFiles().isEmpty());
        QCOMPARE(plan.leftOut(), QStringList({loose.hash}));
    }

    void pulledInFromAbove()
    {
        // §9: a torrent saved one level up whose root folder is the folder itself.
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent rootFolder = torrent(QStringLiteral("games"), m_archive, 3, games);
        const Torrent inside = torrent(QStringLiteral("x"), games, 1);
        MovePlan plan = MovePlan::make(input({rootFolder, inside}, m_fast + "/games"));
        QCOMPARE(plan.pulledIn(), QStringList({rootFolder.hash}));
        QCOMPARE(plan.item(rootFolder.hash)->to, m_fast);
        QCOMPARE(plan.toMove().size(), 2);

        // Renaming the folder would rename that torrent's content: it stays, listed.
        plan = MovePlan::make(input({rootFolder, inside}, m_fast + "/play"));
        QCOMPARE(plan.spans(), QStringList({rootFolder.hash}));
        QCOMPARE(plan.toMove(), QStringList({inside.hash}));
    }

    void noSubfolderFromAbove()
    {
        // "Game Manuals, added without a subfolder": saved at the drive's top, files in games.
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent manuals = torrent(QStringLiteral("Game Manuals"), m_archive, 3, m_archive);
        const Torrent spanning = torrent(QStringLiteral("Mixed"), m_archive, 3, m_archive);
        const Torrent inside = torrent(QStringLiteral("x"), games, 1);
        MovePlan::Input in = input({manuals, spanning, inside}, m_fast + QStringLiteral("/games"));
        MovePlan plan = MovePlan::make(in);
        QCOMPARE(plan.needFiles(), QStringList({manuals.hash, spanning.hash}));

        in.files.insert(manuals.hash,
                        {QStringLiteral("games/a.pdf"), QStringLiteral("games/b.pdf")});
        in.files.insert(spanning.hash,
                        {QStringLiteral("games/c.pdf"), QStringLiteral("other.pdf")});
        plan = MovePlan::make(in);
        QCOMPARE(plan.pulledIn(), QStringList({manuals.hash}));
        QCOMPARE(plan.item(manuals.hash)->to, m_fast);
        QCOMPARE(plan.spans(), QStringList({spanning.hash}));
    }

    void alreadyThereIsSkipped()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent moved = torrent(QStringLiteral("landed"), m_fast + "/games", 7);
        const Torrent rest = torrent(QStringLiteral("rest"), games, 8);
        const MovePlan plan = MovePlan::make(input({moved, rest}, m_fast + "/games"));
        QCOMPARE(plan.alreadyThere(), QStringList({moved.hash}));
        QCOMPARE(plan.toMove(), QStringList({rest.hash}));
    }

    void sharedDataMovesTogether()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent a = torrent(QStringLiteral("Pack"), games, 5);
        Torrent b = torrent(QStringLiteral("Pack"), games, 5); // a cross-seed over the same folder
        const MovePlan plan = MovePlan::make(input({a, b}, m_fast + "/games"));
        QCOMPARE(plan.sharedInside().size(), 1);
        QCOMPARE(plan.toMove().size(), 2);
    }

    void automaticManagementIsListed()
    {
        const QString games = m_archive + QStringLiteral("/games");
        Torrent a = torrent(QStringLiteral("a"), games, 1);
        a.autoTmm = true;
        a.category = QStringLiteral("games");
        Torrent b = torrent(QStringLiteral("b"), games, 2);
        const MovePlan plan = MovePlan::make(input({a, b}, m_fast + "/games"));
        QCOMPARE(plan.autoManaged(), QStringList({a.hash}));
        QCOMPARE(plan.autoManagedCategories(), QStringList({QStringLiteral("games")}));
    }

    void unwritableIsAWarning()
    {
        if (::geteuid() == 0)
            QSKIP("root writes everywhere");
        const QString games = m_archive + QStringLiteral("/games");
        QFile::setPermissions(m_fast, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        const MovePlan plan =
            MovePlan::make(input({torrent(QStringLiteral("a"), games, 1)}, m_fast + "/games"));
        QFile::setPermissions(
            m_fast, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QCOMPARE(plan.writable(), MovePlan::Check::Warn);
        QVERIFY(plan.canMove()); // the first torrent, alone, has the last word (M6)
    }

    void diskCheckAsAnotherOwner()
    {
        // A fake disk: the owner of the folder's files can't write there.
        const QString games = m_archive + QStringLiteral("/games");
        MovePlan::Input in = input({torrent(QStringLiteral("a"), games, 1)}, m_fast + "/games");
        QString askedTarget;
        QString askedOwner;
        in.disk.canWrite = [&](const QString &target, const QString &owner) {
            askedTarget = target;
            askedOwner = owner;
            return MoveDisk::Access::No;
        };
        const MovePlan plan = MovePlan::make(in);
        QCOMPARE(plan.writable(), MovePlan::Check::Warn);
        QCOMPARE(askedTarget, m_fast + QStringLiteral("/games"));
        QCOMPARE(askedOwner, games);
    }
};

QTEST_GUILESS_MAIN(TestMovePlan)

#include "tst_moveplan.moc"
