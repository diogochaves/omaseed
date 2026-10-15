#pragma once

// WeeklyRecap — the seed console's week in one desktop notice (#24 T8, docs/22 §6): one notice
// on Sunday evening, off by default, through the notices omaseed already sends; its button
// opens the console on 7d.
//
//   Your week
//   48.2 GB given this week · ratio 2.40 · a 41-day streak
//   kept 7 rare torrents alive · 3 paid back · best day Thursday
//
// What each number is (docs/DECISIONS.md C6):
// - the week is the seven days ending on the day it is sent (Monday to Sunday), the span the
//   console's 7d shows;
// - "given" is what the ledger booked over those days;
// - the ratio is the server's all time (given ÷ taken), as the console's totals print it;
// - the streak is the ledger's, as the console's;
// - "rare torrents kept alive" are the ones the console's keeping-alive block counts as rare
//   now: complete, seeding, at or under the rare threshold of seeds in the swarm;
// - "paid back" are the torrents whose ratio crossed 1.0 this week: at 1.0 or more now and
//   under it before what the ledger saw them give since Monday;
// - the best day is the weekday that gave the most.
// A part with nothing to say is left out, and a week that gave nothing sends no notice.

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>

namespace omaseed::core {

class SeedLedger;
struct Torrent;

struct WeeklyRecap
{
    /// The hour from which Sunday counts as the evening (local time).
    static constexpr int eveningHour = 18;

    QDate from;
    QDate to;
    qint64 given = 0;
    QString ratioText;
    int streak = 0;
    int rareAlive = 0;
    int paidBack = 0;
    QDate bestDay; ///< invalid when nothing was given

    /// The week ending `today`, from the ledger and the library as it is now. `allTimeUp` and
    /// `allTimeDown` are the server's counters; `rareSeeds` the rare threshold.
    static WeeklyRecap compute(const SeedLedger &ledger,
                               const QList<const Torrent *> &library,
                               const QDate &today,
                               qint64 allTimeUp,
                               qint64 allTimeDown,
                               int rareSeeds);

    /// True on a Sunday from `eveningHour`, unless one was already sent that day. A Sunday
    /// evening omaseed did not run is skipped, not made up on Monday.
    static bool due(const QDateTime &now, const QDate &lastSent);

    bool worthSending() const { return given > 0; }
    /// The notice's title.
    QString summary() const;
    /// The notice's two lines (the second only when it has something to say).
    QString body() const;
};

} // namespace omaseed::core
