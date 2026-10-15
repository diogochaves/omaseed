// RenamePlan (issue #11, docs/18): find and replace over file names, the counter, the parts
// of a name it searches, invalid names, collisions and the order the renames run in.

#include "core/RenamePlan.hpp"

#include <QTest>

using omaseed::core::RenameOptions;
using omaseed::core::RenamePlan;
using omaseed::core::RenameRow;

namespace {

const QString album = QStringLiteral("Field Recordings Vol. 3 [FLAC]/");

QStringList tracks()
{
    return {album + QStringLiteral("01 - Harbour at Dawn.flac"),
            album + QStringLiteral("02 - Rain on Tin.flac"),
            album + QStringLiteral("03 - Market Voices.flac")};
}

RenameOptions regex(const QString &find, const QString &replace)
{
    RenameOptions options;
    options.find = find;
    options.replace = replace;
    options.regex = true;
    return options;
}

RenameOptions text(const QString &find, const QString &replace)
{
    RenameOptions options;
    options.find = find;
    options.replace = replace;
    return options;
}

QStringList newPaths(const RenamePlan &plan)
{
    QStringList out;
    for (const RenameRow &row : plan.rows())
        out.append(row.newPath);
    return out;
}

} // namespace

class TestRenamePlan : public QObject
{
    Q_OBJECT

private slots:
    void regexGroupsRewriteTheName();
    void plainTextIsLiteral();
    void onlyTheFirstMatchUnlessEveryMatch();
    void caseIsIgnoredUnlessMatchCase();
    void theExtensionIsLeftAloneUnlessIncluded();
    void foldersAreNeverSearched();
    void theCounterPadsAndCountsMatchingFiles();
    void marksSayWhereTheReplacementsLanded();
    void anEmptyFindChangesNothing();
    void aBadRegexSaysWhy();
    void invalidNamesBlockTheRun();
    void twoFilesOnOnePathCollide();
    void landingOnAFileThatStaysCollides();
    void chasingRenamesAreOrdered();
    void aSwapIsACycleAndCollides();
    void aTargetTakenOnDiskCollides();
};

void TestRenamePlan::regexGroupsRewriteTheName()
{
    const auto plan =
        RenamePlan::make(tracks(), {}, regex(QStringLiteral("(\\d+) - "), QStringLiteral("$1. ")));
    QVERIFY(plan.error().isEmpty());
    QCOMPARE(newPaths(plan),
             QStringList({album + QStringLiteral("01. Harbour at Dawn.flac"),
                          album + QStringLiteral("02. Rain on Tin.flac"),
                          album + QStringLiteral("03. Market Voices.flac")}));
    QCOMPARE(plan.changed(), 3);
    QVERIFY(plan.canRun());
    QCOMPARE(plan.rows().first().newName, QStringLiteral("01. Harbour at Dawn.flac"));
}

void TestRenamePlan::plainTextIsLiteral()
{
    // In plain mode `.` and `$1` are just characters.
    const QStringList files = {QStringLiteral("a.b.c.txt"), QStringLiteral("abc.txt")};
    const auto plan = RenamePlan::make(files, {}, text(QStringLiteral("."), QStringLiteral("$1")));
    QCOMPARE(newPaths(plan),
             QStringList({QStringLiteral("a$1b.c.txt"), QStringLiteral("abc.txt")}));
    QCOMPARE(plan.rows().at(1).status, RenameRow::Status::Unchanged);
}

void TestRenamePlan::onlyTheFirstMatchUnlessEveryMatch()
{
    const QStringList files = {QStringLiteral("a_b_c.mkv")};
    auto options = text(QStringLiteral("_"), QStringLiteral(" "));
    QCOMPARE(newPaths(RenamePlan::make(files, {}, options)),
             QStringList({QStringLiteral("a b_c.mkv")}));
    options.everyMatch = true;
    QCOMPARE(newPaths(RenamePlan::make(files, {}, options)),
             QStringList({QStringLiteral("a b c.mkv")}));
}

void TestRenamePlan::caseIsIgnoredUnlessMatchCase()
{
    const QStringList files = {QStringLiteral("Track ONE.flac")};
    auto options = text(QStringLiteral("one"), QStringLiteral("1"));
    QCOMPARE(newPaths(RenamePlan::make(files, {}, options)),
             QStringList({QStringLiteral("Track 1.flac")}));
    options.matchCase = true;
    QCOMPARE(RenamePlan::make(files, {}, options).changed(), 0);
}

void TestRenamePlan::theExtensionIsLeftAloneUnlessIncluded()
{
    const QStringList files = {QStringLiteral("flac notes.flac"), QStringLiteral("README")};
    auto options = text(QStringLiteral("flac"), QStringLiteral("FLAC"));
    options.everyMatch = true;
    QCOMPARE(newPaths(RenamePlan::make(files, {}, options)),
             QStringList({QStringLiteral("FLAC notes.flac"), QStringLiteral("README")}));
    options.includeExtension = true;
    QCOMPARE(newPaths(RenamePlan::make(files, {}, options)),
             QStringList({QStringLiteral("FLAC notes.FLAC"), QStringLiteral("README")}));
    // A dotfile's leading dot is its name, not an extension.
    options.includeExtension = false;
    const auto dot = RenamePlan::make({QStringLiteral(".flac")}, {}, options);
    QCOMPARE(dot.rows().first().newName, QStringLiteral(".FLAC"));
}

void TestRenamePlan::foldersAreNeverSearched()
{
    const auto plan = RenamePlan::make({QStringLiteral("Disc 1/Disc 1 - intro.flac")},
                                       {},
                                       text(QStringLiteral("Disc 1"), QStringLiteral("CD1")));
    QCOMPARE(plan.rows().first().newPath, QStringLiteral("Disc 1/CD1 - intro.flac"));
}

void TestRenamePlan::theCounterPadsAndCountsMatchingFiles()
{
    const QStringList files = {
        QStringLiteral("IMG_a.jpg"), QStringLiteral("cover.jpg"), QStringLiteral("IMG_b.jpg")};
    const auto plan =
        RenamePlan::make(files, {}, regex(QStringLiteral("^IMG_.*"), QStringLiteral("Trip {nn}")));
    // cover.jpg does not match, so it takes no number.
    QCOMPARE(newPaths(plan),
             QStringList({QStringLiteral("Trip 01.jpg"),
                          QStringLiteral("cover.jpg"),
                          QStringLiteral("Trip 02.jpg")}));
    const auto wide = RenamePlan::make(
        {QStringLiteral("x.txt")}, {}, text(QStringLiteral("x"), QStringLiteral("{n}-{nnn}-{x}")));
    QCOMPARE(wide.rows().first().newName, QStringLiteral("1-001-{x}.txt"));
}

void TestRenamePlan::marksSayWhereTheReplacementsLanded()
{
    auto options = text(QStringLiteral(" "), QStringLiteral("_"));
    options.everyMatch = true;
    const auto plan = RenamePlan::make({QStringLiteral("a b c.txt")}, {}, options);
    const auto marks = plan.rows().first().marks;
    QCOMPARE(marks.size(), 2);
    using Mark = std::pair<qsizetype, qsizetype>;
    QCOMPARE(marks.at(0), Mark(1, 1));
    QCOMPARE(marks.at(1), Mark(3, 1));
}

void TestRenamePlan::anEmptyFindChangesNothing()
{
    const auto plan = RenamePlan::make(tracks(), {}, text(QString(), QStringLiteral("x")));
    QCOMPARE(newPaths(plan), tracks());
    QCOMPARE(plan.changed(), 0);
    QVERIFY(!plan.canRun());
}

void TestRenamePlan::aBadRegexSaysWhy()
{
    const auto plan = RenamePlan::make(tracks(), {}, regex(QStringLiteral("(\\d+"), QString()));
    QVERIFY(!plan.error().isEmpty());
    QCOMPARE(plan.changed(), 0);
    QVERIFY(!plan.canRun());
    // The same text is fine as plain text.
    RenameOptions plain = text(QStringLiteral("(\\d+"), QStringLiteral("x"));
    QVERIFY(RenamePlan::make(tracks(), {}, plain).error().isEmpty());
}

void TestRenamePlan::invalidNamesBlockTheRun()
{
    const QStringList files = {
        QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")};
    // Emptied stem: ".txt" is no name for a.txt.
    auto plan = RenamePlan::make({files.at(0)}, {}, regex(QStringLiteral(".+"), QString()));
    QCOMPARE(plan.rows().first().status, RenameRow::Status::Invalid);
    QCOMPARE(plan.invalid(), 1);
    QVERIFY(!plan.canRun());
    // A slash would move the file into a folder.
    plan = RenamePlan::make({files.at(1)}, {}, text(QStringLiteral("b"), QStringLiteral("x/b")));
    QCOMPARE(plan.rows().first().status, RenameRow::Status::Invalid);
    // A whole name of ".." with the extension searched too.
    auto options = regex(QStringLiteral(".+"), QStringLiteral(".."));
    options.includeExtension = true;
    plan = RenamePlan::make({files.at(2)}, {}, options);
    QCOMPARE(plan.rows().first().status, RenameRow::Status::Invalid);
}

void TestRenamePlan::twoFilesOnOnePathCollide()
{
    const auto plan =
        RenamePlan::make(tracks(), {}, regex(QStringLiteral(".+"), QStringLiteral("track")));
    QCOMPARE(plan.collisions(), 3);
    QCOMPARE(plan.changed(), 0);
    QVERIFY(!plan.canRun());
    QVERIFY(plan.executionOrder().isEmpty());
}

void TestRenamePlan::landingOnAFileThatStaysCollides()
{
    // cover.jpg is not renamed, so nothing may become cover.jpg; and a selected file that
    // does not change stays where it is too.
    const QStringList files = {QStringLiteral("a.jpg"), QStringLiteral("b.jpg")};
    auto plan = RenamePlan::make(files,
                                 {QStringLiteral("cover.jpg")},
                                 regex(QStringLiteral("^a$"), QStringLiteral("cover")));
    QCOMPARE(plan.rows().at(0).status, RenameRow::Status::Collides);
    plan = RenamePlan::make(files, {}, regex(QStringLiteral("^a$"), QStringLiteral("b")));
    QCOMPARE(plan.rows().at(0).status, RenameRow::Status::Collides);
    QCOMPARE(plan.rows().at(1).status, RenameRow::Status::Unchanged);
}

void TestRenamePlan::chasingRenamesAreOrdered()
{
    // Listed as 10, 1 and numbered by the counter: 10→1 while 1→2. 1.txt has to move out of
    // the way first, so it runs first although it is listed second.
    const QStringList files = {QStringLiteral("10.txt"), QStringLiteral("1.txt")};
    const auto plan =
        RenamePlan::make(files, {}, regex(QStringLiteral("^\\d+$"), QStringLiteral("{n}")));
    QCOMPARE(newPaths(plan), QStringList({QStringLiteral("1.txt"), QStringLiteral("2.txt")}));
    QCOMPARE(plan.collisions(), 0);
    QVERIFY(plan.canRun());
    QCOMPARE(plan.executionOrder(), QList<int>({1, 0}));
}

void TestRenamePlan::aSwapIsACycleAndCollides()
{
    // Listed as 2, 1 and numbered: 2→1 and 1→2. Neither target is ever free.
    const QStringList files = {QStringLiteral("2.txt"), QStringLiteral("1.txt")};
    const auto plan =
        RenamePlan::make(files, {}, regex(QStringLiteral("^\\d+$"), QStringLiteral("{n}")));
    QCOMPARE(plan.collisions(), 2);
    QVERIFY(!plan.canRun());
    QVERIFY(plan.executionOrder().isEmpty());
}

void TestRenamePlan::aTargetTakenOnDiskCollides()
{
    // #64: the caller says which new paths something else holds on disk; it is asked only
    // about changed files, and those rows collide as a clash with the torrent's own would.
    QStringList asked;
    const auto taken = [&asked](const QString &path) {
        asked.append(path);
        return path.endsWith(QStringLiteral("02. Rain on Tin.flac"));
    };
    const auto plan = RenamePlan::make(
        tracks(), {}, regex(QStringLiteral("^(0[12]) - "), QStringLiteral("$1. ")), taken);
    QCOMPARE(asked,
             QStringList({album + QStringLiteral("01. Harbour at Dawn.flac"),
                          album + QStringLiteral("02. Rain on Tin.flac")}));
    QCOMPARE(plan.rows().at(0).status, RenameRow::Status::Changed);
    QCOMPARE(plan.rows().at(1).status, RenameRow::Status::Collides);
    QCOMPARE(plan.rows().at(2).status, RenameRow::Status::Unchanged);
    QCOMPARE(plan.collisions(), 1);
    QVERIFY(!plan.canRun());
}

QTEST_GUILESS_MAIN(TestRenamePlan)
#include "tst_renameplan.moc"
