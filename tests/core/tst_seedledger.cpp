#include "core/SeedLedger.hpp"

#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFile>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>
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
        QCOMPARE(ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")), 1500);
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
        QCOMPARE(reopened.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")), 150);
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
        QCOMPARE(ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")), 330);

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
            ledger.observe(t, 0, 0, {{QStringLiteral("a"), 1000, 0, t - 86400}, {QStringLiteral("b"), 1000, 0, t - 86400}});
            ledger.observe(t + 2, 0, 0, {{QStringLiteral("a"), 1000, 0, t - 86400}, {QStringLiteral("b"), 1000, 0, t - 86400}});
            ledger.close();
        }
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        // Both were added again at t + 600: a has given 300 since, b 1500.
        ledger.observe(t + 3600, 1800, 0, {{QStringLiteral("a"), 300, 0, t + 600}, {QStringLiteral("b"), 1500, 0, t + 600}});
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
        QCOMPARE(ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")), 100);
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
        QCOMPARE(ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")), 250);
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
        QCOMPARE(ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("z")), 0);
    }

    // A ledger written before baselines kept added_on opens, gains the column, and counts on.
    void anOlderLedgerFileOpens()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("old.sqlite"));
        const qint64 t = noon();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE meta(key TEXT PRIMARY KEY, value INTEGER)")));
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE baselines(hash TEXT PRIMARY KEY, uploaded INTEGER, downloaded INTEGER)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO baselines VALUES('a', 100, 0)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO meta VALUES('start', %1), ('last_t', %1), ('last_up', 100), "
                                          "('last_down', 0)").arg(t)));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("old"));
        SeedLedger ledger;
        QVERIFY(ledger.open(path));
        ledger.observe(t + 2, 160, 0, {{QStringLiteral("a"), 160, 0, t - 86400}});
        QCOMPARE(ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")), 60);
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
        QCOMPARE(days[0].up, qRound64(1000.0 * double(midnight - closed) / double(opened - closed)));
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
            library.append({QStringLiteral("%1").arg(i, 40, 16, QLatin1Char('0')), 1'000'000'000, 0});
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
            library.insert(QStringLiteral("t%1").arg(i, 2, 10, QLatin1Char('0')), {i * 1000LL, 1000 + i});
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
        QCOMPARE(ledger.torrentTotals(QDate(2026, 10, 3), QDate(2026, 10, 3)).value(QStringLiteral("a")), 100);
        QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadOther | QFileDevice::ReadGroup), QFileDevice::Permissions());
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
