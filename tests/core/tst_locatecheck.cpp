// DECISIONS C50 GT2 (#294): `do locate`'s find, each wanted file's relative path and size in
// a folder, over a table standing for the disk (PLAN Q9); and do remove's look for any file.

#include "core/LocateCheck.hpp"

#include <QFile>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::ContentFile;
namespace LocateCheck = omaseed::core::LocateCheck;

namespace {

ContentFile file(const QString &path, qint64 size, double progress = 1.0, int priority = 1)
{
    ContentFile out;
    out.path = path;
    out.size = size;
    out.progress = progress;
    out.priority = priority;
    return out;
}

LocateCheck::Stat disk(const QHash<QString, qint64> &table)
{
    return [table](const QString &path) -> std::optional<qint64> {
        if (!table.contains(path))
            return std::nullopt;
        return table.value(path);
    };
}

const QList<ContentFile> kRecordings{file(QStringLiteral("Rec/01.flac"), 100),
                                     file(QStringLiteral("Rec/02.flac"), 200),
                                     file(QStringLiteral("Rec/sub/03.flac"), 300)};

} // namespace

class TestLocateCheck : public QObject
{
    Q_OBJECT

private slots:
    void allThere()
    {
        const auto result =
            LocateCheck::check(kRecordings,
                               QStringLiteral("/data/b/"),
                               disk({{QStringLiteral("/data/b/Rec/01.flac"), 100},
                                     {QStringLiteral("/data/b/Rec/02.flac"), 200},
                                     {QStringLiteral("/data/b/Rec/sub/03.flac"), 300}}));
        QCOMPARE(result.found, 3);
        QCOMPARE(result.files, 3);
        QVERIFY(result.all());
        QCOMPARE(result.bytes, qint64(600));
        QVERIFY(result.firstMissing.isEmpty());
    }

    void noneFound()
    {
        const auto result = LocateCheck::check(kRecordings, QStringLiteral("/data/b"), disk({}));
        QCOMPARE(result.found, 0);
        QCOMPARE(result.files, 3);
        QVERIFY(!result.all());
        QCOMPARE(result.firstMissing, QStringLiteral("Rec/01.flac"));
    }

    void partial()
    {
        const auto result =
            LocateCheck::check(kRecordings,
                               QStringLiteral("/data/b"),
                               disk({{QStringLiteral("/data/b/Rec/01.flac"), 100},
                                     {QStringLiteral("/data/b/Rec/sub/03.flac"), 300}}));
        QCOMPARE(result.found, 2);
        QCOMPARE(result.firstMissing, QStringLiteral("Rec/02.flac"));
        QCOMPARE(result.bytes, qint64(400));
    }

    void sizeDiffers()
    {
        // A complete file of another size is another file.
        const auto result =
            LocateCheck::check(kRecordings,
                               QStringLiteral("/data/b"),
                               disk({{QStringLiteral("/data/b/Rec/01.flac"), 100},
                                     {QStringLiteral("/data/b/Rec/02.flac"), 199},
                                     {QStringLiteral("/data/b/Rec/sub/03.flac"), 300}}));
        QCOMPARE(result.found, 2);
        QCOMPARE(result.firstMissing, QStringLiteral("Rec/02.flac"));
    }

    void skippedFileAbsent()
    {
        const QList<ContentFile> files{file(QStringLiteral("Rec/01.flac"), 100),
                                       file(QStringLiteral("Rec/extra.nfo"), 10, 0.0, 0)};
        const auto result = LocateCheck::check(
            files, QStringLiteral("/d"), disk({{QStringLiteral("/d/Rec/01.flac"), 100}}));
        QCOMPARE(result.files, 1);
        QCOMPARE(result.found, 1);
        QVERIFY(result.all());
    }

    void incompleteFileSmaller()
    {
        const QList<ContentFile> files{file(QStringLiteral("a.bin"), 100, 0.5),
                                       file(QStringLiteral("b.bin"), 100, 0.5)};
        const auto result = LocateCheck::check(
            files,
            QStringLiteral("/d"),
            disk({{QStringLiteral("/d/a.bin"), 60}, {QStringLiteral("/d/b.bin"), 120}}));
        QCOMPARE(result.found, 1); // larger than the torrent's: not its file
        QCOMPARE(result.firstMissing, QStringLiteral("b.bin"));
    }

    // C50 GT1 (#295): do remove's guard counts any file there, at any size, skipped ones too.
    void presentCountsAnyFile()
    {
        const QList<ContentFile> files{file(QStringLiteral("Rec/01.flac"), 100),
                                       file(QStringLiteral("Rec/02.flac"), 200, 0.5),
                                       file(QStringLiteral("Rec/extra.nfo"), 10, 0.0, 0)};
        auto result = LocateCheck::present(files, QStringLiteral("/d"), disk({}));
        QCOMPARE(result.found, 0);
        QCOMPARE(result.files, 3);
        result = LocateCheck::present(files,
                                      QStringLiteral("/d"),
                                      disk({{QStringLiteral("/d/Rec/01.flac"), 7},
                                            {QStringLiteral("/d/Rec/extra.nfo"), 10}}));
        QCOMPARE(result.found, 2);
        QCOMPARE(result.firstMissing, QStringLiteral("Rec/02.flac"));
    }

    void noWantedFile()
    {
        const auto result = LocateCheck::check({}, QStringLiteral("/d"), disk({}));
        QCOMPARE(result.files, 0);
        QVERIFY(!result.all());
    }

    void shownName()
    {
        QCOMPARE(LocateCheck::shownName(kRecordings, QStringLiteral("Rec/sub/03.flac")),
                 QStringLiteral("sub/03.flac"));
        const QList<ContentFile> loose{file(QStringLiteral("a.bin"), 1),
                                       file(QStringLiteral("Dir/b.bin"), 1)};
        QCOMPARE(LocateCheck::shownName(loose, QStringLiteral("Dir/b.bin")),
                 QStringLiteral("Dir/b.bin"));
    }

    // C50 GT1: an unplugged drive isn't files gone. udisks' folder gone entirely, or an
    // empty mount folder left behind, is a drive not mounted; a mounted drive without the
    // folder is files gone.
    void unmountedDrive()
    {
        const QSet<QString> there{QStringLiteral("/"),
                                  QStringLiteral("/run"),
                                  QStringLiteral("/run/media"),
                                  QStringLiteral("/run/media/me"),
                                  QStringLiteral("/run/media/me/OTHER"),
                                  QStringLiteral("/mnt"),
                                  QStringLiteral("/mnt/shelf"),
                                  QStringLiteral("/mnt/disk"),
                                  QStringLiteral("/data"),
                                  QStringLiteral("/home/me")};
        const LocateCheck::Exists exists = [there](const QString &p) { return there.contains(p); };
        LocateCheck::Mounts mounts;
        mounts.mounted = {QStringLiteral("/"),
                          QStringLiteral("/run"),
                          QStringLiteral("/run/media/me/OTHER"),
                          QStringLiteral("/mnt/disk")};
        mounts.listed = {QStringLiteral("/"), QStringLiteral("/data"), QStringLiteral("/mnt/disk")};
        const auto drive = [&](const char *path) {
            return LocateCheck::unmountedDrive(
                QString::fromUtf8(path), mounts, exists, QStringLiteral("me"));
        };
        QCOMPARE(drive("/run/media/me/SHELF/Films"), QStringLiteral("/run/media/me/SHELF"));
        QCOMPARE(drive("/mnt/shelf/Films/"), QStringLiteral("/mnt/shelf")); // an empty folder
        QCOMPARE(drive("/media/me/SHELF/Films"), QStringLiteral("/media/me/SHELF"));
        QCOMPARE(drive("/media/SHELF/Films"), QStringLiteral("/media/SHELF"));
        QCOMPARE(drive("/data/lib"), QStringLiteral("/data")); // in fstab, not mounted
        // Files gone: the drive is mounted, or the path is on no drive of its own.
        QCOMPARE(drive("/run/media/me/OTHER/Films"), QString());
        QCOMPARE(drive("/mnt/disk/Films"), QString());
        QCOMPARE(drive("/home/me/Films"), QString());
        // There: nothing to tell.
        QCOMPARE(drive("/mnt/shelf"), QString());
    }

    void readMounts()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString info = dir.filePath(QStringLiteral("mountinfo"));
        const QString fstab = dir.filePath(QStringLiteral("fstab"));
        QFile a(info);
        QVERIFY(a.open(QIODevice::WriteOnly));
        a.write("22 1 0:21 / / rw - btrfs /dev/x rw\n"
                "40 22 8:1 / /run/media/me/MY\\040DISK rw - ext4 /dev/sdb1 rw\n");
        a.close();
        QFile b(fstab);
        QVERIFY(b.open(QIODevice::WriteOnly));
        b.write("# a comment\nUUID=1 / btrfs rw 0 0\nUUID=2 /data ext4 rw 0 2\n"
                "UUID=3 none swap sw 0 0\n");
        b.close();
        const LocateCheck::Mounts mounts = LocateCheck::readMounts(info, fstab);
        QCOMPARE(mounts.mounted,
                 QStringList({QStringLiteral("/"), QStringLiteral("/run/media/me/MY DISK")}));
        QCOMPARE(mounts.listed, QStringList({QStringLiteral("/"), QStringLiteral("/data")}));
    }

    void statLocal()
    {
        QVERIFY(!LocateCheck::statLocal(QStringLiteral("/nonexistent/omaseed/x")).has_value());
        QVERIFY(!LocateCheck::statLocal(QStringLiteral("/")).has_value()); // a folder
    }
};

QTEST_GUILESS_MAIN(TestLocateCheck)
#include "tst_locatecheck.moc"
