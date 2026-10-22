#include "core/PieceStates.hpp"
#include "core/SkippedJobs.hpp"
#include "core/SkippedOnDisk.hpp"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::AddAgainJob;
using omaseed::core::FreeItem;
using omaseed::core::FreePlan;
using omaseed::core::QbtClient;
using omaseed::core::SkippedOnDisk;
using omaseed::core::SkippedReading;
using omaseed::core::TorrentRecord;

namespace {

constexpr qint64 kMiB = 1024LL * 1024;
constexpr qint64 kGiB = 1024 * kMiB;

QJsonObject file(
    int index, const QString &name, qint64 size, int priority, double progress, int first, int last)
{
    return QJsonObject{{QStringLiteral("index"), index},
                       {QStringLiteral("name"), name},
                       {QStringLiteral("size"), double(size)},
                       {QStringLiteral("priority"), priority},
                       {QStringLiteral("progress"), progress},
                       {QStringLiteral("piece_range"), QJsonArray{first, last}}};
}

/// A torrent like board 44's: one folder wanted, the rest skipped, some of it written.
QJsonArray archive()
{
    return QJsonArray{
        file(0, QStringLiteral("Archive/Sessions/a.flac"), 8 * kGiB, 1, 0.92, 0, 1023),
        file(1, QStringLiteral("Archive/Concert part 1.mp4"), 2 * kGiB, 0, 0.0, 1024, 1279),
        file(2, QStringLiteral("Archive/Concert part 2.mp4"), 750 * kMiB, 0, 0.0, 1280, 1373),
        file(3, QStringLiteral("Archive/Encore.mp4"), 400 * kMiB, 0, 0.05, 1374, 1423),
        file(4, QStringLiteral("Archive/Interview.mov"), 1 * kGiB, 0, 0.0, 1424, 1551),
        file(5, QStringLiteral("Archive/notes.txt"), 4096, 0, 0.0, 1551, 1551),
    };
}

/// What each local file holds: the allocated bytes a pretend disk reports.
struct FakeDisk
{
    QHash<QString, qint64> allocated;

    SkippedOnDisk::AllocatedReader reader() const
    {
        return [this](const QString &path) { return allocated.value(path, -1); };
    }
};

SkippedOnDisk::LocalPathOf under(const QString &root)
{
    return [root](const QString &path) { return root + QLatin1Char('/') + path; };
}

} // namespace

class TestSkippedOnDisk : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsOnlySkippedFilesAndWhatTheyHold();
    void downloadedPiecesComeFromProgressAndPieceStates();
    void unseenDisksHoldAnUnknownAmount();
    void bandThreshold();
    void planDeletesFilesWithoutPieces();
    void planOnAHybridLeavesFilesWithPieces();
    void planGuardsOutsideSharedAndOpen();
    void openFileHoldersReadsProcFds();
    void readAllocatedReadsBlocksNotLength();
    void refreshNowReplacesTheReading();
    void addAgainOptionsFollowTheRecord();
};

void TestSkippedOnDisk::readsOnlySkippedFilesAndWhatTheyHold()
{
    FakeDisk disk;
    // ntfs3 after the misclick: every skipped file at full size, but the one never ticked.
    disk.allocated.insert(QStringLiteral("/mnt/media/Archive/Concert part 1.mp4"), 2 * kGiB);
    disk.allocated.insert(QStringLiteral("/mnt/media/Archive/Concert part 2.mp4"), 750 * kMiB);
    disk.allocated.insert(QStringLiteral("/mnt/media/Archive/Encore.mp4"), 400 * kMiB);
    disk.allocated.insert(QStringLiteral("/mnt/media/Archive/notes.txt"), 0);
    const SkippedReading reading = SkippedOnDisk::read(QStringLiteral("h"),
                                                       archive(),
                                                       QByteArray(),
                                                       12 * kGiB,
                                                       under(QStringLiteral("/mnt/media")),
                                                       disk.reader());
    QVERIFY(reading.seen);
    QCOMPARE(reading.files.size(), 5);
    QCOMPARE(reading.holding, 3);
    QCOMPARE(reading.held, 2 * kGiB + 750 * kMiB + 400 * kMiB);
    QCOMPARE(reading.partlyWritten, 1);
    const QHash<int, qint64> byIndex = reading.allocatedByIndex();
    QCOMPARE(byIndex.value(1), 2 * kGiB);
    QCOMPARE(byIndex.value(5), qint64(0));
    QVERIFY(!byIndex.contains(4)); // not on the disk at all
    QVERIFY(!byIndex.contains(0)); // wanted: not read
}

void TestSkippedOnDisk::downloadedPiecesComeFromProgressAndPieceStates()
{
    QByteArray states(1552, char(omaseed::core::PieceStates::Missing));
    // A downloaded piece inside part 2's range, though its progress still says 0.
    states[1300] = char(omaseed::core::PieceStates::Have);
    // A piece being downloaded is not had yet.
    states[1430] = char(omaseed::core::PieceStates::Downloading);
    const SkippedReading reading =
        SkippedOnDisk::read(QStringLiteral("h"), archive(), states, 12 * kGiB, {}, {});
    QHash<int, bool> pieces;
    for (const auto &f : reading.files)
        pieces.insert(f.index, f.holdsPieces);
    QCOMPARE(pieces.value(1), false);
    QCOMPARE(pieces.value(2), true);
    QCOMPARE(pieces.value(3), true); // progress 5 %
    QCOMPARE(pieces.value(4), false);
}

void TestSkippedOnDisk::unseenDisksHoldAnUnknownAmount()
{
    // D4: no local view, so nothing is read and nothing is claimed; progress still counts.
    const SkippedReading reading =
        SkippedOnDisk::read(QStringLiteral("h"), archive(), QByteArray(), 12 * kGiB, {}, {});
    QVERIFY(!reading.seen);
    QCOMPARE(reading.held, qint64(0));
    QCOMPARE(reading.partlyWritten, 1);
    QVERIFY(reading.allocatedByIndex().isEmpty());

    // A path map that covers some files and not others is not a view of the torrent.
    FakeDisk disk;
    disk.allocated.insert(QStringLiteral("/mnt/a/Archive/Concert part 1.mp4"), 2 * kGiB);
    const auto partial = [](const QString &path) {
        return path.contains(QStringLiteral("part 1")) ? QStringLiteral("/mnt/a/") + path
                                                       : QString();
    };
    const SkippedReading some = SkippedOnDisk::read(
        QStringLiteral("h"), archive(), QByteArray(), 12 * kGiB, partial, disk.reader());
    QVERIFY(!some.seen);
    QCOMPARE(some.held, qint64(0));
}

void TestSkippedOnDisk::bandThreshold()
{
    SkippedReading reading;
    reading.torrentSize = 100 * kGiB;
    reading.held = kGiB;
    QVERIFY(!reading.bandShown()); // 1 GiB of 100: neither more than 1 GiB nor 5 %
    reading.held = kGiB + 1;
    QVERIFY(reading.bandShown());
    reading.torrentSize = 100 * kMiB;
    reading.held = 5 * kMiB;
    QVERIFY(!reading.bandShown()); // exactly 5 %
    reading.held = 6 * kMiB;
    QVERIFY(reading.bandShown());
}

void TestSkippedOnDisk::planDeletesFilesWithoutPieces()
{
    FakeDisk disk;
    disk.allocated.insert(QStringLiteral("/m/Archive/Concert part 1.mp4"), 2 * kGiB);
    disk.allocated.insert(QStringLiteral("/m/Archive/Concert part 2.mp4"), 750 * kMiB);
    disk.allocated.insert(QStringLiteral("/m/Archive/Encore.mp4"), 400 * kMiB);
    const SkippedReading reading = SkippedOnDisk::read(QStringLiteral("h"),
                                                       archive(),
                                                       QByteArray(),
                                                       12 * kGiB,
                                                       under(QStringLiteral("/m")),
                                                       disk.reader());
    // v1-only: Encore holds pieces, so it goes too, with a recheck after the start.
    const FreePlan plan = SkippedOnDisk::plan(reading, false, QStringLiteral("/m/Archive"), {}, {});
    QCOMPARE(plan.deletes, 3);
    QCOMPARE(plan.freed, 2 * kGiB + 750 * kMiB + 400 * kMiB);
    QVERIFY(plan.recheck);
    QCOMPARE(plan.items.first().file.index, 1); // the biggest first

    // Without the file that holds pieces, no recheck.
    disk.allocated.remove(QStringLiteral("/m/Archive/Encore.mp4"));
    const SkippedReading clean = SkippedOnDisk::read(QStringLiteral("h"),
                                                     archive(),
                                                     QByteArray(),
                                                     12 * kGiB,
                                                     under(QStringLiteral("/m")),
                                                     disk.reader());
    QVERIFY(!SkippedOnDisk::plan(clean, false, QStringLiteral("/m/Archive"), {}, {}).recheck);
}

void TestSkippedOnDisk::planOnAHybridLeavesFilesWithPieces()
{
    FakeDisk disk;
    disk.allocated.insert(QStringLiteral("/m/Archive/Concert part 1.mp4"), 2 * kGiB);
    disk.allocated.insert(QStringLiteral("/m/Archive/Encore.mp4"), 400 * kMiB);
    const SkippedReading reading = SkippedOnDisk::read(QStringLiteral("h"),
                                                       archive(),
                                                       QByteArray(),
                                                       12 * kGiB,
                                                       under(QStringLiteral("/m")),
                                                       disk.reader());
    // C17 D3: never a recheck on a hybrid; Encore stays, freed only by adding it again.
    const FreePlan plan = SkippedOnDisk::plan(reading, true, QStringLiteral("/m/Archive"), {}, {});
    QVERIFY(plan.hybrid);
    QVERIFY(!plan.recheck);
    QCOMPARE(plan.deletes, 1);
    QCOMPARE(plan.leftWithPieces, 1);
    QCOMPARE(plan.leftWithPiecesBytes, 400 * kMiB);
    QCOMPARE(plan.items.last().verdict, FreeItem::Verdict::Pieces);
    QCOMPARE(plan.piecesPaths(), QStringList{QStringLiteral("/m/Archive/Encore.mp4")});
    QCOMPARE(plan.deletePaths(), QStringList{QStringLiteral("/m/Archive/Concert part 1.mp4")});
}

void TestSkippedOnDisk::planGuardsOutsideSharedAndOpen()
{
    FakeDisk disk;
    disk.allocated.insert(QStringLiteral("/m/Archive/Concert part 1.mp4"), 2 * kGiB);
    disk.allocated.insert(QStringLiteral("/m/Archive/Concert part 2.mp4"), 750 * kMiB);
    disk.allocated.insert(QStringLiteral("/m/Archive/Interview.mov"), 1 * kGiB);
    const SkippedReading reading = SkippedOnDisk::read(QStringLiteral("h"),
                                                       archive(),
                                                       QByteArray(),
                                                       12 * kGiB,
                                                       under(QStringLiteral("/m")),
                                                       disk.reader());
    const QHash<QString, QString> open{
        {QStringLiteral("/m/Archive/Interview.mov"), QStringLiteral("mpv")}};
    // Another torrent's content path is part 2 itself (a single-file cross-seed).
    const FreePlan plan = SkippedOnDisk::plan(reading,
                                              false,
                                              QStringLiteral("/m/Archive"),
                                              {QStringLiteral("/m/Archive/Concert part 2.mp4")},
                                              open);
    QHash<int, FreeItem> byIndex;
    for (const FreeItem &item : plan.items)
        byIndex.insert(item.file.index, item);
    QCOMPARE(byIndex.value(1).verdict, FreeItem::Verdict::Delete);
    QCOMPARE(byIndex.value(2).verdict, FreeItem::Verdict::Shared);
    QCOMPARE(byIndex.value(4).verdict, FreeItem::Verdict::Open);
    QCOMPARE(byIndex.value(4).openBy, QStringLiteral("mpv"));
    QCOMPARE(plan.deletes, 1);

    // Nothing outside the content path is ever deleted (a file the path map puts elsewhere).
    const FreePlan outside =
        SkippedOnDisk::plan(reading, false, QStringLiteral("/m/Other"), {}, {});
    QCOMPARE(outside.deletes, 0);
    QCOMPARE(outside.items.first().verdict, FreeItem::Verdict::Outside);
    // No local content path: nothing goes.
    QCOMPARE(SkippedOnDisk::plan(reading, false, QString(), {}, {}).deletes, 0);
}

void TestSkippedOnDisk::openFileHoldersReadsProcFds()
{
    QTemporaryDir proc;
    QVERIFY(proc.isValid());
    QTemporaryDir data;
    const QString movie = data.filePath(QStringLiteral("Interview.mov"));
    const QString other = data.filePath(QStringLiteral("other.bin"));
    for (const QString &path : {movie, other}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    QDir root(proc.path());
    QVERIFY(root.mkpath(QStringLiteral("4242/fd")));
    QVERIFY(root.mkpath(QStringLiteral("self/fd")));
    QVERIFY(QFile::link(movie, proc.filePath(QStringLiteral("4242/fd/7"))));
    QVERIFY(QFile::link(other, proc.filePath(QStringLiteral("self/fd/3"))));
    QFile comm(proc.filePath(QStringLiteral("4242/comm")));
    QVERIFY(comm.open(QIODevice::WriteOnly));
    comm.write("mpv\n");
    comm.close();

    const auto holders = SkippedOnDisk::openFileHolders(
        {movie, other, data.filePath(QStringLiteral("x"))}, proc.path());
    QCOMPARE(holders.size(), 1); // "self" is not a process id
    QCOMPARE(holders.value(movie), QStringLiteral("mpv"));
    QVERIFY(SkippedOnDisk::openFileHolders({}, proc.path()).isEmpty());
}

void TestSkippedOnDisk::readAllocatedReadsBlocksNotLength()
{
    QTemporaryDir dir;
    const QString sparse = dir.filePath(QStringLiteral("sparse.bin"));
    QFile f(sparse);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.resize(64 * kMiB)); // a length, no data: sparse on every Linux filesystem here
    f.close();
    QVERIFY(SkippedOnDisk::readAllocated(sparse) < 64 * kMiB);
    const QString full = dir.filePath(QStringLiteral("full.bin"));
    QFile g(full);
    QVERIFY(g.open(QIODevice::WriteOnly));
    g.write(QByteArray(int(kMiB), 'x'));
    g.close();
    QVERIFY(SkippedOnDisk::readAllocated(full) >= kMiB);
    QCOMPARE(SkippedOnDisk::readAllocated(dir.filePath(QStringLiteral("missing"))), qint64(-1));
    QCOMPARE(SkippedOnDisk::readAllocated(dir.path()), qint64(-1)); // a folder
}

void TestSkippedOnDisk::refreshNowReplacesTheReading()
{
    FakeDisk disk;
    disk.allocated.insert(QStringLiteral("/m/Archive/Concert part 1.mp4"), 2 * kGiB);
    SkippedOnDisk skipped(disk.reader());
    QSignalSpy changed(&skipped, &SkippedOnDisk::readingChanged);
    skipped.refreshNow(
        QStringLiteral("h"), archive(), QByteArray(), 12 * kGiB, under(QStringLiteral("/m")));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(skipped.reading().held, 2 * kGiB);

    // The thread pool's reading lands later, and only the newest counts.
    skipped.refresh(
        QStringLiteral("h"), archive(), QByteArray(), 12 * kGiB, under(QStringLiteral("/m")));
    QVERIFY(changed.wait(5000));
    QCOMPARE(skipped.reading().hash, QStringLiteral("h"));
    skipped.clear();
    QVERIFY(skipped.reading().hash.isEmpty());
}

void TestSkippedOnDisk::addAgainOptionsFollowTheRecord()
{
    TorrentRecord record;
    record.savePath = QStringLiteral("/mnt/media/Downloads");
    record.category = QStringLiteral("films");
    record.tags = {QStringLiteral("a"), QStringLiteral("b")};
    record.layout = QStringLiteral("NoSubfolder");
    record.upLimit = 1000;
    record.sequential = true;
    record.priorities = {1, 0, 0, 7, 1, 6};
    const QbtClient::AddOptions options = AddAgainJob::addOptions(record);
    QCOMPARE(options.savepath.value_or(QString()), record.savePath);
    QCOMPARE(options.useDownloadPath.value_or(true), false);
    QCOMPARE(options.category.value_or(QString()), QStringLiteral("films"));
    QCOMPARE(options.tags, record.tags);
    QVERIFY(options.stopped.value_or(false));
    QVERIFY(options.contentLayout == QbtClient::ContentLayout::NoSubfolder);
    QCOMPARE(options.upLimit.value_or(0), qint64(1000));
    QVERIFY(!options.dlLimit.has_value());
    QVERIFY(options.sequentialDownload.value_or(false));

    const auto groups = AddAgainJob::priorityGroups(record);
    QCOMPARE(groups.size(), 3);
    QCOMPARE(groups.value(0), (QList<int>{1, 2}));
    QCOMPARE(groups.value(7), QList<int>{3});
    QCOMPARE(groups.value(6), QList<int>{5});
}

QTEST_GUILESS_MAIN(TestSkippedOnDisk)

#include "tst_skippedondisk.moc"
