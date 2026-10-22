#include "core/FreeSpace.hpp"

#include <QHash>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::FreeSpaceProbe;
using omaseed::core::PathMap;
using omaseed::core::StorageReading;

namespace {

constexpr qint64 kGiB = 1024LL * 1024 * 1024;

/// A pretend machine: each folder that exists, the drive it is on, and that drive's room.
/// A folder that does not exist reads as its nearest listed parent's drive.
struct FakeDisks
{
    QHash<QString, StorageReading> folders;

    void add(const QString &path,
             const QString &root,
             qint64 free,
             qint64 total = 0,
             const QString &fileSystem = QStringLiteral("ext4"))
    {
        StorageReading reading;
        reading.fileSystem = fileSystem;
        reading.valid = true;
        reading.exists = true;
        reading.bytesFree = free;
        reading.bytesTotal = total > 0 ? total : free * 2;
        reading.rootPath = root;
        folders.insert(path, reading);
    }

    StorageReading read(const QString &path) const
    {
        QString probe = path;
        bool exact = true;
        while (!probe.isEmpty()) {
            const auto it = folders.constFind(probe);
            if (it != folders.constEnd()) {
                StorageReading reading = *it;
                reading.exists = exact;
                return reading;
            }
            exact = false;
            const qsizetype cut = probe.lastIndexOf(QLatin1Char('/'));
            probe = cut > 0 ? probe.left(cut)
                            : (probe == QStringLiteral("/") ? QString() : QStringLiteral("/"));
        }
        return {};
    }

    omaseed::core::StorageReader reader() const
    {
        return [this](const QString &path) { return read(path); };
    }
};

} // namespace

class TestFreeSpace : public QObject
{
    Q_OBJECT

private slots:
    void defaultFolderUsesTheServersFigure();
    void unknownBeforeTheServerSaysAnything();
    void localServerReadsOtherFolders();
    void aFolderThatDoesNotExistYetReadsItsParent();
    void dockerOnLoopbackIsNotLocal();
    void calibrationAllowsSlack();
    void pathMapTurnsServerPathsIntoLocalOnes();
    void pathMapLeavesUncoveredPathsUnknown();
    void remoteFolderUnderTheDefaultUsesItsFigure();
    void remoteOtherFolderIsUnknown();
    void readsARealFolder();
    void localPathsOnlyWhereTheDisksAreSeen();
    void answersCarryTheFileSystem();
};

void TestFreeSpace::defaultFolderUsesTheServersFigure()
{
    FakeDisks disks;
    FreeSpaceProbe probe(disks.reader());
    probe.setServer(QStringLiteral("/srv/torrents/"), 812 * kGiB, PathMap(), false);

    for (const QString &path :
         {QString(), QStringLiteral("/srv/torrents"), QStringLiteral(" /srv/torrents/ ")}) {
        const auto answer = probe.answer(path);
        QCOMPARE(answer.source, FreeSpaceProbe::Source::Default);
        QCOMPARE(answer.bytesFree, 812 * kGiB);
        QVERIFY(answer.where.isEmpty());
    }
}

void TestFreeSpace::unknownBeforeTheServerSaysAnything()
{
    FakeDisks disks;
    FreeSpaceProbe probe(disks.reader());
    QCOMPARE(probe.answer(QString()).source, FreeSpaceProbe::Source::Unknown);
    probe.setServer(QStringLiteral("/srv/torrents"), 0, PathMap(), false);
    QCOMPARE(probe.answer(QString()).source, FreeSpaceProbe::Source::Unknown);
}

void TestFreeSpace::localServerReadsOtherFolders()
{
    FakeDisks disks;
    disks.add(QStringLiteral("/home/d/Downloads"), QStringLiteral("/home"), 1500 * kGiB);
    disks.add(
        QStringLiteral("/mnt/media/films"), QStringLiteral("/mnt/media"), 50 * kGiB, 4000 * kGiB);
    FreeSpaceProbe probe(disks.reader());
    probe.setServer(QStringLiteral("/home/d/Downloads"), 1500 * kGiB, PathMap(), false);
    QVERIFY(probe.seesServerDisks());

    const auto answer = probe.answer(QStringLiteral("/mnt/media/films"));
    QCOMPARE(answer.source, FreeSpaceProbe::Source::Local);
    QCOMPARE(answer.bytesFree, 50 * kGiB);
    QCOMPARE(answer.bytesTotal, 4000 * kGiB);
    QCOMPARE(answer.where, QStringLiteral("/mnt/media"));
    // The default folder still says "this drive", with the server's figure.
    QCOMPARE(probe.answer(QString()).source, FreeSpaceProbe::Source::Default);
}

void TestFreeSpace::aFolderThatDoesNotExistYetReadsItsParent()
{
    FakeDisks disks;
    disks.add(QStringLiteral("/home/d/Downloads"), QStringLiteral("/home"), 1500 * kGiB);
    disks.add(QStringLiteral("/mnt/media"), QStringLiteral("/mnt/media"), 50 * kGiB);
    FreeSpaceProbe probe(disks.reader());
    probe.setServer(QStringLiteral("/home/d/Downloads"), 1500 * kGiB, PathMap(), false);

    const auto answer = probe.answer(QStringLiteral("/mnt/media/new/folder"));
    QCOMPARE(answer.source, FreeSpaceProbe::Source::Local);
    QCOMPARE(answer.where, QStringLiteral("/mnt/media"));
}

void TestFreeSpace::dockerOnLoopbackIsNotLocal()
{
    // The container's /downloads is no folder here; "/" exists and would read the wrong drive.
    FakeDisks disks;
    disks.add(QStringLiteral("/"), QStringLiteral("/"), 1500 * kGiB);
    FreeSpaceProbe probe(disks.reader());
    probe.setServer(QStringLiteral("/downloads"), 1500 * kGiB, PathMap(), false);
    QVERIFY(!probe.seesServerDisks());
    QCOMPARE(probe.answer(QStringLiteral("/data/films")).source, FreeSpaceProbe::Source::Unknown);
}

void TestFreeSpace::calibrationAllowsSlack()
{
    FakeDisks disks;
    disks.add(QStringLiteral("/srv"), QStringLiteral("/srv"), 1000 * kGiB);
    FreeSpaceProbe probe(disks.reader());

    probe.setServer(QStringLiteral("/srv"), 1000 * kGiB + 5 * kGiB, PathMap(), false); // 0.5 %
    QVERIFY(probe.seesServerDisks());
    probe.setServer(QStringLiteral("/srv"), 1100 * kGiB, PathMap(), false); // another drive
    QVERIFY(!probe.seesServerDisks());

    // A small drive gets the 256 MiB floor, not 1 % of almost nothing.
    disks.add(QStringLiteral("/small"), QStringLiteral("/small"), kGiB);
    probe.setServer(QStringLiteral("/small"), kGiB + 100 * 1024 * 1024, PathMap(), false);
    QVERIFY(probe.seesServerDisks());
}

void TestFreeSpace::pathMapTurnsServerPathsIntoLocalOnes()
{
    FakeDisks disks;
    disks.add(QStringLiteral("/mnt/nas/torrents"), QStringLiteral("/mnt/nas"), 900 * kGiB);
    disks.add(QStringLiteral("/mnt/nas/films"), QStringLiteral("/mnt/nas"), 900 * kGiB);
    PathMap map;
    map.addMapping(QStringLiteral("/data"), QStringLiteral("/mnt/nas"));
    FreeSpaceProbe probe(disks.reader());
    probe.setServer(QStringLiteral("/data/torrents"), 900 * kGiB, map, true);
    QVERIFY(probe.seesServerDisks());

    const auto answer = probe.answer(QStringLiteral("/data/films"));
    QCOMPARE(answer.source, FreeSpaceProbe::Source::Local);
    QCOMPARE(answer.where, QStringLiteral("/mnt/nas"));

    // The same map switched off: /data/torrents is no folder here.
    probe.setServer(QStringLiteral("/data/torrents"), 900 * kGiB, map, false);
    QVERIFY(!probe.seesServerDisks());
}

void TestFreeSpace::pathMapLeavesUncoveredPathsUnknown()
{
    FakeDisks disks;
    disks.add(QStringLiteral("/mnt/nas/torrents"), QStringLiteral("/mnt/nas"), 900 * kGiB);
    disks.add(QStringLiteral("/"), QStringLiteral("/"), 40 * kGiB);
    PathMap map;
    map.addMapping(QStringLiteral("/data"), QStringLiteral("/mnt/nas"));
    FreeSpaceProbe probe(disks.reader());
    probe.setServer(QStringLiteral("/data/torrents"), 900 * kGiB, map, true);

    QCOMPARE(probe.answer(QStringLiteral("/config/other")).source, FreeSpaceProbe::Source::Unknown);
}

void TestFreeSpace::remoteFolderUnderTheDefaultUsesItsFigure()
{
    FakeDisks disks;
    FreeSpaceProbe probe(disks.reader());
    probe.setServer(QStringLiteral("/data/torrents"), 812 * kGiB, PathMap(), false);
    QVERIFY(!probe.seesServerDisks());

    const auto under = probe.answer(QStringLiteral("/data/torrents/films"));
    QCOMPARE(under.source, FreeSpaceProbe::Source::Default);
    QCOMPARE(under.bytesFree, 812 * kGiB);
    QCOMPARE(probe.answer(QStringLiteral("/data/torrents-old")).source,
             FreeSpaceProbe::Source::Unknown);
}

void TestFreeSpace::remoteOtherFolderIsUnknown()
{
    FakeDisks disks;
    FreeSpaceProbe probe(disks.reader());
    probe.setServer(QStringLiteral("/data/torrents"), 812 * kGiB, PathMap(), false);
    QCOMPARE(probe.answer(QStringLiteral("/data/films")).source, FreeSpaceProbe::Source::Unknown);
}

void TestFreeSpace::readsARealFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const StorageReading here = FreeSpaceProbe::readLocal(dir.path());
    QVERIFY(here.valid);
    QVERIFY(here.exists);
    QVERIFY(here.bytesTotal > 0);
    QVERIFY(!here.rootPath.isEmpty());
    QVERIFY(!here.fileSystem.isEmpty());

    const StorageReading later = FreeSpaceProbe::readLocal(dir.path() + QStringLiteral("/not/yet"));
    QVERIFY(later.valid);
    QVERIFY(!later.exists);
    QCOMPARE(later.rootPath, here.rootPath);

    QVERIFY(!FreeSpaceProbe::readLocal(QStringLiteral("relative/path")).valid);
    QVERIFY(!FreeSpaceProbe::readLocal(QString()).valid);
}

void TestFreeSpace::localPathsOnlyWhereTheDisksAreSeen()
{
    // #64's rename guard reads the server's files through this: only where #29 trusts them.
    FakeDisks disks;
    disks.add(QStringLiteral("/home/d/Downloads"), QStringLiteral("/home"), 1500 * kGiB);
    FreeSpaceProbe probe(disks.reader());
    probe.setServer(QStringLiteral("/home/d/Downloads"), 1500 * kGiB, PathMap(), false);
    QCOMPARE(probe.localPathFor(QStringLiteral("/home/d/Downloads/album/")),
             QStringLiteral("/home/d/Downloads/album"));

    // Mapped: through the map, and nothing for a path it does not cover.
    disks.add(QStringLiteral("/mnt/nas/torrents"), QStringLiteral("/mnt/nas"), 900 * kGiB);
    PathMap map;
    map.addMapping(QStringLiteral("/data"), QStringLiteral("/mnt/nas"));
    probe.setServer(QStringLiteral("/data/torrents"), 900 * kGiB, map, true);
    QCOMPARE(probe.localPathFor(QStringLiteral("/data/torrents/album")),
             QStringLiteral("/mnt/nas/torrents/album"));
    QVERIFY(probe.localPathFor(QStringLiteral("/config/album")).isEmpty());

    // Docker on loopback, or a remote server: not seen, so nothing.
    probe.setServer(QStringLiteral("/downloads"), 1500 * kGiB, PathMap(), false);
    QVERIFY(!probe.seesServerDisks());
    QVERIFY(probe.localPathFor(QStringLiteral("/downloads/album")).isEmpty());
}

void TestFreeSpace::answersCarryTheFileSystem()
{
    // DECISIONS C17 D8: the driver's name, wherever this machine reads the drive.
    FakeDisks disks;
    disks.add(QStringLiteral("/home/d/Downloads"), QStringLiteral("/home"), 1500 * kGiB);
    disks.add(QStringLiteral("/mnt/media"),
              QStringLiteral("/mnt/media"),
              23 * kGiB,
              15000 * kGiB,
              QStringLiteral("ntfs3"));
    disks.add(QStringLiteral("/run/media/stick"),
              QStringLiteral("/run/media/stick"),
              20 * kGiB,
              32 * kGiB,
              QStringLiteral("vfat"));
    FreeSpaceProbe probe(disks.reader());
    probe.setServer(QStringLiteral("/home/d/Downloads"), 1500 * kGiB, PathMap(), false);
    QCOMPARE(probe.answer(QStringLiteral("/mnt/media/Downloads")).fileSystem,
             QStringLiteral("ntfs3"));
    QCOMPARE(probe.answer(QStringLiteral("/run/media/stick/films")).fileSystem,
             QStringLiteral("vfat"));
    // The default folder keeps the server's figure, and still names its drive's type here.
    const auto byDefault = probe.answer(QString());
    QCOMPARE(byDefault.source, FreeSpaceProbe::Source::Default);
    QCOMPARE(byDefault.fileSystem, QStringLiteral("ext4"));

    // A server whose disks this machine can't see: no type, never a guess.
    probe.setServer(QStringLiteral("/downloads"), 1500 * kGiB, PathMap(), false);
    QVERIFY(probe.answer(QString()).fileSystem.isEmpty());
    QVERIFY(probe.answer(QStringLiteral("/downloads/films")).fileSystem.isEmpty());
}

QTEST_APPLESS_MAIN(TestFreeSpace)

#include "tst_freespace.moc"
