#pragma once

// AltSpeed — what the alternative limits are called and until when they last, for the server
// omaseed is on (docs/31, DECISIONS.md C4). The hero's chip, its tooltip, the footer's note
// after A, the `?` sheet, the limits sheet, Settings › Speed, the tray and the status file all
// read it, so every surface says the same word.
//
// It reads the two sets and the schedule from the server's preferences (`ServerPreferences`,
// pending changes included, so Settings' lines follow what is being typed) and whether the
// alternative limits are in force from each poll (`GlobalState`). The pure parts are
// `SpeedSets` (the word) and `AltSchedule` (the schedule, the "until" and the "by hand").
//
// Until the preferences answer after a connect, the word is today's "alt-speed" and there is
// no "until". It works the state out again when either input moves and twice a minute, so an
// "until" that passes is replaced within 30 s (qBittorrent itself looks every 30 s).

#include "core/AltSchedule.hpp"
#include "core/SpeedSets.hpp"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>

namespace omaseed::core {

class AltPresets;
class GlobalState;
class ServerPreferences;

class AltSpeed : public QObject
{
    Q_OBJECT
    /// "burst", "turtle" or "alt" (mixed, the same, or not read yet): the views' key and
    /// the status file's `alt.kind`.
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    /// "burst", "turtle" or "alt-speed".
    Q_PROPERTY(QString word READ word NOTIFY changed)
    /// The set against the normal one, finer than `kind`: "burst", "turtle", "mixed",
    /// "same", or "" before the preferences answer (Settings' set line).
    Q_PROPERTY(QString setKind READ setKind NOTIFY changed)
    /// The alternative limits are in force.
    Q_PROPERTY(bool on READ isOn NOTIFY changed)
    /// qBittorrent's scheduler is on.
    Q_PROPERTY(bool scheduled READ isScheduled NOTIFY changed)
    /// The state goes against the schedule: someone switched it by hand.
    Q_PROPERTY(bool byHand READ isByHand NOTIFY changed)
    /// When the schedule ends the current state (on or off); invalid with no schedule.
    Q_PROPERTY(QDateTime until READ until NOTIFY changed)
    /// `until` as the chip prints it, while on: "07:00", "Mon 07:00", "".
    Q_PROPERTY(QString untilText READ untilText NOTIFY changed)
    /// The hero chip's tooltip ("burst until 07:00, by the schedule · A").
    Q_PROPERTY(QString chipTooltip READ chipTooltip NOTIFY changed)
    /// The footer's note after A while the schedule is on and the state is a hand toggle;
    /// "" otherwise.
    Q_PROPERTY(QString footerNote READ footerNote NOTIFY changed)
    /// The tray menu's checkbox ("burst · until 07:00", "alternative speed limits").
    Q_PROPERTY(QString trayLabel READ trayLabel NOTIFY changed)
    /// Settings › Speed: the line under the four limits, its glyph (▲ ▼ = ⇅).
    Q_PROPERTY(QString setLine READ setLine NOTIFY changed)
    Q_PROPERTY(QString setGlyph READ setGlyph NOTIFY changed)
    /// Settings › Speed: the summary under Days, its second line, and whether it warns (the
    /// days read backwards).
    Q_PROPERTY(QString scheduleSummary READ scheduleSummary NOTIFY changed)
    Q_PROPERTY(QString scheduleNote READ scheduleNote NOTIFY changed)
    Q_PROPERTY(bool scheduleWarn READ scheduleWarn NOTIFY changed)

public:
    explicit AltSpeed(QObject *parent = nullptr);

    void setPreferences(ServerPreferences *preferences);
    void setGlobalState(GlobalState *state);
    /// DECISIONS C32 D8 a: while omaseed's burst is written over the set, the set is burst
    /// whatever its numbers say, and it lasts until the burst's own end.
    void setPresets(AltPresets *presets);
    /// The time the state is worked out at; the system clock until set (tests).
    void setClock(std::function<QDateTime()> clock);

    QString kind() const { return SpeedSets::key(m_kind); }
    QString word() const { return SpeedSets::word(m_kind); }
    QString setKind() const;
    bool isOn() const { return m_on; }
    bool isScheduled() const { return m_schedule.known() && m_schedule.enabled(); }
    bool isByHand() const { return m_reading.byHand; }
    QDateTime until() const { return m_reading.until; }
    QString untilText() const;
    QString chipTooltip() const;
    QString footerNote() const;
    QString trayLabel() const;
    QString setLine() const;
    QString setGlyph() const;
    QString scheduleSummary() const { return m_summary.text; }
    QString scheduleNote() const { return m_summary.note; }
    bool scheduleWarn() const { return m_summary.warn; }

    /// `until` for the current state, on or off, as the status file writes it ("" for none).
    QString stateUntilText() const;

public Q_SLOTS:
    /// Works everything out again from the inputs and the clock.
    void refresh();

Q_SIGNALS:
    void changed();

private:
    QDateTime now() const;
    /// "07:00 tomorrow", "Mon 07:00", "07:00".
    QString when() const;
    void armTimer();

    QPointer<ServerPreferences> m_preferences;
    QPointer<GlobalState> m_state;
    QPointer<AltPresets> m_presets;
    std::function<QDateTime()> m_clock;
    QTimer m_timer;

    SpeedSets::Kind m_kind = SpeedSets::Kind::Unknown;
    AltSchedule m_schedule;
    AltSchedule::Reading m_reading;
    AltSchedule::Summary m_summary;
    QDateTime m_now;
    bool m_on = false;
    /// omaseed's burst runs (C32 D8 a).
    bool m_presetBurst = false;
};

} // namespace omaseed::core
