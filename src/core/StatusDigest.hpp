#pragma once

// StatusDigest — the pure parts of the bar widget's status file (plugin/STATUS.md, schema v2):
// the figures the app works out so the widget never has to (docs/20 §4, I1–I6). Everything
// here takes the time it should use as an argument, so the tests can walk days and minutes
// without waiting for them. integration/StatusWriter puts the pieces together and writes them.

#include "core/Torrent.hpp"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>

namespace omaseed::core {

class TorrentModel;

namespace StatusDigest {

/// `value` rounded to three significant figures (1234567 → 1230000), the precision the change
/// test uses, so a speed that wobbles in its fourth digit does not count as news.
qint64 roundSignificant(qint64 value, int figures = 3);

/// A ratio as the file writes it: three decimals at most, -1 kept as it is.
double roundRatio(double ratio);

/// I3: the complete, seeding torrents by seeds in the swarm (`num_complete`, you included).
/// `buckets` has one entry per seed count 1..5, `rows` up to 6 torrents with five seeds or
/// fewer, rarest first (seeds ascending, then peers waiting descending, then name). A torrent
/// is `quiet` when its last activity (its add time when it never had any) is 30 days or more
/// before `now`. A torrent whose swarm count is unknown (the tracker reports 0 seeds, or an
/// error) is in none of them: the seed console's rule, SeedConsole::knownSeeds (#47).
///
/// `quietAt`, when given, gets the first moment after `now` (unix seconds) at which a torrent
/// counted here turns quiet, or 0 when none will: until then, with the same torrents, the
/// result stays the same (#36: the status file keeps it instead of working it out each write).
QJsonObject alive(const QList<Torrent> &torrents, const QDateTime &now, qint64 *quietAt = nullptr);

/// I4: at most two torrents by what they gave today (`givenToday`, the seed ledger's
/// SeedLedger::dayTotals), largest first; only ones that gave something and that `find` still
/// has (it returns null for a hash no longer in the library). Costs the torrents that gave
/// today, not the library (#47).
QJsonArray topSeeders(const QHash<QString, qint64> &givenToday,
                      const std::function<const Torrent *(const QString &)> &find);

/// Up to three torrents in the downloading group, fastest first (then most done, then name).
QJsonArray active(const QList<Torrent> &torrents);

/// The total size of the complete torrents: what is on offer.
qint64 onOffer(const QList<Torrent> &torrents);

/// Every row of `model`, for the functions above.
QList<Torrent> torrentsOf(const TorrentModel &model);

/// A byte counter that may start over (qBittorrent restarted, or another server): what it moved
/// since the first observation. A counter that goes down counts its new value as moved.
class Counter
{
public:
    /// The bytes moved since the previous observation (0 for the first one).
    qint64 observe(qint64 counter);
    qint64 total() const { return m_total; }
    void reset();

private:
    qint64 m_last = -1;
    qint64 m_total = 0;
};

/// I2: upload per minute over the last hour. Fed one sample (bytes/s) about once a second;
/// `values` gives 60 averages, oldest first, the current minute last, 0 for minutes with no
/// sample.
class UploadSampler
{
public:
    void sample(qint64 bytesPerSecond, qint64 unixSeconds);
    QJsonArray values(qint64 unixSeconds) const;
    void clear() { m_minutes.clear(); }

private:
    struct Minute
    {
        qint64 sum = 0;
        int count = 0;
    };
    QHash<qint64, Minute> m_minutes; ///< keyed by unixSeconds / 60
};

/// I5: upload per local calendar day, from `up_info_data`. Kept in state.json between runs
/// (`toJson`/`fromJson`); the counter's baseline is not, so upload while omaseed was closed is
/// not counted.
class WeekLedger
{
public:
    void observe(qint64 upInfoData, const QDate &today);
    /// Seven `{day: "yyyy-MM-dd", up}` objects ending with `today`, oldest first.
    QJsonArray week(const QDate &today) const;
    /// The days kept, pruned to the last eight.
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);
    /// A new server: the baseline starts over, the days stay (they are per server already).
    void resetBaseline() { m_counter.reset(); }

private:
    Counter m_counter;
    QMap<QDate, qint64> m_days;
};

/// I6: a torrent's ratio crossing 1, 2 or 5. A torrent seen for the first time (the first
/// full sync, a new add) only sets its starting point, so nothing is announced at startup.
class RatioMilestones
{
public:
    struct Crossing
    {
        QString hash;
        int ratio = 0;
    };

    QList<Crossing> observe(const QList<Torrent> &torrents);
    /// observe() for a library that moved only in `changed` (each its current row, new ones
    /// included) and lost `removed`, everything else keeping the ratio it had (#36: a poll's
    /// diff instead of the whole library). Valid after an observe() of the whole list.
    QList<Crossing> observeChanges(const QList<Torrent> &changed, const QStringList &removed);
    void clear() { m_ratios.clear(); }

private:
    QHash<QString, double> m_ratios;
};

/// The milestones a ratio passes on the way from `before` to `after` (1, 2 and 5), highest
/// last.
QList<int> milestonesBetween(double before, double after);

/// `recent` with `entry` in front, at most `limit` long. An entry for the same hash and kind
/// (and ratio, for a milestone) replaces the older one.
QJsonArray pushRecent(const QJsonArray &recent, const QJsonObject &entry, int limit = 5);

} // namespace StatusDigest

} // namespace omaseed::core
