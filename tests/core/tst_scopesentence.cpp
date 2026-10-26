// ScopeSentence: the title bar's sentence for each scope (board 46 `titleBar()`, DECISIONS
// C24 H5), a row per sentence, and the facts TorrentFilterModel gathers for it over the rail's
// scope, never the filter's words.

#include "core/LocalDay.hpp"
#include "core/ScopeSentence.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"

#include <QJsonObject>
#include <QTest>

using namespace omaseed::core;

namespace {

constexpr qint64 kKiB = 1024;
constexpr qint64 kMiB = 1024 * 1024;

/// Board 46's library: 2 need you, 7 arriving (the next in 3 min, the last in 19), 2 landed
/// today and both seeding, 101 giving back at 41 KB/s with ratio 1.6, 5 resting.
ScopeFacts boardFacts()
{
    ScopeFacts facts;
    facts.needsYou = 2;
    facts.arriving = 7;
    facts.landedToday = 2;
    facts.landedSharing = 2;
    facts.givingBack = 101;
    facts.resting = 5;
    facts.total = 117;
    facts.nextEta = 3 * 60;
    facts.lastEta = 19 * 60;
    facts.arrivingSpeed = 2090 * kKiB;
    facts.givingSpeed = 41 * kKiB;
    facts.upSpeed = 41 * kKiB;
    facts.givenUp = 16 * kMiB;
    facts.givenDown = 10 * kMiB;
    facts.bytes = 1200 * kKiB;
    return facts;
}

} // namespace

class TestScopeSentence : public QObject
{
    Q_OBJECT

private slots:
    void theLibrarySaysTheQuestions();
    void theLibraryLeavesOutWhatHasNothingToSay();
    void eachQuestionSaysWhatItHolds_data();
    void eachQuestionSaysWhatItHolds();
    void anEmptyQuestionSaysSo();
    void placesSayTheirCountAndKind();
    void aQuestionNarrowingAPlaceComesFirst();
    void aFolderSaysTheScopeLinesFacts();
    void movesSayWhatIsOnShow();
    void theFacesSayWhatTheyShow();
    void aLostServerSaysTheListsTime();
    void groupHeadsSayTheShortLine();
    void noSeedOnlineIsNoTimeToReady();
    void shelvesSayCountAndSize();
    void waitsAreRounded();
    void factsAreGatheredOverTheRailsScopeAlone();
};

void TestScopeSentence::theLibrarySaysTheQuestions()
{
    QCOMPARE(
        ScopeSentence::library(boardFacts()),
        QStringLiteral(
            "2 need you · 7 arriving, the next in 3 min · 2 landed today · giving back 41 KB/s"));
    ScopeFacts one = boardFacts();
    one.needsYou = 1;
    QVERIFY(ScopeSentence::library(one).startsWith(QStringLiteral("1 needs you · ")));
}

void TestScopeSentence::theLibraryLeavesOutWhatHasNothingToSay()
{
    ScopeFacts facts;
    QCOMPARE(ScopeSentence::library(facts), QStringLiteral("no torrents yet"));
    facts.total = 5;
    facts.resting = 5;
    QCOMPARE(ScopeSentence::library(facts), QStringLiteral("5 resting"));
    facts.total = 8;
    facts.givingBack = 3;
    QCOMPARE(ScopeSentence::library(facts), QStringLiteral("3 giving back"));
    facts.arriving = 1;
    facts.total = 9;
    QCOMPARE(ScopeSentence::library(facts), QStringLiteral("1 arriving · 3 giving back"));
}

void TestScopeSentence::eachQuestionSaysWhatItHolds_data()
{
    QTest::addColumn<int>("question");
    QTest::addColumn<QString>("sentence");
    // Board 46's words; "two" is a figure here, as every count in a sentence is.
    QTest::newRow("needs you") << int(TorrentFilterModel::NeedsYou)
                               << QStringLiteral(
                                      "2 torrents wait for a fix · nothing else is wrong");
    QTest::newRow("arriving")
        << int(TorrentFilterModel::Arriving)
        << QStringLiteral(
               "7 torrents · the next ready in 3 min, the last in about 19 min · 2.04 MB/s");
    QTest::newRow("landed today") << int(TorrentFilterModel::LandedToday)
                                  << QStringLiteral("2 since midnight · both seeding");
    QTest::newRow("giving back") << int(TorrentFilterModel::GivingBack)
                                 << QStringLiteral("101 torrents at 41 KB/s · ratio 1.6");
    QTest::newRow("resting") << int(TorrentFilterModel::Resting)
                             << QStringLiteral("5 torrents · not sharing");
    QTest::newRow("the library") << int(TorrentFilterModel::AnyQuestion)
                                 << ScopeSentence::library(boardFacts());
}

void TestScopeSentence::eachQuestionSaysWhatItHolds()
{
    QFETCH(int, question);
    QFETCH(QString, sentence);
    QCOMPARE(ScopeSentence::question(question, boardFacts()), sentence);
}

void TestScopeSentence::anEmptyQuestionSaysSo()
{
    const ScopeFacts none;
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::NeedsYou, none),
             QStringLiteral("nothing needs you"));
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::Arriving, none),
             QStringLiteral("nothing arriving"));
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::LandedToday, none),
             QStringLiteral("nothing landed yet today"));
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::GivingBack, none),
             QStringLiteral("nothing giving back"));
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::Resting, none),
             QStringLiteral("nothing resting"));

    // One of each, and the variants of the counts.
    ScopeFacts facts;
    facts.needsYou = 1;
    facts.arriving = 1;
    facts.nextEta = 40;
    facts.lastEta = 40;
    facts.landedToday = 1;
    facts.givingBack = 1;
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::NeedsYou, facts),
             QStringLiteral("1 torrent waits for a fix · nothing else is wrong"));
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::Arriving, facts),
             QStringLiteral("1 torrent · ready in 40 s"));
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::LandedToday, facts),
             QStringLiteral("1 since midnight · stopped"));
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::GivingBack, facts),
             QStringLiteral("1 torrent · nobody asking now"));
    facts.landedToday = 3;
    facts.landedSharing = 3;
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::LandedToday, facts),
             QStringLiteral("3 since midnight · all seeding"));
    facts.landedSharing = 2;
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::LandedToday, facts),
             QStringLiteral("3 since midnight · 2 seeding"));
}

void TestScopeSentence::placesSayTheirCountAndKind()
{
    ScopeFacts games;
    games.total = 49;
    games.bytes = 1238 * kKiB; // "1.21 MB"
    QCOMPARE(ScopeSentence::category(games, QStringLiteral("/home/sbx/library/games")),
             QStringLiteral("49 torrents · 1.21 MB · saves to /home/sbx/library/games"));
    QCOMPARE(ScopeSentence::category(games, QString()), QStringLiteral("49 torrents · 1.21 MB"));
    ScopeFacts sizeless;
    sizeless.total = 3;
    QCOMPARE(ScopeSentence::category(sizeless, QString()), QStringLiteral("3 torrents"));
    // #167 assumed 9.
    ScopeFacts one;
    one.total = 1;
    QCOMPARE(ScopeSentence::tag(one), QStringLiteral("1 torrent · tag"));
    QCOMPARE(ScopeSentence::tracker(games), QStringLiteral("49 torrents · tracker"));
}

void TestScopeSentence::aQuestionNarrowingAPlaceComesFirst()
{
    QCOMPARE(
        ScopeSentence::narrowed(TorrentFilterModel::Arriving, QStringLiteral("3 torrents · tag")),
        QStringLiteral("arriving · 3 torrents · tag"));
    QCOMPARE(ScopeSentence::narrowed(TorrentFilterModel::AnyQuestion, QStringLiteral("x")),
             QStringLiteral("x"));
    QCOMPARE(ScopeSentence::questionWord(TorrentFilterModel::LandedToday),
             QStringLiteral("landed today"));
}

void TestScopeSentence::aFolderSaysTheScopeLinesFacts()
{
    // Board 46's folder picked.
    QCOMPARE(
        ScopeSentence::folder(49, 1384 * kKiB, 1, 2, QStringLiteral("/home/sbx/library/games")),
        QStringLiteral("49 torrents · 1.35 MB on disk · 2 loose files · /home/sbx/library/games"));
    QCOMPARE(ScopeSentence::folder(1, 0, 3, 1, QStringLiteral("/srv/a")),
             QStringLiteral("1 torrent · 0 on disk · 3 save paths · 1 loose file · /srv/a"));
    QCOMPARE(ScopeSentence::folderMoving(QStringLiteral("/tmp/fast/games"), 17, 49),
             QStringLiteral("moving to /tmp/fast/games · 17 of 49 landed"));
}

void TestScopeSentence::movesSayWhatIsOnShow()
{
    QCOMPARE(ScopeSentence::moveJob(49, 3), QStringLiteral("49 torrents · 3 with qBittorrent"));
    QCOMPARE(ScopeSentence::moveJob(1, 0), QStringLiteral("1 torrent"));
    QCOMPARE(ScopeSentence::moveSheet(3), QStringLiteral("3 torrents the move sheet listed"));
    QCOMPARE(ScopeSentence::moveSheet(1), QStringLiteral("1 torrent the move sheet listed"));
}

void TestScopeSentence::theFacesSayWhatTheyShow()
{
    QCOMPARE(ScopeSentence::seed(QStringLiteral("41 KB/s"),
                                 QStringLiteral("18.4 GB today"),
                                 QStringLiteral("every torrent")),
             QStringLiteral("giving back 41 KB/s · 18.4 GB today · every torrent"));
    QCOMPARE(ScopeSentence::seed(QString(), QString(), QStringLiteral("the selection")),
             QStringLiteral("the selection"));
    QCOMPARE(ScopeSentence::log(QStringLiteral("public"), 412, QStringLiteral("09:14")),
             QStringLiteral("qBittorrent's log on public · 412 lines since 09:14"));
    QCOMPARE(ScopeSentence::log(QStringLiteral("public"), 1, QString()),
             QStringLiteral("qBittorrent's log on public · 1 line"));
    QCOMPARE(ScopeSentence::log(QString(), 0, QString()), QStringLiteral("qBittorrent's log"));
}

void TestScopeSentence::aLostServerSaysTheListsTime()
{
    // Board 46: "the list is from 11:52 · public isn't answering".
    QCOMPARE(ScopeSentence::lost(QStringLiteral("11:52"), QStringLiteral("public isn't answering")),
             QStringLiteral("the list is from 11:52 · public isn't answering"));
    QCOMPARE(ScopeSentence::lost(QString(), QStringLiteral("public refused the sign-in")),
             QStringLiteral("public refused the sign-in"));
}

void TestScopeSentence::waitsAreRounded()
{
    QCOMPARE(ScopeSentence::soon(0), QStringLiteral("0 s"));
    QCOMPARE(ScopeSentence::soon(59), QStringLiteral("59 s"));
    QCOMPARE(ScopeSentence::soon(60), QStringLiteral("1 min"));
    QCOMPARE(ScopeSentence::soon(150), QStringLiteral("3 min"));
    QCOMPARE(ScopeSentence::soon(2 * 3600 + 41 * 60), QStringLiteral("2h 41m"));
}

void TestScopeSentence::factsAreGatheredOverTheRailsScopeAlone()
{
    QDateTime now(QDate(2026, 10, 23), QTime(18, 0));
    LocalDay day;
    day.setClock([&now]() { return now; });
    TorrentModel model;
    TorrentFilterModel filtered;
    filtered.setDay(&day);
    filtered.setSourceModel(&model);
    const qint64 thisMorning = QDateTime(QDate(2026, 10, 23), QTime(7, 0)).toSecsSinceEpoch();
    model.applyFullUpdate(QJsonObject{
        {QStringLiteral("a"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("alpha")},
                     {QStringLiteral("state"), QStringLiteral("downloading")},
                     {QStringLiteral("progress"), 0.5},
                     {QStringLiteral("dlspeed"), 2048},
                     {QStringLiteral("eta"), 300},
                     {QStringLiteral("size"), 100},
                     {QStringLiteral("category"), QStringLiteral("games")}}},
        {QStringLiteral("b"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("beta")},
                     {QStringLiteral("state"), QStringLiteral("downloading")},
                     {QStringLiteral("progress"), 0.1},
                     {QStringLiteral("eta"), 900},
                     {QStringLiteral("size"), 200},
                     {QStringLiteral("category"), QStringLiteral("games")}}},
        {QStringLiteral("c"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("gamma")},
                     {QStringLiteral("state"), QStringLiteral("uploading")},
                     {QStringLiteral("progress"), 1.0},
                     {QStringLiteral("completion_on"), thisMorning},
                     {QStringLiteral("upspeed"), 1024},
                     {QStringLiteral("size"), 400}}},
        {QStringLiteral("d"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("delta")},
                     {QStringLiteral("state"), QStringLiteral("stalledUP")},
                     {QStringLiteral("progress"), 1.0},
                     {QStringLiteral("uploaded"), 30},
                     {QStringLiteral("downloaded"), 20},
                     {QStringLiteral("size"), 800}}},
        {QStringLiteral("e"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("epsilon")},
                     {QStringLiteral("state"), QStringLiteral("stoppedDL")},
                     {QStringLiteral("progress"), 0.2}}},
    });

    ScopeFacts facts = filtered.scopeFacts();
    QCOMPARE(facts.total, 5);
    QCOMPARE(facts.arriving, 2);
    QCOMPARE(facts.landedToday, 1);
    QCOMPARE(facts.landedSharing, 1);
    QCOMPARE(facts.givingBack, 1);
    QCOMPARE(facts.resting, 1);
    QCOMPARE(facts.nextEta, 300);
    QCOMPARE(facts.lastEta, 900);
    QCOMPARE(facts.arrivingSpeed, 2048);
    QCOMPARE(facts.upSpeed, 1024);
    QCOMPARE(facts.givenUp, 30);
    QCOMPARE(facts.givenDown, 20);
    QCOMPARE(facts.bytes, 1500);
    QCOMPARE(ScopeSentence::library(facts),
             QStringLiteral("2 arriving, the next in 5 min · 1 landed today · giving back 1 KB/s"));

    // The words narrow the rows, not the scope the sentence names.
    filtered.setText(QStringLiteral("alpha"));
    QCOMPARE(filtered.count(), 1);
    QCOMPARE(filtered.scopeFacts().total, 5);

    // A category, and a question in it.
    filtered.setText(QString());
    filtered.setCategory(QStringLiteral("games"));
    facts = filtered.scopeFacts();
    QCOMPARE(facts.total, 2);
    QCOMPARE(facts.bytes, 300);
    filtered.setQuestion(TorrentFilterModel::Arriving);
    QCOMPARE(filtered.scopeFacts().arriving, 2);
    filtered.setQuestion(TorrentFilterModel::Resting);
    QCOMPARE(filtered.scopeFacts().total, 0);
}

// DECISIONS C24 H3: the rows display's group heads, board 46 `rows()`.
void TestScopeSentence::groupHeadsSayTheShortLine()
{
    const ScopeFacts facts = boardFacts();
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::NeedsYou, facts),
             QStringLiteral("2 fixes wait"));
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::Arriving, facts),
             QStringLiteral("next ready in 3 min"));
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::LandedToday, facts),
             QStringLiteral("since midnight"));
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::GivingBack, facts),
             QStringLiteral("41 KB/s · ratio 1.6"));
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::Resting, facts),
             QStringLiteral("not sharing"));

    ScopeFacts one;
    one.needsYou = 1;
    one.arriving = 1;
    one.nextEta = 40;
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::NeedsYou, one),
             QStringLiteral("1 fix waits"));
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::Arriving, one),
             QStringLiteral("ready in 40 s"));
    // Nothing known yet: no line, never a zero.
    ScopeFacts quiet;
    quiet.arriving = 2;
    quiet.givingBack = 3;
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::Arriving, quiet), QString());
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::GivingBack, quiet),
             QStringLiteral("nobody asking now"));
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::AnyQuestion, quiet), QString());
}

// #201 F14: a stalled torrent (no seed online) keeps qBittorrent's estimate from its old
// speed, but nothing can finish it: the head says it waits, and with others the next ready
// comes from those that can finish.
void TestScopeSentence::noSeedOnlineIsNoTimeToReady()
{
    TorrentModel model;
    TorrentFilterModel filtered;
    filtered.setSourceModel(&model);
    model.applyFullUpdate(QJsonObject{
        {QStringLiteral("a"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("alpha")},
                     {QStringLiteral("state"), QStringLiteral("stalledDL")},
                     {QStringLiteral("progress"), 0.4},
                     {QStringLiteral("eta"), 97200}}},
    });
    ScopeFacts facts = filtered.scopeFacts();
    QCOMPARE(facts.arriving, 1);
    QCOMPARE(facts.waitingSeed, 1);
    QCOMPARE(facts.nextEta, -1);
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::Arriving, facts),
             QStringLiteral("waiting for a seed"));
    QCOMPARE(ScopeSentence::question(TorrentFilterModel::Arriving, facts),
             QStringLiteral("1 torrent · waiting for a seed"));
    QCOMPARE(ScopeSentence::library(facts), QStringLiteral("1 arriving"));

    model.applyFullUpdate(QJsonObject{
        {QStringLiteral("a"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("alpha")},
                     {QStringLiteral("state"), QStringLiteral("stalledDL")},
                     {QStringLiteral("progress"), 0.4},
                     {QStringLiteral("eta"), 60}}},
        {QStringLiteral("b"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("beta")},
                     {QStringLiteral("state"), QStringLiteral("downloading")},
                     {QStringLiteral("progress"), 0.1},
                     {QStringLiteral("eta"), 900}}},
    });
    facts = filtered.scopeFacts();
    QCOMPARE(facts.waitingSeed, 1);
    QCOMPARE(facts.nextEta, 900);
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::Arriving, facts),
             QStringLiteral("next ready in 15 min"));

    ScopeFacts two;
    two.arriving = 2;
    two.waitingSeed = 2;
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::Arriving, two),
             QStringLiteral("all waiting for a seed"));
    two.arriving = 3;
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::Arriving, two),
             QStringLiteral("2 waiting for a seed"));
    two.arrivingSpeed = 2048;
    QCOMPARE(ScopeSentence::groupLine(TorrentFilterModel::Arriving, two), QStringLiteral("2 KB/s"));
}

void TestScopeSentence::shelvesSayCountAndSize()
{
    ScopeFacts facts;
    facts.total = 49;
    facts.bytes = 1384120;
    QCOMPARE(ScopeSentence::shelfLine(facts), QStringLiteral("49 · 1.32 MB"));
    facts.bytes = 0;
    QCOMPARE(ScopeSentence::shelfLine(facts), QStringLiteral("49"));
}

QTEST_GUILESS_MAIN(TestScopeSentence)
#include "tst_scopesentence.moc"
