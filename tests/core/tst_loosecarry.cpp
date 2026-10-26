// LooseCarry: the files no torrent owns, carried after the torrents (board 42 §7, C15 M17).

#include "core/LooseCarry.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>

using namespace omaseed::core;

namespace {

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// The dry run's view of a file, as the walk would have seen it.
CarryFile seen(const QString &root, const QString &relative)
{
    const QFileInfo info(root + QLatin1Char('/') + relative);
    CarryFile file;
    file.relative = relative;
    file.size = info.size();
    file.modified = info.lastModified();
    return file;
}

dev_t deviceOf(const QString &path)
{
    struct stat st{};
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? st.st_dev : dev_t(0);
}

/// An hour from now, so nothing written by the test is "recent".
QDateTime later()
{
    return QDateTime::currentDateTime().addSecs(3600);
}

} // namespace

class TestLooseCarry : public QObject
{
    Q_OBJECT

private slots:
    void sameDriveRenames();
    void anotherDriveCopiesChecksAndRemoves();
    void sameFileAtTargetCountsAsCarried();
    void differentFileAtTargetStaysBothSides();
    void changedSinceTheDryRunStays();
    void tooRecentStays();
    void settledFilesAreLeftAlone();
    void resumesAfterAStop();
    void stopLeavesTheFileToDo();
    void emptyFoldersGoOnlyWhenEmpty();
    void jsonRoundTrip();
    void fullDestination();
    void asCarriedReadsTheTarget();
    void backListKeepsWhatChangedSince();
    void movedBackAsTheMoveDid();
};

void TestLooseCarry::sameDriveRenames()
{
    QTemporaryDir dir;
    const QString from = dir.filePath(QStringLiteral("games"));
    const QString to = dir.filePath(QStringLiteral("fast/games"));
    write(from + u"/notes.txt", "hello");
    const QDateTime when = QFileInfo(from + u"/notes.txt").lastModified();
    QDir().mkpath(to);

    const LooseCarry::Result result =
        LooseCarry::run(from, to, {seen(from, QStringLiteral("notes.txt"))}, later());
    QCOMPARE(result.carried, 1);
    QCOMPARE(result.stayed, 0);
    QCOMPARE(result.files.first().outcome, CarryFile::Outcome::Carried);
    QVERIFY(!QFile::exists(from + u"/notes.txt"));
    QCOMPARE(read(to + u"/notes.txt"), QByteArray("hello"));
    QCOMPARE(QFileInfo(to + u"/notes.txt").lastModified(), when);
}

void TestLooseCarry::anotherDriveCopiesChecksAndRemoves()
{
    QTemporaryDir dir;
    QTemporaryDir shm(QStringLiteral("/dev/shm/omaseed-carry-XXXXXX"));
    if (!shm.isValid() || deviceOf(dir.path()) == deviceOf(shm.path()))
        QSKIP("no second filesystem here (/dev/shm on the same one as the temp dir)");
    const QString from = dir.filePath(QStringLiteral("games"));
    const QString to = shm.filePath(QStringLiteral("games"));
    QByteArray big(3 * 1024 * 1024 + 17, 'x');
    for (int i = 0; i < big.size(); i += 4096)
        big[i] = char(i % 251);
    write(from + u"/disk-images/arena.iso", big);
    QFile::setPermissions(from + u"/disk-images/arena.iso",
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ReadGroup);
    const QDateTime when = QFileInfo(from + u"/disk-images/arena.iso").lastModified();

    QList<LooseCarry::Progress> heard;
    const LooseCarry::Result result =
        LooseCarry::run(from,
                        to,
                        {seen(from, QStringLiteral("disk-images/arena.iso"))},
                        later(),
                        std::chrono::minutes(15),
                        {},
                        [&](const LooseCarry::Progress &p) { heard << p; });
    QCOMPARE(result.carried, 1);
    QCOMPARE(result.files.first().why, QStringLiteral("copied and checked"));
    QCOMPARE(read(to + u"/disk-images/arena.iso"), big);
    QCOMPARE(QFileInfo(to + u"/disk-images/arena.iso").lastModified(), when);
    QVERIFY(!(QFileInfo(to + u"/disk-images/arena.iso").permissions() & QFileDevice::ReadOther));
    QVERIFY(!QFile::exists(from + u"/disk-images/arena.iso"));
    QVERIFY(!QFile::exists(LooseCarry::tempPathFor(to + u"/disk-images/arena.iso")));
    QVERIFY(!heard.isEmpty());
    QCOMPARE(heard.last().done, 1);
    QCOMPARE(heard.last().bytesDone, qint64(big.size()));
    QVERIFY(std::any_of(heard.cbegin(), heard.cend(), [](const LooseCarry::Progress &p) {
        return p.checking && p.current == QStringLiteral("disk-images/arena.iso");
    }));
}

void TestLooseCarry::sameFileAtTargetCountsAsCarried()
{
    QTemporaryDir dir;
    const QString from = dir.filePath(QStringLiteral("a"));
    const QString to = dir.filePath(QStringLiteral("b"));
    write(from + u"/x.bin", "same bytes");
    write(to + u"/x.bin", "same bytes");
    const LooseCarry::Result result =
        LooseCarry::run(from, to, {seen(from, QStringLiteral("x.bin"))}, later());
    QCOMPARE(result.files.first().outcome, CarryFile::Outcome::AlreadyThere);
    QCOMPARE(result.carried, 1);
    QVERIFY(!QFile::exists(from + u"/x.bin"));
    QCOMPARE(read(to + u"/x.bin"), QByteArray("same bytes"));
}

void TestLooseCarry::differentFileAtTargetStaysBothSides()
{
    QTemporaryDir dir;
    const QString from = dir.filePath(QStringLiteral("a"));
    const QString to = dir.filePath(QStringLiteral("b"));
    write(from + u"/x.bin", "mine, ten.");
    write(to + u"/x.bin", "theirs, 10");
    const LooseCarry::Result result =
        LooseCarry::run(from, to, {seen(from, QStringLiteral("x.bin"))}, later());
    QCOMPARE(result.files.first().outcome, CarryFile::Outcome::Different);
    QCOMPARE(result.stayed, 1);
    QCOMPARE(read(from + u"/x.bin"), QByteArray("mine, ten."));
    QCOMPARE(read(to + u"/x.bin"), QByteArray("theirs, 10"));
}

void TestLooseCarry::changedSinceTheDryRunStays()
{
    QTemporaryDir dir;
    const QString from = dir.filePath(QStringLiteral("a"));
    write(from + u"/x.bin", "short");
    CarryFile file = seen(from, QStringLiteral("x.bin"));
    write(from + u"/x.bin", "longer now");
    const LooseCarry::Result result =
        LooseCarry::run(from, dir.filePath(QStringLiteral("b")), {file}, later());
    QCOMPARE(result.files.first().outcome, CarryFile::Outcome::Changed);
    QCOMPARE(result.files.first().why, QStringLiteral("changed since the dry run"));
    QVERIFY(QFile::exists(from + u"/x.bin"));
}

void TestLooseCarry::tooRecentStays()
{
    QTemporaryDir dir;
    const QString from = dir.filePath(QStringLiteral("a"));
    write(from + u"/x.bin", "new");
    const LooseCarry::Result result = LooseCarry::run(from,
                                                      dir.filePath(QStringLiteral("b")),
                                                      {seen(from, QStringLiteral("x.bin"))},
                                                      QDateTime::currentDateTime());
    QCOMPARE(result.files.first().outcome, CarryFile::Outcome::Changed);
    QCOMPARE(result.files.first().why, QStringLiteral("changed in the last 15 min"));
    QVERIFY(QFile::exists(from + u"/x.bin"));
}

void TestLooseCarry::settledFilesAreLeftAlone()
{
    QTemporaryDir dir;
    const QString from = dir.filePath(QStringLiteral("a"));
    write(from + u"/recent.bin", "r");
    CarryFile recent = seen(from, QStringLiteral("recent.bin"));
    recent.outcome = CarryFile::Outcome::Recent;
    recent.why = QStringLiteral("changed 3 min ago");
    const LooseCarry::Result result =
        LooseCarry::run(from, dir.filePath(QStringLiteral("b")), {recent}, later());
    QCOMPARE(result.files.first(), recent);
    QCOMPARE(result.stayed, 1);
    QVERIFY(QFile::exists(from + u"/recent.bin"));
}

void TestLooseCarry::resumesAfterAStop()
{
    QTemporaryDir dir;
    const QString from = dir.filePath(QStringLiteral("a"));
    const QString to = dir.filePath(QStringLiteral("b"));
    // The earlier run carried one.txt, then stopped mid-copy of two.txt.
    write(from + u"/one.txt", "1111");
    const CarryFile one = seen(from, QStringLiteral("one.txt"));
    write(to + u"/one.txt", "1111");
    QFile::remove(from + u"/one.txt");
    write(from + u"/two.txt", "2222");
    write(LooseCarry::tempPathFor(to + u"/two.txt"), "22");

    const LooseCarry::Result result =
        LooseCarry::run(from, to, {one, seen(from, QStringLiteral("two.txt"))}, later());
    QCOMPARE(result.carried, 2);
    QCOMPARE(result.files.at(0).why, QStringLiteral("carried earlier"));
    QCOMPARE(read(to + u"/two.txt"), QByteArray("2222"));
    QVERIFY(!QFile::exists(LooseCarry::tempPathFor(to + u"/two.txt")));
}

void TestLooseCarry::stopLeavesTheFileToDo()
{
    QTemporaryDir dir;
    const QString from = dir.filePath(QStringLiteral("a"));
    write(from + u"/x.bin", "x");
    const LooseCarry::Result result = LooseCarry::run(from,
                                                      dir.filePath(QStringLiteral("b")),
                                                      {seen(from, QStringLiteral("x.bin"))},
                                                      later(),
                                                      std::chrono::minutes(15),
                                                      [] { return true; });
    QVERIFY(result.stopped);
    QCOMPARE(result.files.first().outcome, CarryFile::Outcome::ToDo);
    QCOMPARE(result.carried + result.stayed, 0);
    QVERIFY(QFile::exists(from + u"/x.bin"));
}

void TestLooseCarry::emptyFoldersGoOnlyWhenEmpty()
{
    QTemporaryDir dir;
    const QString from = dir.filePath(QStringLiteral("a"));
    const QString to = dir.filePath(QStringLiteral("b"));
    write(from + u"/covers/deep/c.png", "c");
    write(from + u"/notes/n.txt", "n");
    write(from + u"/notes/keep.txt", "k");
    const LooseCarry::Result result =
        LooseCarry::run(from,
                        to,
                        {seen(from, QStringLiteral("covers/deep/c.png")),
                         seen(from, QStringLiteral("notes/n.txt"))},
                        later());
    QCOMPARE(result.carried, 2);
    QCOMPARE(result.foldersRemoved, 2); // covers/deep and covers
    QVERIFY(!QFileInfo::exists(from + u"/covers"));
    QVERIFY(QFileInfo::exists(from + u"/notes/keep.txt"));
    QVERIFY(QFileInfo::exists(from));
}

void TestLooseCarry::jsonRoundTrip()
{
    CarryFile a;
    a.relative = QStringLiteral("x/ü ñ.txt");
    a.size = 1234567890123;
    a.modified = QDateTime::fromMSecsSinceEpoch(1760000000123);
    a.outcome = CarryFile::Outcome::Kept;
    a.why = QStringLiteral("copied and checked, but can't be removed here");
    CarryFile b;
    b.relative = QStringLiteral("y");
    const QList<CarryFile> files{a, b};
    QCOMPARE(LooseCarry::fromJson(LooseCarry::toJson(files)), files);
    const LooseCarry::Result tally = LooseCarry::tally(files);
    QCOMPARE(tally.carried, 0);
    QCOMPARE(tally.stayed, 1);
}

// Run by tst_loosecarry_full: OMASEED_TEST_SMALL_FS is a 1 MiB tmpfs in a user namespace.
void TestLooseCarry::fullDestination()
{
    const QString small = qEnvironmentVariable("OMASEED_TEST_SMALL_FS");
    if (small.isEmpty())
        QSKIP("needs OMASEED_TEST_SMALL_FS (ctest's tst_loosecarry_full sets one up)");
    QTemporaryDir dir;
    const QString from = dir.filePath(QStringLiteral("a"));
    const QString to = small + QStringLiteral("/games");
    write(from + u"/small.txt", "fits");
    write(from + u"/big.bin", QByteArray(2 * 1024 * 1024, 'b'));
    write(from + u"/zz-after.txt", "never tried");
    const LooseCarry::Result result = LooseCarry::run(from,
                                                      to,
                                                      {seen(from, QStringLiteral("small.txt")),
                                                       seen(from, QStringLiteral("big.bin")),
                                                       seen(from, QStringLiteral("zz-after.txt"))},
                                                      later());
    QVERIFY(result.full);
    QCOMPARE(result.files.at(0).outcome, CarryFile::Outcome::Carried);
    QCOMPARE(result.files.at(1).outcome, CarryFile::Outcome::NoRoom);
    QCOMPARE(result.files.at(2).outcome, CarryFile::Outcome::NoRoom);
    QCOMPARE(result.files.at(1).why, QStringLiteral("the destination is full"));
    QVERIFY(QFile::exists(from + u"/big.bin"));
    QVERIFY(QFile::exists(from + u"/zz-after.txt"));
    QVERIFY(!QFile::exists(to + u"/big.bin"));
    QVERIFY(!QFile::exists(LooseCarry::tempPathFor(to + u"/big.bin")));
}

// C32 D19: the record keeps each carried file as it lies at the new place.
void TestLooseCarry::asCarriedReadsTheTarget()
{
    QTemporaryDir dir;
    const QString to = dir.filePath(QStringLiteral("fast/games"));
    write(to + u"/manual.pdf", "the manual");
    CarryFile carried = seen(to, QStringLiteral("manual.pdf"));
    carried.size = 3; // as the dry run saw it at the source: the target's own wins
    carried.outcome = CarryFile::Outcome::AlreadyThere;
    CarryFile stayed;
    stayed.relative = QStringLiteral("recent.txt");
    stayed.outcome = CarryFile::Outcome::Changed;
    const QList<CarryFile> kept = LooseCarry::asCarried(to, {carried, stayed});
    QCOMPARE(kept.size(), 1);
    QCOMPARE(kept.first().relative, QStringLiteral("manual.pdf"));
    QCOMPARE(kept.first().size, qint64(10));
    QCOMPARE(kept.first().modified, QFileInfo(to + u"/manual.pdf").lastModified());
    QCOMPARE(kept.first().outcome, CarryFile::Outcome::ToDo);
    QVERIFY(kept.first().why.isEmpty());
}

// C32 D19 a: unchanged goes back; changed since (size or time) stays and says when; gone stays.
void TestLooseCarry::backListKeepsWhatChangedSince()
{
    QTemporaryDir dir;
    const QString at = dir.filePath(QStringLiteral("fast/games"));
    write(at + u"/manual.pdf", "the manual");
    write(at + u"/notes/readme.txt", "read me");
    write(at + u"/gone.txt", "gone soon");
    const QList<CarryFile> carried = {seen(at, QStringLiteral("manual.pdf")),
                                      seen(at, QStringLiteral("notes/readme.txt")),
                                      seen(at, QStringLiteral("gone.txt"))};
    write(at + u"/notes/readme.txt", "read me, edited");
    QFile::remove(at + u"/gone.txt");
    write(at + u"/new-scan.png", "put there since");

    const QList<CarryFile> back = LooseCarry::backList(at, carried, QDateTime::currentDateTime());
    QCOMPARE(back.size(), 3); // never the file the move didn't carry
    QCOMPARE(back.at(0).outcome, CarryFile::Outcome::ToDo);
    QCOMPARE(back.at(1).outcome, CarryFile::Outcome::Changed);
    const QString time = QLocale().toString(
        QFileInfo(at + u"/notes/readme.txt").lastModified().time(), QLocale::ShortFormat);
    QCOMPARE(back.at(1).why, QStringLiteral("changed at %1, after the move").arg(time));
    QCOMPARE(back.at(2).outcome, CarryFile::Outcome::Gone);

    // Only the time changed: it stays too.
    QFile manual(at + u"/manual.pdf");
    QVERIFY(manual.open(QIODevice::ReadWrite));
    QVERIFY(manual.setFileTime(QDateTime::currentDateTime().addSecs(-60),
                               QFileDevice::FileModificationTime));
    manual.close();
    QCOMPARE(LooseCarry::backList(at, carried, QDateTime::currentDateTime()).at(0).outcome,
             CarryFile::Outcome::Changed);
}

// C32 D19 a: run() from the new place to the old one carries the list back by M17's steps.
void TestLooseCarry::movedBackAsTheMoveDid()
{
    QTemporaryDir dir;
    const QString from = dir.filePath(QStringLiteral("games"));
    const QString to = dir.filePath(QStringLiteral("fast/games"));
    write(from + u"/manual.pdf", "the manual");
    write(from + u"/notes/readme.txt", "read me");
    const LooseCarry::Result there = LooseCarry::run(
        from,
        to,
        {seen(from, QStringLiteral("manual.pdf")), seen(from, QStringLiteral("notes/readme.txt"))},
        later());
    QCOMPARE(there.carried, 2);
    const QList<CarryFile> carried = LooseCarry::asCarried(to, there.files);
    write(to + u"/notes/readme.txt", "read me, edited");
    write(to + u"/new-scan.png", "put there since");

    const QList<CarryFile> list = LooseCarry::backList(to, carried, later());
    const LooseCarry::Result back = LooseCarry::run(to, from, list, later());
    QCOMPARE(back.carried, 1);
    QCOMPARE(back.stayed, 1);
    QCOMPARE(read(from + u"/manual.pdf"), QByteArray("the manual"));
    QVERIFY(!QFile::exists(to + u"/manual.pdf"));
    QCOMPARE(read(to + u"/notes/readme.txt"), QByteArray("read me, edited"));
    QVERIFY(!QFile::exists(from + u"/notes/readme.txt"));
    QVERIFY(QFile::exists(to + u"/new-scan.png"));
    QVERIFY(!QFile::exists(from + u"/new-scan.png"));
}

QTEST_GUILESS_MAIN(TestLooseCarry)
#include "tst_loosecarry.moc"
