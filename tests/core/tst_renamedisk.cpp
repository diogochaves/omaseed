// RenameDisk (#64): which rename targets would land on a file the torrent does not own, read
// from a real folder. The torrent here is upstream#85's: album/a.txt and album/b.txt, with a
// stray album/d.txt beside them that qBittorrent would let a rename replace.

#include "core/RenameDisk.hpp"
#include "core/RenamePlan.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::RenameDisk;
using omaseed::core::RenameOptions;
using omaseed::core::RenamePlan;
using omaseed::core::RenameRow;

namespace {

bool write(const QString &path, const QByteArray &content)
{
    if (!QDir().mkpath(QFileInfo(path).path()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(content) == content.size();
}

const QStringList own = {QStringLiteral("album/a.txt"), QStringLiteral("album/b.txt")};

} // namespace

class TestRenameDisk : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void aStrayFileIsTaken();
    void theTorrentsOwnFilesAndFreeNamesAreNot();
    void aFolderOrABrokenLinkOfThatNameIsTaken();
    void aFolderHoldingAStrayFileIsTaken();
    void anEmptyOrAbsentFolderIsFree();
    void aFolderOfThatNameAsAFileIsTaken();
    void seeingNothingTakesNothing();
    void thePlanMarksTheRowAndRefusesToRun();
    void chasingRenamesThroughOwnFilesStillRun();

private:
    QTemporaryDir m_dir;
    QString m_base;
};

void TestRenameDisk::init()
{
    QVERIFY(m_dir.isValid());
    m_base = m_dir.path() + QStringLiteral("/downloads");
    QDir(m_base).removeRecursively();
    QVERIFY(write(m_base + QStringLiteral("/album/a.txt"), "alpha"));
    QVERIFY(write(m_base + QStringLiteral("/album/b.txt"), "beta"));
    QVERIFY(write(m_base + QStringLiteral("/album/d.txt"), "keep me, not part of the torrent"));
}

void TestRenameDisk::aStrayFileIsTaken()
{
    const RenameDisk disk(m_base, own);
    QVERIFY(disk.sees());
    QVERIFY(disk.fileTaken(QStringLiteral("album/d.txt")));
    // A trailing slash on the folder changes nothing.
    QVERIFY(RenameDisk(m_base + QLatin1Char('/'), own).fileTaken(QStringLiteral("album/d.txt")));
}

void TestRenameDisk::theTorrentsOwnFilesAndFreeNamesAreNot()
{
    const RenameDisk disk(m_base, own);
    // qBittorrent refuses these itself (409), and RenamePlan checks them against the list.
    QVERIFY(!disk.fileTaken(QStringLiteral("album/a.txt")));
    QVERIFY(!disk.fileTaken(QStringLiteral("album/c.txt")));
    // Linux tells case apart: D.txt is another name.
    QVERIFY(!disk.fileTaken(QStringLiteral("album/D.txt")));
    QVERIFY(!disk.fileTaken(QString()));
}

void TestRenameDisk::aFolderOrABrokenLinkOfThatNameIsTaken()
{
    QVERIFY(QDir().mkpath(m_base + QStringLiteral("/album/sub")));
    QVERIFY(
        QFile::link(m_base + QStringLiteral("/nowhere"), m_base + QStringLiteral("/album/link")));
    const RenameDisk disk(m_base, own);
    QVERIFY(disk.fileTaken(QStringLiteral("album/sub")));
    QVERIFY(disk.fileTaken(QStringLiteral("album/link")));
}

void TestRenameDisk::aFolderHoldingAStrayFileIsTaken()
{
    // Renaming album → other would put a.txt over other/a.txt.
    QVERIFY(write(m_base + QStringLiteral("/other/deep/a.txt"), "other"));
    const RenameDisk disk(m_base, own);
    QVERIFY(disk.folderTaken(QStringLiteral("other")));
    // The torrent's own folder holds a stray (d.txt) too.
    QVERIFY(disk.folderTaken(QStringLiteral("album")));
    // Hidden files count.
    QVERIFY(write(m_base + QStringLiteral("/hidden/.stray"), "x"));
    QVERIFY(disk.folderTaken(QStringLiteral("hidden")));
}

void TestRenameDisk::anEmptyOrAbsentFolderIsFree()
{
    QVERIFY(QDir().mkpath(m_base + QStringLiteral("/empty/nested")));
    QVERIFY(QFile::remove(m_base + QStringLiteral("/album/d.txt")));
    const RenameDisk disk(m_base, own);
    QVERIFY(!disk.folderTaken(QStringLiteral("empty")));
    QVERIFY(!disk.folderTaken(QStringLiteral("absent")));
    // A folder holding only the torrent's own files.
    QVERIFY(!disk.folderTaken(QStringLiteral("album")));
}

void TestRenameDisk::aFolderOfThatNameAsAFileIsTaken()
{
    const RenameDisk disk(m_base, own);
    QVERIFY(disk.folderTaken(QStringLiteral("album/d.txt")));
}

void TestRenameDisk::seeingNothingTakesNothing()
{
    // A server whose disks this machine cannot read: say nothing, as before #64.
    for (const RenameDisk &disk : {RenameDisk(),
                                   RenameDisk(QString(), own),
                                   RenameDisk(QStringLiteral("relative/path"), own)}) {
        QVERIFY(!disk.sees());
        QVERIFY(!disk.fileTaken(QStringLiteral("album/d.txt")));
        QVERIFY(!disk.folderTaken(QStringLiteral("album")));
    }
}

void TestRenameDisk::thePlanMarksTheRowAndRefusesToRun()
{
    const RenameDisk disk(m_base, own);
    RenameOptions options;
    options.find = QStringLiteral("b");
    options.replace = QStringLiteral("d");
    const auto taken = [&disk](const QString &path) { return disk.fileTaken(path); };
    const auto plan = RenamePlan::make({own.at(1)}, {own.at(0)}, options, taken);
    QCOMPARE(plan.rows().first().newPath, QStringLiteral("album/d.txt"));
    QCOMPARE(plan.rows().first().status, RenameRow::Status::Collides);
    QCOMPARE(plan.collisions(), 1);
    QVERIFY(!plan.canRun());
    QVERIFY(plan.executionOrder().isEmpty());

    // b → c is free on disk and runs.
    options.replace = QStringLiteral("c");
    QVERIFY(RenamePlan::make({own.at(1)}, {own.at(0)}, options, taken).canRun());
}

void TestRenameDisk::chasingRenamesThroughOwnFilesStillRun()
{
    // 10 → 1 while 1 → 2: 1.txt is on disk, but it is the torrent's own and moves out first.
    const QStringList numbered = {QStringLiteral("album/10.txt"), QStringLiteral("album/1.txt")};
    QVERIFY(write(m_base + QStringLiteral("/album/10.txt"), "ten"));
    QVERIFY(write(m_base + QStringLiteral("/album/1.txt"), "one"));
    RenameOptions options;
    options.find = QStringLiteral("^\\d+$");
    options.replace = QStringLiteral("{n}");
    options.regex = true;
    const RenameDisk disk(m_base, numbered);
    const auto taken = [&disk](const QString &path) { return disk.fileTaken(path); };
    const auto plan = RenamePlan::make(numbered, {}, options, taken);
    QVERIFY(plan.canRun());
    QCOMPARE(plan.executionOrder(), QList<int>({1, 0}));

    // A stray 2.txt: 1 → 2 is refused, so 1.txt never moves and 10 → 1 cannot run either.
    QVERIFY(write(m_base + QStringLiteral("/album/2.txt"), "stray"));
    const auto blocked = RenamePlan::make(numbered, {}, options, taken);
    QCOMPARE(blocked.rows().at(1).status, RenameRow::Status::Collides);
    QCOMPARE(blocked.collisions(), 2);
    QVERIFY(!blocked.canRun());
}

QTEST_GUILESS_MAIN(TestRenameDisk)
#include "tst_renamedisk.moc"
