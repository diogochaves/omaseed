// LooseCarry: the files no torrent owns, carried after the torrents (board 42 §7, C15 M17).

#include "core/LooseCarry.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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

QTEST_GUILESS_MAIN(TestLooseCarry)
#include "tst_loosecarry.moc"
