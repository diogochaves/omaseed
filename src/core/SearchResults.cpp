#include "core/SearchResults.hpp"

#include "core/MagnetLink.hpp"
#include "core/SearchWords.hpp"
#include "core/TrackerMerge.hpp"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace omaseed::core {

namespace {

/// A copy's key: its v1 hash, else the start of its v2 (as the library keys a v2-only torrent).
QString keyOf(const QString &hashV1, const QString &hashV2)
{
    return !hashV1.isEmpty() ? hashV1 : hashV2.left(40);
}

/// The copy that names a row: the most seeds (SR4's order), then the newest, then the first.
bool namesBefore(const SearchHit &a, const SearchHit &b)
{
    const qint64 ra = SearchResults::seedRank(a.seeds);
    const qint64 rb = SearchResults::seedRank(b.seeds);
    if (ra != rb)
        return ra > rb;
    return a.published > b.published;
}

SearchRow rowOf(const SearchHit &hit)
{
    SearchRow row;
    row.name = hit.name;
    row.size = hit.size;
    row.link = hit.link;
    row.page = hit.page;
    row.seeds = hit.seeds;
    row.leechers = hit.leechers;
    row.published = hit.published;
    row.plugin = hit.plugin;
    row.plugins = {hit.plugin};
    row.hashV1 = hit.hashV1;
    row.hashV2 = hit.hashV2;
    row.trackers = hit.trackers;
    row.isLink = !hit.magnet;
    row.hits = {hit};
    return row;
}

} // namespace

QString SearchHit::hash() const
{
    return magnet ? keyOf(hashV1, hashV2) : QString();
}

SearchHit SearchHit::fromJson(const QJsonObject &result, const QString &plugin)
{
    SearchHit hit;
    hit.name = result.value(QStringLiteral("fileName")).toString();
    hit.size = result.value(QStringLiteral("fileSize")).toInteger(-1);
    hit.link = result.value(QStringLiteral("fileUrl")).toString();
    hit.page = result.value(QStringLiteral("descrLink")).toString();
    hit.seeds = result.value(QStringLiteral("nbSeeders")).toInt(-1);
    hit.leechers = result.value(QStringLiteral("nbLeechers")).toInt(-1);
    hit.published = result.value(QStringLiteral("pubDate")).toInteger(-1);
    if (hit.published <= 0)
        hit.published = -1;
    hit.plugin = plugin.isEmpty() ? result.value(QStringLiteral("engineName")).toString() : plugin;
    if (hit.seeds < -1)
        hit.seeds = -1;
    if (hit.leechers < -1)
        hit.leechers = -1;
    const MagnetLink magnet = MagnetLink::parse(hit.link);
    if (magnet.isValid()) {
        hit.magnet = true;
        hit.hashV1 = magnet.infoHashV1;
        hit.hashV2 = magnet.infoHashV2;
        hit.trackers = magnet.trackers;
    }
    return hit;
}

QString SearchRow::hash() const
{
    return isLink ? QString() : keyOf(hashV1, hashV2);
}

namespace SearchResults {

QList<SearchRow> fold(const QList<SearchHit> &hits,
                      const std::function<QString(const QStringList &)> &privateHostOf)
{
    QList<SearchRow> rows;
    QHash<QString, int> byHash; // hash -> index in rows of its folded (public) row
    QHash<QString, int> privateCopies;
    QSet<QString> links;
    for (const SearchHit &hit : hits) {
        if (!hit.magnet) {
            // A link: its own row until its hash is known; the same link twice is one row.
            if (links.contains(hit.link))
                continue;
            links.insert(hit.link);
            SearchRow row = rowOf(hit);
            row.key = QStringLiteral("link:") + hit.link;
            rows.append(row);
            continue;
        }
        const QString hash = hit.hash();
        const QString host = privateHostOf ? privateHostOf(hit.trackers) : QString();
        if (!host.isEmpty()) {
            // C5 D7, board 25: a private copy is never folded nor given another's trackers.
            SearchRow row = rowOf(hit);
            row.privateHost = host;
            row.key = QStringLiteral("%1#%2").arg(hash).arg(++privateCopies[hash]);
            rows.append(row);
            continue;
        }
        const auto found = byHash.constFind(hash);
        if (found == byHash.cend()) {
            SearchRow row = rowOf(hit);
            row.key = hash;
            byHash.insert(hash, int(rows.size()));
            rows.append(row);
            continue;
        }
        SearchRow &row = rows[*found];
        row.hits.append(hit);
        std::stable_sort(row.hits.begin(), row.hits.end(), namesBefore);
        const SearchHit &best = row.hits.constFirst();
        // The naming copy's facts; every copy's trackers, its first.
        QStringList trackers = best.trackers;
        QSet<QString> seen;
        for (const QString &url : std::as_const(trackers))
            seen.insert(TrackerMerge::key(url).isEmpty() ? url : TrackerMerge::key(url));
        QStringList plugins = {best.plugin};
        for (const SearchHit &copy : std::as_const(row.hits)) {
            if (!plugins.contains(copy.plugin))
                plugins.append(copy.plugin);
            for (const QString &url : copy.trackers) {
                const QString key = TrackerMerge::key(url).isEmpty() ? url : TrackerMerge::key(url);
                if (seen.contains(key))
                    continue;
                seen.insert(key);
                trackers.append(url);
            }
        }
        const QString key = row.key;
        const QList<SearchHit> copies = row.hits;
        row = rowOf(best);
        row.key = key;
        row.hits = copies;
        row.plugins = plugins;
        row.trackers = trackers;
        row.copies = int(copies.size());
        if (row.hashV1.isEmpty() || row.hashV2.isEmpty()) {
            // A hybrid's other hash, when another copy's magnet named it.
            for (const SearchHit &copy : copies) {
                if (row.hashV1.isEmpty() && !copy.hashV1.isEmpty())
                    row.hashV1 = copy.hashV1;
                if (row.hashV2.isEmpty() && !copy.hashV2.isEmpty())
                    row.hashV2 = copy.hashV2;
            }
        }
        row.link = magnetWithTrackers(best.link, trackers);
    }
    return rows;
}

qint64 seedRank(int seeds)
{
    // 0 lowest, "?" (-1) above it, every known count above both (SR4).
    if (seeds < 0)
        return 1;
    if (seeds == 0)
        return 0;
    return qint64(seeds) + 1;
}

Health health(int seeds)
{
    if (seeds < 0)
        return Health::Unknown;
    if (seeds == 0)
        return Health::Dead;
    return seeds < 5 ? Health::Few : Health::Good;
}

QString seedsText(int seeds)
{
    return seeds < 0 ? QStringLiteral("?") : QString::number(seeds);
}

QString seedsCaption(int seeds)
{
    switch (health(seeds)) {
    case Health::Unknown:
        return QStringLiteral("not reported");
    case Health::Dead:
        return QStringLiteral("no seeds");
    case Health::Few:
        return QStringLiteral("few seeds");
    case Health::Good:
        break;
    }
    return seeds == 1 ? QStringLiteral("seed") : QStringLiteral("seeds");
}

QString ageText(qint64 published, qint64 now)
{
    if (published <= 0)
        return QStringLiteral("no date");
    const qint64 seconds = std::max<qint64>(0, now - published);
    const auto ago = [](qint64 n, const char *one, const char *many) {
        return n == 1 ? QStringLiteral("1 %1 ago").arg(QLatin1String(one))
                      : QStringLiteral("%1 %2 ago").arg(n).arg(QLatin1String(many));
    };
    if (seconds < 3600)
        return seconds < 120 ? QStringLiteral("just now") : ago(seconds / 60, "min", "min");
    if (seconds < 86400)
        return ago(seconds / 3600, "hour", "hours");
    const qint64 days = seconds / 86400;
    if (days < 14)
        return ago(days, "day", "days");
    if (days < 60)
        return ago(days / 7, "week", "weeks");
    if (days < 365)
        return ago(days / 30, "month", "months");
    return ago(days / 365, "year", "years");
}

QString pluginWords(const QStringList &plugins,
                    const std::function<QString(const QString &)> &names)
{
    QStringList words;
    for (const QString &plugin : plugins)
        words.append(names ? names(plugin) : plugin);
    if (words.size() <= 1)
        return words.value(0);
    const QString last = words.takeLast();
    return QStringLiteral("%1 and %2").arg(words.join(QStringLiteral(", ")), last);
}

QString magnetWithTrackers(const QString &magnet, const QStringList &trackers)
{
    MagnetLink link = MagnetLink::parse(magnet);
    if (!link.isValid())
        return magnet;
    link.trackers = trackers;
    return link.toString();
}

bool before(const SearchRow &a, const SearchRow &b, Order order)
{
    switch (order) {
    case Order::Newest:
        if (a.published != b.published)
            return a.published > b.published;
        break;
    case Order::Size:
        if (a.size != b.size)
            return a.size > b.size;
        break;
    case Order::Name: {
        const int byName = QString::compare(
            SearchWords::fold(a.name), SearchWords::fold(b.name), Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        break;
    }
    case Order::MostSeeds:
        break;
    }
    const qint64 ra = seedRank(a.seeds);
    const qint64 rb = seedRank(b.seeds);
    if (ra != rb)
        return ra > rb;
    if (a.published != b.published)
        return a.published > b.published;
    return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
}

Order orderFromName(const QString &name)
{
    if (name == QLatin1String("newest"))
        return Order::Newest;
    if (name == QLatin1String("size"))
        return Order::Size;
    if (name == QLatin1String("name"))
        return Order::Name;
    return Order::MostSeeds;
}

QString orderName(Order order)
{
    switch (order) {
    case Order::Newest:
        return QStringLiteral("newest");
    case Order::Size:
        return QStringLiteral("size");
    case Order::Name:
        return QStringLiteral("name");
    case Order::MostSeeds:
        break;
    }
    return QStringLiteral("seeds");
}

} // namespace SearchResults

} // namespace omaseed::core
