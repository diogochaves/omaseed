#pragma once

// SeedLedger — what this server gave and took, kept over time (docs/22 §3).
//
// qBittorrent keeps running totals only: what each torrent uploaded since it was added, and
// what the server moved since it was installed (`alltime_ul` / `alltime_dl`). It has no
// history. omaseed is resident and polls all day, so the ledger turns those totals into a
// history: every observe() takes the new totals, subtracts the last ones and books the
// difference into buckets.
//
// Resolutions, all kept in one SQLite file per server:
//   seconds   the last hour, in memory only (sampleSpeed()), for the 1 h tide and the hero
//   minutes   48 h
//   hours     90 days
//   days      forever, the server's total and one row per torrent per day it uploaded
//
// Rules:
// - The first observation ever only sets the baseline; nothing is counted until the next.
// - A step backwards (qBittorrent reinstalled, its statistics lost, a torrent re-added) starts
//   a new baseline instead of counting as a loss.
// - A difference that spans more than `gapSeconds` (omaseed was closed, the server was away)
//   is exact in total but not in time: it is spread evenly over the span and every bucket it
//   touches is marked `gap`, so the tide can hatch it (docs/22 D7 a).
//
// Writes are buffered in memory and flushed in one transaction every `flushInterval` seconds
// of ledger time (and on flush()/close()), so a poll a second costs no disk write. Queries
// read the file and add what is still buffered.
//
// Everything takes "now" as an argument (unix seconds) so tests can walk days in a loop.

#include <QDate>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace omaseed::core {

class SeedLedger
{
public:
    /// One torrent's running totals, as maindata reports them.
    struct TorrentTotals
    {
        QString hash;
        qint64 uploaded = 0;
        qint64 downloaded = 0;
    };

    /// One bucket of the tide: average bytes per second over the bucket.
    struct Point
    {
        qint64 start = 0; ///< unix seconds, the bucket's start
        double up = 0.0;
        double down = 0.0;
        bool gap = false;   ///< some of it was spread over a span omaseed did not see
        bool known = false; ///< false before the ledger began
    };

    /// One day of the year: bytes given, -1 before the ledger began.
    struct Day
    {
        QDate date;
        qint64 up = -1;
    };

    static constexpr qint64 gapSeconds = 90;
    static constexpr qint64 flushInterval = 60;
    static constexpr qint64 minutesKept = 48 * 3600;
    static constexpr qint64 hoursKept = 90 * 86400;

    SeedLedger();
    ~SeedLedger();
    SeedLedger(const SeedLedger &) = delete;
    SeedLedger &operator=(const SeedLedger &) = delete;

    /// Opens (creating) the file at `path`, closing any open one first. False when SQLite
    /// cannot open it; the ledger then keeps working in memory only.
    bool open(const QString &path);
    /// Flushes and closes. Safe to call twice.
    void close();
    bool isOpen() const;
    QString path() const { return m_path; }

    /// `~/.local/share/omaseed/ledger/<serverId>.sqlite` ($XDG_DATA_HOME honoured).
    static QString defaultPath(const QString &serverId);

    /// One poll: the server's all-time totals and every torrent's totals (the whole list:
    /// a torrent missing from it loses its baseline, so a later re-add starts afresh).
    void observe(qint64 now,
                 qint64 allTimeUp,
                 qint64 allTimeDown,
                 const QList<TorrentTotals> &torrents);
    /// The speeds of this second (`up_info_speed`, `dl_info_speed`), kept for an hour.
    void sampleSpeed(qint64 now, qint64 up, qint64 down);

    /// Writes what is buffered. observe() calls it on its own every `flushInterval`.
    void flush(qint64 now);

    /// When the ledger began (unix seconds), 0 before the first observation.
    qint64 start() const { return m_start; }

    /// `count` buckets of `bucketSeconds` ending with the one that holds `now`, oldest first.
    /// Under 60 s buckets read the in-memory seconds, under an hour the minutes, under a day
    /// the hours, and days above.
    QList<Point> series(qint64 now, qint64 bucketSeconds, int count) const;
    /// The last `count` speed samples, oldest first, 0 where none was taken (the hero's 60 s).
    QList<qint64> recentUpload(qint64 now, int count) const;
    /// Every day from `from` to `to`, inclusive.
    QList<Day> days(const QDate &from, const QDate &to) const;
    /// Bytes given by each torrent from `from` to `to`, inclusive.
    QHash<QString, qint64> torrentTotals(const QDate &from, const QDate &to) const;
    /// Each torrent's bytes per day from `from` to `to` (one entry per day, 0 where none).
    QHash<QString, QList<qint64>> torrentDays(const QDate &from, const QDate &to) const;
    /// Days in a row with any upload, ending today (or yesterday, while today is still 0).
    int streak(const QDate &today) const;
    /// The longest such run in the ledger.
    int longestStreak() const;

    /// Dev only (OMASEED_LEDGER_DEMO): when the ledger holds under two days, makes up a year
    /// of plausible history before now for `hashes`, so the console can be judged as it will
    /// look a year in. Deterministic for a given `seed`.
    void fillDemo(qint64 now, const QStringList &hashes, quint32 seed = 22);

private:
    struct Bucket
    {
        qint64 up = 0;
        qint64 down = 0;
        bool gap = false;
    };

    void book(qint64 from, qint64 to, qint64 up, qint64 down, const QHash<QString, qint64> &perTorrent, qint64 now);
    void addTo(QMap<qint64, Bucket> &map, qint64 key, qint64 up, qint64 down, bool gap);
    QMap<qint64, Bucket> readBuckets(const QString &table, qint64 from, qint64 to) const;
    QMap<QDate, Bucket> readDays(const QDate &from, const QDate &to) const;
    void loadState();
    void saveMeta();
    void prune(qint64 now);

    QString m_connection;
    QString m_path;
    bool m_open = false;

    qint64 m_start = 0;
    qint64 m_lastTime = 0;
    qint64 m_lastUp = -1;
    qint64 m_lastDown = -1;
    QHash<QString, QPair<qint64, qint64>> m_baselines; ///< hash -> (uploaded, downloaded)
    qint64 m_lastFlush = 0;

    QMap<qint64, Bucket> m_pendingMinutes;
    QMap<qint64, Bucket> m_pendingHours;
    QMap<QDate, Bucket> m_pendingDays;
    QHash<QPair<QDate, QString>, qint64> m_pendingTorrentDays;
    bool m_metaDirty = false;

    struct Speed
    {
        qint64 t = 0;
        qint64 up = 0;
        qint64 down = 0;
    };
    QList<Speed> m_speeds; ///< the last hour, oldest first
};

} // namespace omaseed::core
