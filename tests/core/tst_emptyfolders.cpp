// EmptyFolders: the folders a move left empty at its source (C15 M17, #203).

#include "core/EmptyFolders.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace omaseed::core;

namespace {

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
}

bool isDir(const QString &path)
{
    return QFileInfo(path).isDir();
}

} // namespace

class TestEmptyFolders : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void nestedEmptiesGoWithTheRoot()
    {
        // games and games/handheld, the torrents' save paths, both left empty (#203).
        QTemporaryDir dir;
        const QString root = dir.path() + QStringLiteral("/fast/games");
        QVERIFY(QDir().mkpath(root + QStringLiteral("/handheld")));
        const int removed = EmptyFolders::remove(
            root, {root + QStringLiteral("/handheld"), root}, true, {dir.path() + "/lib/games"});
        QCOMPARE(removed, 2);
        QVERIFY(!isDir(root));
        QVERIFY(isDir(dir.path() + QStringLiteral("/fast"))); // above the root: kept
    }

    void rootKeptWithoutWithRoot()
    {
        QTemporaryDir dir;
        const QString root = dir.path() + QStringLiteral("/sel/keep");
        QVERIFY(QDir().mkpath(root + QStringLiteral("/games/handheld")));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/films")));
        const int removed = EmptyFolders::remove(
            root,
            {root + QStringLiteral("/games/handheld"), root + QStringLiteral("/films")},
            false);
        QCOMPARE(removed, 3);
        QVERIFY(isDir(root));
        QVERIFY(!isDir(root + QStringLiteral("/games")));
    }

    void aFolderThatHoldsAnythingStays()
    {
        QTemporaryDir dir;
        const QString root = dir.path() + QStringLiteral("/games");
        // handheld holds a hidden file the user left there; arcade is empty.
        touch(root + QStringLiteral("/handheld/.keep"));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/arcade")));
        const int removed = EmptyFolders::remove(
            root, {root + QStringLiteral("/handheld"), root + QStringLiteral("/arcade")}, true);
        QCOMPARE(removed, 1);
        QVERIFY(!isDir(root + QStringLiteral("/arcade")));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/handheld/.keep")));
        QVERIFY(isDir(root)); // holds handheld
    }

    void onlyWhatTheMoveEmptied()
    {
        // An empty folder no torrent left stays: only the given ones and those above them.
        QTemporaryDir dir;
        const QString root = dir.path() + QStringLiteral("/games");
        QVERIFY(QDir().mkpath(root + QStringLiteral("/handheld")));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/made-by-hand")));
        const int removed = EmptyFolders::remove(root, {root + QStringLiteral("/handheld")}, true);
        QCOMPARE(removed, 1);
        QVERIFY(isDir(root + QStringLiteral("/made-by-hand")));
        QVERIFY(isDir(root));
    }

    void neverOutsideTheRoot()
    {
        QTemporaryDir dir;
        const QString root = dir.path() + QStringLiteral("/games");
        const QString elsewhere = dir.path() + QStringLiteral("/films/short");
        QVERIFY(QDir().mkpath(root));
        QVERIFY(QDir().mkpath(elsewhere));
        // A sibling whose name starts with the root's is outside it too.
        QVERIFY(QDir().mkpath(dir.path() + QStringLiteral("/games-old")));
        const int removed = EmptyFolders::remove(
            root, {elsewhere, dir.path() + QStringLiteral("/games-old"), dir.path()}, true);
        QCOMPARE(removed, 0);
        QVERIFY(isDir(elsewhere));
        QVERIFY(isDir(dir.path() + QStringLiteral("/games-old")));
        QVERIFY(isDir(root));
    }

    void keptPathsAndWhatHoldsThem()
    {
        // The destination (inside the root here) and every folder holding it stay.
        QTemporaryDir dir;
        const QString root = dir.path() + QStringLiteral("/games");
        const QString to = root + QStringLiteral("/a/b");
        QVERIFY(QDir().mkpath(to));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/c")));
        const int removed =
            EmptyFolders::remove(root, {to, root + QStringLiteral("/c")}, true, {to});
        QCOMPARE(removed, 1);
        QVERIFY(isDir(to));
        QVERIFY(!isDir(root + QStringLiteral("/c")));
    }

    void aLinkIsNeverFollowedNorRemoved()
    {
        QTemporaryDir dir;
        const QString root = dir.path() + QStringLiteral("/games");
        const QString target = dir.path() + QStringLiteral("/elsewhere");
        QVERIFY(QDir().mkpath(root));
        QVERIFY(QDir().mkpath(target));
        QVERIFY(QFile::link(target, root + QStringLiteral("/link")));
        const int removed = EmptyFolders::remove(root, {root + QStringLiteral("/link")}, true);
        QCOMPARE(removed, 0);
        QVERIFY(QFileInfo(root + QStringLiteral("/link")).isSymLink());
        QVERIFY(isDir(target));
    }

    void noBoundaryNoSweep()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.path() + QStringLiteral("/a")));
        QCOMPARE(EmptyFolders::remove(QString(), {dir.path() + QStringLiteral("/a")}, true), 0);
        QCOMPARE(EmptyFolders::remove(QStringLiteral("/"), {dir.path() + "/a"}, true), 0);
        QCOMPARE(EmptyFolders::remove(QStringLiteral("rel"), {QStringLiteral("rel/a")}, true), 0);
        QVERIFY(isDir(dir.path() + QStringLiteral("/a")));
    }

    void missingFoldersAreFine()
    {
        // libtorrent removed some already; the rest still go.
        QTemporaryDir dir;
        const QString root = dir.path() + QStringLiteral("/games");
        QVERIFY(QDir().mkpath(root + QStringLiteral("/handheld")));
        const int removed =
            EmptyFolders::remove(root,
                                 {root + QStringLiteral("/handheld/Handheld Pack 001"),
                                  root + QStringLiteral("/handheld")},
                                 true);
        QCOMPARE(removed, 2);
        QVERIFY(!isDir(root));
    }
};

QTEST_GUILESS_MAIN(TestEmptyFolders)
#include "tst_emptyfolders.moc"
