#include "core/SharedData.hpp"

#include <QTest>

using omaseed::core::SharedData;
using omaseed::core::SharedDataRow;
using Relation = omaseed::core::SharedData::Relation;

namespace {

SharedDataRow row(const char *hash, const char *save, const char *content, qint64 size = 100)
{
    return {QString::fromUtf8(hash), QString::fromUtf8(save), QString::fromUtf8(content), size};
}

QStringList list(std::initializer_list<const char *> items)
{
    QStringList out;
    for (const char *item : items)
        out.append(QString::fromUtf8(item));
    return out;
}

} // namespace

// docs/26 §3, a row at a time.
class TestSharedData : public QObject
{
    Q_OBJECT

private slots:
    void normalizeTrimsAndReadsBackslashes();
    void isUnderRespectsComponentBoundaries();
    void sameFolderShares();
    void sameSingleFileShares();
    void oneInsideTheOtherShares();
    void siblingNamesThatOnlyStartAlikeDoNotShare();
    void comparisonIsCaseSensitive();
    void differentPathsNeverShare();
    void noRootBesideAnotherNeedsTheFileLists();
    void noRootInAnotherSavePathIsNotComparedByFiles();
    void noRootWithEqualContentPathElsewhereShares();
    void magnetWithoutMetadataSharesNothing();
    void filesOverlapComparesAbsolutePaths();
    void findListsSharersOutsideTheSelection();
    void findAsksForMissingFileListsAndSettlesThemWhenGiven();
    void findGroupsSharersInsideTheSelection();
    void findCountsSharedDataOnce();
    void findWithNothingSharedIsEmpty();
};

void TestSharedData::normalizeTrimsAndReadsBackslashes()
{
    QCOMPARE(SharedData::normalize(QStringLiteral("/media/music/")),
             QStringLiteral("/media/music"));
    QCOMPARE(SharedData::normalize(QStringLiteral("C:\\Downloads\\Album\\")),
             QStringLiteral("C:/Downloads/Album"));
    QCOMPARE(SharedData::normalize(QStringLiteral("/")), QStringLiteral("/"));
}

void TestSharedData::isUnderRespectsComponentBoundaries()
{
    QVERIFY(
        SharedData::isUnder(QStringLiteral("/media/music/Album"), QStringLiteral("/media/music")));
    QVERIFY(SharedData::isUnder(QStringLiteral("/media/music"), QStringLiteral("/media/music")));
    QVERIFY(
        !SharedData::isUnder(QStringLiteral("/media/musicals"), QStringLiteral("/media/music")));
    QVERIFY(SharedData::isUnder(QStringLiteral("/anything"), QStringLiteral("/")));
}

void TestSharedData::sameFolderShares()
{
    // The cross-seed case: tracker B's torrent saved where tracker A's files are.
    const auto a = row("a", "/media/music", "/media/music/Field Recordings Vol. 3 [FLAC]");
    const auto b = row("b", "/media/music/", "/media/music/Field Recordings Vol. 3 [FLAC]/");
    QCOMPARE(SharedData::relate(a, b), Relation::Same);
    QCOMPARE(SharedData::relate(b, a), Relation::Same);
}

void TestSharedData::sameSingleFileShares()
{
    const auto a = row("a", "/iso", "/iso/debian-13.1.0-amd64-DVD-1.iso");
    const auto b = row("b", "/iso", "/iso/debian-13.1.0-amd64-DVD-1.iso");
    QCOMPARE(SharedData::relate(a, b), Relation::Same);
}

void TestSharedData::oneInsideTheOtherShares()
{
    // A season pack and one episode added on its own, into the pack's folder.
    const auto pack = row("pack", "/tv", "/tv/Show S01");
    const auto episode = row("ep", "/tv/Show S01", "/tv/Show S01/Show S01E03.mkv");
    QCOMPARE(SharedData::relate(pack, episode), Relation::Nested);
    QCOMPARE(SharedData::relate(episode, pack), Relation::Nested);
}

void TestSharedData::siblingNamesThatOnlyStartAlikeDoNotShare()
{
    const auto a = row("a", "/media", "/media/Album");
    const auto b = row("b", "/media", "/media/Album (Deluxe)");
    QCOMPARE(SharedData::relate(a, b), Relation::None);
}

void TestSharedData::comparisonIsCaseSensitive()
{
    const auto a = row("a", "/media", "/media/Album");
    const auto b = row("b", "/media", "/media/album");
    QCOMPARE(SharedData::relate(a, b), Relation::None);
}

void TestSharedData::differentPathsNeverShare()
{
    // A hardlinked copy (qui's or cross-seed's link mode) lives at another path.
    const auto a = row("a", "/media/music", "/media/music/Album");
    const auto b = row("b", "/links/tracker-b", "/links/tracker-b/Album");
    QCOMPARE(SharedData::relate(a, b), Relation::None);
}

void TestSharedData::noRootBesideAnotherNeedsTheFileLists()
{
    // "No subfolder": the content path is the save path, so by path it holds the whole folder.
    const auto noRoot = row("n", "/downloads", "/downloads");
    QVERIFY(SharedData::hasNoRoot(noRoot));
    const auto neighbour = row("x", "/downloads", "/downloads/Movie (2026)");
    QCOMPARE(SharedData::relate(noRoot, neighbour), Relation::NeedsFiles);
    QCOMPARE(SharedData::relate(neighbour, noRoot), Relation::NeedsFiles);
    const auto otherNoRoot = row("m", "/downloads/", "/downloads/");
    QCOMPARE(SharedData::relate(noRoot, otherNoRoot), Relation::NeedsFiles);
}

void TestSharedData::noRootInAnotherSavePathIsNotComparedByFiles()
{
    const auto noRoot = row("n", "/downloads", "/downloads");
    const auto deeper = row("x", "/downloads/sub", "/downloads/sub/Movie (2026)");
    QCOMPARE(SharedData::relate(noRoot, deeper), Relation::None);
}

void TestSharedData::noRootWithEqualContentPathElsewhereShares()
{
    // Saved with "No subfolder" straight into another torrent's root folder.
    const auto noRoot = row("n", "/media/Album", "/media/Album");
    const auto rooted = row("r", "/media", "/media/Album");
    QCOMPARE(SharedData::relate(noRoot, rooted), Relation::Same);
}

void TestSharedData::magnetWithoutMetadataSharesNothing()
{
    const auto magnet = row("m", "/downloads", "");
    const auto other = row("o", "/downloads", "/downloads/Thing");
    QCOMPARE(SharedData::relate(magnet, other), Relation::None);
}

void TestSharedData::filesOverlapComparesAbsolutePaths()
{
    const auto noRoot = row("n", "/downloads", "/downloads");
    const auto rooted = row("r", "/downloads", "/downloads/Album");
    QVERIFY(SharedData::filesOverlap(noRoot,
                                     list({"Album/01.flac", "Album/02.flac"}),
                                     rooted,
                                     list({"Album/02.flac", "Album/03.flac"})));
    QVERIFY(
        !SharedData::filesOverlap(noRoot, list({"readme.txt"}), rooted, list({"Album/01.flac"})));
}

void TestSharedData::findListsSharersOutsideTheSelection()
{
    const QList<SharedDataRow> library = {
        row("a", "/media/music", "/media/music/Album", 103),
        row("b", "/media/music", "/media/music/Album", 103),
        row("c", "/media/films", "/media/films/Film", 700),
        row("d", "/media/music/Album", "/media/music/Album/CD2", 40),
    };
    const SharedData::Result result = SharedData::find(list({"a", "c"}), library);
    QCOMPARE(result.sharing, list({"a"}));
    QCOMPARE(result.outsideOf.value(QStringLiteral("a")), list({"b", "d"}));
    QCOMPARE(result.outside, list({"b", "d"}));
    QVERIFY(result.inside.isEmpty());
    QVERIFY(result.needFiles.isEmpty());
    QCOMPARE(result.diskSize, qint64(803));
    // b and d are a's own data: freeing them frees nothing more.
    QCOMPARE(result.diskSizeWithOutside, qint64(803));
}

void TestSharedData::findAsksForMissingFileListsAndSettlesThemWhenGiven()
{
    const QList<SharedDataRow> library = {
        row("n", "/downloads", "/downloads", 50),
        row("x", "/downloads", "/downloads/Album", 50),
        row("y", "/downloads", "/downloads/Other", 10),
    };
    SharedData::Result result = SharedData::find(list({"n"}), library);
    QVERIFY(result.sharing.isEmpty());
    QCOMPARE(result.needFiles, list({"n", "x", "y"}));

    QHash<QString, QStringList> files;
    files.insert(QStringLiteral("n"), list({"Album/01.flac", "Album/02.flac"}));
    files.insert(QStringLiteral("x"), list({"Album/01.flac", "Album/02.flac"}));
    files.insert(QStringLiteral("y"), list({"Other/notes.txt"}));
    result = SharedData::find(list({"n"}), library, files);
    QVERIFY(result.needFiles.isEmpty());
    QCOMPARE(result.sharing, list({"n"}));
    QCOMPARE(result.outside, list({"x"}));
}

void TestSharedData::findGroupsSharersInsideTheSelection()
{
    const QList<SharedDataRow> library = {
        row("a", "/media/music", "/media/music/Album", 103),
        row("b", "/media/music", "/media/music/Album", 103),
        row("c", "/media/films", "/media/films/Film", 700),
    };
    const SharedData::Result result = SharedData::find(list({"b", "a", "c"}), library);
    QVERIFY(result.sharing.isEmpty());
    QVERIFY(result.outside.isEmpty());
    QCOMPARE(result.inside.size(), 1);
    QCOMPARE(result.inside.constFirst(), list({"b", "a"}));
    QCOMPARE(result.diskSize, qint64(803));
    QCOMPARE(result.insideSize, qint64(103));
}

void TestSharedData::findCountsSharedDataOnce()
{
    // A pack and an episode inside it, both selected: the pack's bytes already hold it.
    const QList<SharedDataRow> library = {
        row("pack", "/tv", "/tv/Show S01", 8000),
        row("ep", "/tv/Show S01", "/tv/Show S01/E03.mkv", 900),
        row("other", "/tv", "/tv/Another", 1000),
    };
    const SharedData::Result result = SharedData::find(list({"pack", "ep"}), library);
    QCOMPARE(result.diskSize, qint64(8000));
    QCOMPARE(result.insideSize, qint64(900));
    QCOMPARE(result.inside.size(), 1);
}

void TestSharedData::findWithNothingSharedIsEmpty()
{
    const QList<SharedDataRow> library = {
        row("a", "/media", "/media/A", 1),
        row("b", "/media", "/media/B", 2),
    };
    const SharedData::Result result = SharedData::find(list({"a", "missing"}), library);
    QVERIFY(result.sharing.isEmpty());
    QVERIFY(result.outside.isEmpty());
    QVERIFY(result.inside.isEmpty());
    QCOMPARE(result.diskSize, qint64(1));
    QCOMPARE(result.diskSizeWithOutside, qint64(1));
}

QTEST_GUILESS_MAIN(TestSharedData)
#include "tst_shareddata.moc"
