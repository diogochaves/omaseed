#include "core/SeedLedger.hpp"

#include <QDateTime>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::SeedLedger;

namespace {

// A fixed local noon, so a few polls never cross midnight by accident.
qint64 noon(int daysAgo = 0)
{
    return QDate(2026, 10, 3).addDays(-daysAgo).startOfDay().addSecs(12 * 3600).toSecsSinceEpoch();
}

QList<SeedLedger::TorrentTotals> totals(std::initializer_list<std::tuple<const char *, qint64>> rows)
{
    QList<SeedLedger::TorrentTotals> out;
    for (const auto &[hash, up] : rows)
        out.append({QString::fromLatin1(hash), up, 0});
    return out;
}

qint64 sumUp(const QList<SeedLedger::Day> &days)
{
    qint64 s = 0;
    for (const auto &d : days)
        s += std::max<qint64>(0, d.up);
    return s;
}

} // namespace

class TestSeedLedger : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void firstObservationOnlySetsTheBaseline()
    {
        SeedLedger ledger;
        ledger.observe(noon(), 1000, 500, totals({{"a", 1000}}));
        QCOMPARE(ledger.start(), noon());
        const auto days = ledger.days(QDate(2026, 10, 3), QDate(2026, 10, 3));
        QCOMPARE(days.size(), 1);
        QCOMPARE(days.first().up, 0);
    }

    void pollsAddUpInEveryResolution()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 0, 0, totals({{"a", 0}, {"b", 0}}));
        ledger.observe(t + 2, 300, 10, totals({{"a", 200}, {"b", 100}}));
        ledger.observe(t + 4, 600, 10, totals({{"a", 500}, {"b", 100}}));

        QCOMPARE(ledger.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up, 600);
        const auto hours = ledger.series(t + 4, 3600, 1);
        QCOMPARE(hours.first().up * 3600, 600.0);
        QCOMPARE(hours.first().down * 3600, 10.0);
        QVERIFY(!hours.first().gap);
        const auto perTorrent = ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3));
        QCOMPARE(perTorrent.value(QStringLiteral("a")), 500);
        QCOMPARE(perTorrent.value(QStringLiteral("b")), 100);
    }

    void aStepBackwardsStartsANewBaseline()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 5000, 0, {});
        ledger.observe(t + 2, 100, 0, {}); // qBittorrent lost its statistics
        ledger.observe(t + 4, 160, 0, {});
        QCOMPARE(ledger.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up, 60);
    }

    void aReAddedTorrentIsNotCountedAsALoss()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 0, 0, totals({{"a", 900}}));
        ledger.observe(t + 2, 0, 0, {});                // removed
        ledger.observe(t + 4, 50, 0, totals({{"a", 0}})); // added again: a baseline only
        ledger.observe(t + 6, 80, 0, totals({{"a", 30}}));
        QCOMPARE(ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")), 30);
    }

    void aGapIsSpreadEvenlyAndMarked()
    {
        SeedLedger ledger;
        // Closed at 22:00 the day before, opened again at 07:00: 9 h, 900 bytes.
        const qint64 closed = QDate(2026, 10, 2).startOfDay().addSecs(22 * 3600).toSecsSinceEpoch();
        const qint64 opened = QDate(2026, 10, 3).startOfDay().addSecs(7 * 3600).toSecsSinceEpoch();
        ledger.observe(closed - 2, 0, 0, totals({{"a", 0}}));
        ledger.observe(closed, 0, 0, totals({{"a", 0}}));
        ledger.observe(opened, 900, 0, totals({{"a", 900}}));

        const auto days = ledger.days(QDate(2026, 10, 2), QDate(2026, 10, 3));
        QCOMPARE(days[0].up, 200); // 2 of the 9 hours
        QCOMPARE(days[1].up, 700);
        const auto perTorrent = ledger.torrentDays(QDate(2026, 10, 2), QDate(2026, 10, 3));
        QCOMPARE(perTorrent.value(QStringLiteral("a")), (QList<qint64>{200, 700}));

        const auto hours = ledger.series(opened, 3600, 10);
        int gapHours = 0;
        double total = 0;
        for (const auto &p : hours) {
            gapHours += p.gap ? 1 : 0;
            total += p.up * 3600;
        }
        QCOMPARE(gapHours, 9);
        QCOMPARE(qRound(total), 900);
    }

    void bucketsBeforeTheLedgerAreUnknown()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 0, 0, {});
        ledger.observe(t + 2, 10, 0, {});
        const auto hours = ledger.series(t + 2, 3600, 4);
        QCOMPARE(hours.size(), 4);
        QVERIFY(!hours[0].known);
        QVERIFY(!hours[2].known);
        QVERIFY(hours[3].known);
        const auto days = ledger.days(QDate(2026, 10, 1), QDate(2026, 10, 3));
        QCOMPARE(days[0].up, -1);
        QCOMPARE(days[2].up, 10);
    }

    void streaksCountDaysInARow()
    {
        SeedLedger ledger;
        qint64 up = 0;
        ledger.observe(noon(5), up, 0, {});
        for (int back = 5; back >= 0; --back) {
            ledger.observe(noon(back), up, 0, {});
            if (back != 3)
                up += 100; // day 3 ago gives nothing
            ledger.observe(noon(back) + 2, up, 0, {});
        }
        QCOMPARE(ledger.streak(QDate(2026, 10, 3)), 3);
        QCOMPARE(ledger.longestStreak(), 3);
    }

    void survivesAReopen()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("ledger/x.sqlite"));
        const qint64 t = noon();
        {
            SeedLedger ledger;
            QVERIFY(ledger.open(path));
            ledger.observe(t, 0, 0, totals({{"a", 0}}));
            ledger.observe(t + 2, 40, 0, totals({{"a", 40}}));
            ledger.close();
        }
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        QCOMPARE(ledger.start(), t);
        // The baseline came back too: the next poll counts only what is new.
        ledger.observe(t + 4, 100, 0, totals({{"a", 100}}));
        QCOMPARE(ledger.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up, 100);
        QCOMPARE(ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")), 100);
        QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadOther | QFileDevice::ReadGroup), QFileDevice::Permissions());
    }

    void speedsFeedTheLastHour()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        for (int i = 0; i < 90; ++i)
            ledger.sampleSpeed(t + i, 1000, 0);
        const auto recent = ledger.recentUpload(t + 89, 60);
        QCOMPARE(recent.size(), 60);
        QCOMPARE(recent.last(), 1000);
        const auto half = ledger.series(t + 89, 30, 4);
        QVERIFY(half.last().known);
        QCOMPARE(half.last().up, 1000.0);
    }

    void demoFillsAYearOnce()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 0, 0, {});
        ledger.fillDemo(t, {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
        const auto year = ledger.days(QDate(2026, 10, 3).addDays(-364), QDate(2026, 10, 3));
        QVERIFY(year.first().up >= 0);
        QVERIFY(sumUp(year) > 0);
        QVERIFY(ledger.streak(QDate(2026, 10, 3)) >= 30);
        const qint64 before = sumUp(year);
        ledger.fillDemo(t, {QStringLiteral("a")}); // already a year in: nothing
        QCOMPARE(sumUp(ledger.days(QDate(2026, 10, 3).addDays(-364), QDate(2026, 10, 3))), before);
    }
};

QTEST_GUILESS_MAIN(TestSeedLedger)
#include "tst_seedledger.moc"
