#include "core/StatusDigest.hpp"

#include "core/SeedConsole.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace omaseed::core;
namespace digest = omaseed::core::StatusDigest;

namespace {

Torrent seeding(const QString &hash, int swarmSeeds, int waiting = 0)
{
    Torrent t;
    t.hash = hash;
    t.name = hash.toUpper();
    t.state = TorrentState::StalledUpload;
    t.progress = 1.0;
    t.size = 1000;
    t.numComplete = swarmSeeds;
    t.numIncomplete = waiting;
    return t;
}

Torrent downloading(const QString &hash, qint64 speed, double progress)
{
    Torrent t;
    t.hash = hash;
    t.name = hash.toUpper();
    t.state = TorrentState::Downloading;
    t.progress = progress;
    t.dlspeed = speed;
    t.eta = 120;
    t.size = 500;
    return t;
}

const QDateTime kNow(QDate(2026, 10, 2), QTime(12, 0));

} // namespace

class TestStatusDigest : public QObject
{
    Q_OBJECT

private slots:
    void roundsToThreeFigures_data();
    void roundsToThreeFigures();
    void roundsRatios();
    void counterSurvivesARestart();
    void samplerAveragesEachMinute();
    void samplerForgetsPastTheHour();
    void weekEndsToday();
    void weekRollsOverAtMidnight();
    void weekRoundTripsAndPrunes();
    void milestonesBetweenRatios();
    void milestonesNeedABefore();
    void milestonesFromADiffMatchTheWholeList();
    void aliveBucketsBySwarmSeeds();
    void aliveTakesTheConsolesRule();
    void aliveRowsRarestFirst();
    void aliveCountsQuietTorrents();
    void aliveSaysWhenItWillChange();
    void topSeedersByTodaysUpload();
    void activeFastestFirst();
    void onOfferCountsCompleteOnly();
    void recentReplacesAndCaps();
};

void TestStatusDigest::roundsToThreeFigures_data()
{
    QTest::addColumn<qint64>("value");
    QTest::addColumn<qint64>("expected");
    QTest::newRow("zero") << qint64(0) << qint64(0);
    QTest::newRow("small") << qint64(999) << qint64(999);
    QTest::newRow("down") << qint64(1234) << qint64(1230);
    QTest::newRow("half up") << qint64(1235) << qint64(1240);
    QTest::newRow("no double rounding") << qint64(1234567) << qint64(1230000);
    QTest::newRow("carry") << qint64(999999) << qint64(1000000);
    QTest::newRow("negative") << qint64(-4567) << qint64(-4570);
}

void TestStatusDigest::roundsToThreeFigures()
{
    QFETCH(qint64, value);
    QFETCH(qint64, expected);
    QCOMPARE(digest::roundSignificant(value), expected);
}

void TestStatusDigest::roundsRatios()
{
    QCOMPARE(digest::roundRatio(1.23456), 1.235);
    QCOMPARE(digest::roundRatio(-1.0), -1.0);
    QCOMPARE(digest::roundRatio(-0.5), -1.0);
}

void TestStatusDigest::counterSurvivesARestart()
{
    digest::Counter counter;
    QCOMPARE(counter.observe(100), qint64(0));
    QCOMPARE(counter.observe(150), qint64(50));
    // qBittorrent restarted: its counter starts again from zero.
    QCOMPARE(counter.observe(30), qint64(30));
    QCOMPARE(counter.total(), qint64(80));
    counter.reset();
    QCOMPARE(counter.observe(500), qint64(0));
    QCOMPARE(counter.total(), qint64(0));
}

void TestStatusDigest::samplerAveragesEachMinute()
{
    digest::UploadSampler sampler;
    const qint64 base = 1789346400; // a whole minute
    sampler.sample(1000, base - 60);
    sampler.sample(100, base);
    sampler.sample(200, base + 30);
    const QJsonArray values = sampler.values(base + 59);
    QCOMPARE(values.size(), 60);
    QCOMPARE(values.last().toInteger(), qint64(150));
    QCOMPARE(values.at(58).toInteger(), qint64(1000));
    QCOMPARE(values.first().toInteger(), qint64(0));
}

void TestStatusDigest::samplerForgetsPastTheHour()
{
    digest::UploadSampler sampler;
    const qint64 base = 1789346400;
    sampler.sample(5000, base);
    QCOMPARE(sampler.values(base + 59 * 60).first().toInteger(), qint64(5000));
    QCOMPARE(sampler.values(base + 60 * 60).first().toInteger(), qint64(0));
    for (const QJsonValue &value : sampler.values(base + 60 * 60))
        QCOMPARE(value.toInteger(), qint64(0));
}

void TestStatusDigest::weekEndsToday()
{
    digest::WeekLedger ledger;
    const QDate today(2026, 10, 2);
    ledger.observe(1000, today);
    ledger.observe(4000, today);
    const QJsonArray week = ledger.week(today);
    QCOMPARE(week.size(), 7);
    QCOMPARE(week.first().toObject().value(QStringLiteral("day")).toString(),
             QStringLiteral("2026-09-26"));
    QCOMPARE(week.last().toObject().value(QStringLiteral("day")).toString(),
             QStringLiteral("2026-10-02"));
    QCOMPARE(week.last().toObject().value(QStringLiteral("up")).toInteger(), qint64(3000));
}

void TestStatusDigest::weekRollsOverAtMidnight()
{
    digest::WeekLedger ledger;
    const QDate day(2026, 10, 2);
    ledger.observe(0, day);
    ledger.observe(700, day);
    ledger.observe(1000, day.addDays(1));
    const QJsonArray week = ledger.week(day.addDays(1));
    QCOMPARE(week.at(5).toObject().value(QStringLiteral("up")).toInteger(), qint64(700));
    QCOMPARE(week.at(6).toObject().value(QStringLiteral("up")).toInteger(), qint64(300));
    // A week later the day has left the strip.
    const QJsonArray later = ledger.week(day.addDays(7));
    for (const QJsonValue &value : later)
        QVERIFY(value.toObject().value(QStringLiteral("up")).toInteger() != 700);
}

void TestStatusDigest::weekRoundTripsAndPrunes()
{
    digest::WeekLedger ledger;
    const QDate start(2026, 9, 1);
    for (int i = 0; i < 12; ++i) {
        ledger.resetBaseline();
        ledger.observe(0, start.addDays(i));
        ledger.observe(10 + i, start.addDays(i));
    }
    const QJsonObject json = ledger.toJson();
    QCOMPARE(json.size(), 8);
    QVERIFY(!json.contains(QStringLiteral("2026-09-01")));

    digest::WeekLedger restored;
    restored.fromJson(json);
    QCOMPARE(restored.week(start.addDays(11)), ledger.week(start.addDays(11)));
    QCOMPARE(
        restored.week(start.addDays(11)).last().toObject().value(QStringLiteral("up")).toInteger(),
        qint64(21));
}

void TestStatusDigest::milestonesBetweenRatios()
{
    QCOMPARE(digest::milestonesBetween(0.9, 1.2), QList<int>({1}));
    QCOMPARE(digest::milestonesBetween(1.9, 5.1), QList<int>({2, 5}));
    QCOMPARE(digest::milestonesBetween(1.0, 1.5), QList<int>());
    QCOMPARE(digest::milestonesBetween(2.5, 1.0), QList<int>());
}

void TestStatusDigest::milestonesNeedABefore()
{
    digest::RatioMilestones milestones;
    Torrent a = seeding(QStringLiteral("a"), 3);
    a.ratio = 0.9;
    Torrent b = seeding(QStringLiteral("b"), 3);
    b.ratio = 3.0;
    // The first sight (startup, a new add) only sets the starting point.
    QVERIFY(milestones.observe({a, b}).isEmpty());

    a.ratio = 1.1;
    Torrent c = seeding(QStringLiteral("c"), 3);
    c.ratio = 7.0;
    const auto crossings = milestones.observe({a, b, c});
    QCOMPARE(crossings.size(), 1);
    QCOMPARE(crossings.first().hash, QStringLiteral("a"));
    QCOMPARE(crossings.first().ratio, 1);
}

// #36: a poll's diff gives the same crossings as the whole list.
void TestStatusDigest::milestonesFromADiffMatchTheWholeList()
{
    digest::RatioMilestones whole;
    digest::RatioMilestones diff;
    Torrent a = seeding(QStringLiteral("a"), 3);
    a.ratio = 0.9;
    Torrent b = seeding(QStringLiteral("b"), 3);
    b.ratio = 1.8;
    Torrent c = seeding(QStringLiteral("c"), 3);
    c.ratio = 4.0;
    QVERIFY(whole.observe({a, b, c}).isEmpty());
    QVERIFY(diff.observe({a, b, c}).isEmpty());

    const auto same = [](const QList<digest::RatioMilestones::Crossing> &x,
                         const QList<digest::RatioMilestones::Crossing> &y) {
        if (x.size() != y.size())
            return false;
        for (qsizetype i = 0; i < x.size(); ++i) {
            if (x.at(i).hash != y.at(i).hash || x.at(i).ratio != y.at(i).ratio)
                return false;
        }
        return true;
    };

    // a crosses 1, b crosses 2, c holds; a new torrent d only sets its start.
    a.ratio = 1.2;
    b.ratio = 2.1;
    Torrent d = seeding(QStringLiteral("d"), 3);
    d.ratio = 9.0;
    auto w = whole.observe({a, b, c, d});
    auto f = diff.observeChanges({a, b, d}, {});
    QCOMPARE(w.size(), 2);
    QVERIFY(same(w, f));

    // c removed, then added again later: a fresh start in both, nothing announced.
    w = whole.observe({a, b, d});
    f = diff.observeChanges({}, {QStringLiteral("c")});
    QVERIFY(w.isEmpty() && f.isEmpty());
    c.ratio = 5.5;
    w = whole.observe({a, b, c, d});
    f = diff.observeChanges({c}, {});
    QVERIFY(w.isEmpty() && f.isEmpty());

    // Removed and added again in one poll: still compared with what it had.
    d.ratio = 1.0; // went down: nothing
    c.ratio = 6.0;
    w = whole.observe({a, b, c, d});
    f = diff.observeChanges({c, d}, {QStringLiteral("c")});
    QVERIFY(same(w, f));
    a.ratio = 5.0;
    w = whole.observe({a, b, c, d});
    f = diff.observeChanges({a}, {});
    QCOMPARE(w.size(), 2); // 2 and 5
    QVERIFY(same(w, f));
}

void TestStatusDigest::aliveBucketsBySwarmSeeds()
{
    Torrent stopped = seeding(QStringLiteral("stopped"), 1);
    stopped.state = TorrentState::StoppedUpload;
    Torrent failing = seeding(QStringLiteral("failing"), 1, 4);
    failing.hasTrackerError = true;
    const QList<Torrent> torrents{
        seeding(QStringLiteral("unknown"), 0, 2), // no scrape: unknown, not "only you" (#47)
        failing,                                  // a tracker error: unknown too
        seeding(QStringLiteral("alone"), 1),
        seeding(QStringLiteral("pair"), 2, 1),
        seeding(QStringLiteral("five"), 5),
        seeding(QStringLiteral("crowd"), 40, 9),
        stopped,
        downloading(QStringLiteral("dl"), 10, 0.5),
    };
    const QJsonObject alive = digest::alive(torrents, kNow);
    const QJsonArray buckets = alive.value(QStringLiteral("buckets")).toArray();
    QCOMPARE(buckets.size(), 5);
    const auto bucket = [&](int seeds, const char *key) {
        return buckets.at(seeds - 1).toObject().value(QLatin1String(key)).toInt();
    };
    QCOMPARE(bucket(1, "seeds"), 1);
    QCOMPARE(bucket(1, "count"), 1);
    QCOMPARE(bucket(1, "waiting"), 0);
    QCOMPARE(bucket(1, "peers"), 0);
    QCOMPARE(bucket(2, "count"), 1);
    QCOMPARE(bucket(2, "waiting"), 1);
    QCOMPARE(bucket(2, "peers"), 1);
    QCOMPARE(bucket(3, "count"), 0);
    QCOMPARE(bucket(5, "count"), 1);
    QStringList rows;
    for (const QJsonValue &row : alive.value(QStringLiteral("rows")).toArray())
        rows << row.toObject().value(QStringLiteral("hash")).toString();
    QCOMPARE(rows, QStringList({"alone", "pair", "five"}));
}

// #47: the bar and the seed console count a swarm with one rule, so they cannot disagree.
void TestStatusDigest::aliveTakesTheConsolesRule()
{
    QList<Torrent> torrents;
    for (const int seeds : {-1, 0, 1, 2, 5, 6}) {
        for (const bool error : {false, true}) {
            Torrent t = seeding(QStringLiteral("s%1%2").arg(seeds).arg(error ? "e" : ""), seeds);
            t.hasTrackerError = error;
            torrents.append(t);
        }
    }
    const QJsonArray buckets =
        digest::alive(torrents, kNow).value(QStringLiteral("buckets")).toArray();
    for (int seeds = 1; seeds <= 5; ++seeds) {
        int expected = 0;
        for (const Torrent &t : torrents)
            expected += SeedConsole::knownSeeds(t) == seeds ? 1 : 0;
        QCOMPARE(buckets.at(seeds - 1).toObject().value(QStringLiteral("count")).toInt(), expected);
    }
}

void TestStatusDigest::aliveRowsRarestFirst()
{
    QList<Torrent> torrents{
        seeding(QStringLiteral("b"), 2, 5),
        seeding(QStringLiteral("a"), 1, 0),
        seeding(QStringLiteral("c"), 1, 3),
        seeding(QStringLiteral("d"), 3),
        seeding(QStringLiteral("e"), 4),
        seeding(QStringLiteral("f"), 5),
        seeding(QStringLiteral("g"), 5),
        seeding(QStringLiteral("h"), 9),
    };
    const QJsonArray rows = digest::alive(torrents, kNow).value(QStringLiteral("rows")).toArray();
    QCOMPARE(rows.size(), 6);
    QStringList order;
    for (const QJsonValue &row : rows)
        order << row.toObject().value(QStringLiteral("hash")).toString();
    QCOMPARE(order, QStringList({"c", "a", "b", "d", "e", "f"}));
    const QJsonObject first = rows.first().toObject();
    QCOMPARE(first.value(QStringLiteral("seeds")).toInt(), 1);
    QCOMPARE(first.value(QStringLiteral("waiting")).toInt(), 3);
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("C"));
}

void TestStatusDigest::aliveCountsQuietTorrents()
{
    Torrent old = seeding(QStringLiteral("old"), 1);
    old.lastActivity = kNow.addDays(-31);
    Torrent recent = seeding(QStringLiteral("recent"), 1);
    recent.lastActivity = kNow.addDays(-2);
    Torrent neverMoved = seeding(QStringLiteral("never"), 1);
    neverMoved.addedOn = kNow.addDays(-40);
    Torrent fresh = seeding(QStringLiteral("fresh"), 1);
    fresh.addedOn = kNow.addDays(-1);
    const QJsonObject alive = digest::alive({old, recent, neverMoved, fresh}, kNow);
    const QJsonObject one = alive.value(QStringLiteral("buckets")).toArray().first().toObject();
    QCOMPARE(one.value(QStringLiteral("count")).toInt(), 4);
    QCOMPARE(one.value(QStringLiteral("quiet")).toInt(), 2);
}

// #36: the status file keeps `alive` until the moment its quiet count would move.
void TestStatusDigest::aliveSaysWhenItWillChange()
{
    Torrent old = seeding(QStringLiteral("old"), 1);
    old.lastActivity = kNow.addDays(-31);
    Torrent recent = seeding(QStringLiteral("recent"), 1);
    recent.lastActivity = kNow.addDays(-2);
    Torrent fresh = seeding(QStringLiteral("fresh"), 2);
    fresh.addedOn = kNow.addDays(-1);
    Torrent crowd = seeding(QStringLiteral("crowd"), 40); // not counted: never asked about
    crowd.lastActivity = kNow.addDays(-29);
    qint64 quietAt = -1;
    digest::alive({old, recent, fresh, crowd}, kNow, &quietAt);
    const qint64 expected = recent.lastActivity.toSecsSinceEpoch() + 30 * 24 * 3600;
    QCOMPARE(quietAt, expected);

    // Just before it, the same; at it, one more quiet.
    const auto quietCount = [&](const QDateTime &now) {
        const QJsonObject alive = digest::alive({old, recent, fresh, crowd}, now);
        int quiet = 0;
        for (const QJsonValue &bucket : alive.value(QStringLiteral("buckets")).toArray())
            quiet += bucket.toObject().value(QStringLiteral("quiet")).toInt();
        return quiet;
    };
    QCOMPARE(quietCount(QDateTime::fromSecsSinceEpoch(expected - 1)), 1);
    QCOMPARE(quietCount(QDateTime::fromSecsSinceEpoch(expected)), 2);

    // Everything already quiet, or nothing with a date: never.
    digest::alive({old}, kNow, &quietAt);
    QCOMPARE(quietAt, qint64(0));
    digest::alive({seeding(QStringLiteral("undated"), 1)}, kNow, &quietAt);
    QCOMPARE(quietAt, qint64(0));
}

void TestStatusDigest::topSeedersByTodaysUpload()
{
    Torrent a = seeding(QStringLiteral("a"), 3);
    a.uploadedSession = 900'000; // the session's total ranks nothing any more (#47)
    a.ratio = 1.23456;
    Torrent b = seeding(QStringLiteral("b"), 3);
    Torrent c = seeding(QStringLiteral("c"), 3);
    Torrent d = seeding(QStringLiteral("d"), 3);
    const QHash<QString, Torrent> library{{a.hash, a}, {b.hash, b}, {c.hash, c}, {d.hash, d}};
    const auto find = [&library](const QString &hash) -> const Torrent * {
        const auto it = library.constFind(hash);
        return it == library.constEnd() ? nullptr : &it.value();
    };
    const QHash<QString, qint64> today{
        {QStringLiteral("a"), 500},
        {QStringLiteral("b"), 900},
        {QStringLiteral("c"), 100},
        {QStringLiteral("d"), 0},
        {QStringLiteral("gone"), 5000}, // removed since: skipped
    };
    const QJsonArray top = digest::topSeeders(today, find);
    QCOMPARE(top.size(), 2);
    QCOMPARE(top.at(0).toObject().value(QStringLiteral("hash")).toString(), QStringLiteral("b"));
    QCOMPARE(top.at(0).toObject().value(QStringLiteral("uploaded")).toInteger(), qint64(900));
    QCOMPARE(top.at(1).toObject().value(QStringLiteral("hash")).toString(), QStringLiteral("a"));
    QCOMPARE(top.at(1).toObject().value(QStringLiteral("uploaded")).toInteger(), qint64(500));
    QCOMPARE(top.at(1).toObject().value(QStringLiteral("ratio")).toDouble(), 1.235);

    // A tie goes by name; nothing given today, no rows.
    const QJsonArray tied = digest::topSeeders(
        {{QStringLiteral("d"), 7}, {QStringLiteral("c"), 7}, {QStringLiteral("b"), 7}}, find);
    QCOMPARE(tied.at(0).toObject().value(QStringLiteral("hash")).toString(), QStringLiteral("b"));
    QCOMPARE(tied.at(1).toObject().value(QStringLiteral("hash")).toString(), QStringLiteral("c"));
    QVERIFY(digest::topSeeders({{QStringLiteral("d"), 0}}, find).isEmpty());
    QVERIFY(digest::topSeeders({}, find).isEmpty());
}

void TestStatusDigest::activeFastestFirst()
{
    Torrent stalled = downloading(QStringLiteral("stalled"), 0, 0.9);
    stalled.state = TorrentState::StalledDownload;
    const QJsonArray rows = digest::active({
        downloading(QStringLiteral("slow"), 10, 0.2),
        downloading(QStringLiteral("fast"), 123456, 0.4891),
        stalled,
        downloading(QStringLiteral("mid"), 500, 0.1),
        seeding(QStringLiteral("seed"), 3),
    });
    QCOMPARE(rows.size(), 3);
    const QJsonObject fast = rows.first().toObject();
    QCOMPARE(fast.value(QStringLiteral("hash")).toString(), QStringLiteral("fast"));
    QCOMPARE(fast.value(QStringLiteral("dl")).toInteger(), qint64(123000));
    QCOMPARE(fast.value(QStringLiteral("progress")).toDouble(), 0.489);
    QCOMPARE(fast.value(QStringLiteral("eta")).toInteger(), qint64(120));
    QCOMPARE(rows.at(2).toObject().value(QStringLiteral("hash")).toString(),
             QStringLiteral("slow"));
    // A queued download has no estimate, nor one qBittorrent cannot estimate.
    Torrent queued = downloading(QStringLiteral("queued"), 0, 0.3);
    queued.state = TorrentState::QueuedDownload;
    QCOMPARE(digest::active({queued}).first().toObject().value(QStringLiteral("eta")).toInteger(),
             qint64(-1));
    stalled.eta = 8640000;
    QCOMPARE(digest::active({stalled}).first().toObject().value(QStringLiteral("eta")).toInteger(),
             qint64(-1));
}

void TestStatusDigest::onOfferCountsCompleteOnly()
{
    QCOMPARE(digest::onOffer({seeding(QStringLiteral("a"), 1),
                              seeding(QStringLiteral("b"), 1),
                              downloading(QStringLiteral("c"), 1, 0.5)}),
             qint64(2000));
}

void TestStatusDigest::recentReplacesAndCaps()
{
    const auto entry = [](const QString &hash, const QString &kind, int ratio = 0) {
        QJsonObject object{{QStringLiteral("hash"), hash}, {QStringLiteral("kind"), kind}};
        if (ratio > 0)
            object.insert(QStringLiteral("ratio"), ratio);
        return object;
    };
    QJsonArray recent;
    for (int i = 0; i < 7; ++i)
        recent = digest::pushRecent(recent, entry(QString::number(i), QStringLiteral("completed")));
    QCOMPARE(recent.size(), 5);
    QCOMPARE(recent.first().toObject().value(QStringLiteral("hash")).toString(),
             QStringLiteral("6"));

    recent = digest::pushRecent(recent, entry(QStringLiteral("4"), QStringLiteral("completed")));
    QCOMPARE(recent.size(), 5);
    QCOMPARE(recent.first().toObject().value(QStringLiteral("hash")).toString(),
             QStringLiteral("4"));
    QCOMPARE(recent.at(1).toObject().value(QStringLiteral("hash")).toString(), QStringLiteral("6"));

    // A milestone sits beside its completion, and 2.0 beside 1.0.
    recent = digest::pushRecent(recent, entry(QStringLiteral("4"), QStringLiteral("milestone"), 1));
    recent = digest::pushRecent(recent, entry(QStringLiteral("4"), QStringLiteral("milestone"), 2));
    QCOMPARE(recent.at(0).toObject().value(QStringLiteral("ratio")).toInt(), 2);
    QCOMPARE(recent.at(1).toObject().value(QStringLiteral("ratio")).toInt(), 1);
    QCOMPARE(recent.at(2).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("completed"));
}

QTEST_GUILESS_MAIN(TestStatusDigest)
#include "tst_statusdigest.moc"
