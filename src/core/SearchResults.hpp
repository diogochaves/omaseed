#pragma once

// SearchResults — what a search's answers become (#145, board 48, DECISIONS C26 SR3–SR8).
//
// qBittorrent's search/results gives one result per plugin per torrent. omaseed shows one row
// per info hash (SR3): the copy with the most seeds names the row, and the other copies'
// trackers fold into its magnet, de-duplicated (core/TrackerMerge's sameness). A copy on a
// tracker the library knows as private (C5 D7, board 25) is never folded, nor given another's
// trackers: it keeps its own row. A link to a `.torrent` has no hash until it is fetched, so
// it is its own row too.
//
// Seeds a plugin doesn't report are −1: "?" and "not reported", sorted below every known count
// and above 0 (SR4). Health is words and one colour until the seeding coach (#25) has its own
// (SR8): five or more good, one to four few, "?" and 0 dim.
//
// Pure: no network, no QObject. SearchModel holds the rows and their marks.

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace omaseed::core {

/// One result as qBittorrent's search/results gives it, with the hash and trackers read from
/// its magnet.
struct SearchHit
{
    QString name;
    qint64 size = -1;
    QString link;          ///< fileUrl: a magnet, or a link to a .torrent
    QString page;          ///< descrLink, opened only on `o` (SR11)
    int seeds = -1;        ///< -1: not reported
    int leechers = -1;     ///< -1: not reported
    qint64 published = -1; ///< pubDate, seconds since the epoch; -1: none
    QString plugin;        ///< engineName: the plugin's name, which `downloader` takes
    bool magnet = false;
    QString hashV1; ///< lowercase hex, from the magnet
    QString hashV2;
    QStringList trackers; ///< the magnet's `tr=`

    /// The info hash the row is keyed by (TorrentFile::torrentIdFor), "" for a link.
    QString hash() const;

    static SearchHit fromJson(const QJsonObject &result, const QString &plugin = QString());
};

/// One row of the view: one torrent, however many plugins found it.
struct SearchRow
{
    QString key; ///< the hash, "link:URL" for a link, "HASH#n" for a private copy kept apart
    QString name;
    qint64 size = -1;
    QString link; ///< what add… hands the sheet: the folded magnet, or the link
    QString page;
    int seeds = -1;
    int leechers = -1;
    qint64 published = -1;
    QString plugin;      ///< the copy that names the row: `downloader` for a link
    QStringList plugins; ///< every plugin that found it, the naming copy's first
    QString hashV1;
    QString hashV2;
    QStringList trackers; ///< the folded magnet's trackers
    int copies = 1;
    bool isLink = false;
    QString privateHost;   ///< the private tracker that kept this copy apart, or ""
    QList<SearchHit> hits; ///< the copies, the naming one first

    QString hash() const;
};

namespace SearchResults {

/// SR3: one row per hash. `privateHostOf(trackers)` is the library's private tracker one of
/// `trackers` announces to, or "" (App::privateTrackerOf); null treats none as private.
QList<SearchRow> fold(const QList<SearchHit> &hits,
                      const std::function<QString(const QStringList &)> &privateHostOf = {});

/// SR4: a seed count's place in "most seeds": known counts above "?" above 0.
qint64 seedRank(int seeds);

enum class Health { Good, Few, Unknown, Dead };
Health health(int seeds);
/// The fact: "41", "?", "0".
QString seedsText(int seeds);
/// Its caption (SR8): "seeds", "few seeds", "not reported", "no seeds".
QString seedsCaption(int seeds);

/// "3 days ago", "2 weeks ago", "1 year ago", "no date" (−1), from `now` (seconds).
QString ageText(qint64 published, qint64 now);

/// The plugins that found a row, in words: "plugin a", "plugin a and plugin c",
/// "plugin a, plugin b and plugin c". `names` maps a plugin's name to its own (fullName).
QString pluginWords(const QStringList &plugins,
                    const std::function<QString(const QString &)> &names = {});

/// The magnet `row.link` with `trackers` in place of its own (SR3's fold).
QString magnetWithTrackers(const QString &magnet, const QStringList &trackers);

/// The order rows are sorted by (SR6's sort ▾).
enum class Order { MostSeeds, Newest, Size, Name };
/// `a` comes before `b` in `order`; ties go most seeds, then newest, then by name.
bool before(const SearchRow &a, const SearchRow &b, Order order);
Order orderFromName(const QString &name);
QString orderName(Order order);

} // namespace SearchResults

} // namespace omaseed::core
