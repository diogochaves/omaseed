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
        // #131: the check names the destination, and the folder it would be made in.
        QCOMPARE(plan.writeChecks().size(), 1);
        QCOMPARE(plan.writeChecks().constFirst().folder, m_fast + QStringLiteral("/games"));
        QCOMPARE(plan.writeChecks().constFirst().madeIn, m_fast);
        QCOMPARE(plan.writeChecks().constFirst().access, MoveDisk::Access::No);
    }

    // #131: a destination that exists and can't be written, under a parent that can: the check
    // looks at the destination itself and names it, with nothing to be made.
    void unwritableNamesTheDestination()
    {
        if (::geteuid() == 0)
            QSKIP("root writes everywhere");
        const QString games = m_archive + QStringLiteral("/games");
        const QString dest = m_fast + QStringLiteral("/locked/dest");
        QVERIFY(QDir().mkpath(dest));
        QFile::setPermissions(dest, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        const MovePlan plan = MovePlan::make(input({torrent(QStringLiteral("a"), games, 1)}, dest));
        QFile::setPermissions(
            dest, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QCOMPARE(plan.writable(), MovePlan::Check::Warn);
        QCOMPARE(plan.writeChecks().size(), 1);
        QCOMPARE(plan.writeChecks().constFirst().folder, dest);
        QVERIFY(plan.writeChecks().constFirst().madeIn.isEmpty());
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

    // ---- a selection (DECISIONS C16 L1, L5, L11; #103) -------------------------------------

    MovePlan::Input selection(const QList<Torrent> &torrents,
                              const QStringList &hashes,
                              const QString &newPath,
                              MovePlan::Rule rule)
    {
        MovePlan::Input in = input(torrents, newPath);
        in.root.clear();
        in.hashes = hashes;
        in.rule = rule;
        return in;
    }

    void theCommonFolder()
    {
        QCOMPARE(MovePlan::commonFolder({}), QString());
        QCOMPARE(MovePlan::commonFolder({QStringLiteral("/a/b/")}), QStringLiteral("/a/b"));
        QCOMPARE(MovePlan::commonFolder({QStringLiteral("/a/b"), QStringLiteral("/a/b/c")}),
                 QStringLiteral("/a/b"));
        QCOMPARE(MovePlan::commonFolder({QStringLiteral("/a/bc"), QStringLiteral("/a/b")}),
                 QStringLiteral("/a"));
        QCOMPARE(MovePlan::commonFolder({QStringLiteral("/srv/x"), QStringLiteral("/mnt/y")}),
                 QStringLiteral("/"));
    }

    void aSelectionIntoOneFolder()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent a = torrent(QStringLiteral("Open Arena"), games, 2 * kGiB);
        const Torrent b = torrent(QStringLiteral("Tiny"), games + "/handheld", kGiB);
        const Torrent there = torrent(QStringLiteral("Done"), m_fast + "/games", kGiB);
        const Torrent other = torrent(QStringLiteral("Not chosen"), games, kGiB);
        const MovePlan plan = MovePlan::make(selection({a, b, there, other},
                                                       {a.hash, b.hash, there.hash},
                                                       m_fast + QStringLiteral("/games"),
                                                       MovePlan::Rule::OneFolder));
        QVERIFY(plan.isSelection());
        QVERIFY(plan.canMove());
        QCOMPARE(plan.toMove(), QStringList({b.hash, a.hash}));
        QCOMPARE(plan.item(b.hash)->to, m_fast + QStringLiteral("/games"));
        QCOMPARE(plan.alreadyThere(), QStringList({there.hash}));
        QVERIFY(plan.item(other.hash) == nullptr);
        QCOMPARE(plan.savePaths().size(), 3);
        QCOMPARE(plan.pathRows().size(), 2);
        QCOMPARE(plan.room(), MovePlan::Check::Ok);
        QCOMPARE(plan.bytesFreeAfter(), 97 * kGiB);
        QCOMPARE(plan.writable(), MovePlan::Check::Ok);

        // Every one already there: nothing to send, and the button says why.
        const MovePlan none = MovePlan::make(selection(
            {there}, {there.hash}, m_fast + QStringLiteral("/games"), MovePlan::Rule::OneFolder));
        QCOMPARE(none.blocker(), MovePlan::Blocker::AllThere);
        QCOMPARE(MovePlan::make(selection({a}, {a.hash}, QString(), MovePlan::Rule::OneFolder))
                     .blocker(),
                 MovePlan::Blocker::NoPath);
    }

    void aSelectionKeepsItsPlacesBelowTheCommonFolder()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent a = torrent(QStringLiteral("Open Arena"), games, 3);
        const Torrent b = torrent(QStringLiteral("Tiny"), games + "/handheld", 2);
        const Torrent c = torrent(QStringLiteral("Film"), m_archive + "/films", 1);
        const MovePlan plan = MovePlan::make(selection(
            {a, b, c}, {a.hash, b.hash, c.hash}, m_fast + "/all", MovePlan::Rule::KeepPlaces));
        QCOMPARE(plan.root(), m_archive);
        QCOMPARE(plan.item(a.hash)->to, m_fast + QStringLiteral("/all/games"));
        QCOMPARE(plan.item(b.hash)->to, m_fast + QStringLiteral("/all/games/handheld"));
        QCOMPARE(plan.item(c.hash)->to, m_fast + QStringLiteral("/all/films"));
        QCOMPARE(plan.toMove(), QStringList({c.hash, b.hash, a.hash}));

        // Into a folder below the common one is fine; into a torrent's own folder is not.
        const MovePlan inside = MovePlan::make(selection(
            {a, b}, {a.hash, b.hash}, games + "/Open Arena/sub", MovePlan::Rule::OneFolder));
        QCOMPARE(inside.intoItself(), QStringList({a.hash}));
        QCOMPARE(inside.item(a.hash)->why, games + QStringLiteral("/Open Arena"));
        QCOMPARE(inside.toMove(), QStringList({b.hash}));
    }

    void moveBackSendsEachToItsOwnPlace()
    {
        const Torrent a = torrent(QStringLiteral("Open Arena"), m_fast + "/films", 3);
        const Torrent b = torrent(QStringLiteral("Tiny"), m_fast + "/films", 2);
        const Torrent moved = torrent(QStringLiteral("Moved since"), m_archive + "/other", 1);
        MovePlan::Input in = selection(
            {a, b, moved}, {a.hash, b.hash, moved.hash}, QString(), MovePlan::Rule::EachOwn);
        in.targets = {{a.hash, m_archive + "/games"},
                      {b.hash, m_archive + "/games/handheld"},
                      {moved.hash, m_archive + "/films"}};
        in.expectedAt = {{a.hash, m_fast + "/films"},
                         {b.hash, m_fast + "/films/"},
                         {moved.hash, m_fast + "/films"}};
        const MovePlan plan = MovePlan::make(in);
        QVERIFY(plan.canMove());
        QCOMPARE(plan.toMove(), QStringList({b.hash, a.hash}));
        QCOMPARE(plan.item(a.hash)->to, m_archive + QStringLiteral("/games"));
        QCOMPARE(plan.item(b.hash)->to, m_archive + QStringLiteral("/games/handheld"));
        QCOMPARE(plan.movedSince(), QStringList({moved.hash}));
        QCOMPARE(plan.item(moved.hash)->why, m_archive + QStringLiteral("/other"));
        QCOMPARE(plan.pathRows().size(), 2);
        QCOMPARE(plan.room(), MovePlan::Check::Ok);
        QCOMPARE(plan.drive(), m_archive);
    }

    void aSelectionCountsRoomPerDrive()
    {
        // One torrent is renamed on the fast drive (no room needed); the other comes across.
        m_fastFree = 10 * kGiB;
        const Torrent near = torrent(QStringLiteral("Near"), m_fast + "/old", 50 * kGiB);
        const Torrent far = torrent(QStringLiteral("Far"), m_archive + "/games", 9 * kGiB);
        MovePlan plan = MovePlan::make(selection(
            {near, far}, {near.hash, far.hash}, m_fast + "/new", MovePlan::Rule::OneFolder));
        QVERIFY(!plan.sameDrive());
        QCOMPARE(plan.room(), MovePlan::Check::Block); // 1 GiB left, under the 2 GiB kept free
        QCOMPARE(plan.blocker(), MovePlan::Blocker::NoRoom);
        QCOMPARE(plan.bytesFreeAfter(), kGiB);
        QCOMPARE(plan.bytesToMove(), 59 * kGiB);

        plan = MovePlan::make(
            selection({near}, {near.hash}, m_fast + "/new", MovePlan::Rule::OneFolder));
        QVERIFY(plan.sameDrive());
        QCOMPARE(plan.room(), MovePlan::Check::Ok);
    }

    void moveBackToTwoDrivesNamesBoth()
    {
        // #105: back from a third drive to the fast one and the archive; each is read for room,
        // and the one with the least left after comes first.
        QDir().mkpath(m_tmp.filePath(QStringLiteral("usb")));
        const QString usb = m_tmp.filePath(QStringLiteral("usb"));
        m_probe = FreeSpaceProbe([this, usb](const QString &path) {
            StorageReading reading;
            reading.valid = true;
            reading.exists = QFileInfo::exists(path);
            reading.rootPath = path.startsWith(m_fast) ? m_fast
                               : path.startsWith(usb)  ? usb
                                                       : m_archive;
            reading.bytesFree = path.startsWith(m_fast) ? 10 * kGiB : 500 * kGiB;
            reading.bytesTotal = 1000 * kGiB;
            return reading;
        });
        m_probe.setServer(m_archive, 500 * kGiB, PathMap(), false);
        const Torrent a = torrent(QStringLiteral("Open Arena"), usb + "/films", 3 * kGiB);
        const Torrent b = torrent(QStringLiteral("Tiny"), usb + "/films", 2 * kGiB);
        MovePlan::Input in =
            selection({a, b}, {a.hash, b.hash}, QString(), MovePlan::Rule::EachOwn);
        in.targets = {{a.hash, m_archive + "/games"}, {b.hash, m_fast + "/films"}};
        const MovePlan plan = MovePlan::make(in);
        QVERIFY(plan.canMove());
        QCOMPARE(plan.room(), MovePlan::Check::Ok);
        QCOMPARE(plan.drives().size(), 2);
        QCOMPARE(plan.drives().at(0).drive, m_fast);
        QCOMPARE(plan.drives().at(0).bytes, 2 * kGiB);
        QCOMPARE(plan.drives().at(0).bytesFree, 10 * kGiB);
        QCOMPARE(plan.drives().at(1).drive, m_archive);
        QCOMPARE(plan.drives().at(1).bytes, 3 * kGiB);
        QCOMPARE(plan.drive(), m_fast);

        // One drive: drive() says it all.
        in.targets = {{a.hash, m_archive + "/games"}, {b.hash, m_archive + "/films"}};
        QVERIFY(MovePlan::make(in).drives().isEmpty());
    }

    void aSelectionLeavesItsOutsideSharersToTheGuard()
    {
        const QString games = m_archive + QStringLiteral("/games");
        const Torrent a = torrent(QStringLiteral("Pack"), games, 5);
        const Torrent b = torrent(QStringLiteral("Pack"), games, 5); // a cross-seed
        const MovePlan plan = MovePlan::make(
            selection({a, b}, {a.hash}, m_fast + "/games", MovePlan::Rule::OneFolder));
        QCOMPARE(plan.toMove(), QStringList({a.hash}));
        QCOMPARE(plan.sharedOutside(), QStringList({b.hash}));
        QVERIFY(plan.spans().isEmpty());
    }

    void withoutAViewASelectionAsksTheServerToo()
    {
        // #102 for a selection: the target's folder is listed on the server.
        const Torrent a = torrent(QStringLiteral("Clash"), QStringLiteral("/srv/games"), 5);
        MovePlan::Input in =
            selection({a}, {a.hash}, QStringLiteral("/volume2/games"), MovePlan::Rule::OneFolder);
        FreeSpaceProbe remote([](const QString &) { return StorageReading(); });
        remote.setServer(QStringLiteral("/downloads"), 50 * kGiB, PathMap(), false);
        in.probe = &remote;
        MovePlan plan = MovePlan::make(in);
        QVERIFY(!plan.seesDisks());
        QCOMPARE(plan.namesCheck(), MovePlan::NamesCheck::Pending);
        QCOMPARE(plan.needListings(), QStringList({QStringLiteral("/volume2/games")}));
        ServerFolder listed;
        listed.state = ServerFolder::State::Listed;
        listed.names = {QStringLiteral("Clash")};
        in.serverFolders.insert(QStringLiteral("/volume2/games"), listed);
        plan = MovePlan::make(in);
        QCOMPARE(plan.namesCheck(), MovePlan::NamesCheck::Server);
        QCOMPARE(plan.leftOut(), QStringList({a.hash}));
        QCOMPARE(plan.blocker(), MovePlan::Blocker::NothingToMove);
    }
};

QTEST_GUILESS_MAIN(TestMovePlan)

#include "tst_moveplan.moc"
