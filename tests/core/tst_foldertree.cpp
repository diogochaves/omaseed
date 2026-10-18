#include "core/FolderTree.hpp"

#include <QTest>

using omaseed::core::FolderNode;
using omaseed::core::FolderTree;
using omaseed::core::Torrent;

namespace {

Torrent at(const QString &savePath, qint64 completed = 1, qint64 size = 0)
{
    static int serial = 0;
    Torrent torrent;
    torrent.hash = QStringLiteral("%1").arg(++serial, 40, 16, QLatin1Char('0'));
    torrent.savePath = savePath;
    torrent.completed = completed;
    torrent.size = size > 0 ? size : completed;
    return torrent;
}

QStringList shape(const QList<FolderNode> &nodes)
{
    QStringList out;
    for (const FolderNode &node : nodes) {
        out << QStringLiteral("%1%2%3 %4")
                   .arg(QString(node.depth * 2, QLatin1Char(' ')),
                        node.isHeading() ? QStringLiteral("# ") : QString(),
                        node.name)
                   .arg(node.count);
    }
    return out;
}

} // namespace

// Board 42 §2 (DECISIONS C15 M2, M3): the rail's folders.
class TestFolderTree : public QObject
{
    Q_OBJECT

private slots:
    void boardF1()
    {
        QList<Torrent> torrents;
        for (int i = 0; i < 3; ++i)
            torrents << at(QStringLiteral("/mnt/archive/games"), 100);
        for (int i = 0; i < 2; ++i)
            torrents << at(QStringLiteral("/mnt/archive/games/handheld"), 10);
        torrents << at(QStringLiteral("/mnt/fast/downloads"));
        torrents << at(QStringLiteral("/srv/media/series"));
        torrents << at(QStringLiteral("/srv/media/films"));

        const QList<FolderNode> nodes = FolderTree::build(torrents);
        QCOMPARE(shape(nodes),
                 QStringList({QStringLiteral("# /mnt/archive 5"),
                              QStringLiteral("  games 5"),
                              QStringLiteral("    handheld 2"),
                              QStringLiteral("# /mnt/fast 1"),
                              QStringLiteral("  downloads 1"),
                              QStringLiteral("# /srv/media 2"),
                              QStringLiteral("  films 1"),
                              QStringLiteral("  series 1")}));
        const FolderNode *games = FolderTree::find(nodes, QStringLiteral("/mnt/archive/games"));
        QVERIFY(games);
        QCOMPARE(games->countHere, 3);
        QCOMPARE(games->bytesHere, qint64(300));
        QCOMPARE(games->bytes, qint64(320));
        QCOMPARE(games->savePaths, 2);
        QVERIFY(games->hasChildren);
        QCOMPARE(games->parentPath, QStringLiteral("/mnt/archive"));
        const FolderNode *archive = FolderTree::find(nodes, QStringLiteral("/mnt/archive"));
        QVERIFY(archive && archive->isHeading());
        QCOMPARE(archive->countHere, 0);
    }

    void aDrivesTopThatIsASavePath()
    {
        // F2: torrents saved at the drive's top make it a row of its own; with the mount
        // point known there is no heading above it.
        const QList<Torrent> torrents{at(QStringLiteral("/mnt/archive")),
                                      at(QStringLiteral("/mnt/archive/games")),
                                      at(QStringLiteral("/mnt/archive/games/handheld"))};
        const auto mounts = [](const QString &path) {
            return path.startsWith(QLatin1String("/mnt/archive")) ? QStringLiteral("/mnt/archive")
                                                                  : QString();
        };
        const QList<FolderNode> nodes = FolderTree::build(torrents, mounts);
        QCOMPARE(shape(nodes),
                 QStringList({QStringLiteral("/mnt/archive 3"),
                              QStringLiteral("  games 2"),
                              QStringLiteral("    handheld 1")}));
        QCOMPARE(nodes.at(0).countHere, 1);
    }

    void theMountPointHeadsTheDrive()
    {
        // With the mount point known, a save path two folders down still hangs from it.
        const auto mounts = [](const QString &) { return QStringLiteral("/mnt/fast"); };
        const QList<FolderNode> nodes =
            FolderTree::build({at(QStringLiteral("/mnt/fast/a/b"))}, mounts);
        QCOMPARE(shape(nodes),
                 QStringList({QStringLiteral("# /mnt/fast 1"), QStringLiteral("  a/b 1")}));
    }

    void withoutMountsTheParentIsTheHeading()
    {
        const QList<FolderNode> nodes = FolderTree::build(
            {at(QStringLiteral("/mnt/archive")), at(QStringLiteral("/mnt/fast/downloads"))});
        QCOMPARE(shape(nodes),
                 QStringList({QStringLiteral("# /mnt 2"),
                              QStringLiteral("  archive 1"),
                              QStringLiteral("  # fast 1"),
                              QStringLiteral("    downloads 1")}));
    }

    void aSavePathAtTheTopHasNoHeading()
    {
        const QList<FolderNode> nodes =
            FolderTree::build({at(QStringLiteral("/downloads")), at(QStringLiteral("/"))});
        QCOMPARE(shape(nodes),
                 QStringList({QStringLiteral("/ 2"), QStringLiteral("  downloads 1")}));
    }

    void trailingSeparatorsAndBackslashes()
    {
        const QList<FolderNode> nodes = FolderTree::build({at(QStringLiteral("/srv/media/films/")),
                                                           at(QStringLiteral("/srv/media/films")),
                                                           at(QStringLiteral("D:\\seed\\films"))});
        QCOMPARE(shape(nodes),
                 QStringList({QStringLiteral("# /srv/media 2"),
                              QStringLiteral("  films 2"),
                              QStringLiteral("# D:/seed 1"),
                              QStringLiteral("  films 1")}));
    }

    void caseIsKept()
    {
        // Linux paths: two folders that differ in case are two folders.
        const QList<FolderNode> nodes = FolderTree::build(
            {at(QStringLiteral("/srv/media/Films")), at(QStringLiteral("/srv/media/films"))});
        QCOMPARE(nodes.size(), 3);
        QCOMPARE(nodes.at(1).path, QStringLiteral("/srv/media/Films"));
        QCOMPARE(nodes.at(2).path, QStringLiteral("/srv/media/films"));
    }

    void bytesAreWhatIsOnDisk()
    {
        // A partial pack: 4 GB of a 200 GB torrent.
        const qint64 gib = 1024LL * 1024 * 1024;
        const QList<FolderNode> nodes =
            FolderTree::build({at(QStringLiteral("/srv/games"), 4 * gib, 200 * gib)});
        QCOMPARE(FolderTree::find(nodes, QStringLiteral("/srv/games"))->bytes, 4 * gib);
    }

    void targetKeepsThePathBelow_data()
    {
        QTest::addColumn<QString>("savePath");
        QTest::addColumn<QString>("root");
        QTest::addColumn<QString>("newPath");
        QTest::addColumn<QString>("target");
        QTest::newRow("the root") << "/mnt/archive/games" << "/mnt/archive/games"
                                  << "/mnt/fast/games" << "/mnt/fast/games";
        QTest::newRow("below") << "/mnt/archive/games/handheld" << "/mnt/archive/games"
                               << "/mnt/fast/games" << "/mnt/fast/games/handheld";
        QTest::newRow("two below") << "/a/b/c/d" << "/a/b" << "/x" << "/x/c/d";
        QTest::newRow("trailing") << "/a/b/c/" << "/a/b/" << "/x/" << "/x/c";
        QTest::newRow("renamed") << "/a/games/h" << "/a/games" << "/b/play" << "/b/play/h";
        QTest::newRow("not under") << "/a/gamesx" << "/a/games" << "/b" << "";
        QTest::newRow("above") << "/a" << "/a/games" << "/b" << "";
        QTest::newRow("from the top") << "/games/h" << "/" << "/mnt/new" << "/mnt/new/games/h";
    }
    void targetKeepsThePathBelow()
    {
        QFETCH(QString, savePath);
        QFETCH(QString, root);
        QFETCH(QString, newPath);
        QFETCH(QString, target);
        QCOMPARE(FolderTree::targetFor(savePath, root, newPath), target);
    }

    void pathHelpers()
    {
        QCOMPARE(FolderTree::parentOf(QStringLiteral("/a/b")), QStringLiteral("/a"));
        QCOMPARE(FolderTree::parentOf(QStringLiteral("/a")), QStringLiteral("/"));
        QCOMPARE(FolderTree::parentOf(QStringLiteral("/")), QString());
        QCOMPARE(FolderTree::baseName(QStringLiteral("/a/b/")), QStringLiteral("b"));
        QCOMPARE(FolderTree::join(QStringLiteral("/"), QStringLiteral("a")), QStringLiteral("/a"));
        QCOMPARE(FolderTree::join(QStringLiteral("/x/"), QStringLiteral("/a")),
                 QStringLiteral("/x/a"));
    }
};

QTEST_GUILESS_MAIN(TestFolderTree)

#include "tst_foldertree.moc"
