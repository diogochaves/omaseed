#pragma once

// AltSchedule — qBittorrent's alternative-limits scheduler, worked out the way qBittorrent
// works it out (docs/31 §2, DECISIONS.md C4), so omaseed can say until when a state lasts
// and whether it came from the schedule or from a hand toggle.
//
// The rule, read in qBittorrent 5.2.3 for behaviour only and checked on the playground:
//   * One window, `schedule_from` to `schedule_to`, and `scheduler_days`: 0 every day,
//     1 weekdays, 2 weekends, 3–9 Monday to Sunday.
//   * From before to: on inside the window, on the picked days.
//   * From after to (past midnight): qBittorrent swaps the two, starts from "on", and turns it
//     off only in the daytime between them on a picked day. So on a day that is not picked it
//     is on all day: the days read backwards (diogochaves/upstream#84). Reproduced as is.
//   * From and to the same: never on (the window is a single instant).
//   * Every 30 s it works the state out and acts only when that changed since its last look.
//     A hand toggle is left alone until then; if that change goes the same way as the hand
//     toggle nothing happens, and the hand's state lasts until the change after it.
//
// Times are the server's clock; the API gives no time zone, so omaseed reads them with this
// computer's (the `QDateTime`s handed in). Minutes are the unit: a boundary is the minute
// qBittorrent's window starts or ends. Pure.

#include <QDateTime>
#include <QString>
#include <QVariantMap>

namespace omaseed::core {

class AltSchedule
{
public:
    enum Days { EveryDay = 0, Weekdays = 1, Weekends = 2, Monday = 3, Sunday = 9 };

    AltSchedule() = default;
    /// `from` and `to` in minutes since midnight.
    AltSchedule(bool enabled, int fromMinute, int toMinute, int days);

    /// From `app/preferences`: scheduler_enabled, schedule_from_hour/min, schedule_to_hour/min,
    /// scheduler_days. `known()` is false when the window's keys are missing.
    static AltSchedule fromPreferences(const QVariantMap &preferences);

    bool known() const { return m_known; }
    bool enabled() const { return m_enabled; }
    int fromMinute() const { return m_from; }
    int toMinute() const { return m_to; }
    int days() const { return m_days; }

    /// From and to the same: it never turns on.
    bool never() const { return m_from == m_to; }
    /// The window passes midnight.
    bool overnight() const { return m_from > m_to; }
    /// Past midnight on some days only: the picked days are the ones it holds back.
    bool backwards() const { return overnight() && m_days != EveryDay; }

    /// What the schedule says at `when` (whether it is enabled or not).
    bool onAt(const QDateTime &when) const;

    /// The first time after `from` the schedule would move the server out of `state`: the
    /// first change that goes the other way. From a state the schedule agrees with that is its
    /// next change; from a hand toggle that is the change after the one that agrees with it.
    /// Invalid when nothing changes within `days` days.
    QDateTime nextChange(const QDateTime &from, bool state, int days = 8) const;

    /// How the alternative limits being `on` at `now` came about, and until when they last.
    struct Reading
    {
        /// On (or off) against what the schedule says: someone pressed A (or the Web UI's
        /// switch). Only with the schedule enabled.
        bool byHand = false;
        /// When the schedule ends the current state; invalid with the schedule off.
        QDateTime until;
    };
    /// With the schedule off there is no "until" and no "by hand". A state the schedule only
    /// just reached, before qBittorrent's 30 s look, still counts as the schedule's.
    Reading read(bool on, const QDateTime &now) const;

    /// "07:00", or "Mon 07:00" when it is more than a day away; "" for an invalid time.
    static QString untilText(const QDateTime &until, const QDateTime &now);
    /// `until` falls tomorrow (and within a day): tooltips add "tomorrow".
    static bool isTomorrow(const QDateTime &until, const QDateTime &now);
    /// "07:00 tomorrow", "Mon 07:00", "07:00".
    static QString whenText(const QDateTime &until, const QDateTime &now);

    /// Settings › Speed's plain summary under Days (docs/31 P1–P3), for a set called `word`
    /// ("burst"): `text`, a second line `note` (small), and `warn` when the days run
    /// backwards (the line turns yellow and `note` says why). `on` is whether alternative
    /// limits are in force now; it decides the note while the schedule is off.
    struct Summary
    {
        QString text;
        QString note;
        bool warn = false;
    };
    Summary summary(const QString &word, bool on, const QDateTime &now) const;

    /// qBittorrent's scheduler looks every 30 s.
    static constexpr int lookSeconds = 30;

private:
    bool dayPicked(int dayOfWeek) const;

    bool m_known = false;
    bool m_enabled = false;
    int m_from = 0;
    int m_to = 0;
    int m_days = EveryDay;
};

} // namespace omaseed::core
