#include "core/AltSchedule.hpp"

#include <QTest>
#include <QTimeZone>

using namespace omaseed::core;

namespace {

// 2026-10-15 is a Thursday, the day docs/31 §2's checks ran.
QDateTime at(int day, int hour, int minute, int second = 0)
{
    return QDateTime(QDate(2026, 10, day), QTime(hour, minute, second), QTimeZone::UTC);
}

constexpr int H = 60;
const AltSchedule nights(true, 23 * H, 7 * H, AltSchedule::EveryDay);

} // namespace

// docs/31 §2: qBittorrent's scheduler rule, reproduced quirk included, and the "until" built
// on it. Fixed times stand in for T1–T7 from the playground.
class TestAltSchedule : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fromPreferences()
    {
        const AltSchedule schedule = AltSchedule::fromPreferences({
            {QStringLiteral("scheduler_enabled"), true},
            {QStringLiteral("schedule_from_hour"), 23},
            {QStringLiteral("schedule_from_min"), 30},
            {QStringLiteral("schedule_to_hour"), 7},
            {QStringLiteral("schedule_to_min"), 0},
            {QStringLiteral("scheduler_days"), 2},
        });
        QVERIFY(schedule.known());
        QVERIFY(schedule.enabled());
        QCOMPARE(schedule.fromMinute(), 23 * H + 30);
        QCOMPARE(schedule.toMinute(), 7 * H);
        QCOMPARE(schedule.days(), int(AltSchedule::Weekends));
        QVERIFY(schedule.overnight());
        QVERIFY(schedule.backwards());

        // Not read yet: nothing is known, nothing is on.
        const AltSchedule unknown = AltSchedule::fromPreferences({});
        QVERIFY(!unknown.known());
        QVERIFY(!unknown.onAt(at(15, 1, 0)));
        QVERIFY(!unknown.nextChange(at(15, 1, 0), false).isValid());
    }

    void onAt_data()
    {
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<int>("days");
        QTest::addColumn<QDateTime>("when");
        QTest::addColumn<bool>("on");

        // A window inside one day.
        QTest::newRow("08-18 inside") << 8 * H << 18 * H << 0 << at(15, 9, 40) << true;
        QTest::newRow("08-18 at its start") << 8 * H << 18 * H << 0 << at(15, 8, 0) << true;
        QTest::newRow("08-18 at its end") << 8 * H << 18 * H << 0 << at(15, 18, 0) << false;
        QTest::newRow("08-18 before") << 8 * H << 18 * H << 0 << at(15, 7, 59) << false;
        // Past midnight, every day.
        QTest::newRow("T1/H1: 23-07 at 01:20") << 23 * H << 7 * H << 0 << at(15, 1, 20) << true;
        QTest::newRow("T4b: 23-07 every day, Thursday 09:40")
            << 23 * H << 7 * H << 0 << at(15, 9, 40) << false;
        QTest::newRow("23-07 at 23:00") << 23 * H << 7 * H << 0 << at(15, 23, 0) << true;
        QTest::newRow("23-07 at 07:00") << 23 * H << 7 * H << 0 << at(15, 7, 0) << false;
        // The days read backwards past midnight (diogochaves/upstream#84).
        QTest::newRow("T4d: weekdays, Thursday 09:40 is off")
            << 23 * H << 7 * H << 1 << at(15, 9, 40) << false;
        QTest::newRow("weekdays: Saturday all day")
            << 23 * H << 7 * H << 1 << at(17, 12, 0) << true;
        QTest::newRow("T4e: weekends, Thursday 09:40 is on")
            << 23 * H << 7 * H << 2 << at(15, 9, 40) << true;
        QTest::newRow("weekends: Saturday 12:00 is off")
            << 23 * H << 7 * H << 2 << at(17, 12, 0) << false;
        QTest::newRow("weekends: Saturday night is on")
            << 23 * H << 7 * H << 2 << at(17, 23, 30) << true;
        QTest::newRow("T4: Monday, Thursday 09:40 is on")
            << 23 * H << 7 * H << 3 << at(15, 9, 40) << true;
        QTest::newRow("Monday: Monday 12:00 is off")
            << 23 * H << 7 * H << 3 << at(19, 12, 0) << false;
        // Inside one day the days read as they say.
        QTest::newRow("T4c: 08-10 Monday, Thursday is off")
            << 8 * H << 10 * H << 3 << at(15, 9, 0) << false;
        QTest::newRow("08-10 Monday, Monday 09:00") << 8 * H << 10 * H << 3 << at(19, 9, 0) << true;
        QTest::newRow("08-18 weekends, Sunday 10:00")
            << 8 * H << 18 * H << 2 << at(18, 10, 0) << true;
        QTest::newRow("08-18 weekdays, Sunday 10:00")
            << 8 * H << 18 * H << 1 << at(18, 10, 0) << false;
        // From and to the same: never.
        QTest::newRow("07-07 at 07:00") << 7 * H << 7 * H << 0 << at(15, 7, 0) << false;
        QTest::newRow("07-07 at noon") << 7 * H << 7 * H << 0 << at(15, 12, 0) << false;
        // Midnight as an end, and as a start.
        QTest::newRow("22-00 at 23:59") << 22 * H << 0 << 0 << at(15, 23, 59) << true;
        QTest::newRow("22-00 at 00:00") << 22 * H << 0 << 0 << at(16, 0, 0) << false;
        QTest::newRow("00-06 at 00:00") << 0 << 6 * H << 0 << at(16, 0, 0) << true;
    }

    void onAt()
    {
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(int, days);
        QFETCH(QDateTime, when);
        QFETCH(bool, on);
        QCOMPARE(AltSchedule(true, from, to, days).onAt(when), on);
    }

    void nextChange()
    {
        // From the schedule's own state: its next change.
        QCOMPARE(nights.nextChange(at(15, 1, 20), true), at(15, 7, 0));
        QCOMPARE(nights.nextChange(at(15, 15, 0), false), at(15, 23, 0));
        // From a hand toggle: the change after the one that agrees with it (§2).
        QCOMPARE(nights.nextChange(at(15, 15, 0), true), at(16, 7, 0));
        QCOMPARE(nights.nextChange(at(15, 1, 0), false), at(15, 23, 0));
        // Midnight.
        const AltSchedule lateEvening(true, 22 * H, 0, AltSchedule::EveryDay);
        QCOMPARE(lateEvening.nextChange(at(15, 21, 0), false), at(15, 22, 0));
        QCOMPARE(lateEvening.nextChange(at(15, 23, 0), true), at(16, 0, 0));
        // Never on: nothing ever changes.
        QVERIFY(!AltSchedule(true, 7 * H, 7 * H, 0).nextChange(at(15, 9, 0), false).isValid());
        // Seconds into a minute: the next whole minute is the first looked at.
        QCOMPARE(nights.nextChange(at(15, 22, 59, 30), false), at(15, 23, 0));
    }

    // A weekend ending on Sunday, and a window past midnight on weekends (backwards).
    void weekendEnd()
    {
        const AltSchedule weekendDays(true, 8 * H, 18 * H, AltSchedule::Weekends);
        QCOMPARE(weekendDays.nextChange(at(18, 10, 0), true), at(18, 18, 0));
        QCOMPARE(weekendDays.nextChange(at(18, 20, 0), false), at(24, 8, 0));
        QCOMPARE(AltSchedule::untilText(at(24, 8, 0), at(18, 20, 0)), QStringLiteral("Sat 08:00"));

        // 23:00–07:00 "weekends": on Monday to Friday all day, so Sunday 23:30 lasts until
        // the next Saturday's daytime.
        const AltSchedule weekendNights(true, 23 * H, 7 * H, AltSchedule::Weekends);
        QVERIFY(weekendNights.onAt(at(18, 23, 30)));
        QCOMPARE(weekendNights.nextChange(at(18, 23, 30), true), at(24, 7, 0));
    }

    void daylightSaving()
    {
        const QTimeZone berlin("Europe/Berlin");
        if (!berlin.isValid())
            QSKIP("no Europe/Berlin time zone here");
        // Clocks go back at 03:00 on 2026-10-25: the night is an hour longer, and the end is
        // still 07:00 on the clock.
        const QDateTime evening(QDate(2026, 10, 24), QTime(23, 30), berlin);
        const QDateTime end = nights.nextChange(evening, true);
        QCOMPARE(end.date(), QDate(2026, 10, 25));
        QCOMPARE(end.time(), QTime(7, 0));
        QCOMPARE(evening.secsTo(end), qint64(7 * 3600 + 30 * 60 + 3600));
        // Clocks go forward at 02:00 on 2026-03-29: 02:30 never happens, so a window that
        // starts then starts at 03:00.
        const AltSchedule early(true, 2 * H + 30, 4 * H, AltSchedule::EveryDay);
        const QDateTime night(QDate(2026, 3, 29), QTime(1, 0), berlin);
        const QDateTime start = early.nextChange(night, false);
        QCOMPARE(start.date(), QDate(2026, 3, 29));
        QCOMPARE(start.time(), QTime(3, 0));
    }

    void read()
    {
        // By the schedule: H1, 01:20.
        AltSchedule::Reading reading = nights.read(true, at(15, 1, 20));
        QVERIFY(!reading.byHand);
        QCOMPARE(reading.until, at(15, 7, 0));
        // H3: burst by hand at 15:00 lasts until 07:00 tomorrow, not 23:00.
        reading = nights.read(true, at(15, 15, 0));
        QVERIFY(reading.byHand);
        QCOMPARE(reading.until, at(16, 7, 0));
        // T3a: off by hand at 01:00 stays off until 23:00.
        reading = nights.read(false, at(15, 1, 0));
        QVERIFY(reading.byHand);
        QCOMPARE(reading.until, at(15, 23, 0));
        // H4: off by the schedule, burst at 23:00.
        reading = nights.read(false, at(15, 15, 0));
        QVERIFY(!reading.byHand);
        QCOMPARE(reading.until, at(15, 23, 0));
        // The boundary just passed and qBittorrent has not looked yet (it looks every 30 s):
        // still the schedule's, ending now.
        reading = nights.read(false, at(15, 23, 0, 10));
        QVERIFY(!reading.byHand);
        QCOMPARE(reading.until, at(15, 23, 0));
        // Past its look, it is a hand toggle (T2: off by hand, still off after 40 s).
        reading = nights.read(false, at(15, 23, 0, 40));
        QVERIFY(reading.byHand);
        QCOMPARE(reading.until, at(16, 23, 0));
        // T5: the schedule off leaves the state as it is: no "until", no "by hand".
        const AltSchedule off(false, 23 * H, 7 * H, AltSchedule::EveryDay);
        reading = off.read(true, at(15, 15, 0));
        QVERIFY(!reading.byHand);
        QVERIFY(!reading.until.isValid());
    }

    void untilText()
    {
        QCOMPARE(AltSchedule::untilText(at(16, 7, 0), at(15, 15, 0)), QStringLiteral("07:00"));
        QVERIFY(AltSchedule::isTomorrow(at(16, 7, 0), at(15, 15, 0)));
        QCOMPARE(AltSchedule::whenText(at(16, 7, 0), at(15, 15, 0)),
                 QStringLiteral("07:00 tomorrow"));
        QCOMPARE(AltSchedule::whenText(at(15, 23, 0), at(15, 15, 0)), QStringLiteral("23:00"));
        // More than a day away: the day, and no "tomorrow".
        QCOMPARE(AltSchedule::untilText(at(19, 7, 0), at(15, 15, 0)), QStringLiteral("Mon 07:00"));
        QCOMPARE(AltSchedule::whenText(at(19, 7, 0), at(15, 15, 0)), QStringLiteral("Mon 07:00"));
        QCOMPARE(AltSchedule::untilText(QDateTime(), at(15, 15, 0)), QString());
    }

    void summary_data()
    {
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<int>("days");
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("warn");

        // P1, 15:00 on a Thursday.
        QTest::newRow("P1 nights")
            << 23 * H << 7 * H << 0 << true
            << "Burst every night from 23:00 to 07:00, by the server's clock. Next: on at 23:00."
            << false;
        // P2: the days, backwards.
        QTest::newRow("P2 weekends past midnight")
            << 23 * H << 7 * H << 2 << true
            << "Burst all day Monday to Friday, and Saturday and Sunday nights." << true;
        QTest::newRow("weekdays past midnight")
            << 23 * H << 7 * H << 1 << true
            << "Burst all day Saturday and Sunday, and Monday to Friday nights." << true;
        QTest::newRow("Monday past midnight")
            << 23 * H << 7 * H << 3 << true
            << "Burst all day every day but Monday, and Monday nights." << true;
        QTest::newRow("daytime, every day")
            << 8 * H << 18 * H << 0 << true
            << "Burst every day from 08:00 to 18:00, by the server's clock. Next: off at 18:00."
            << false;
        QTest::newRow("daytime, Mondays")
            << 8 * H << 10 * H << 3 << true
            << "Burst on Mondays from 08:00 to 10:00, by the server's clock. Next: on at Mon 08:00."
            << false;
        QTest::newRow("daytime, weekdays") << 8 * H << 18 * H << 1 << true
                                           << "Burst Monday to Friday from 08:00 to 18:00, by the "
                                              "server's clock. Next: off at 18:00."
                                           << false;
        // P3: a schedule that can't fire.
        QTest::newRow("never") << 7 * H << 7 * H << 0 << true
                               << "Never on: from and to are the same time." << false;
    }

    void summary()
    {
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(int, days);
        QFETCH(bool, enabled);
        QFETCH(QString, text);
        QFETCH(bool, warn);
        const AltSchedule::Summary got =
            AltSchedule(enabled, from, to, days)
                .summary(QStringLiteral("burst"), false, at(15, 15, 0));
        QCOMPARE(got.text, text);
        QCOMPARE(got.warn, warn);
        if (warn)
            QVERIFY(got.note.contains(QStringLiteral("the other way round")));
    }

    void summaryNotes()
    {
        const QDateTime night = at(15, 23, 30);
        // On: how a hand toggle and the schedule share.
        QCOMPARE(
            nights.summary(QStringLiteral("burst"), true, night).note,
            QStringLiteral("A switches it by hand. The schedule takes over at its next change."));
        // Off, in the evening: ticking it starts burst at once (§2, T1).
        const AltSchedule unticked(false, 23 * H, 7 * H, AltSchedule::EveryDay);
        QCOMPARE(unticked.summary(QStringLiteral("burst"), false, night).note,
                 QStringLiteral("Ticked now, it starts burst at once."));
        QCOMPARE(unticked.summary(QStringLiteral("turtle"), true, at(15, 12, 0)).note,
                 QStringLiteral("Ticked now, it ends turtle at once."));
        QCOMPARE(unticked.summary(QStringLiteral("burst"), false, at(15, 12, 0)).note, QString());
        // The word leads the sentence.
        QVERIFY(unticked.summary(QStringLiteral("turtle"), false, night)
                    .text.startsWith(QStringLiteral("Turtle every night")));
        QVERIFY(unticked.summary(QStringLiteral("alt-speed"), false, night)
                    .text.startsWith(QStringLiteral("Alt-speed every night")));
    }
};

QTEST_GUILESS_MAIN(TestAltSchedule)
#include "tst_altschedule.moc"
