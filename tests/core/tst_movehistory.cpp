#include "core/MoveHistory.hpp"

#include <QTest>

using omaseed::core::MoveHistory;
using omaseed::core::MoveRecord;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 10, 21), QTime(16, 0), QTimeZone::UTC);

/// A record moving each of `hashes` from `from` to `to`, finished `ago` minutes before kNow.
MoveRecord record(const QString &id,
                  const QStringList &hashes,
                  const QString &from,
                  const QString &to,
                  int ago = 0)
{
    MoveRecord r;
    r.id = id;
    r.kind = QStringLiteral("selection");
    r.title = id;
    r.newPath = to;
    r.finishedAt = kNow.addSecs(-60LL * ago);
    r.startedAt = r.finishedAt.addSecs(-5);
    for (const QString &hash : hashes) {
        r.from.insert(hash, from);
        r.to.insert(hash, to);
        r.names.insert(hash, hash.toUpper());
    }
    return r;
}

QHash<QString, QString> at(const QStringList &hashes, const QString &path)
{
    QHash<QString, QString> out;
    for (const QString &hash : hashes)
        out.insert(hash, path);
    return out;
}

} // namespace

class TestMoveHistory : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void keepsTheLastTwenty()
    {
        MoveHistory history;
        for (int i = 0; i < 25; ++i)
            history.add(record(QStringLiteral("m%1").arg(i), {QStringLiteral("h")}, "/a", "/b"),
                        kNow);
        QCOMPARE(history.records().size(), MoveHistory::kMaxMoves);
        // Newest first: the five oldest went.
        QCOMPARE(history.newest()->id, QStringLiteral("m24"));
        QCOMPARE(history.records().constLast().id, QStringLiteral("m5"));
    }

    void dropsWhatIsOlderThanThirtyDays()
    {
        MoveHistory history;
        history.add(record(QStringLiteral("old"), {QStringLiteral("h")}, "/a", "/b", 31 * 24 * 60),
                    kNow);
        history.add(
            record(QStringLiteral("recent"), {QStringLiteral("h")}, "/b", "/c", 29 * 24 * 60),
            kNow);
        QCOMPARE(history.records().size(), 1);
        QCOMPARE(history.newest()->id, QStringLiteral("recent"));
        // A day later, the other one goes too.
        QVERIFY(history.prune(kNow.addDays(2)));
        QVERIFY(history.isEmpty());
    }

    void ignoresAMoveThatMovedNothing()
    {
        MoveHistory history;
        history.add(record(QStringLiteral("empty"), {}, "/a", "/b"), kNow);
        QVERIFY(history.isEmpty());
    }

    void savesAndLoads()
    {
        MoveHistory history;
        MoveRecord first = record(QStringLiteral("games"),
                                  {QStringLiteral("h1"), QStringLiteral("h2")},
                                  QStringLiteral("/library/games"),
                                  QStringLiteral("/fast/games"),
                                  10);
        first.kind = QStringLiteral("folder");
        first.hadAutoTmm = {QStringLiteral("h2")};
        first.carried = 3;
        first.carriedTo = QStringLiteral("/fast/games");
        history.add(first, kNow);
        history.add(record(QStringLiteral("one"), {QStringLiteral("h3")}, "/x", "/y"), kNow);

        const MoveHistory back = MoveHistory::fromJson(history.toJson());
        QCOMPARE(back.records().size(), 2);
        QCOMPARE(back.newest()->id, QStringLiteral("one"));
        const MoveRecord *games = back.find(QStringLiteral("games"));
        QVERIFY(games != nullptr);
        QCOMPARE(games->kind, QStringLiteral("folder"));
        QCOMPARE(games->from.value(QStringLiteral("h2")), QStringLiteral("/library/games"));
        QCOMPARE(games->to.value(QStringLiteral("h1")), QStringLiteral("/fast/games"));
        QCOMPARE(games->names.value(QStringLiteral("h1")), QStringLiteral("H1"));
        QCOMPARE(games->hadAutoTmm, QStringList{QStringLiteral("h2")});
        QCOMPARE(games->carried, 3);
        QCOMPARE(games->carriedTo, QStringLiteral("/fast/games"));
        QCOMPARE(games->finishedAt, first.finishedAt);
    }

    void lastForIsTheNewestThatMovedIt()
    {
        MoveHistory history;
        history.add(record(QStringLiteral("a"),
                           {QStringLiteral("h1"), QStringLiteral("h2")},
                           "/lib",
                           "/fast",
                           10),
                    kNow);
        history.add(record(QStringLiteral("b"), {QStringLiteral("h1")}, "/fast", "/archive", 5),
                    kNow);
        QCOMPARE(history.lastFor(QStringLiteral("h1"))->id, QStringLiteral("b"));
        QCOMPARE(history.lastFor(QStringLiteral("h2"))->id, QStringLiteral("a"));
        QVERIFY(history.lastFor(QStringLiteral("none")) == nullptr);
    }

    void eachTorrentStillWhereTheMovePutItGoesBack()
    {
        MoveHistory history;
        const QStringList all{QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3")};
        history.add(record(QStringLiteral("games"), all, "/lib/games", "/fast/games"), kNow);
        // A trailing slash is the same folder.
        auto paths = at(all, QStringLiteral("/fast/games"));
        paths[QStringLiteral("h2")] = QStringLiteral("/fast/games/");
        const MoveHistory::BackCheck check = history.check(QStringLiteral("games"), paths);
        QCOMPARE(check.goBack, all);
        QCOMPARE(check.stay(), 0);
    }

    void aLaterMoveOfOtherTorrentsNeverBlocksIt()
    {
        // C18 D7: only an overlap matters.
        MoveHistory history;
        const QStringList games{QStringLiteral("g1"), QStringLiteral("g2")};
        history.add(record(QStringLiteral("games"), games, "/lib/games", "/fast/games", 10), kNow);
        history.add(
            record(QStringLiteral("film"), {QStringLiteral("f1")}, "/lib/films", "/archive", 5),
            kNow);
        auto paths = at(games, QStringLiteral("/fast/games"));
        paths.insert(QStringLiteral("f1"), QStringLiteral("/archive"));
        const MoveHistory::BackCheck check = history.check(QStringLiteral("games"), paths);
        QCOMPARE(check.goBack, games);
        QVERIFY(check.movedAgain.isEmpty());
    }

    void anOverlapNamesTheLaterMove()
    {
        // Diogo's case: a folder moved, then three of it moved elsewhere, then the folder's
        // move back.
        MoveHistory history;
        const QStringList games{QStringLiteral("g1"),
                                QStringLiteral("g2"),
                                QStringLiteral("g3"),
                                QStringLiteral("g4"),
                                QStringLiteral("g5")};
        const QStringList three{QStringLiteral("g1"), QStringLiteral("g2"), QStringLiteral("g3")};
        history.add(record(QStringLiteral("games"), games, "/lib/games", "/fast/games", 10), kNow);
        history.add(record(QStringLiteral("three"), three, "/fast/games", "/archive", 5), kNow);
        auto paths = at(games, QStringLiteral("/fast/games"));
        for (const QString &hash : three)
            paths[hash] = QStringLiteral("/archive");

        MoveHistory::BackCheck check = history.check(QStringLiteral("games"), paths);
        QCOMPARE(check.goBack, (QStringList{QStringLiteral("g4"), QStringLiteral("g5")}));
        QCOMPARE(check.movedAgain.size(), 1);
        QCOMPARE(check.movedAgain.constFirst().first, QStringLiteral("three"));
        QCOMPARE(check.movedAgain.constFirst().second, three);
        QCOMPARE(check.stay(), 3);

        // "move that one back first": the three land where the folder's move put them, and the
        // folder's move back is whole again.
        for (const QString &hash : three)
            paths[hash] = QStringLiteral("/fast/games");
        QVERIFY(history.backDone(QStringLiteral("three"), three));
        QVERIFY(history.find(QStringLiteral("three")) == nullptr);
        check = history.check(QStringLiteral("games"), paths);
        QCOMPARE(check.goBack, games);
        QCOMPARE(check.stay(), 0);
    }

    void aChainIsUndoneOneStepAtATime()
    {
        // Moved three times: only the move that put it where it is now is named.
        MoveHistory history;
        const QString h = QStringLiteral("h");
        history.add(record(QStringLiteral("a"), {h}, "/one", "/two", 30), kNow);
        history.add(record(QStringLiteral("b"), {h}, "/two", "/three", 20), kNow);
        history.add(record(QStringLiteral("c"), {h}, "/three", "/four", 10), kNow);
        QHash<QString, QString> paths{{h, QStringLiteral("/four")}};
        QCOMPARE(history.check(QStringLiteral("a"), paths).movedAgain.constFirst().first,
                 QStringLiteral("c"));
        paths[h] = QStringLiteral("/three");
        QVERIFY(history.backDone(QStringLiteral("c"), {h}));
        QCOMPARE(history.check(QStringLiteral("a"), paths).movedAgain.constFirst().first,
                 QStringLiteral("b"));
        paths[h] = QStringLiteral("/two");
        QVERIFY(history.backDone(QStringLiteral("b"), {h}));
        QCOMPARE(history.check(QStringLiteral("a"), paths).goBack, QStringList{h});
    }

    void movedOutsideOmaseedStays()
    {
        MoveHistory history;
        history.add(
            record(
                QStringLiteral("a"), {QStringLiteral("h1"), QStringLiteral("h2")}, "/lib", "/fast"),
            kNow);
        const QHash<QString, QString> paths{{QStringLiteral("h1"), QStringLiteral("/fast")},
                                            {QStringLiteral("h2"), QStringLiteral("/elsewhere")}};
        const MoveHistory::BackCheck check = history.check(QStringLiteral("a"), paths);
        QCOMPARE(check.goBack, QStringList{QStringLiteral("h1")});
        QCOMPARE(check.outside, QStringList{QStringLiteral("h2")});
        QVERIFY(check.movedAgain.isEmpty());
    }

    void goneFromTheLibraryIsListed()
    {
        MoveHistory history;
        history.add(
            record(
                QStringLiteral("a"), {QStringLiteral("h1"), QStringLiteral("h2")}, "/lib", "/fast"),
            kNow);
        const QHash<QString, QString> paths{{QStringLiteral("h1"), QStringLiteral("/fast")}};
        const MoveHistory::BackCheck check = history.check(QStringLiteral("a"), paths);
        QCOMPARE(check.gone, QStringList{QStringLiteral("h2")});
        QCOMPARE(check.stay(), 1);
    }

    void onlyNarrowsToOneTorrent()
    {
        // The details' "this one only".
        MoveHistory history;
        const QStringList all{QStringLiteral("h1"), QStringLiteral("h2")};
        history.add(record(QStringLiteral("a"), all, "/lib", "/fast"), kNow);
        const MoveHistory::BackCheck check = history.check(
            QStringLiteral("a"), at(all, QStringLiteral("/fast")), {QStringLiteral("h2")});
        QCOMPARE(check.goBack, QStringList{QStringLiteral("h2")});
    }

    void backDoneLeavesTheRestOfTheRecord()
    {
        MoveHistory history;
        history.add(
            record(
                QStringLiteral("a"), {QStringLiteral("h1"), QStringLiteral("h2")}, "/lib", "/fast"),
            kNow);
        QVERIFY(history.backDone(QStringLiteral("a"), {QStringLiteral("h1")}));
        const MoveRecord *a = history.find(QStringLiteral("a"));
        QVERIFY(a != nullptr);
        QCOMPARE(a->from.keys(), QStringList{QStringLiteral("h2")});
        QVERIFY(!history.backDone(QStringLiteral("unknown"), {QStringLiteral("h2")}));
    }

    void anUnknownRecordChecksToNothing()
    {
        MoveHistory history;
        const MoveHistory::BackCheck check = history.check(QStringLiteral("none"), {});
        QVERIFY(check.goBack.isEmpty());
        QCOMPARE(check.stay(), 0);
    }
};

QTEST_GUILESS_MAIN(TestMoveHistory)

#include "tst_movehistory.moc"
