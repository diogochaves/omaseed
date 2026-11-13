// TorrentStory: the story tab's "what happened" (board 46 H6, DECISIONS C24): what goes in,
// in what order, and how its times read.

#include "core/TorrentStory.hpp"

#include <QRegularExpression>
#include <QTest>
#include <QTimeZone>

using namespace omaseed::core;

namespace {

constexpr qint64 kMiB = 1024 * 1024;

QDateTime at(int day, int hour, int minute, int year = 2026)
{
    return QDateTime(QDate(year, 10, day), QTime(hour, minute), QTimeZone::LocalTime);
}

Torrent added(const QDateTime &when, const QString &category = {})
{
    Torrent t;
    t.hash = QStringLiteral("aa");
    t.name = QStringLiteral("Open Movie Night");
    t.addedOn = when;
    t.category = category;
    return t;
}

StoryEntry renamed(const QDateTime &when,
                   StoryEntry::What what,
                   const QString &was,
                   const QString &now,
                   const QString &in = {})
{
    StoryEntry r;
    r.at = when.toUTC();
    r.hash = QStringLiteral("aa");
    r.what = what;
    r.was = was;
    r.now = now;
    r.in = in;
    return r;
}

// The one line a single rename makes, beside the add.
StoryEvent renameLine(const StoryEntry &rename)
{
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(10, 9, 0));
    input.entries = {rename};
    const QList<StoryEvent> events = TorrentStory::events(input);
    return events.size() == 2 ? events.at(0) : StoryEvent{};
}

// A filing or place entry (C43), by omaseed unless `by` says otherwise.
StoryEntry edited(const QDateTime &when,
                  StoryEntry::What what,
                  const QString &was,
                  const QString &now,
                  StoryEntry::By by = StoryEntry::By::Omaseed)
{
    StoryEntry e = renamed(when, what, was, now);
    e.by = by;
    return e;
}

StoryEntry tags(const QDateTime &when,
                const QStringList &tagged,
                const QStringList &untagged,
                StoryEntry::By by = StoryEntry::By::Omaseed)
{
    StoryEntry e = renamed(when, StoryEntry::What::Tags, {}, {});
    e.tagged = tagged;
    e.untagged = untagged;
    e.by = by;
    return e;
}

// The one line an entry makes, beside the add, in the words of the engine the flags name.
StoryEvent
line(const StoryEntry &entry, bool categoriesAreLabels = false, bool tagsAreLabels = false)
{
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(10, 9, 0));
    input.categoriesAreLabels = categoriesAreLabels;
    input.tagsAreLabels = tagsAreLabels;
    input.entries = {entry};
    const QList<StoryEvent> events = TorrentStory::events(input);
    return events.size() == 2 ? events.at(0) : StoryEvent{};
}

QStringList titles(const QList<StoryEvent> &events)
{
    QStringList out;
    for (const StoryEvent &event : events)
        out << event.title;
    return out;
}

} // namespace

class TestTorrentStory : public QObject
{
    Q_OBJECT

private slots:
    void timesReadFromNow();
    void addedSaysWhere();
    void finishedSaysHowLong();
    void aTorrentAddedCompleteNeverFinishes();
    void movesSayWithWhat();
    void theErrorAndTheFailureComeAtTheirLoggedTimes();
    void everythingIsNewestFirst();
    void twoAtTheSameSecondReadTheLaterStepFirst();
    void anEventWithNoTimeIsLeftOut();
    void aTorrentRenameSaysTheOldName();
    void aRenameOutsideSaysSo();
    void transmissionSaysItsFolderMovedToo();
    void aFileOrAFolderSaysOldToNew();
    void aRunOfFilesIsOneLine();
    void aRenameFromAnEarlierDay();
    void aRenameAtTheSameSecondReadsAbove();
    void renamesTakeTheirPlaceInTheStory();
    // DECISIONS C43, board 58 §3 and §5.
    void aCategorySaysWhatItWasIn();
    void noCategoryBefore();
    void takenOutOfItsCategory();
    void delugeLabelWords();
    void aCategoryChangedOutside();
    void theCategoryWasRemoved();
    void theLabelWasRemoved();
    void taggedUntaggedAndBoth();
    void transmissionLabelWords();
    void threeNamesThenAndNMore();
    void theTagWasRemoved();
    void movedOutsideOmaseed();
    void willLandIn();
    void byItsCategoryAutomaticManagement();
    void automaticManagementWithNoCategory();
    void byItsLabel();
    void addedSaysTheCategoryItCameWith();
    void addedWithNoCategoryLineSaysTheCategoryNow();
    void aPlaceReadsAboveTheFilingOfItsPoll();
    void aRenameStillReadsAboveAll();
    void kindNameEdit();
};

void TestTorrentStory::timesReadFromNow()
{
    const QDateTime now = at(24, 16, 30);
    QCOMPARE(TorrentStory::timeWords(at(24, 9, 15), now), QStringLiteral("09:15"));
    QCOMPARE(TorrentStory::timeWords(at(23, 22, 5), now), QStringLiteral("yesterday 22:05"));
    QCOMPARE(TorrentStory::timeWords(at(12, 9, 41), now), QStringLiteral("12 Oct 09:41"));
    QCOMPARE(TorrentStory::timeWords(at(12, 9, 41, 2025), now), QStringLiteral("12 Oct 2025"));
}

void TestTorrentStory::addedSaysWhere()
{
    StoryInput input;
    input.now = at(24, 16, 0);
    input.torrent = added(at(24, 9, 15), QStringLiteral("films"));
    QList<StoryEvent> events = TorrentStory::events(input);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).title, QStringLiteral("Added"));
    QCOMPARE(events.at(0).detail, QStringLiteral("into films"));
    QCOMPARE(events.at(0).time, QStringLiteral("09:15"));
    QCOMPARE(events.at(0).kind, StoryEvent::Kind::Plain);

    input.torrent.category.clear();
    events = TorrentStory::events(input);
    QCOMPARE(events.at(0).detail, QStringLiteral("with no category"));
    // Deluge's labels (C25 K13).
    input.categoriesAreLabels = true;
    QCOMPARE(TorrentStory::events(input).at(0).detail, QStringLiteral("with no label"));
    input.categoriesAreLabels = false;

    // An engine with no categories (Transmission, C14 E4) says nothing about one.
    input.categories = false;
    events = TorrentStory::events(input);
    QCOMPARE(events.at(0).title, QStringLiteral("Added"));
    QCOMPARE(events.at(0).detail, QString());
}

void TestTorrentStory::finishedSaysHowLong()
{
    StoryInput input;
    input.now = at(24, 16, 0);
    input.torrent = added(at(24, 9, 15));
    input.torrent.completionOn = at(24, 10, 42);
    input.torrent.totalSize = 716 * kMiB;
    const QList<StoryEvent> events = TorrentStory::events(input);
    QCOMPARE(titles(events), QStringList({"Finished downloading", "Added"}));
    QCOMPARE(events.at(0).detail, QStringLiteral("716 MB in 1h 27m"));
    QCOMPARE(events.at(0).time, QStringLiteral("10:42"));
}

void TestTorrentStory::aTorrentAddedCompleteNeverFinishes()
{
    // A seed added in place is complete the second it is added: nothing finished.
    StoryInput input;
    input.now = at(24, 16, 0);
    input.torrent = added(at(24, 9, 15));
    input.torrent.completionOn = at(24, 9, 15);
    QCOMPARE(titles(TorrentStory::events(input)), QStringList({"Added"}));
}

void TestTorrentStory::movesSayWithWhat()
{
    // #250: a form with no %1 made QString::arg warn on every rebuild.
    QTest::failOnWarning(QRegularExpression(QStringLiteral("QString::arg")));
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(24, 8, 2), QStringLiteral("games"));
    input.moves = {
        {at(24, 16, 24), QStringLiteral("/data/fast/games"), 48, QStringLiteral("games")},
        {at(24, 17, 0), QStringLiteral("/data/keep"), 2, QString()},
        {at(24, 17, 15), QStringLiteral("/data/pair"), 1, QString()},
        {at(24, 17, 30), QStringLiteral("/data/one"), 0, QString()},
        {at(24, 17, 45), QStringLiteral("/data/alone"), 0, QStringLiteral("alone")}};
    const QList<StoryEvent> events = TorrentStory::events(input);
    QCOMPARE(titles(events),
             QStringList({"Moved to /data/alone",
                          "Moved to /data/one",
                          "Moved to /data/pair",
                          "Moved to /data/keep",
                          "Moved to /data/fast/games",
                          "Added"}));
    QCOMPARE(events.at(4).detail, QStringLiteral("with the folder games, 49 torrents"));
    QCOMPARE(events.at(4).kind, StoryEvent::Kind::Move);
    QCOMPARE(events.at(3).detail, QStringLiteral("with 2 other torrents"));
    QCOMPARE(events.at(2).detail, QStringLiteral("with 1 other torrent"));
    QCOMPARE(events.at(1).detail, QStringLiteral("on its own"));
    QCOMPARE(events.at(0).detail, QStringLiteral("with the folder alone"));
}

void TestTorrentStory::theErrorAndTheFailureComeAtTheirLoggedTimes()
{
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(24, 9, 0));
    input.problem = QStringLiteral("Can't write to its save folder");
    input.problemAt = at(24, 11, 40);
    input.failure = QStringLiteral("The move to dest didn't happen");
    input.failureAt = at(24, 10, 20);
    const QList<StoryEvent> events = TorrentStory::events(input);
    QCOMPARE(
        titles(events),
        QStringList({"Can't write to its save folder", "The move to dest didn't happen", "Added"}));
    QCOMPARE(events.at(1).kind, StoryEvent::Kind::Warn);
    QCOMPARE(events.at(0).kind, StoryEvent::Kind::Error);
    QCOMPARE(events.at(0).detail, QStringLiteral("from qBittorrent's log"));
    // C25 K24: an error in an engine's own words says who stopped it, not the log.
    input.problemEngine = QStringLiteral("Transmission");
    QCOMPARE(TorrentStory::events(input).at(0).detail, QStringLiteral("Transmission stopped it"));
    QCOMPARE(TorrentStory::kindName(StoryEvent::Kind::Error), QStringLiteral("error"));
    QCOMPARE(TorrentStory::kindName(StoryEvent::Kind::Move), QStringLiteral("move"));
}

// DECISIONS C32 D15: newest first (C24 H6 read oldest first); two at the same second read
// the later step first.
void TestTorrentStory::everythingIsNewestFirst()
{
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(20, 9, 0));
    input.torrent.completionOn = at(22, 9, 0);
    input.moves = {{at(23, 9, 0), QStringLiteral("/b"), 0, {}},
                   {at(21, 9, 0), QStringLiteral("/a"), 0, {}}};
    input.problem = QStringLiteral("Its files aren't where qBittorrent left them");
    input.problemAt = at(24, 9, 0);
    QCOMPARE(titles(TorrentStory::events(input)),
             QStringList({"Its files aren't where qBittorrent left them",
                          "Moved to /b",
                          "Finished downloading",
                          "Moved to /a",
                          "Added"}));
}

void TestTorrentStory::twoAtTheSameSecondReadTheLaterStepFirst()
{
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(24, 9, 0));
    input.torrent.completionOn = at(24, 10, 0);
    input.problem = QStringLiteral("The disk is full");
    input.problemAt = at(24, 10, 0);
    QCOMPARE(titles(TorrentStory::events(input)),
             QStringList({"The disk is full", "Finished downloading", "Added"}));
}

void TestTorrentStory::anEventWithNoTimeIsLeftOut()
{
    // A problem the log didn't time (docs/28 S4) and a torrent with no added date.
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(QDateTime());
    input.problem = QStringLiteral("qBittorrent didn't log a reason");
    QVERIFY(TorrentStory::events(input).isEmpty());
}

// DECISIONS C38 R1, R2: Renamed, was OLD, the accent's kind.
void TestTorrentStory::aTorrentRenameSaysTheOldName()
{
    const StoryEvent line = renameLine(renamed(at(24, 14, 3),
                                               StoryEntry::What::Torrent,
                                               QStringLiteral("night.market.rec.2026"),
                                               QStringLiteral("Night Market Field Recordings")));
    QCOMPARE(line.title, QStringLiteral("Renamed"));
    QCOMPARE(line.detail, QStringLiteral("was night.market.rec.2026"));
    QCOMPARE(line.time, QStringLiteral("14:03"));
    QCOMPARE(line.kind, StoryEvent::Kind::Rename);
    QCOMPARE(TorrentStory::kindName(StoryEvent::Kind::Rename), QStringLiteral("rename"));
}

// C38 R4: seen in a poll, C18 D7's words.
void TestTorrentStory::aRenameOutsideSaysSo()
{
    StoryEntry rename = renamed(
        at(24, 14, 3), StoryEntry::What::Torrent, QStringLiteral("NMFR_v2"), QStringLiteral("x"));
    rename.by = StoryEntry::By::Elsewhere;
    QCOMPARE(renameLine(rename).title, QStringLiteral("Renamed"));
    QCOMPARE(renameLine(rename).detail, QStringLiteral("was NMFR_v2 · renamed outside omaseed"));
}

// C38 R7: only Transmission's rename moved the folder on disk (K28); seen in a poll too, in
// board 56 §5's order.
void TestTorrentStory::transmissionSaysItsFolderMovedToo()
{
    StoryEntry rename = renamed(
        at(24, 14, 3), StoryEntry::What::Torrent, QStringLiteral("NMFR_v2"), QStringLiteral("x"));
    rename.disk = true;
    QCOMPARE(renameLine(rename).detail, QStringLiteral("was NMFR_v2 · its folder on disk too"));
    rename.by = StoryEntry::By::Elsewhere;
    QCOMPARE(renameLine(rename).detail,
             QStringLiteral("was NMFR_v2 · renamed outside omaseed · its folder on disk too"));
}

// C38 R3: a file at the top, a file in a folder, a folder.
void TestTorrentStory::aFileOrAFolderSaysOldToNew()
{
    StoryEvent line = renameLine(renamed(at(24, 14, 3),
                                         StoryEntry::What::File,
                                         QStringLiteral("cover.jpg"),
                                         QStringLiteral("folder.jpg")));
    QCOMPARE(line.title, QStringLiteral("Renamed a file"));
    QCOMPARE(line.detail, QStringLiteral("cover.jpg → folder.jpg"));
    QCOMPARE(line.kind, StoryEvent::Kind::Rename);

    line = renameLine(renamed(at(24, 14, 3),
                              StoryEntry::What::File,
                              QStringLiteral("01.flac"),
                              QStringLiteral("01 Lanterns.flac"),
                              QStringLiteral("Disc 1")));
    QCOMPARE(line.title, QStringLiteral("Renamed a file"));
    QCOMPARE(line.detail, QStringLiteral("01.flac → 01 Lanterns.flac in Disc 1/"));

    line = renameLine(renamed(at(24, 14, 3),
                              StoryEntry::What::Folder,
                              QStringLiteral("CD1"),
                              QStringLiteral("Disc 1"),
                              QStringLiteral("Extras")));
    QCOMPARE(line.title, QStringLiteral("Renamed a folder"));
    QCOMPARE(line.detail, QStringLiteral("CD1 → Disc 1"));
}

// C38 R3: board 18's sheet is one line however many files it renamed: in one folder, at the
// top, in several folders.
void TestTorrentStory::aRunOfFilesIsOneLine()
{
    StoryEntry run = renamed(at(24, 13, 58),
                             StoryEntry::What::Files,
                             QStringLiteral("01.flac"),
                             QStringLiteral("01 Lanterns.flac"),
                             QStringLiteral("Disc 1"));
    run.count = 12;
    StoryEvent line = renameLine(run);
    QCOMPARE(line.title, QStringLiteral("Renamed 12 files"));
    QCOMPARE(line.detail,
             QStringLiteral("in Disc 1/ · the first 01.flac → 01 Lanterns.flac, and 11 more"));
    QCOMPARE(line.time, QStringLiteral("13:58"));

    run.in.clear();
    QCOMPARE(renameLine(run).detail,
             QStringLiteral("the first 01.flac → 01 Lanterns.flac, and 11 more"));

    run.folders = 3;
    line = renameLine(run);
    QCOMPARE(line.title, QStringLiteral("Renamed 12 files"));
    QCOMPARE(line.detail,
             QStringLiteral("in 3 folders · the first 01.flac → 01 Lanterns.flac, and 11 more"));
}

// C38 R1: the story's own time words.
void TestTorrentStory::aRenameFromAnEarlierDay()
{
    QCOMPARE(renameLine(renamed(at(23, 21, 10),
                                StoryEntry::What::Torrent,
                                QStringLiteral("a"),
                                QStringLiteral("b")))
                 .time,
             QStringLiteral("yesterday 21:10"));
    QCOMPARE(renameLine(renamed(at(12, 9, 15),
                                StoryEntry::What::Torrent,
                                QStringLiteral("a"),
                                QStringLiteral("b")))
                 .time,
             QStringLiteral("12 Oct 09:15"));
}

// C38 amends C24 H6: a rename at the same second as another line is the later act, even a few
// milliseconds before it.
void TestTorrentStory::aRenameAtTheSameSecondReadsAbove()
{
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(24, 9, 0));
    input.moves = {{at(24, 10, 0).addMSecs(900), QStringLiteral("/b"), 0, {}}};
    input.entries = {renamed(at(24, 10, 0).addMSecs(100),
                             StoryEntry::What::Torrent,
                             QStringLiteral("old"),
                             QStringLiteral("new"))};
    QCOMPARE(titles(TorrentStory::events(input)), QStringList({"Renamed", "Moved to /b", "Added"}));

    // Two renames at the very same time: the newer record (first) leads.
    input.moves.clear();
    StoryEntry later = input.entries.at(0);
    later.was = QStringLiteral("new");
    later.now = QStringLiteral("newer");
    input.entries.prepend(later);
    const QList<StoryEvent> events = TorrentStory::events(input);
    QCOMPARE(events.at(0).detail, QStringLiteral("was new"));
    QCOMPARE(events.at(1).detail, QStringLiteral("was old"));
}

// C32 D15: newest first, renames among the rest by their time; no rename changes nothing.
void TestTorrentStory::renamesTakeTheirPlaceInTheStory()
{
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(20, 9, 0));
    input.torrent.completionOn = at(22, 9, 0);
    input.moves = {{at(23, 9, 0), QStringLiteral("/b"), 0, {}}};
    const QList<StoryEvent> without = TorrentStory::events(input);
    input.entries = {
        renamed(at(24, 9, 0), StoryEntry::What::Torrent, QStringLiteral("b"), QStringLiteral("c")),
        renamed(at(21, 9, 0), StoryEntry::What::Torrent, QStringLiteral("a"), QStringLiteral("b"))};
    const QList<StoryEvent> events = TorrentStory::events(input);
    QCOMPARE(titles(events),
             QStringList({"Renamed", "Moved to /b", "Finished downloading", "Renamed", "Added"}));
    QCOMPARE(events.at(0).detail, QStringLiteral("was b"));
    QCOMPARE(events.at(3).detail, QStringLiteral("was a"));
    QCOMPARE(events.at(0).kind, StoryEvent::Kind::Rename);
    QCOMPARE(titles(without), QStringList({"Moved to /b", "Finished downloading", "Added"}));
}

// C43 Y2 a, Y4 a: the strip's words, what it was in, the accent.
void TestTorrentStory::aCategorySaysWhatItWasIn()
{
    const StoryEvent event = line(edited(at(24, 16, 12),
                                         StoryEntry::What::Category,
                                         QStringLiteral("inbox"),
                                         QStringLiteral("music")));
    QCOMPARE(event.title, QStringLiteral("Put in music"));
    QCOMPARE(event.detail, QStringLiteral("was in inbox"));
    QCOMPARE(event.kind, StoryEvent::Kind::Edit);
    QCOMPARE(event.time, QStringLiteral("16:12"));
}

void TestTorrentStory::noCategoryBefore()
{
    const StoryEvent event =
        line(edited(at(24, 16, 12), StoryEntry::What::Category, {}, QStringLiteral("music")));
    QCOMPARE(event.title, QStringLiteral("Put in music"));
    QCOMPARE(event.detail, QStringLiteral("had no category"));
}

void TestTorrentStory::takenOutOfItsCategory()
{
    const StoryEvent event =
        line(edited(at(24, 16, 12), StoryEntry::What::Category, QStringLiteral("music"), {}));
    QCOMPARE(event.title, QStringLiteral("Taken out of its category"));
    QCOMPARE(event.detail, QStringLiteral("was in music"));
}

// Y2 a on Deluge (C25 K13): its label's words.
void TestTorrentStory::delugeLabelWords()
{
    StoryEvent event = line(edited(at(24, 16, 12),
                                   StoryEntry::What::Category,
                                   QStringLiteral("archive"),
                                   QStringLiteral("music")),
                            true);
    QCOMPARE(event.title, QStringLiteral("Labelled music"));
    QCOMPARE(event.detail, QStringLiteral("was archive"));
    event =
        line(edited(at(24, 16, 12), StoryEntry::What::Category, {}, QStringLiteral("music")), true);
    QCOMPARE(event.detail, QStringLiteral("had no label"));
    event =
        line(edited(at(24, 16, 12), StoryEntry::What::Category, QStringLiteral("music"), {}), true);
    QCOMPARE(event.title, QStringLiteral("Taken out of its label"));
    QCOMPARE(event.detail, QStringLiteral("was music"));
    QCOMPARE(event.kind, StoryEvent::Kind::Edit);
}

// Y8 a: seen in a poll, the dim line ends in "changed outside omaseed".
void TestTorrentStory::aCategoryChangedOutside()
{
    const StoryEvent event = line(edited(at(24, 16, 12),
                                         StoryEntry::What::Category,
                                         QStringLiteral("inbox"),
                                         QStringLiteral("music"),
                                         StoryEntry::By::Elsewhere));
    QCOMPARE(event.title, QStringLiteral("Put in music"));
    QCOMPARE(event.detail, QStringLiteral("was in inbox · changed outside omaseed"));
}

// Y8: the name left the server's list, whoever removed it.
void TestTorrentStory::theCategoryWasRemoved()
{
    StoryEntry entry = edited(at(24, 16, 12),
                              StoryEntry::What::Category,
                              QStringLiteral("music"),
                              {},
                              StoryEntry::By::Elsewhere);
    entry.removed = true;
    const StoryEvent event = line(entry);
    QCOMPARE(event.title, QStringLiteral("Taken out of its category"));
    QCOMPARE(event.detail, QStringLiteral("was in music · the category was removed"));
}

void TestTorrentStory::theLabelWasRemoved()
{
    StoryEntry entry = edited(at(24, 16, 12),
                              StoryEntry::What::Category,
                              QStringLiteral("music"),
                              {},
                              StoryEntry::By::Elsewhere);
    entry.removed = true;
    const StoryEvent event = line(entry, true);
    QCOMPARE(event.title, QStringLiteral("Taken out of its label"));
    QCOMPARE(event.detail, QStringLiteral("was music · the label was removed"));
}

// Y3 a: omaseed's own one-kind edit has no dim line; both at once say what changed.
void TestTorrentStory::taggedUntaggedAndBoth()
{
    StoryEvent event = line(tags(at(24, 16, 12), {"live", "flac"}, {}));
    QCOMPARE(event.title, QStringLiteral("Tagged live, flac"));
    QCOMPARE(event.detail, QString());
    QCOMPARE(event.kind, StoryEvent::Kind::Edit);
    event = line(tags(at(24, 16, 12), {}, {"wip"}));
    QCOMPARE(event.title, QStringLiteral("Untagged wip"));
    QCOMPARE(event.detail, QString());
    event = line(tags(at(24, 16, 12), {"live", "flac"}, {"wip"}));
    QCOMPARE(event.title, QStringLiteral("Tags changed"));
    QCOMPARE(event.detail, QStringLiteral("tagged live, flac · untagged wip"));
    // Y8: seen in a poll.
    event = line(tags(at(24, 16, 12), {"live"}, {}, StoryEntry::By::Elsewhere));
    QCOMPARE(event.title, QStringLiteral("Tagged live"));
    QCOMPARE(event.detail, QStringLiteral("changed outside omaseed"));
    event = line(tags(at(24, 16, 12), {"live"}, {"wip"}, StoryEntry::By::Elsewhere));
    QCOMPARE(event.detail, QStringLiteral("tagged live · untagged wip · changed outside omaseed"));
}

// Y3 a on Transmission: its labels' words; a category on it is never one.
void TestTorrentStory::transmissionLabelWords()
{
    StoryEvent event = line(tags(at(24, 16, 12), {"live"}, {}), false, true);
    QCOMPARE(event.title, QStringLiteral("Labelled live"));
    event = line(tags(at(24, 16, 12), {}, {"wip"}), false, true);
    QCOMPARE(event.title, QStringLiteral("Unlabelled wip"));
    event = line(tags(at(24, 16, 12), {"live"}, {"wip"}, StoryEntry::By::Elsewhere), false, true);
    QCOMPARE(event.title, QStringLiteral("Labels changed"));
    QCOMPARE(event.detail,
             QStringLiteral("labelled live · unlabelled wip · changed outside omaseed"));
}

void TestTorrentStory::threeNamesThenAndNMore()
{
    StoryEvent event = line(tags(at(24, 16, 12), {"a", "b", "c"}, {}));
    QCOMPARE(event.title, QStringLiteral("Tagged a, b, c"));
    event = line(tags(at(24, 16, 12), {"a", "b", "c", "d", "e"}, {}));
    QCOMPARE(event.title, QStringLiteral("Tagged a, b, c and 2 more"));
    event = line(tags(at(24, 16, 12), {"x"}, {"a", "b", "c", "d"}));
    QCOMPARE(event.detail, QStringLiteral("tagged x · untagged a, b, c and 1 more"));
}

void TestTorrentStory::theTagWasRemoved()
{
    StoryEntry entry = tags(at(24, 16, 12), {}, {"wip"}, StoryEntry::By::Elsewhere);
    entry.removed = true;
    const StoryEvent event = line(entry);
    QCOMPARE(event.title, QStringLiteral("Untagged wip"));
    QCOMPARE(event.detail, QStringLiteral("the tag was removed"));
}

// Y6 a: a save path no omaseed move made is a move, in C18 D7's words.
void TestTorrentStory::movedOutsideOmaseed()
{
    const StoryEvent event = line(edited(at(24, 16, 13),
                                         StoryEntry::What::Place,
                                         QStringLiteral("/srv/a"),
                                         QStringLiteral("/srv/b"),
                                         StoryEntry::By::Elsewhere));
    QCOMPARE(event.title, QStringLiteral("Moved to /srv/b"));
    QCOMPARE(event.detail, QStringLiteral("moved outside omaseed"));
    QCOMPARE(event.kind, StoryEvent::Kind::Move);
}

// Y6: waiting in its incomplete folder, nothing moved yet.
void TestTorrentStory::willLandIn()
{
    StoryEntry entry = edited(at(24, 16, 13),
                              StoryEntry::What::Place,
                              QStringLiteral("/srv/a"),
                              QStringLiteral("/srv/far"),
                              StoryEntry::By::Elsewhere);
    entry.landed = false;
    const StoryEvent event = line(entry);
    QCOMPARE(event.title, QStringLiteral("Will land in /srv/far"));
    QCOMPARE(event.detail, QStringLiteral("moved outside omaseed"));
    QCOMPARE(event.kind, StoryEvent::Kind::Move);
}

// Y7 a: the server's own move names its cause.
void TestTorrentStory::byItsCategoryAutomaticManagement()
{
    StoryEntry entry = edited(at(24, 16, 13),
                              StoryEntry::What::Place,
                              QStringLiteral("/srv/a"),
                              QStringLiteral("/srv/music"),
                              StoryEntry::By::Server);
    entry.cause = QStringLiteral("category");
    entry.causeName = QStringLiteral("music");
    const StoryEvent event = line(entry);
    QCOMPARE(event.title, QStringLiteral("Moved to /srv/music"));
    QCOMPARE(event.detail, QStringLiteral("by its category music · automatic management"));
}

void TestTorrentStory::automaticManagementWithNoCategory()
{
    StoryEntry entry = edited(at(24, 16, 13),
                              StoryEntry::What::Place,
                              QStringLiteral("/srv/music"),
                              QStringLiteral("/srv"),
                              StoryEntry::By::Server);
    entry.cause = QStringLiteral("category");
    QCOMPARE(line(entry).detail, QStringLiteral("automatic management"));
}

void TestTorrentStory::byItsLabel()
{
    StoryEntry entry = edited(at(24, 16, 13),
                              StoryEntry::What::Place,
                              QStringLiteral("/srv/a"),
                              QStringLiteral("/srv/far"),
                              StoryEntry::By::Server);
    entry.landed = false;
    entry.cause = QStringLiteral("label");
    entry.causeName = QStringLiteral("music");
    const StoryEvent event = line(entry, true);
    QCOMPARE(event.title, QStringLiteral("Will land in /srv/far"));
    QCOMPARE(event.detail, QStringLiteral("by its label music"));
}

// Y5 a: Added tells the category it came with, the oldest category line's was.
void TestTorrentStory::addedSaysTheCategoryItCameWith()
{
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(24, 9, 0), QStringLiteral("films"));
    input.entries = {edited(at(24, 12, 0),
                            StoryEntry::What::Category,
                            QStringLiteral("music"),
                            QStringLiteral("films")),
                     tags(at(24, 11, 0), {"live"}, {}),
                     edited(at(24, 10, 0),
                            StoryEntry::What::Category,
                            QStringLiteral("inbox"),
                            QStringLiteral("music"))};
    QList<StoryEvent> events = TorrentStory::events(input);
    QCOMPARE(titles(events), QStringList({"Put in films", "Tagged live", "Put in music", "Added"}));
    QCOMPARE(events.last().detail, QStringLiteral("into inbox"));

    // It came with none.
    input.entries = {
        edited(at(24, 10, 0), StoryEntry::What::Category, {}, QStringLiteral("films"))};
    QCOMPARE(TorrentStory::events(input).last().detail, QStringLiteral("with no category"));
    input.categoriesAreLabels = true;
    QCOMPARE(TorrentStory::events(input).last().detail, QStringLiteral("with no label"));
}

// Y5: with no category line, the category now, as before; Transmission still says nothing.
void TestTorrentStory::addedWithNoCategoryLineSaysTheCategoryNow()
{
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(24, 9, 0), QStringLiteral("films"));
    input.entries = {tags(at(24, 11, 0), {"live"}, {}),
                     edited(at(24, 10, 0),
                            StoryEntry::What::Place,
                            QStringLiteral("/a"),
                            QStringLiteral("/b"),
                            StoryEntry::By::Elsewhere)};
    QCOMPARE(TorrentStory::events(input).last().detail, QStringLiteral("into films"));
    input.categories = false;
    input.tagsAreLabels = true;
    const QList<StoryEvent> events = TorrentStory::events(input);
    QCOMPARE(events.last().detail, QString());
    QCOMPARE(events.first().title, QStringLiteral("Labelled live"));
}

// Y7, docs/58 §10: a poll's category line and the move it caused share a time; the move reads
// above, and both above a move of omaseed's at that second.
void TestTorrentStory::aPlaceReadsAboveTheFilingOfItsPoll()
{
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(24, 9, 0));
    const QDateTime poll = at(24, 16, 13).addMSecs(500);
    StoryEntry place = edited(poll,
                              StoryEntry::What::Place,
                              QStringLiteral("/srv/a"),
                              QStringLiteral("/srv/music"),
                              StoryEntry::By::Server);
    place.cause = QStringLiteral("category");
    place.causeName = QStringLiteral("music");
    // The record keeps them newest first, the category line written after the place.
    input.entries = {edited(poll,
                            StoryEntry::What::Category,
                            {},
                            QStringLiteral("music"),
                            StoryEntry::By::Elsewhere),
                     place};
    input.moves = {{at(24, 16, 13).addMSecs(900), QStringLiteral("/srv/b"), 0, {}}};
    QCOMPARE(titles(TorrentStory::events(input)),
             QStringList({"Moved to /srv/music", "Put in music", "Moved to /srv/b", "Added"}));
}

// C38: a rename at the same second stays above every other line.
void TestTorrentStory::aRenameStillReadsAboveAll()
{
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(24, 9, 0));
    const QDateTime second = at(24, 16, 13);
    input.entries = {edited(second.addMSecs(900),
                            StoryEntry::What::Place,
                            QStringLiteral("/a"),
                            QStringLiteral("/b"),
                            StoryEntry::By::Elsewhere),
                     tags(second.addMSecs(800), {"live"}, {}),
                     renamed(second.addMSecs(100),
                             StoryEntry::What::Torrent,
                             QStringLiteral("old"),
                             QStringLiteral("new"))};
    QCOMPARE(titles(TorrentStory::events(input)),
             QStringList({"Renamed", "Moved to /b", "Tagged live", "Added"}));
}

void TestTorrentStory::kindNameEdit()
{
    QCOMPARE(TorrentStory::kindName(StoryEvent::Kind::Edit), QStringLiteral("edit"));
    QCOMPARE(TorrentStory::kindName(StoryEvent::Kind::Rename), QStringLiteral("rename"));
    QCOMPARE(TorrentStory::kindName(StoryEvent::Kind::Move), QStringLiteral("move"));
}

QTEST_GUILESS_MAIN(TestTorrentStory)
#include "tst_torrentstory.moc"
