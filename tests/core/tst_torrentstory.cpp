// TorrentStory: the story tab's "what happened" (board 46 H6, DECISIONS C24): what goes in,
// in what order, and how its times read.

#include "core/TorrentStory.hpp"

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
    void everythingIsOldestFirst();
    void anEventWithNoTimeIsLeftOut();
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
}

void TestTorrentStory::finishedSaysHowLong()
{
    StoryInput input;
    input.now = at(24, 16, 0);
    input.torrent = added(at(24, 9, 15));
    input.torrent.completionOn = at(24, 10, 42);
    input.torrent.totalSize = 716 * kMiB;
    const QList<StoryEvent> events = TorrentStory::events(input);
    QCOMPARE(titles(events), QStringList({"Added", "Finished downloading"}));
    QCOMPARE(events.at(1).detail, QStringLiteral("716 MB in 1h 27m"));
    QCOMPARE(events.at(1).time, QStringLiteral("10:42"));
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
    StoryInput input;
    input.now = at(24, 18, 0);
    input.torrent = added(at(24, 8, 2), QStringLiteral("games"));
    input.moves = {
        {at(24, 16, 24), QStringLiteral("/data/fast/games"), 48, QStringLiteral("games")},
        {at(24, 17, 0), QStringLiteral("/data/keep"), 2, QString()},
        {at(24, 17, 30), QStringLiteral("/data/one"), 0, QString()},
        {at(24, 17, 45), QStringLiteral("/data/alone"), 0, QStringLiteral("alone")}};
    const QList<StoryEvent> events = TorrentStory::events(input);
    QCOMPARE(titles(events),
             QStringList({"Added",
                          "Moved to /data/fast/games",
                          "Moved to /data/keep",
                          "Moved to /data/one",
                          "Moved to /data/alone"}));
    QCOMPARE(events.at(1).detail, QStringLiteral("with the folder games, 49 torrents"));
    QCOMPARE(events.at(1).kind, StoryEvent::Kind::Move);
    QCOMPARE(events.at(2).detail, QStringLiteral("with 2 other torrents"));
    QCOMPARE(events.at(3).detail, QStringLiteral("on its own"));
    QCOMPARE(events.at(4).detail, QStringLiteral("with the folder alone"));
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
        QStringList({"Added", "The move to dest didn't happen", "Can't write to its save folder"}));
    QCOMPARE(events.at(1).kind, StoryEvent::Kind::Warn);
    QCOMPARE(events.at(2).kind, StoryEvent::Kind::Error);
    QCOMPARE(events.at(2).detail, QStringLiteral("from qBittorrent's log"));
    QCOMPARE(TorrentStory::kindName(StoryEvent::Kind::Error), QStringLiteral("error"));
    QCOMPARE(TorrentStory::kindName(StoryEvent::Kind::Move), QStringLiteral("move"));
}

void TestTorrentStory::everythingIsOldestFirst()
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
             QStringList({"Added",
                          "Moved to /a",
                          "Finished downloading",
                          "Moved to /b",
                          "Its files aren't where qBittorrent left them"}));
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

QTEST_GUILESS_MAIN(TestTorrentStory)
#include "tst_torrentstory.moc"
