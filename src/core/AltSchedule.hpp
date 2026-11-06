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
//     is on all day: the days read backwards (qBittorrent's own rule). Reproduced as is.
//   * From and to the same: never on (the window is a single instant).
//   * Every 30 s it works the state out and acts only when that changed since its last look.
//     A hand toggle is left alone until then; if that change goes the same way as the hand
//     toggle nothing happens, and the hand's state lasts until the change after it.
//
// Transmission's turtle clock (alt_speed_time_*, DECISIONS C25 K20, board 47 §D "the until
// follows Transmission's own timer"), read in libtransmission 4.1 for behaviour only:
//   * One window, begin to end, on the days of a mask (Sunday 1, Monday 2, … Saturday 64).
//   * The window starts on a picked day; past midnight it runs on into the next day (not
//     backwards, unlike qBittorrent). Begin and end the same: on the whole day from begin.
//   * It looks every second and, like qBittorrent, acts only when its reading changes, so a
//     hand toggle lasts until the change that goes the other way.
// `fromPreferences` reads it when the preferences carry `scheduler_day_mask` (the mask; what
// TransmissionTranslate::speedPreferences writes) instead of `scheduler_days`.
//
// Times are the server's clock; the API gives no time zone, so omaseed reads them with this
// computer's (the `QDateTime`s handed in). Minutes are the unit: a boundary is the minute
// the window starts or ends. Pure.

#include <QDateTime>
#include <QString>
#include <QVariantMap>

namespace omaseed::core {

class AltSchedule
{
public:
    enum Days { EveryDay = 0, Weekdays = 1, Weekends = 2, Monday = 3, Sunday = 9 };
    /// Whose scheduler: the days and the overnight window differ (see above).
    enum class Rule { Qbittorrent, Transmission };
    /// Transmission's day mask: Sunday 1 << 0 to Saturday 1 << 6.
    enum DayMask { AllDays = 127, WeekdayMask = 62, WeekendMask = 65 };

    AltSchedule() = default;
    /// `from` and `to` in minutes since midnight.
    AltSchedule(bool enabled, int fromMinute, int toMinute, int days);
    /// Transmission's: `dayMask` as alt_speed_time_day keeps it.
    static AltSchedule transmission(bool enabled, int fromMinute, int toMinute, int dayMask);

    /// From `app/preferences`: scheduler_enabled, schedule_from_hour/min, schedule_to_hour/min,
    /// scheduler_days (or Transmission's scheduler_day_mask). `known()` is false when the
    /// window's keys are missing.
    static AltSchedule fromPreferences(const QVariantMap &preferences);

    bool known() const { return m_known; }
    bool enabled() const { return m_enabled; }
    int fromMinute() const { return m_from; }
    int toMinute() const { return m_to; }
    int days() const { return m_days; }
    Rule rule() const { return m_rule; }
    /// Transmission's days (Rule::Transmission only).
    int dayMask() const { return m_mask; }

    /// It never turns on: qBittorrent's from and to the same, or Transmission with no day.
    bool never() const
    {
        return m_rule == Rule::Transmission ? (m_mask & AllDays) == 0 : m_from == m_to;
    }
    /// The window passes midnight (Transmission's from and to the same: a whole day from).
    bool overnight() const { return m_rule == Rule::Transmission ? m_from >= m_to : m_from > m_to; }
    /// Past midnight on some days only: the picked days are the ones it holds back
    /// (qBittorrent's rule; Transmission's runs forwards).
    bool backwards() const
    {
        return m_rule == Rule::Qbittorrent && overnight() && m_days != EveryDay;
    }

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
    /// How often this one looks: 30 s on qBittorrent, every second on Transmission.
    int lookEvery() const { return m_rule == Rule::Transmission ? 1 : lookSeconds; }

private:
    bool dayPicked(int dayOfWeek) const;
    /// Transmission's days in words ("every day", "Monday to Friday", "Mon, Wed").
    QString maskWords() const;

    bool m_known = false;
    bool m_enabled = false;
    int m_from = 0;
    int m_to = 0;
    int m_days = EveryDay;
    Rule m_rule = Rule::Qbittorrent;
    int m_mask = AllDays;
};

} // namespace omaseed::core
