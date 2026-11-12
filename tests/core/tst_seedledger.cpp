#include "core/SeedLedger.hpp"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using omaseed::core::SeedLedger;

namespace {

// A fixed local noon, so a few polls never cross midnight by accident.
qint64 noon(int daysAgo = 0)
{
    return QDate(2026, 10, 3).addDays(-daysAgo).startOfDay().addSecs(12 * 3600).toSecsSinceEpoch();
}

QList<SeedLedger::TorrentTotals>
totals(std::initializer_list<std::tuple<const char *, qint64>> rows)
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

/// One statement on a ledger file, through a connection of its own.
void sql(const QString &path, const QString &statement)
{
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("tst-sql"));
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY2(q.exec(statement), qPrintable(statement));
    }
    QSqlDatabase::removeDatabase(QStringLiteral("tst-sql"));
}

/// Every row of a ledger file's table, in order, one string each.
QStringList rowsOf(const QString &path, const QString &table)
{
    QStringList out;
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("tst-rows"));
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery q(db);
            q.exec(QStringLiteral("SELECT * FROM %1 ORDER BY 1, 2").arg(table));
            while (q.next()) {
                QStringList cells;
                for (int i = 0; i < q.record().count(); ++i)
                    cells.append(q.value(i).toString());
                out.append(cells.join(QLatin1Char('|')));
            }
        }
    }
    QSqlDatabase::removeDatabase(QStringLiteral("tst-rows"));
    return out;
}

/// An empty ledger file with the given references (#256's repair must not go by them).
void makeLedger(const QString &path, qint64 lastUp, qint64 lastDown)
{
    {
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
    }
    sql(path,
        QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES('start', %1), ('last_t', "
                       "%2), ('last_up', %3), ('last_down', %4)")
            .arg(noon(30))
            .arg(noon() + 6 * 3600)
            .arg(lastUp)
            .arg(lastDown));
}

void addHour(const QString &path, qint64 t, qint64 up, qint64 down)
{
    sql(path, QStringLiteral("INSERT INTO hours VALUES(%1, %2, %3, 0)").arg(t).arg(up).arg(down));
}

/// Opens and closes the ledger: what the repair at open does to the file.
void openOnce(const QString &path)
{
    SeedLedger ledger;
    QVERIFY(ledger.open(path));
}

/// Bytes this process has handed to write() so far (/proc/self/io `wchar`), -1 if unknown.
qint64 bytesWritten()
{
    QFile io(QStringLiteral("/proc/self/io"));
    if (!io.open(QIODevice::ReadOnly))
        return -1;
    const QList<QByteArray> lines = io.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("wchar:"))
            return line.mid(6).trimmed().toLongLong();
    }
    return -1;
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

    // #256: a qBittorrent that has just started answers sync/maindata before its first
    // statistics refresh, with alltime_ul and alltime_dl at 0. That read is not a new
    // baseline: the next poll's real totals must not be booked whole.
    void aRestartingServersZeroTotalsAreNotABaseline()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        const qint64 allUp = 27'000'000'000'000;
        const qint64 allDown = 1'300'000'000'000;
        ledger.observe(t, allUp, allDown, totals({{"a", 1000}}));
        ledger.observe(t + 2, allUp + 500, allDown + 20, totals({{"a", 1500}}));
        // The server went away and came back: its first answer has no totals yet.
        ledger.observe(t + 60, 0, 0, {});
        ledger.observe(t + 62, allUp + 800, allDown + 30, totals({{"a", 1800}}));
        ledger.observe(t + 64, allUp + 900, allDown + 35, totals({{"a", 1900}}));
        const qint64 up = ledger.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up;
        QVERIFY2(up <= 900, qPrintable(QStringLiteral("booked %1 up").arg(up)));
    }

    // #256: the poll after a not-ready one books what moved since the last good one, in
    // total and per torrent, and as any span under gapSeconds: whole, in its own minute.
    void aNotReadyReadThenTheRealDifference()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        const QDate day(2026, 10, 3);
        ledger.observe(t, 5000, 700, totals({{"a", 1000}}));
        ledger.observe(t + 2, 5100, 700, totals({{"a", 1100}}));
        // Not ready, and its torrents read low too: nothing of this poll is taken.
        ledger.observe(t + 40, 0, 0, totals({{"a", 0}}));
        QVERIFY(ledger.wantsWholeList()); // what it skipped comes with the whole list
        ledger.observe(t + 42, 5400, 760, totals({{"a", 1400}}));
        QVERIFY(!ledger.wantsWholeList());
        QCOMPARE(ledger.totals().up, 100 + 300);
        QCOMPARE(ledger.totals().down, 60);
        QCOMPARE(ledger.torrentTotals(day, day).value(QStringLiteral("a")), 100 + 300);
        const auto minutes = ledger.series(t + 42, 60, 2);
        QCOMPARE(qint64(std::llround(minutes.last().up * 60)), 400);
        QVERIFY(!minutes.first().gap && !minutes.last().gap);
    }

    // #256: an outage longer than gapSeconds is a gap as any other, measured from the last
    // good poll: exact in total, spread over the span and marked.
    void aNotReadyReadAfterALongOutageIsAGap()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 5000, 0, {});
        ledger.observe(t + 2, 5100, 0, {});
        ledger.observe(t + 300, 0, 0, {});
        ledger.observe(t + 302, 5700, 0, {});
        QCOMPARE(ledger.totals().up, 100 + 600);
        const auto minutes = ledger.series(t + 302, 60, 6);
        int gaps = 0;
        for (const auto &p : minutes) {
            gaps += p.gap ? 1 : 0;
            QVERIFY(p.up * 60 <= 100 + 600 / 5.0 + 1); // spread, not in one minute
        }
        QVERIFY(gaps >= 5);
    }

    // #256: restarted twice before it was ready once.
    void twoNotReadyReadsInARow()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 5000, 300, {});
        ledger.observe(t + 2, 5100, 300, {});
        ledger.observe(t + 20, 0, 0, {});
        ledger.observe(t + 22, 0, 0, {});
        ledger.observe(t + 40, 0, 0, {});
        ledger.observe(t + 42, 5150, 310, {});
        ledger.observe(t + 44, 5160, 310, {});
        QCOMPARE(ledger.totals().up, 100 + 50 + 10);
        QCOMPARE(ledger.totals().down, 10);
    }

    // #256: the same through observeChanges(), and across a run: the reference a file kept
    // is not given up for a not-ready first poll.
    void aNotReadyReadOfChangesAndAfterAReopen()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("t.sqlite"));
        const qint64 t = noon();
        {
            SeedLedger ledger;
            QVERIFY(ledger.open(path));
            ledger.observe(t, 5000, 0, totals({{"a", 1000}}));
            ledger.observeChanges(t + 2, 5100, 0, totals({{"a", 1100}}), {});
            ledger.observeChanges(t + 4, 0, 0, totals({{"a", 0}}), {QStringLiteral("a")});
            QVERIFY(ledger.wantsWholeList());
            ledger.observe(t + 6, 5130, 0, totals({{"a", 1130}}));
            QCOMPARE(ledger.totals().up, 130);
        }
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        ledger.observe(t + 30, 0, 0, {});
        ledger.observe(t + 32, 5200, 0, totals({{"a", 1200}}));
        QCOMPARE(ledger.totals().up, 200);
        const QDate day(2026, 10, 3);
        QCOMPARE(ledger.torrentTotals(day, day).value(QStringLiteral("a")), 200);
    }

    // #256: 0 and 0 with nothing counted before is a first poll like any other, and a server
    // that stays at 0 is no outage.
    void aFirstPollOfZeroTotalsIsABaseline()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 0, 0, totals({{"a", 0}}));
        QVERIFY(!ledger.wantsWholeList());
        ledger.observe(t + 2, 0, 0, totals({{"a", 0}}));
        ledger.observe(t + 4, 70, 5, totals({{"a", 70}}));
        QCOMPARE(ledger.totals().up, 70);
        QCOMPARE(ledger.totals().down, 5);
    }

    // #256: one total at 0 and the other not is a step backwards, not a not-ready read.
    void oneZeroTotalStillStartsANewBaseline()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 5000, 800, {});
        ledger.observe(t + 2, 0, 20, {});
        ledger.observe(t + 4, 40, 25, {});
        QCOMPARE(ledger.totals().up, 40);
        QCOMPARE(ledger.totals().down, 5);
    }

    // #256, the backstop: what the span could carry at maxBytesPerSecond is booked, down to
    // the last byte; a short span is allowed what maxRateFloorSeconds could carry.
    void aDifferenceTheSpanCouldCarryIsBooked()
    {
        const qint64 t = noon();
        const qint64 floor = SeedLedger::maxBytesPerSecond * SeedLedger::maxRateFloorSeconds;
        {
            SeedLedger ledger;
            ledger.observe(t, 1000, 1000, {});
            ledger.observe(t + 2, 1000 + floor, 1000 + floor, {});
            QCOMPARE(ledger.totals().up, floor);
            QCOMPARE(ledger.totals().down, floor);
        }
        SeedLedger ledger;
        const qint64 span = 600;
        ledger.observe(t, 1000, 0, {});
        ledger.observe(t + span, 1000 + SeedLedger::maxBytesPerSecond * span, 0, {});
        QCOMPARE(ledger.totals().up, SeedLedger::maxBytesPerSecond * span);
    }

    // #256, the backstop: a byte more than the span could carry is no traffic. Nothing is
    // booked, the span is a gap, and the next poll counts from the new baseline. What each
    // torrent gave has its own baseline and still counts.
    void aDifferenceNoSpanCouldCarryIsAGap()
    {
        const qint64 t = noon();
        const QDate day(2026, 10, 3);
        const qint64 floor = SeedLedger::maxBytesPerSecond * SeedLedger::maxRateFloorSeconds;
        for (const bool download : {false, true}) {
            SeedLedger ledger;
            ledger.observe(t, 1000, 1000, totals({{"a", 100}}));
            ledger.observe(t + 2,
                           1000 + (download ? 10 : floor + 1),
                           1000 + (download ? floor + 1 : 10),
                           totals({{"a", 160}}));
            QCOMPARE(ledger.totals().up, 0);
            QCOMPARE(ledger.totals().down, 0);
            QCOMPARE(ledger.torrentTotals(day, day).value(QStringLiteral("a")), 60);
            QVERIFY(ledger.series(t + 2, 60, 1).first().gap);
            QVERIFY(ledger.series(t + 2, 3600, 1).first().gap);
            // The baseline is that poll's totals: the next one counts from them.
            ledger.observe(t + 4,
                           1000 + (download ? 10 : floor + 1) + 300,
                           1000 + (download ? floor + 1 : 10) + 20,
                           totals({{"a", 160}}));
            QCOMPARE(ledger.totals().up, 300);
            QCOMPARE(ledger.totals().down, 20);
        }
        // Over a longer span the bound grows with it.
        SeedLedger ledger;
        const qint64 span = 600;
        ledger.observe(t, 1000, 0, {});
        ledger.observe(t + span, 1001 + SeedLedger::maxBytesPerSecond * span, 0, {});
        QCOMPARE(ledger.totals().up, 0);
        ledger.observe(t + span + 2, 1001 + SeedLedger::maxBytesPerSecond * span + 45, 0, {});
        QCOMPARE(ledger.totals().up, 45);
    }

    // #256, the repair at open: the server's whole all-time total, booked twice into one
    // hour (55.4 TB, more than an hour could carry at maxBytesPerSecond). The hour takes its
    // neighbours' mean, the day gives up the difference, the minutes are mended on their own, and
    // nothing else in the file moves. A second open finds nothing.
    void anAllTimeSizedHourIsRepairedAtOpen()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("t.sqlite"));
        const QString copy = dir.filePath(QStringLiteral("older-copy.sqlite"));
        const qint64 h = noon();
        const qint64 allUp = 27'700'000'000'000;
        const qint64 allDown = 1'330'000'000'000;
        const qint64 GB = 1'000'000'000;
        makeLedger(path, allUp + 40 * GB, allDown + GB);
        addHour(path, h - 3600, 4 * GB, 200);
        addHour(path, h, 2 * allUp + 5 * GB, 2 * allDown + 300);
        addHour(path, h + 3600, 6 * GB, 600);
        sql(path,
            QStringLiteral("INSERT INTO minutes VALUES(%1, 70000000, 3, 0), (%2, %3, %4, 0), "
                           "(%5, %6, %7, 0), (%8, 90000000, 5, 0)")
                .arg(h)
                .arg(h + 120)
                .arg(allUp)
                .arg(allDown)
                .arg(h + 660)
                .arg(allUp + 1000)
                .arg(allDown + 10)
                .arg(h + 720));
        sql(path,
            QStringLiteral("INSERT INTO days VALUES('2026-10-02', 9000000000, 70, 0), "
                           "('2026-10-03', %1, %2, 0)")
                .arg(2 * allUp + 15 * GB)
                .arg(2 * allDown + 1100));
        sql(path,
            QStringLiteral("INSERT INTO torrent_days VALUES('2026-10-02', 'a', 8000000000), "
                           "('2026-10-03', 'a', 9000000000), ('2026-10-03', 'b', 5000000000)"));
        sql(path, QStringLiteral("INSERT INTO baselines VALUES('a', 1, 2, 3), ('b', 4, 5, 6)"));
        QVERIFY(QFile::copy(path, copy));
        const QStringList meta = rowsOf(path, QStringLiteral("meta"));
        const QStringList torrentDays = rowsOf(path, QStringLiteral("torrent_days"));
        const QStringList baselines = rowsOf(path, QStringLiteral("baselines"));

        QTest::ignoreMessage(QtInfoMsg,
                             QRegularExpression(QStringLiteral("repaired 1 hour.* 2 minute")));
        openOnce(path);
        const QStringList hours = rowsOf(path, QStringLiteral("hours"));
        QCOMPARE(hours,
                 (QStringList{QStringLiteral("%1|4000000000|200|0").arg(h - 3600),
                              QStringLiteral("%1|5000000000|400|1").arg(h),
                              QStringLiteral("%1|6000000000|600|0").arg(h + 3600)}));
        const QStringList days = rowsOf(path, QStringLiteral("days"));
        QCOMPARE(days,
                 (QStringList{QStringLiteral("2026-10-02|9000000000|70|0"),
                              QStringLiteral("2026-10-03|15000000000|1200|1")}));
        const QStringList minutes = rowsOf(path, QStringLiteral("minutes"));
        QCOMPARE(minutes,
                 (QStringList{QStringLiteral("%1|70000000|3|0").arg(h),
                              QStringLiteral("%1|80000000|4|1").arg(h + 120),
                              QStringLiteral("%1|80000000|4|1").arg(h + 660),
                              QStringLiteral("%1|90000000|5|0").arg(h + 720)}));
        QCOMPARE(rowsOf(path, QStringLiteral("meta")), meta);
        QCOMPARE(rowsOf(path, QStringLiteral("torrent_days")), torrentDays);
        QCOMPARE(rowsOf(path, QStringLiteral("baselines")), baselines);

        // Again: nothing to mend, nothing said, nothing moved.
        openOnce(path);
        QCOMPARE(rowsOf(path, QStringLiteral("hours")), hours);
        QCOMPARE(rowsOf(path, QStringLiteral("days")), days);
        QCOMPARE(rowsOf(path, QStringLiteral("minutes")), minutes);

        // A ledger put back from a copy made before the repair is mended the same way.
        QTest::ignoreMessage(QtInfoMsg,
                             QRegularExpression(QStringLiteral("repaired 1 hour.* 2 minute")));
        openOnce(copy);
        QCOMPARE(rowsOf(copy, QStringLiteral("hours")), hours);
        QCOMPARE(rowsOf(copy, QStringLiteral("days")), days);
        QCOMPARE(rowsOf(copy, QStringLiteral("minutes")), minutes);

        // And the ledger reads what the file now says.
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        QCOMPARE(ledger.totals().up, 24 * GB);
        QVERIFY(ledger.series(h, 3600, 1).first().gap);
    }

    // #256: the all-time total booked once (27.7 TB) is under what an hour could carry at
    // maxBytesPerSecond; ten times what the day's torrents gave tells it.
    void aSingleAllTimeSizedHourIsToldByItsDaysTorrents()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("t.sqlite"));
        const qint64 h = noon();
        const qint64 allUp = 27'700'000'000'000;
        const qint64 allDown = 1'330'000'000'000;
        const qint64 GB = 1'000'000'000;
        QVERIFY(allUp + 5 * GB < SeedLedger::maxBytesPerSecond * 3600);
        makeLedger(path, allUp + 40 * GB, allDown + GB);
        addHour(path, h - 3600, 4 * GB, 200);
        addHour(path, h, allUp + 5 * GB, allDown + 300);
        addHour(path, h + 3600, 6 * GB, 600);
        sql(path,
            QStringLiteral("INSERT INTO days VALUES('2026-10-03', %1, %2, 0)")
                .arg(allUp + 15 * GB)
                .arg(allDown + 1100));
        sql(path,
            QStringLiteral("INSERT INTO torrent_days VALUES('2026-10-03', 'a', 9000000000), "
                           "('2026-10-03', 'b', 5000000000)"));
        QTest::ignoreMessage(QtInfoMsg,
                             QRegularExpression(QStringLiteral("repaired 1 hour.* 0 minute")));
        openOnce(path);
        QCOMPARE(rowsOf(path, QStringLiteral("hours")),
                 (QStringList{QStringLiteral("%1|4000000000|200|0").arg(h - 3600),
                              QStringLiteral("%1|5000000000|400|1").arg(h),
                              QStringLiteral("%1|6000000000|600|0").arg(h + 3600)}));
        QCOMPARE(rowsOf(path, QStringLiteral("days")),
                 QStringList{QStringLiteral("2026-10-03|15000000000|1200|1")});
    }

    // #256: two wrong hours in a day, side by side: each takes the mean of the nearest sound
    // hours around the pair, and the day gives up both differences.
    void twoWrongHoursInADayAreRepaired()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("t.sqlite"));
        const qint64 h = noon();
        const qint64 most = SeedLedger::maxBytesPerSecond * 3600;
        makeLedger(path, 1'000'000, 50'000);
        addHour(path, h - 3600, 100, 10);
        addHour(path, h, most + 100, 30); // only its upload is over; both columns are mended
        addHour(path, h + 3600, 900, most + 20);
        addHour(path, h + 7200, 301, 21);
        sql(path,
            QStringLiteral("INSERT INTO days VALUES('2026-10-03', %1, %2, 0)")
                .arg(most + 1401)
                .arg(most + 81));
        QTest::ignoreMessage(QtInfoMsg,
                             QRegularExpression(QStringLiteral("repaired 2 hour.* 0 minute")));
        openOnce(path);
        QCOMPARE(rowsOf(path, QStringLiteral("hours")),
                 (QStringList{QStringLiteral("%1|100|10|0").arg(h - 3600),
                              QStringLiteral("%1|200|15|1").arg(h),
                              QStringLiteral("%1|200|15|1").arg(h + 3600),
                              QStringLiteral("%1|301|21|0").arg(h + 7200)}));
        QCOMPARE(rowsOf(path, QStringLiteral("days")),
                 QStringList{QStringLiteral("2026-10-03|801|61|1")});
    }

    // #256: a wrong hour with one sound neighbour takes its value; alone, 0. The day never
    // goes under 0.
    void aWrongHourAtAnEndIsRepairedFromItsOneNeighbour()
    {
        QTemporaryDir dir;
        const qint64 h = noon();
        const qint64 most = SeedLedger::maxBytesPerSecond * 3600;
        const QString first = dir.filePath(QStringLiteral("first.sqlite"));
        makeLedger(first, 1'000'000, 50'000);
        addHour(first, h, most + 1000, 30);
        addHour(first, h + 3600, 900, 20);
        sql(first,
            QStringLiteral("INSERT INTO days VALUES('2026-10-03', %1, 50, 0)").arg(most + 1900));
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("repaired 1 hour")));
        openOnce(first);
        QCOMPARE(rowsOf(first, QStringLiteral("hours")),
                 (QStringList{QStringLiteral("%1|900|20|1").arg(h),
                              QStringLiteral("%1|900|20|0").arg(h + 3600)}));
        QCOMPARE(rowsOf(first, QStringLiteral("days")),
                 QStringList{QStringLiteral("2026-10-03|1800|40|1")});

        const QString last = dir.filePath(QStringLiteral("last.sqlite"));
        makeLedger(last, 1'000'000, 50'000);
        addHour(last, h - 3600, 700, 20);
        addHour(last, h, 100, most + 60'000); // its download is over
        sql(last,
            QStringLiteral("INSERT INTO days VALUES('2026-10-03', 800, %1, 0)").arg(most + 60'020));
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("repaired 1 hour")));
        openOnce(last);
        QCOMPARE(rowsOf(last, QStringLiteral("hours")),
                 (QStringList{QStringLiteral("%1|700|20|0").arg(h - 3600),
                              QStringLiteral("%1|700|20|1").arg(h)}));
        // The day takes each column's own difference: 600 more up, the excess less down.
        QCOMPARE(rowsOf(last, QStringLiteral("days")),
                 QStringList{QStringLiteral("2026-10-03|1400|40|1")});

        const QString alone = dir.filePath(QStringLiteral("alone.sqlite"));
        makeLedger(alone, 1'000'000, 50'000);
        addHour(alone, h, 2 * most, 30);
        sql(alone, QStringLiteral("INSERT INTO days VALUES('2026-10-03', 1500000, 30, 0)"));
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("repaired 1 hour")));
        openOnce(alone);
        QCOMPARE(rowsOf(alone, QStringLiteral("hours")),
                 QStringList{QStringLiteral("%1|0|0|1").arg(h)});
        QCOMPARE(rowsOf(alone, QStringLiteral("days")),
                 QStringList{QStringLiteral("2026-10-03|0|0|1")});
    }

    // #256: a busy hour or minute that could be real is left alone, right at each rule; a
    // byte over is mended.
    void aBusyHourUnderEachRuleIsLeftAlone()
    {
        QTemporaryDir dir;
        const qint64 h = noon();
        const qint64 floor = SeedLedger::repairFloor;
        const qint64 hourMost = SeedLedger::maxBytesPerSecond * 3600;
        const qint64 minuteMost = SeedLedger::maxBytesPerSecond * 60;
        // (a): all that maxBytesPerSecond could carry, up and down, on a day with no
        // per-torrent rows; and (b): ten times the day's per-torrent uploads, or more than
        // that but only the floor.
        const QString path = dir.filePath(QStringLiteral("under.sqlite"));
        makeLedger(path, 1000, 7);
        addHour(path, h - 86400, hourMost, hourMost);
        addHour(path, h - 3600, 30 * floor, 1);
        addHour(path, h, floor, 1);
        addHour(path, h + 86400, floor, 1);
        sql(path,
            QStringLiteral("INSERT INTO torrent_days VALUES('2026-10-03', 'a', %1), "
                           "('2026-10-03', 'b', %2), ('2026-10-04', 'a', 5)")
                .arg(2 * floor)
                .arg(floor));
        sql(path,
            QStringLiteral("INSERT INTO minutes VALUES(%1, %2, %2, 0), (%3, %2, 1, 0), "
                           "(%4, %5, 1, 0)")
                .arg(h - 86400)
                .arg(minuteMost)
                .arg(h)
                .arg(h + 86400)
                .arg(floor));
        const QStringList hours = rowsOf(path, QStringLiteral("hours"));
        const QStringList minutes = rowsOf(path, QStringLiteral("minutes"));
        openOnce(path);
        QCOMPARE(rowsOf(path, QStringLiteral("hours")), hours);
        QCOMPARE(rowsOf(path, QStringLiteral("minutes")), minutes);

        // One byte over each, one at a time.
        const auto mended =
            [&](const QString &name, const QString &table, qint64 t, qint64 up, qint64 down) {
                const QString file = dir.filePath(name);
                QFile::copy(path, file);
                sql(file,
                    QStringLiteral("UPDATE %1 SET up = %2, down = %3 WHERE t = %4")
                        .arg(table)
                        .arg(up)
                        .arg(down)
                        .arg(t));
                QTest::ignoreMessage(
                    QtInfoMsg,
                    QRegularExpression(table == QLatin1String("hours")
                                           ? QStringLiteral("repaired 1 hour.* 0 minute")
                                           : QStringLiteral("repaired 0 hour.* 1 minute")));
                openOnce(file);
                return rowsOf(file, table) != (table == QLatin1String("hours") ? hours : minutes);
            };
        const QString hoursTable = QStringLiteral("hours");
        const QString minutesTable = QStringLiteral("minutes");
        QVERIFY(
            mended(QStringLiteral("a-up.sqlite"), hoursTable, h - 86400, hourMost + 1, hourMost));
        QVERIFY(
            mended(QStringLiteral("a-down.sqlite"), hoursTable, h - 86400, hourMost, hourMost + 1));
        QVERIFY(mended(QStringLiteral("b-factor.sqlite"), hoursTable, h - 3600, 30 * floor + 1, 1));
        QVERIFY(mended(QStringLiteral("b-floor.sqlite"), hoursTable, h + 86400, floor + 1, 1));
        QVERIFY(mended(QStringLiteral("m-up.sqlite"), minutesTable, h - 86400, minuteMost + 1, 1));
        QVERIFY(
            mended(QStringLiteral("m-down.sqlite"), minutesTable, h - 86400, 1, minuteMost + 1));
        QVERIFY(mended(QStringLiteral("m-floor.sqlite"), minutesTable, h + 86400, floor + 1, 1));
    }

    // #256: the totals the ledger last saw are no test. A server that lost its statistics,
    // or a session that began again (Deluge), has them far under what its real hours hold:
    // the repair leaves such a ledger as it is, byte for byte.
    void referencesUnderThePastAreNoReasonToRepair()
    {
        QTemporaryDir dir;
        const qint64 h = noon();
        const qint64 GB = 1'000'000'000;
        int n = 0;
        // Statistics lost (small references), a session just begun (0), none kept yet.
        for (const qint64 reference : {qint64(100), qint64(0), qint64(-1)}) {
            const QString path = dir.filePath(QStringLiteral("t%1.sqlite").arg(n++));
            makeLedger(path, reference, reference);
            addHour(path, h - 3600, 300 * GB, 40 * GB);
            addHour(path, h, 450 * GB, 2 * GB);
            addHour(path, h + 3600, 200 * GB, 0);
            sql(path,
                QStringLiteral("INSERT INTO minutes VALUES(%1, %2, 5, 0), (%3, %4, 7, 0)")
                    .arg(h)
                    .arg(7 * GB)
                    .arg(h + 60)
                    .arg(110 * GB));
            sql(path,
                QStringLiteral("INSERT INTO days VALUES('2026-10-03', %1, %2, 0)")
                    .arg(950 * GB)
                    .arg(42 * GB));
            sql(path,
                QStringLiteral("INSERT INTO torrent_days VALUES('2026-10-03', 'a', %1), "
                               "('2026-10-03', 'b', %2)")
                    .arg(600 * GB)
                    .arg(340 * GB));
            QStringList before;
            const QStringList tables{QStringLiteral("hours"),
                                     QStringLiteral("minutes"),
                                     QStringLiteral("days"),
                                     QStringLiteral("torrent_days"),
                                     QStringLiteral("meta")};
            for (const QString &table : tables)
                before += rowsOf(path, table);
            openOnce(path);
            QStringList after;
            for (const QString &table : tables)
                after += rowsOf(path, table);
            QCOMPARE(after, before);
        }
    }

    // #256: a day with no per-torrent rows is judged by what an hour could carry alone:
    // under it the hour stays, however far over the floor; over it, it is mended.
    void aDayWithoutTorrentRowsIsJudgedByTheBoundAlone()
    {
        QTemporaryDir dir;
        const qint64 h = noon();
        const qint64 most = SeedLedger::maxBytesPerSecond * 3600;
        const QString path = dir.filePath(QStringLiteral("t.sqlite"));
        makeLedger(path, 100, 5);
        addHour(path, h - 3600, 500, 1);
        addHour(path, h, most - 1, 1); // far over the floor, with nothing to compare it to
        addHour(path, h + 3600, 700, 3);
        const QStringList hours = rowsOf(path, QStringLiteral("hours"));
        openOnce(path);
        QCOMPARE(rowsOf(path, QStringLiteral("hours")), hours);

        sql(path, QStringLiteral("UPDATE hours SET up = %1 WHERE t = %2").arg(most + 1).arg(h));
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("repaired 1 hour")));
        openOnce(path);
        QCOMPARE(rowsOf(path, QStringLiteral("hours")),
                 (QStringList{QStringLiteral("%1|500|1|0").arg(h - 3600),
                              QStringLiteral("%1|600|2|1").arg(h),
                              QStringLiteral("%1|700|3|0").arg(h + 3600)}));
    }

    // C25 K34: everything booked since the ledger began, flushed or not; a session's totals
    // fed in place of all-time ones (Deluge) start afresh after a daemon restart.
    void totalsAreEverythingBookedSinceTheStart()
    {
        QTemporaryDir dir;
        SeedLedger ledger;
        QVERIFY(ledger.open(dir.filePath(QStringLiteral("t.sqlite"))));
        QCOMPARE(ledger.totals().up, 0);
        const qint64 t = noon(1);
        ledger.observe(t, 100, 40, {});
        ledger.observe(t + 2, 400, 90, {});
        ledger.flush(t + 2);
        ledger.observe(noon(), 400, 90, {});   // a day later, nothing moved
        ledger.observe(noon() + 2, 30, 5, {}); // the daemon restarted: a new baseline
        ledger.observe(noon() + 4, 80, 25, {});
        const SeedLedger::Totals booked = ledger.totals();
        QCOMPARE(booked.up, 300 + 50);
        QCOMPARE(booked.down, 50 + 20);
    }

    void aReAddedTorrentIsNotCountedAsALoss()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 0, 0, totals({{"a", 900}}));
        ledger.observe(t + 2, 0, 0, {});                  // removed
        ledger.observe(t + 4, 50, 0, totals({{"a", 0}})); // added again: a baseline only
        ledger.observe(t + 6, 80, 0, totals({{"a", 30}}));
        QCOMPARE(
            ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")),
            30);
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

    void aFlushKeepsOnlyTheBaselinesThatAreLeft()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.sqlite"));
        const qint64 t = noon();
        {
            SeedLedger ledger;
            QVERIFY(ledger.open(path));
            ledger.observe(t, 0, 0, totals({{"a", 10}, {"b", 20}, {"c", 30}}));
            ledger.flush(t);
            ledger.observe(t + 2, 5, 0, totals({{"a", 15}, {"c", 30}})); // b removed, a moved
            // b stays away past the day a missing torrent is kept.
            ledger.observe(t + 2 + SeedLedger::missingKept, 5, 0, totals({{"a", 15}, {"c", 30}}));
            ledger.close();
        }
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        // a counts from 15, c from 30; b is a stranger again and only takes a baseline.
        const qint64 later = t + 4 + SeedLedger::missingKept;
        ledger.observe(later, 30, 0, totals({{"a", 25}, {"b", 50}, {"c", 35}}));
        const QDate day = QDateTime::fromSecsSinceEpoch(later).date();
        const auto given = ledger.torrentTotals(day, day);
        QCOMPARE(given.value(QStringLiteral("a")), 10);
        QCOMPARE(given.value(QStringLiteral("b")), 0);
        QCOMPARE(given.value(QStringLiteral("c")), 5);
    }

    // omaseed dies between two flushes: what it had not written is counted again on the next
    // run, from the baselines of the last flush, as a gap.
    void aCrashLosesNoBytes()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("live.sqlite"));
        const QString crashed = dir.filePath(QStringLiteral("crashed.sqlite"));
        const qint64 t = noon();
        SeedLedger live;
        QVERIFY(live.open(path));
        live.observe(t, 0, 0, totals({{"a", 0}}));
        live.observe(t + 2, 100, 0, totals({{"a", 100}}));
        live.flush(t + 2);
        for (int i = 1; i <= 100; ++i)
            live.observe(t + 2 + 2 * i, 100 + 9 * i, 0, totals({{"a", 100 + 9 * i}}));
        // The process dies here: only what reached the file survives.
        for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
            if (QFile::exists(path + suffix))
                QVERIFY(QFile::copy(path + suffix, crashed + suffix));
        }

        SeedLedger ledger;
        QVERIFY(ledger.open(crashed));
        ledger.observe(t + 400, 1500, 0, totals({{"a", 1500}}));
        QCOMPARE(ledger.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up, 1500);
        QCOMPARE(
            ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")),
            1500);
        bool gap = false;
        for (const auto &p : ledger.series(t + 400, 60, 8))
            gap = gap || p.gap;
        QVERIFY(gap);
    }

    // A full disk (here: the connection made read-only) must cost nothing: the flush keeps
    // what it held, queries still see it, and the next flush that can write writes it once.
    void aFailedFlushKeepsTheBytes()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.sqlite"));
        const qint64 t = noon();
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        ledger.observe(t, 0, 0, totals({{"a", 0}}));
        ledger.observe(t + 2, 100, 0, totals({{"a", 100}}));
        QSqlQuery lock(QSqlDatabase::database(ledger.connectionName()));
        QVERIFY(lock.exec(QStringLiteral("PRAGMA query_only = 1")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("cannot write")));
        ledger.flush(t + 2);
        ledger.observe(t + 4, 150, 0, totals({{"a", 150}}));
        QCOMPARE(ledger.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up, 150);

        QVERIFY(lock.exec(QStringLiteral("PRAGMA query_only = 0")));
        ledger.flush(t + 4);
        ledger.close();
        SeedLedger reopened;
        QVERIFY(reopened.open(path));
        QCOMPARE(reopened.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up, 150);
        QCOMPARE(reopened.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3))
                     .value(QStringLiteral("a")),
                 150);
    }

    // Something that is not a ledger where the ledger should be: set aside, never lost, and a
    // new ledger begins.
    void aBrokenFileIsSetAside()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.sqlite"));
        {
            QFile junk(path);
            QVERIFY(junk.open(QIODevice::WriteOnly));
            junk.write(QByteArray(8192, 'x'));
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("not a database")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("not a ledger")));
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        const QStringList aside = QDir(dir.path()).entryList({QStringLiteral("x.sqlite.broken-*")});
        QCOMPARE(aside.size(), 1);
        QCOMPARE(QFileInfo(dir.filePath(aside.first())).size(), 8192);
        const qint64 t = noon();
        ledger.observe(t, 0, 0, {});
        ledger.observe(t + 2, 70, 0, {});
        ledger.close();
        SeedLedger reopened;
        QVERIFY(reopened.open(path));
        QCOMPARE(reopened.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up, 70);
    }

    // With no file at all, the minutes and hours still keep only what they keep on disk.
    void memoryOnlyForgetsOldMinutes()
    {
        SeedLedger ledger;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("cannot open")));
        QVERIFY(!ledger.open(QStringLiteral("/proc/omaseed-nowhere/x.sqlite")));
        const qint64 t = noon(3);
        qint64 up = 0;
        for (qint64 at = t; at <= noon(); at += 60) {
            ledger.observe(at, up, 0, {});
            up += 10;
        }
        // Three days of 10-minute buckets: the oldest day is past the minutes' 48 h.
        const auto points = ledger.series(noon(), 600, 3 * 144);
        // (The 144th bucket from the left starts exactly 48 h back, so it keeps its minutes.)
        double oldest = 0, newest = 0;
        for (int i = 0; i < 143; ++i) {
            oldest += points.at(i).up;
            newest += points.at(points.size() - 1 - i).up;
        }
        QCOMPARE(oldest, 0.0);
        QVERIFY(newest > 0);
        // The days keep everything.
        QCOMPARE(sumUp(ledger.days(QDate(2026, 9, 30), QDate(2026, 10, 3))), up - 10);
    }

    // The clock steps back an hour (a wrong clock put right, a timezone mix-up): every byte
    // is still counted, and flushes do not wait an hour for the clock to catch up.
    void aClockGoingBackLosesNothing()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.sqlite"));
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        const qint64 t = noon();
        ledger.observe(t, 0, 0, totals({{"a", 0}}));
        ledger.observe(t + 2, 100, 0, totals({{"a", 100}}));
        const qint64 back = t + 4 - 3600;
        ledger.observe(back, 130, 0, totals({{"a", 130}}));
        for (int i = 1; i <= 200; ++i)
            ledger.observe(back + 2 * i, 130 + i, 0, totals({{"a", 130 + i}}));
        QCOMPARE(ledger.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up, 330);
        QCOMPARE(
            ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")),
            330);

        // 400 s after the step, a flush has happened: a second reader sees the bytes.
        SeedLedger reader;
        QVERIFY(reader.open(path));
        QVERIFY(reader.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up >= 130);
    }

    // Removed and added again while omaseed was closed: everything the new copy gave is new,
    // whether it is below or above what the old copy had given.
    void aTorrentAddedAgainWhileAwayCountsWhatItGave()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.sqlite"));
        const qint64 t = noon();
        {
            SeedLedger ledger;
            QVERIFY(ledger.open(path));
            ledger.observe(t,
                           0,
                           0,
                           {{QStringLiteral("a"), 1000, 0, t - 86400},
                            {QStringLiteral("b"), 1000, 0, t - 86400}});
            ledger.observe(t + 2,
                           0,
                           0,
                           {{QStringLiteral("a"), 1000, 0, t - 86400},
                            {QStringLiteral("b"), 1000, 0, t - 86400}});
            ledger.close();
        }
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        // Both were added again at t + 600: a has given 300 since, b 1500.
        ledger.observe(
            t + 3600,
            1800,
            0,
            {{QStringLiteral("a"), 300, 0, t + 600}, {QStringLiteral("b"), 1500, 0, t + 600}});
        const auto given = ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3));
        QCOMPARE(given.value(QStringLiteral("a")), 300);
        QCOMPARE(given.value(QStringLiteral("b")), 1500);
    }

    // qBittorrent lists a torrent late (while it starts, say): when it shows again, what it
    // gave meanwhile is still its own.
    void aTorrentMissingForAMomentKeepsItsBaseline()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 0, 0, totals({{"a", 0}, {"b", 0}}));
        ledger.observe(t + 2, 0, 0, totals({{"b", 0}}));
        ledger.observe(t + 4, 100, 0, totals({{"a", 100}, {"b", 0}}));
        QCOMPARE(
            ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")),
            100);
    }

    // qBittorrent crashes and comes back with the totals it last saved, below what omaseed
    // already counted. Those bytes stay counted once; what it gives after is counted too.
    void aQBittorrentCrashNeitherLosesNorDoubles()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        ledger.observe(t, 1000, 0, totals({{"a", 1000}}));
        ledger.observe(t + 2, 1200, 0, totals({{"a", 1200}}));  // 200 given
        ledger.observe(t + 30, 1100, 0, totals({{"a", 1100}})); // back from a crash, 100 lost
        ledger.observe(t + 32, 1150, 0, totals({{"a", 1150}})); // 50 more, really given
        QCOMPARE(ledger.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up, 250);
        QCOMPARE(
            ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")),
            250);
    }

    // Each server has its own file: a switch neither mixes them nor counts one's totals as
    // the other's progress.
    void aServerSwitchKeepsLedgersApart()
    {
        QTemporaryDir dir;
        const QString home = dir.filePath(QStringLiteral("home.sqlite"));
        const QString box = dir.filePath(QStringLiteral("box.sqlite"));
        const qint64 t = noon();
        SeedLedger ledger;
        QVERIFY(ledger.open(home));
        ledger.observe(t, 1000, 0, totals({{"a", 1000}}));
        ledger.observe(t + 2, 1100, 0, totals({{"a", 1100}}));
        QVERIFY(ledger.open(box)); // the switch
        ledger.observe(t + 4, 9'000'000, 0, totals({{"z", 9'000'000}}));
        ledger.observe(t + 6, 9'000'040, 0, totals({{"z", 9'000'040}}));
        QCOMPARE(ledger.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up, 40);
        QVERIFY(ledger.open(home)); // and back, some time later
        ledger.observe(t + 600, 1300, 0, totals({{"a", 1300}}));
        QCOMPARE(ledger.days(QDate(2026, 10, 3), QDate(2026, 10, 3)).first().up, 300);
        QCOMPARE(
            ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("z")),
            0);
    }

    // A ledger written before baselines kept added_on opens, gains the column, and counts on.
    void anOlderLedgerFileOpens()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("old.sqlite"));
        const qint64 t = noon();
        {
            QSqlDatabase db =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(
                q.exec(QStringLiteral("CREATE TABLE meta(key TEXT PRIMARY KEY, value INTEGER)")));
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE baselines(hash TEXT PRIMARY KEY, uploaded "
                                          "INTEGER, downloaded INTEGER)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO baselines VALUES('a', 100, 0)")));
            QVERIFY(q.exec(
                QStringLiteral(
                    "INSERT INTO meta VALUES('start', %1), ('last_t', %1), ('last_up', 100), "
                    "('last_down', 0)")
                    .arg(t)));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("old"));
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        ledger.observe(t + 2, 160, 0, {{QStringLiteral("a"), 160, 0, t - 86400}});
        QCOMPARE(
            ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")),
            60);
    }

    // A night the clocks change (ctest runs this binary in Europe/Lisbon): a gap across it
    // keeps every byte and splits it by the real seconds on each side of midnight.
    void aDaylightSavingNightKeepsEveryByte()
    {
        const QTimeZone zone = QTimeZone::systemTimeZone();
        const auto change = zone.nextTransition(QDateTime(QDate(2026, 7, 1), QTime(0, 0)));
        if (!change.atUtc.isValid() || change.atUtc.date().year() != 2026)
            QSKIP("this time zone has no clock change in the second half of 2026");
        const QDate day = change.atUtc.toLocalTime().date();
        const qint64 closed = day.addDays(-1).startOfDay().addSecs(22 * 3600).toSecsSinceEpoch();
        const qint64 midnight = day.startOfDay().toSecsSinceEpoch();
        const qint64 opened = QDateTime(day, QTime(7, 0)).toSecsSinceEpoch();
        SeedLedger ledger;
        ledger.observe(closed - 2, 0, 0, {});
        ledger.observe(closed, 0, 0, {});
        ledger.observe(opened, 1000, 0, {});
        const auto days = ledger.days(day.addDays(-1), day);
        QCOMPARE(days[0].up + days[1].up, 1000);
        QCOMPARE(days[0].up,
                 qRound64(1000.0 * double(midnight - closed) / double(opened - closed)));
    }

    // An hour of a big library at the window's poll rate: 1,000 torrents, 50 of them
    // uploading, a poll every 1.5 s. What goes to disk is what the flushes write, so the
    // bound is on bytes handed to write() over the hour.
    void anHourOfABigLibraryWritesLittle()
    {
        QTemporaryDir dir;
        SeedLedger ledger;
        QVERIFY(ledger.open(dir.filePath(QStringLiteral("big.sqlite"))));
        QList<SeedLedger::TorrentTotals> library;
        for (int i = 0; i < 1000; ++i)
            library.append(
                {QStringLiteral("%1").arg(i, 40, 16, QLatin1Char('0')), 1'000'000'000, 0});
        const qint64 t = noon();
        qint64 server = 1'000'000'000'000;
        ledger.observe(t, server, 0, library);
        ledger.flush(t);

        const qint64 before = bytesWritten();
        QElapsedTimer cpu;
        cpu.start();
        for (int poll = 1; poll <= 2400; ++poll) {
            for (int i = 0; i < 50; ++i)
                library[i * 20].uploaded += 30'000;
            server += 50 * 30'000;
            ledger.observe(t + poll * 3 / 2, server, 0, library);
        }
        const qint64 written = bytesWritten() - before;
        const qint64 ms = cpu.elapsed();
        qInfo("an hour, 1000 torrents: %lld KB written, %lld ms of work", written / 1024, ms);
        if (before < 0)
            QSKIP("no /proc/self/io here");
        QVERIFY2(written < 2LL * 1024 * 1024, qPrintable(QString::number(written)));
    }

    // #36: fed only each poll's diff, the ledger books exactly what the whole list books:
    // the same polls, through both, give the same days, the same per-torrent totals, and the
    // same baselines on disk (a reopen counts the next poll the same way). The library moves,
    // loses torrents (back within the grace and after it), and gets one added again.
    void aPollsDiffBooksWhatTheWholeListBooks()
    {
        QTemporaryDir dir;
        SeedLedger whole;
        SeedLedger diff;
        QVERIFY(whole.open(dir.filePath(QStringLiteral("whole.sqlite"))));
        QVERIFY(diff.open(dir.filePath(QStringLiteral("diff.sqlite"))));
        QVERIFY(diff.wantsWholeList());

        struct Row
        {
            qint64 up = 0;
            qint64 added = 0;
        };
        QMap<QString, Row> library;
        for (int i = 0; i < 24; ++i)
            library.insert(QStringLiteral("t%1").arg(i, 2, 10, QLatin1Char('0')),
                           {i * 1000LL, 1000 + i});
        const auto listOf = [](const QMap<QString, Row> &rows) {
            QList<SeedLedger::TorrentTotals> out;
            for (auto it = rows.cbegin(); it != rows.cend(); ++it)
                out.append({it.key(), it->up, 0, it->added});
            return out;
        };

        qint64 t = noon(2);
        qint64 server = 1'000'000;
        whole.observe(t, server, 0, listOf(library));
        diff.observe(t, server, 0, listOf(library));
        QVERIFY(!diff.wantsWholeList());

        quint32 seed = 36;
        const auto next = [&seed](quint32 below) {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 8) % below;
        };
        QMap<QString, Row> away; // removed, to come back
        for (int poll = 1; poll <= 600; ++poll) {
            // Most polls are idle; the gaps cross the grace a few times.
            t += poll % 150 == 0 ? SeedLedger::missingKept / 2 + 7 : 2;
            QMap<QString, Row> now = library;
            if (next(3) == 0) {
                for (int k = 0; k < 3; ++k) {
                    auto it = std::next(now.begin(), next(quint32(now.size())));
                    const qint64 given = 100 + next(5000);
                    it->up += given;
                    server += given;
                }
            }
            if (next(40) == 0 && now.size() > 4) {
                auto it = std::next(now.begin(), next(quint32(now.size())));
                away.insert(it.key(), *it);
                now.erase(it);
            }
            if (next(30) == 0 && !away.isEmpty()) {
                auto it = away.begin();
                Row back = *it;
                if (next(2) == 0) { // added again: a new date, totals from nothing
                    back.added += 500;
                    back.up = next(300);
                }
                now.insert(it.key(), back);
                away.erase(it);
            }
            if (next(60) == 0)
                server += 77; // the server moves without a torrent (a deleted one's last bytes)

            QList<SeedLedger::TorrentTotals> changed;
            QStringList removed;
            for (auto it = now.cbegin(); it != now.cend(); ++it) {
                const auto before = library.constFind(it.key());
                if (before == library.cend() || before->up != it->up || before->added != it->added)
                    changed.append({it.key(), it->up, 0, it->added});
            }
            for (auto it = library.cbegin(); it != library.cend(); ++it) {
                if (!now.contains(it.key()))
                    removed.append(it.key());
            }
            library = now;
            whole.observe(t, server, 0, listOf(library));
            diff.observeChanges(t, server, 0, changed, removed);
        }

        const QDate from = QDateTime::fromSecsSinceEpoch(noon(2)).date();
        const QDate to = QDateTime::fromSecsSinceEpoch(t).date();
        QCOMPARE(sumUp(diff.days(from, to)), sumUp(whole.days(from, to)));
        QCOMPARE(diff.torrentTotals(from, to), whole.torrentTotals(from, to));
        QVERIFY(sumUp(whole.days(from, to)) > 0);

        // The baselines each wrote: a reopen counts the next poll the same way.
        whole.close();
        diff.close();
        QVERIFY(whole.open(dir.filePath(QStringLiteral("whole.sqlite"))));
        QVERIFY(diff.open(dir.filePath(QStringLiteral("diff.sqlite"))));
        for (auto it = library.begin(); it != library.end(); ++it)
            it->up += 10;
        for (auto it = away.cbegin(); it != away.cend(); ++it)
            library.insert(it.key(), {it->up + 10, it->added});
        t += 4;
        whole.observe(t, server + 1000, 0, listOf(library));
        diff.observe(t, server + 1000, 0, listOf(library));
        const QDate last = QDateTime::fromSecsSinceEpoch(t).date();
        QCOMPARE(diff.torrentTotals(last, last), whole.torrentTotals(last, last));
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
        QCOMPARE(
            ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")),
            100);
        QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadOther | QFileDevice::ReadGroup),
                 QFileDevice::Permissions());
    }

    void speedsFeedTheLastHour()
    {
        SeedLedger ledger;
        const qint64 t = noon();
        for (int i = 0; i < 90; ++i)
            ledger.sampleSpeed(t + i, 1000, 300);
        const auto recent = ledger.recentUpload(t + 89, 60);
        QCOMPARE(recent.size(), 60);
        QCOMPARE(recent.last(), 1000);
        QCOMPARE(ledger.recentDownload(t + 89, 60).last(), 300);
        const auto half = ledger.series(t + 89, 30, 4);
        QVERIFY(half.last().known);
        QCOMPARE(half.last().up, 1000.0);
    }

    // #47: the widget asks for today's per-torrent upload every busy poll. dayTotals() gives
    // what torrentTotals(day, day) would, through polls, flushes, a gap over midnight and a
    // reopen, without reading the file again once it has the day.
    void dayTotalsFollowThePolls()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("day.sqlite"));
        const QDate today(2026, 10, 3);
        const QDate tomorrow = today.addDays(1);
        const qint64 t = noon();
        const auto given = [](const QHash<QString, qint64> &totals) {
            QHash<QString, qint64> out;
            for (auto it = totals.cbegin(); it != totals.cend(); ++it) {
                if (it.value() != 0)
                    out.insert(it.key(), it.value());
            }
            return out;
        };
        const auto agrees = [&given](const SeedLedger &ledger, const QDate &day) {
            return given(ledger.dayTotals(day)) == given(ledger.torrentTotals(day, day));
        };

        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        ledger.observe(t, 0, 0, totals({{"a", 0}, {"b", 0}}));
        QVERIFY(ledger.dayTotals(today).isEmpty());
        quint64 revision = ledger.revision();
        ledger.observe(t + 2, 300, 0, totals({{"a", 200}, {"b", 100}}));
        QVERIFY(ledger.revision() > revision);
        QCOMPARE(ledger.dayTotals(today).value(QStringLiteral("a")), 200);
        QVERIFY(agrees(ledger, today));

        // An idle poll books nothing and leaves the revision alone.
        revision = ledger.revision();
        ledger.observe(t + 4, 300, 0, totals({{"a", 200}, {"b", 100}}));
        QCOMPARE(ledger.revision(), revision);

        ledger.flush(t + 4);
        ledger.observeChanges(t + 6, 350, 0, totals({{"b", 150}}), {});
        QCOMPARE(ledger.dayTotals(today).value(QStringLiteral("b")), 150);
        QVERIFY(agrees(ledger, today));

        // Kept in memory: a row written behind its back is not seen until the day is read again.
        QSqlQuery behind(QSqlDatabase::database(ledger.connectionName()));
        QVERIFY(behind.exec(QStringLiteral(
            "INSERT INTO torrent_days(day, hash, up) VALUES('2026-10-03', 'z', 7)")));
        QVERIFY(!ledger.dayTotals(today).contains(QStringLiteral("z")));
        QCOMPARE(ledger.dayTotals(tomorrow).size(), 0);
        QCOMPARE(ledger.dayTotals(today).value(QStringLiteral("z")), 7);

        // A gap over midnight: spread over both days, each kept day booked its share.
        const qint64 lateNight = today.startOfDay().addSecs(23 * 3600 + 59 * 60).toSecsSinceEpoch();
        ledger.observe(lateNight, 350, 0, totals({{"a", 200}, {"b", 150}}));
        const QHash<QString, qint64> before = ledger.dayTotals(today);
        ledger.observe(lateNight + 240, 590, 0, totals({{"a", 440}, {"b", 150}}));
        QVERIFY(ledger.dayTotals(today).value(QStringLiteral("a")) >
                before.value(QStringLiteral("a")));
        QVERIFY(agrees(ledger, today));
        QVERIFY(agrees(ledger, tomorrow));
        QCOMPARE(ledger.dayTotals(today).value(QStringLiteral("a")) +
                     ledger.dayTotals(tomorrow).value(QStringLiteral("a")),
                 440);
        ledger.close();
        QVERIFY(ledger.dayTotals(tomorrow).isEmpty()); // closed: nothing but what is buffered

        SeedLedger reopened;
        QVERIFY(reopened.open(path));
        QVERIFY(agrees(reopened, today));
        QCOMPARE(reopened.dayTotals(tomorrow).value(QStringLiteral("a")),
                 reopened.torrentTotals(tomorrow, tomorrow).value(QStringLiteral("a")));
        QVERIFY(reopened.dayTotals(tomorrow).value(QStringLiteral("a")) > 0);
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
