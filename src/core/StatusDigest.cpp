#include "core/StatusDigest.hpp"

#include "core/TorrentModel.hpp"

#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace omaseed::core::StatusDigest {

namespace {

constexpr int kBucketCount = 5;
constexpr int kRowLimit = 6;
constexpr qint64 kQuietDays = 30;

/// Seeds in the swarm, you included: a seeding torrent always has at least one.
int seedsOf(const Torrent &torrent)
{
    return std::max(torrent.numComplete, 1);
}

bool isHeld(const Torrent &torrent)
{
    return torrent.isComplete() && torrent.inGroup(StatusGroup::Seeding);
}

QDateTime lastActive(const Torrent &torrent)
{
    return torrent.lastActivity.isValid() ? torrent.lastActivity : torrent.addedOn;
}

bool isQuiet(const Torrent &torrent, const QDateTime &now)
{
    const QDateTime last = lastActive(torrent);
    if (!last.isValid())
        return false;
    return last.secsTo(now) >= kQuietDays * 24 * 3600;
}

} // namespace

qint64 roundSignificant(qint64 value, int figures)
{
    if (value == 0)
        return 0;
    const bool negative = value < 0;
    const qint64 magnitude = negative ? -value : value;
    qint64 limit = 1;
    for (int i = 0; i < figures; ++i)
        limit *= 10;
    // The power of ten below the last figure kept; half up at the first figure dropped.
    qint64 factor = 1;
    while (magnitude / factor >= limit)
        factor *= 10;
    const qint64 rounded = (magnitude + factor / 2) / factor * factor;
    return negative ? -rounded : rounded;
}

double roundRatio(double ratio)
{
    if (ratio < 0)
        return -1.0;
    return std::round(ratio * 1000.0) / 1000.0;
}

QJsonObject alive(const QList<Torrent> &torrents, const QDateTime &now, qint64 *quietAt)
{
    qint64 nextQuiet = 0;
    struct Bucket
    {
        int count = 0;
        int waiting = 0;
        int peers = 0;
        int quiet = 0;
    };
    Bucket buckets[kBucketCount];
    QList<const Torrent *> rare;

    for (const Torrent &torrent : torrents) {
        if (!isHeld(torrent))
            continue;
        const int seeds = seedsOf(torrent);
        if (seeds > kBucketCount)
            continue;
        Bucket &bucket = buckets[seeds - 1];
        ++bucket.count;
        if (torrent.numIncomplete > 0)
            ++bucket.waiting;
        bucket.peers += torrent.numIncomplete;
        if (isQuiet(torrent, now)) {
            ++bucket.quiet;
        } else if (const QDateTime last = lastActive(torrent); last.isValid()) {
            const qint64 at = last.toSecsSinceEpoch() + kQuietDays * 24 * 3600;
            if (nextQuiet == 0 || at < nextQuiet)
                nextQuiet = at;
        }
        rare.append(&torrent);
    }

    std::sort(rare.begin(), rare.end(), [](const Torrent *a, const Torrent *b) {
        if (seedsOf(*a) != seedsOf(*b))
            return seedsOf(*a) < seedsOf(*b);
        if (a->numIncomplete != b->numIncomplete)
            return a->numIncomplete > b->numIncomplete;
        return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
    });

    QJsonArray bucketJson;
    for (int i = 0; i < kBucketCount; ++i) {
        bucketJson.append(QJsonObject{
            {QStringLiteral("seeds"), i + 1},
            {QStringLiteral("count"), buckets[i].count},
            {QStringLiteral("waiting"), buckets[i].waiting},
            {QStringLiteral("peers"), buckets[i].peers},
            {QStringLiteral("quiet"), buckets[i].quiet},
        });
    }
    QJsonArray rows;
    for (const Torrent *torrent : std::as_const(rare)) {
        if (rows.size() >= kRowLimit)
            break;
        rows.append(QJsonObject{
            {QStringLiteral("hash"), torrent->hash},
            {QStringLiteral("name"), torrent->name},
            {QStringLiteral("seeds"), seedsOf(*torrent)},
            {QStringLiteral("waiting"), torrent->numIncomplete},
        });
    }
    if (quietAt)
        *quietAt = nextQuiet;
    return QJsonObject{{QStringLiteral("buckets"), bucketJson}, {QStringLiteral("rows"), rows}};
}

QJsonArray topSeeders(const QList<Torrent> &torrents)
{
    QList<const Torrent *> seeders;
    for (const Torrent &torrent : torrents) {
        if (torrent.uploadedSession > 0)
            seeders.append(&torrent);
    }
    std::sort(seeders.begin(), seeders.end(), [](const Torrent *a, const Torrent *b) {
        if (a->uploadedSession != b->uploadedSession)
            return a->uploadedSession > b->uploadedSession;
        return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
    });
    QJsonArray rows;
    for (const Torrent *torrent : std::as_const(seeders)) {
        if (rows.size() >= 2)
            break;
        rows.append(QJsonObject{
            {QStringLiteral("hash"), torrent->hash},
            {QStringLiteral("name"), torrent->name},
            {QStringLiteral("uploaded"), torrent->uploadedSession},
            {QStringLiteral("ratio"), roundRatio(torrent->ratio)},
        });
    }
    return rows;
}

QJsonArray active(const QList<Torrent> &torrents)
{
    QList<const Torrent *> downloads;
    for (const Torrent &torrent : torrents) {
        if (torrent.inGroup(StatusGroup::Downloading) && !torrent.isComplete())
            downloads.append(&torrent);
    }
    std::sort(downloads.begin(), downloads.end(), [](const Torrent *a, const Torrent *b) {
        if (a->dlspeed != b->dlspeed)
            return a->dlspeed > b->dlspeed;
        if (a->progress != b->progress)
            return a->progress > b->progress;
        return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
    });
    QJsonArray rows;
    for (const Torrent *torrent : std::as_const(downloads)) {
        if (rows.size() >= 3)
            break;
        rows.append(QJsonObject{
            {QStringLiteral("hash"), torrent->hash},
            {QStringLiteral("name"), torrent->name},
            {QStringLiteral("progress"), std::round(torrent->progress * 1000.0) / 1000.0},
            {QStringLiteral("dl"), roundSignificant(torrent->dlspeed)},
            // 8640000 is qBittorrent's "no estimate".
            {QStringLiteral("eta"),
             torrent->hasEta() && torrent->eta < 8640000 ? torrent->eta : qint64(-1)},
        });
    }
    return rows;
}

qint64 onOffer(const QList<Torrent> &torrents)
{
    qint64 total = 0;
    for (const Torrent &torrent : torrents) {
        if (torrent.isComplete())
            total += torrent.size;
    }
    return total;
}

QList<Torrent> torrentsOf(const TorrentModel &model)
{
    QList<Torrent> rows;
    rows.reserve(model.count());
    for (int row = 0; row < model.count(); ++row)
        rows.append(model.torrentAt(row));
    return rows;
}

// --- Counter ---------------------------------------------------------------------------------

qint64 Counter::observe(qint64 counter)
{
    if (counter < 0)
        return 0;
    qint64 delta = 0;
    if (m_last >= 0)
        delta = counter >= m_last ? counter - m_last : counter;
    m_last = counter;
    m_total += delta;
    return delta;
}

void Counter::reset()
{
    m_last = -1;
    m_total = 0;
}

// --- UploadSampler ---------------------------------------------------------------------------

void UploadSampler::sample(qint64 bytesPerSecond, qint64 unixSeconds)
{
    const qint64 minute = unixSeconds / 60;
    Minute &slot = m_minutes[minute];
    slot.sum += std::max<qint64>(bytesPerSecond, 0);
    ++slot.count;
    // Keep the hour and nothing older.
    for (auto it = m_minutes.begin(); it != m_minutes.end();) {
        if (it.key() <= minute - 60)
            it = m_minutes.erase(it);
        else
            ++it;
    }
}

QJsonArray UploadSampler::values(qint64 unixSeconds) const
{
    const qint64 now = unixSeconds / 60;
    QJsonArray values;
    for (qint64 minute = now - 59; minute <= now; ++minute) {
        const auto it = m_minutes.constFind(minute);
        if (it == m_minutes.constEnd() || it->count == 0)
            values.append(0);
        else
            values.append(roundSignificant(it->sum / it->count));
    }
    return values;
}

// --- WeekLedger ------------------------------------------------------------------------------

void WeekLedger::observe(qint64 upInfoData, const QDate &today)
{
    const qint64 delta = m_counter.observe(upInfoData);
    if (delta > 0 && today.isValid())
        m_days[today] += delta;
    while (m_days.size() > 8)
        m_days.erase(m_days.begin());
}

QJsonArray WeekLedger::week(const QDate &today) const
{
    QJsonArray days;
    for (int back = 6; back >= 0; --back) {
        const QDate day = today.addDays(-back);
        days.append(QJsonObject{
            {QStringLiteral("day"), day.toString(Qt::ISODate)},
            {QStringLiteral("up"), m_days.value(day, 0)},
        });
    }
    return days;
}

QJsonObject WeekLedger::toJson() const
{
    QJsonObject days;
    for (auto it = m_days.constBegin(); it != m_days.constEnd(); ++it)
        days.insert(it.key().toString(Qt::ISODate), it.value());
    return days;
}

void WeekLedger::fromJson(const QJsonObject &json)
{
    m_days.clear();
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        const QDate day = QDate::fromString(it.key(), Qt::ISODate);
        const qint64 up = it.value().toInteger(-1);
        if (day.isValid() && up >= 0)
            m_days.insert(day, up);
    }
    while (m_days.size() > 8)
        m_days.erase(m_days.begin());
}

// --- RatioMilestones -------------------------------------------------------------------------

QList<int> milestonesBetween(double before, double after)
{
    QList<int> passed;
    for (const int mark : {1, 2, 5}) {
        if (before < mark && after >= mark)
            passed.append(mark);
    }
    return passed;
}

QList<RatioMilestones::Crossing> RatioMilestones::observe(const QList<Torrent> &torrents)
{
    QList<Crossing> crossings;
    QHash<QString, double> next;
    next.reserve(torrents.size());
    for (const Torrent &torrent : torrents) {
        next.insert(torrent.hash, torrent.ratio);
        const auto it = m_ratios.constFind(torrent.hash);
        if (it == m_ratios.constEnd())
            continue;
        for (const int mark : milestonesBetween(it.value(), torrent.ratio))
            crossings.append(Crossing{torrent.hash, mark});
    }
    m_ratios = std::move(next);
    return crossings;
}

QList<RatioMilestones::Crossing> RatioMilestones::observeChanges(const QList<Torrent> &changed,
                                                                 const QStringList &removed)
{
    // Removed and added again in one poll: still listed, so observe() would compare it with
    // the ratio it had.
    if (!removed.isEmpty()) {
        QSet<QString> listed;
        for (const Torrent &torrent : changed)
            listed.insert(torrent.hash);
        for (const QString &hash : removed) {
            if (!listed.contains(hash))
                m_ratios.remove(hash);
        }
    }
    QList<Crossing> crossings;
    for (const Torrent &torrent : changed) {
        const auto it = m_ratios.find(torrent.hash);
        if (it == m_ratios.end()) {
            m_ratios.insert(torrent.hash, torrent.ratio);
            continue;
        }
        for (const int mark : milestonesBetween(it.value(), torrent.ratio))
            crossings.append(Crossing{torrent.hash, mark});
        it.value() = torrent.ratio;
    }
    return crossings;
}

// --- recent ----------------------------------------------------------------------------------

QJsonArray pushRecent(const QJsonArray &recent, const QJsonObject &entry, int limit)
{
    const auto sameAs = [&entry](const QJsonObject &other) {
        if (other.value(QStringLiteral("hash")) != entry.value(QStringLiteral("hash")) ||
            other.value(QStringLiteral("kind")) != entry.value(QStringLiteral("kind")))
            return false;
        return entry.value(QStringLiteral("kind")).toString() != QLatin1String("milestone") ||
               other.value(QStringLiteral("ratio")) == entry.value(QStringLiteral("ratio"));
    };
    QJsonArray next{entry};
    for (const QJsonValue &value : recent) {
        if (next.size() >= limit)
            break;
        if (!value.isObject() || sameAs(value.toObject()))
            continue;
        next.append(value);
    }
    return next;
}

} // namespace omaseed::core::StatusDigest
