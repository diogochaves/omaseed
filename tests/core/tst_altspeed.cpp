#include "QbtFeed.hpp"
#include "core/AltSpeed.hpp"

#include "core/GlobalState.hpp"
#include "core/engines/qbt/ServerPreferences.hpp"

#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace omaseed::core;

namespace {

QDateTime at(int day, int hour, int minute)
{
    return QDateTime(QDate(2026, 10, day), QTime(hour, minute), QTimeZone::UTC);
}

// The burst of docs/31: a daytime upload cap, the full line at night.
QVariantMap burstSet(bool scheduled, int days = 0)
{
    return {
        {QStringLiteral("dl_limit"), 0},
        {QStringLiteral("up_limit"), 1228800},
        {QStringLiteral("alt_dl_limit"), 0},
        {QStringLiteral("alt_up_limit"), 0},
        {QStringLiteral("scheduler_enabled"), scheduled},
        {QStringLiteral("schedule_from_hour"), 23},
        {QStringLiteral("schedule_from_min"), 0},
        {QStringLiteral("schedule_to_hour"), 7},
        {QStringLiteral("schedule_to_min"), 0},
        {QStringLiteral("scheduler_days"), days},
    };
}

QVariantMap turtleSet(bool scheduled)
{
    QVariantMap values = burstSet(scheduled);
    values.insert(QStringLiteral("up_limit"), 0);
    values.insert(QStringLiteral("alt_dl_limit"), 10240);
    values.insert(QStringLiteral("alt_up_limit"), 10240);
    values.insert(QStringLiteral("schedule_from_hour"), 8);
    values.insert(QStringLiteral("schedule_to_hour"), 18);
    return values;
}

struct Rig
{
    GlobalState state;
    ServerPreferences preferences{nullptr};
    AltSpeed alt;
    QDateTime now = at(15, 15, 0);

    Rig()
    {
        alt.setClock([this]() { return now; });
        alt.setGlobalState(&state);
        alt.setPreferences(&preferences);
    }

    void setOn(bool on)
    {
        qbtfeed::serverState(state, QJsonObject{{QStringLiteral("use_alt_speed_limits"), on}});
    }
};

} // namespace

// docs/31 §3–§4: one word, the same everywhere, and "until" when the schedule knows.
class TestAltSpeed : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Before the preferences answer: today's word, no "until", no claims.
    void notReadYet()
    {
        Rig rig;
        rig.setOn(true);
        QCOMPARE(rig.alt.kind(), QStringLiteral("alt"));
        QCOMPARE(rig.alt.word(), QStringLiteral("alt-speed"));
        QCOMPARE(rig.alt.setKind(), QString());
        QCOMPARE(rig.alt.chipTooltip(), QStringLiteral("alt-speed on · A"));
        QCOMPARE(rig.alt.trayLabel(), QStringLiteral("alternative speed limits"));
        QCOMPARE(rig.alt.untilText(), QString());
        QCOMPARE(rig.alt.footerNote(), QString());
        QCOMPARE(rig.alt.setLine(), QString());
        QCOMPARE(rig.alt.scheduleSummary(), QString());
    }

    // Someone who uses the set as a turtle and no schedule sees today's chip with its word.
    void turtleWithoutSchedule()
    {
        Rig rig;
        rig.preferences.setOffline(turtleSet(false));
        QCOMPARE(rig.alt.kind(), QStringLiteral("turtle"));
        QCOMPARE(rig.alt.chipTooltip(), QStringLiteral("turtle · A"));
        rig.setOn(true);
        QCOMPARE(rig.alt.chipTooltip(), QStringLiteral("turtle on · A"));
        QCOMPARE(rig.alt.untilText(), QString());
        QCOMPARE(rig.alt.trayLabel(), QStringLiteral("turtle"));
        QCOMPARE(rig.alt.footerNote(), QString());
        QCOMPARE(rig.alt.setGlyph(), QStringLiteral("▼"));
        QCOMPARE(
            rig.alt.setLine(),
            QStringLiteral("Slower than your limits above, so omaseed calls this set turtle."));
    }

    // H2: turtle by the schedule, 08:00 to 18:00.
    void turtleBySchedule()
    {
        Rig rig;
        rig.now = at(15, 9, 40);
        rig.setOn(true);
        rig.preferences.setOffline(turtleSet(true));
        QVERIFY(!rig.alt.isByHand());
        QCOMPARE(rig.alt.untilText(), QStringLiteral("18:00"));
        QCOMPARE(rig.alt.chipTooltip(), QStringLiteral("turtle until 18:00, by the schedule · A"));
        QCOMPARE(rig.alt.trayLabel(), QStringLiteral("turtle · until 18:00"));
    }

    // H1: burst by the schedule at 01:20.
    void burstBySchedule()
    {
        Rig rig;
        rig.now = at(15, 1, 20);
        rig.setOn(true);
        rig.preferences.setOffline(burstSet(true));
        QCOMPARE(rig.alt.kind(), QStringLiteral("burst"));
        QCOMPARE(rig.alt.untilText(), QStringLiteral("07:00"));
        QCOMPARE(rig.alt.stateUntilText(), QStringLiteral("07:00"));
        QCOMPARE(rig.alt.chipTooltip(), QStringLiteral("burst until 07:00, by the schedule · A"));
        QCOMPARE(rig.alt.trayLabel(), QStringLiteral("burst · until 07:00"));
        QCOMPARE(rig.alt.footerNote(), QString());
        QCOMPARE(rig.alt.setGlyph(), QStringLiteral("▲"));
    }

    // H3 and F1: burst by hand at 15:00 with the schedule on.
    void burstByHand()
    {
        Rig rig;
        rig.preferences.setOffline(burstSet(true));
        QSignalSpy changed(&rig.alt, &AltSpeed::changed);
        rig.setOn(true);
        QVERIFY(changed.count() >= 1);
        QVERIFY(rig.alt.isByHand());
        QCOMPARE(rig.alt.untilText(), QStringLiteral("07:00"));
        QCOMPARE(rig.alt.chipTooltip(),
                 QStringLiteral("burst by hand · the schedule ends it at 07:00 tomorrow · A"));
        QCOMPARE(rig.alt.footerNote(),
                 QStringLiteral("burst on by hand · the schedule ends it at 07:00 tomorrow"));
    }

    // H4: off, burst scheduled; and off by hand at 01:00.
    void burstOff()
    {
        Rig rig;
        rig.preferences.setOffline(burstSet(true));
        QCOMPARE(rig.alt.chipTooltip(),
                 QStringLiteral("burst at 23:00, by the schedule · A starts it now"));
        QCOMPARE(rig.alt.untilText(), QString());
        QCOMPARE(rig.alt.stateUntilText(), QStringLiteral("23:00"));
        QCOMPARE(rig.alt.footerNote(), QString());

        rig.now = at(15, 1, 0);
        rig.alt.refresh();
        QVERIFY(rig.alt.isByHand());
        QCOMPARE(rig.alt.chipTooltip(),
                 QStringLiteral("off by hand · the schedule starts burst at 23:00 · A"));
        QCOMPARE(rig.alt.footerNote(),
                 QStringLiteral("burst off by hand · back at 23:00 by the schedule"));
        // No schedule: the word alone.
        rig.preferences.setOffline(burstSet(false));
        QCOMPARE(rig.alt.chipTooltip(), QStringLiteral("burst · A"));
    }

    // Mixed or the same: today's words, with the end when the schedule knows it.
    void mixedAndSame()
    {
        Rig rig;
        QVariantMap mixed = burstSet(true);
        mixed.insert(QStringLiteral("alt_dl_limit"), 2 * 1024 * 1024);
        rig.preferences.setOffline(mixed);
        rig.setOn(true);
        QCOMPARE(rig.alt.kind(), QStringLiteral("alt"));
        QCOMPARE(rig.alt.setKind(), QStringLiteral("mixed"));
        QCOMPARE(rig.alt.chipTooltip(),
                 QStringLiteral("alt-speed on by hand · until 07:00 tomorrow · A"));
        QCOMPARE(rig.alt.trayLabel(), QStringLiteral("alternative speed limits"));
        QCOMPARE(rig.alt.setGlyph(), QStringLiteral("⇅"));

        QVariantMap same = burstSet(false);
        same.insert(QStringLiteral("alt_up_limit"), 1228800);
        rig.preferences.setOffline(same);
        QCOMPARE(rig.alt.setKind(), QStringLiteral("same"));
        QCOMPARE(rig.alt.chipTooltip(), QStringLiteral("alt-speed on · A"));
        QCOMPARE(rig.alt.setLine(),
                 QStringLiteral("The same as your limits above: switching changes nothing."));
    }

    // Settings' summary follows the preferences as they are typed.
    void summaryFollowsPreferences()
    {
        Rig rig;
        rig.preferences.setOffline(burstSet(true));
        QCOMPARE(rig.alt.scheduleSummary(),
                 QStringLiteral("Burst every night from 23:00 to 07:00, by the server's clock. "
                                "Next: on at 23:00."));
        QVERIFY(!rig.alt.scheduleWarn());
        QSignalSpy changed(&rig.alt, &AltSpeed::changed);
        rig.preferences.apply({{QStringLiteral("scheduler_days"), 2}});
        QCOMPARE(changed.count(), 1);
        QVERIFY(rig.alt.scheduleWarn());
        QCOMPARE(rig.alt.scheduleSummary(),
                 QStringLiteral("Burst all day Monday to Friday, and Saturday and Sunday nights."));
    }

    // Nothing moved: no signal (the hero and the status file would redraw for nothing).
    void quietWhenNothingMoves()
    {
        Rig rig;
        rig.preferences.setOffline(burstSet(true));
        QSignalSpy changed(&rig.alt, &AltSpeed::changed);
        rig.alt.refresh();
        rig.now = rig.now.addSecs(30);
        rig.alt.refresh();
        QCOMPARE(changed.count(), 0);
        // The schedule's boundary passes: it moves.
        rig.now = at(15, 23, 0).addSecs(10);
        rig.alt.refresh();
        QCOMPARE(changed.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestAltSpeed)
#include "tst_altspeed.moc"
