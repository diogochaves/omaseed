// #24 T8 (docs/22 §6): the weekly recap's numbers over a fixture ledger, its words, and when
// it is due.

#include "core/Format.hpp"
#include "core/SeedLedger.hpp"
#include "core/Torrent.hpp"
#include "core/WeeklyRecap.hpp"
#include "core/engines/qbt/QbtTranslate.hpp"

#include <QDateTime>
#include <QTest>

namespace Format = omaseed::core::Format;
using omaseed::core::SeedLedger;
using omaseed::core::Torrent;
using omaseed::core::WeeklyRecap;

namespace {

constexpr qint64 kMB = 1000LL * 1000;
constexpr qint64 kGB = 1000LL * kMB;

// Sunday 18 October 2026, the recap's evening; the week is Monday 12 to Sunday 18.
const QDate kSunday(2026, 10, 18);

qint64 at(const QDate &day, int hour = 12)
{
    return day.startOfDay().addSecs(hour * 3600).toSecsSinceEpoch();
}

Torrent seeding(const char *hash, qint64 uploaded, double ratio, int seedsInSwarm)
{
    Torrent t;
    t.hash = QString::fromLatin1(hash);
    t.name = t.hash;
    t.size = 10 * kGB;
    t.progress = 1.0;
    t.status = omaseed::core::QbtTranslate::status(u"stalledUP");
    t.uploaded = uploaded;
    t.downloaded = 10 * kGB;
    t.ratio = ratio;
    t.numComplete = seedsInSwarm;
    return t;
}

/// One poll a day at noon, each torrent giving what `perDay` says for that day (days counted
/// back from kSunday: 0 is Sunday itself). Totals start from `start` per torrent.
struct Fixture
{
    SeedLedger ledger;
    QHash<QString, qint64> totals;
    qint64 serverUp = 0;

    QList<SeedLedger::TorrentTotals> rows() const
    {
        QList<SeedLedger::TorrentTotals> out;
        for (auto it = totals.cbegin(); it != totals.cend(); ++it)
            out.append({it.key(), it.value(), 0, 1});
        return out;
    }

    void poll(const QDate &day, const QHash<QString, qint64> &gave)
    {
        // A poll with nothing new a few seconds before, so what the day gave is booked at
        // noon and not spread over the days since the last poll as a gap.
        ledger.observe(at(day) - 2, serverUp, 0, rows());
        for (auto it = gave.cbegin(); it != gave.cend(); ++it) {
            totals[it.key()] += it.value();
            serverUp += it.value();
        }
        ledger.observe(at(day), serverUp, 0, rows());
    }
};

} // namespace

class TestWeeklyRecap : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theWeekIsTheSevenDaysEndingToday()
    {
        Fixture f;
        f.totals = {{QStringLiteral("a"), 0}, {QStringLiteral("b"), 0}};
        // A baseline on the Sunday before, then one poll a day.
        f.poll(kSunday.addDays(-8), {});
        f.poll(kSunday.addDays(-7), {{QStringLiteral("a"), 50 * kGB}}); // last week: not counted
        f.poll(kSunday.addDays(-6), {{QStringLiteral("a"), 1 * kGB}});  // Monday
        f.poll(kSunday.addDays(-3), {{QStringLiteral("b"), 9 * kGB}});  // Thursday, the best
        f.poll(kSunday, {{QStringLiteral("a"), 2 * kGB}});              // Sunday

        const WeeklyRecap r = WeeklyRecap::compute(f.ledger, {}, kSunday, 0, 0, 2);
        QCOMPARE(r.from, QDate(2026, 10, 12));
        QCOMPARE(r.to, kSunday);
        QCOMPARE(r.given, 12 * kGB);
        QCOMPARE(r.bestDay, QDate(2026, 10, 15));
        QVERIFY(r.worthSending());
    }

    void aWeekThatGaveNothingIsNotSent()
    {
        Fixture f;
        f.totals = {{QStringLiteral("a"), 0}};
        f.poll(kSunday.addDays(-9), {});
        f.poll(kSunday.addDays(-8), {{QStringLiteral("a"), 5 * kGB}});
        f.poll(kSunday, {});
        const WeeklyRecap r = WeeklyRecap::compute(f.ledger, {}, kSunday, 0, 0, 2);
        QCOMPARE(r.given, 0);
        QVERIFY(!r.bestDay.isValid());
        QVERIFY(!r.worthSending());
    }

    void theStreakIsTheLedgers()
    {
        Fixture f;
        f.totals = {{QStringLiteral("a"), 0}};
        f.poll(kSunday.addDays(-12), {});
        for (int back = 11; back >= 0; --back)
            f.poll(kSunday.addDays(-back), {{QStringLiteral("a"), 100 * kMB}});
        const WeeklyRecap r = WeeklyRecap::compute(f.ledger, {}, kSunday, 0, 0, 2);
        QCOMPARE(r.streak, 12);
        QCOMPARE(r.streak, f.ledger.streak(kSunday));
    }

    void theRatioIsTheServersAllTime()
    {
        Fixture f;
        f.totals = {{QStringLiteral("a"), 0}};
        f.poll(kSunday.addDays(-1), {});
        f.poll(kSunday, {{QStringLiteral("a"), kGB}});
        QCOMPARE(WeeklyRecap::compute(f.ledger, {}, kSunday, 24 * kGB, 10 * kGB, 2).ratioText,
                 QStringLiteral("2.40"));
        QCOMPARE(WeeklyRecap::compute(f.ledger, {}, kSunday, 24 * kGB, 0, 2).ratioText,
                 QStringLiteral("∞"));
    }

    void rareAliveCountsWhatTheConsoleCalls_rare()
    {
        Fixture f;
        f.totals = {{QStringLiteral("a"), 0}};
        f.poll(kSunday.addDays(-1), {});
        f.poll(kSunday, {{QStringLiteral("a"), kGB}});

        const Torrent only = seeding("only", 0, 0.1, 1);
        const Torrent two = seeding("two", 0, 0.1, 2);
        const Torrent many = seeding("many", 0, 0.1, 40);
        const Torrent unknown = seeding("unknown", 0, 0.1, 0); // no count from the tracker
        Torrent stopped = seeding("stopped", 0, 0.1, 1);       // not seeding: not keeping alive
        stopped.status = omaseed::core::QbtTranslate::status(u"stoppedUP");
        Torrent broken = seeding("broken", 0, 0.1, 1); // a tracker error: its count is not trusted
        broken.hasTrackerError = true;
        Torrent fetching = seeding("fetching", 0, 0.1, 1); // incomplete
        fetching.progress = 0.5;
        fetching.status = omaseed::core::QbtTranslate::status(u"downloading");

        const QList<const Torrent *> lib{
            &only, &two, &many, &unknown, &stopped, &broken, &fetching};
        QCOMPARE(WeeklyRecap::compute(f.ledger, lib, kSunday, 0, 0, 2).rareAlive, 2);
        QCOMPARE(WeeklyRecap::compute(f.ledger, lib, kSunday, 0, 0, 1).rareAlive, 1);
    }

    void paidBackIsARatioThatCrossedOneThisWeek()
    {
        Fixture f;
        f.totals = {{QStringLiteral("crossed"), 8 * kGB},
                    {QStringLiteral("already"), 30 * kGB},
                    {QStringLiteral("short"), 2 * kGB},
                    {QStringLiteral("idle"), 12 * kGB}};
        f.poll(kSunday.addDays(-8), {});
        f.poll(kSunday.addDays(-2),
               {{QStringLiteral("crossed"), 4 * kGB},
                {QStringLiteral("already"), 5 * kGB},
                {QStringLiteral("short"), 3 * kGB}});
        f.poll(kSunday, {});

        // Each downloaded 10 GB: ratio = uploaded / 10 GB.
        const Torrent crossed = seeding("crossed", 12 * kGB, 1.2, 40); // 0.8 → 1.2
        const Torrent already = seeding("already", 35 * kGB, 3.5, 40); // 3.0 → 3.5
        const Torrent shortOf = seeding("short", 5 * kGB, 0.5, 40);    // 0.2 → 0.5
        const Torrent idle = seeding("idle", 12 * kGB, 1.2, 40);       // gave nothing this week
        const QList<const Torrent *> lib{&crossed, &already, &shortOf, &idle};
        QCOMPARE(WeeklyRecap::compute(f.ledger, lib, kSunday, 0, 0, 2).paidBack, 1);
    }

    void theNoticeSaysItInTwoLines()
    {
        WeeklyRecap r;
        r.from = QDate(2026, 10, 12);
        r.to = kSunday;
        r.given = 48'200 * kMB;
        r.ratioText = QStringLiteral("2.40");
        r.streak = 41;
        r.rareAlive = 7;
        r.paidBack = 3;
        r.bestDay = QDate(2026, 10, 15);
        QCOMPARE(r.summary(), QStringLiteral("Your week"));
        QCOMPARE(r.body(),
                 QStringLiteral("%1 given this week · ratio 2.40 · a 41-day streak\n"
                                "kept 7 rare torrents alive · 3 paid back · best day Thursday")
                     .arg(Format::size(r.given)));
    }

    void partsWithNothingToSayAreLeftOut()
    {
        WeeklyRecap r;
        r.from = QDate(2026, 10, 12);
        r.to = kSunday;
        r.given = 750 * kMB;
        r.ratioText = QStringLiteral("—");
        r.streak = 1;
        r.rareAlive = 1;
        r.bestDay = QDate(2026, 10, 18);
        const QString given = Format::size(r.given);
        QCOMPARE(r.body(),
                 QStringLiteral("%1 given this week\n"
                                "kept 1 rare torrent alive · best day Sunday")
                     .arg(given));
        r.rareAlive = 0;
        r.bestDay = {};
        QCOMPARE(r.body(), QStringLiteral("%1 given this week").arg(given));
    }

    void dueOnceOnSundayEvening_data()
    {
        QTest::addColumn<QDateTime>("now");
        QTest::addColumn<QDate>("lastSent");
        QTest::addColumn<bool>("due");
        const QTime evening(WeeklyRecap::eveningHour, 0);
        QTest::newRow("sunday 18:00") << QDateTime(kSunday, evening) << QDate() << true;
        QTest::newRow("sunday 23:59") << QDateTime(kSunday, QTime(23, 59)) << QDate() << true;
        QTest::newRow("sunday 17:59") << QDateTime(kSunday, QTime(17, 59)) << QDate() << false;
        QTest::newRow("sent today") << QDateTime(kSunday, QTime(20, 0)) << kSunday << false;
        QTest::newRow("sent last week")
            << QDateTime(kSunday, QTime(20, 0)) << kSunday.addDays(-7) << true;
        QTest::newRow("monday, missed")
            << QDateTime(kSunday.addDays(1), QTime(9, 0)) << QDate() << false;
        QTest::newRow("saturday") << QDateTime(kSunday.addDays(-1), QTime(20, 0)) << QDate()
                                  << false;
    }

    void dueOnceOnSundayEvening()
    {
        QFETCH(QDateTime, now);
        QFETCH(QDate, lastSent);
        QFETCH(bool, due);
        QCOMPARE(WeeklyRecap::due(now, lastSent), due);
    }
};

QTEST_GUILESS_MAIN(TestWeeklyRecap)
#include "tst_weeklyrecap.moc"
