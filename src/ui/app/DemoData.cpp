#include "ui/app/DemoData.hpp"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QString>
#include <QTime>

#include <algorithm>
#include <iterator>

namespace omaseed::app::DemoData {

namespace {

constexpr qint64 kKb = 1024;
constexpr qint64 kMb = 1024 * kKb;
constexpr qint64 kGb = 1024 * kMb;
constexpr qint64 kTb = 1024 * kGb;

/// The spec's ETA column has no seconds count, only a label; these are the seconds that
/// Format::etaShort() prints back as that label. -1 is the em dash, 8640000 the infinity.
constexpr qint64 kNoEta = -1;
constexpr qint64 kInfiniteEta = 8640000;

/// One row of spec §1 Fig. 1's `ROWS` array, with its ETA already in seconds.
struct SpecRow
{
    const char *name;
    const char *category;
    qint64 size;
    double progress;
    const char *state; ///< the API's `state` string, see stateFor() below
    int seeds;
    int peers;
    qint64 dlspeed;
    qint64 upspeed;
    qint64 eta;
};

/// The figure's `ST` table maps eight short keys to the Status column's words. The API
/// state strings below are the ones whose Torrent::stateText() prints exactly that word:
///   down → downloading   seed → uploading ("seeding")   stall → stalledDL
///   check → checkingUP   err → error      queue → queuedDL
///   stop → stoppedDL ("stopped")          done → stoppedUP ("completed")
/// `stop` is the one place where the figure and qBittorrent disagree: the figure draws a
/// 100 %-complete torrent labelled "stopped", which a real server would call `stoppedUP`
/// and omaseed would then label "completed". The label wins here (it is what the figure is
/// compared against), so this one row is also counted under the rail's `downloading` group.
const SpecRow kSpecRows[] = {
    {"archlinux-2026.09.01-x86_64.iso",
     "linux",
     qint64(1.31 * double(kGb)),
     0.489,
     "downloading",
     21,
     42,
     qint64(1.84 * double(kMb)),
     112 * kKb,
     160},
    {"Fedora-Workstation-Live-44-1.6.x86_64.iso",
     "linux",
     qint64(2.34 * double(kGb)),
     0.124,
     "downloading",
     8,
     19,
     qint64(1.02 * double(kMb)),
     0,
     1140},
    {"Cosmos Laundromat 2K (CC-BY)",
     "movies",
     qint64(3.12 * double(kGb)),
     0.872,
     "downloading",
     40,
     12,
     940 * kKb,
     220 * kKb,
     425},
    {"debian-13.1.0-amd64-DVD-1.iso",
     "linux",
     qint64(3.98 * double(kGb)),
     0.031,
     "downloading",
     3,
     6,
     410 * kKb,
     0,
     9660},
    {"Wikipedia enwiki-20260901-pages-articles.xml.bz2",
     "",
     qint64(24.7 * double(kGb)),
     0.610,
     "downloading",
     14,
     9,
     0,
     0,
     kInfiniteEta},
    {"Blender 5.1 LTS linux-x64.tar.xz",
     "linux",
     412 * kMb,
     1.0,
     "uploading",
     0,
     3,
     0,
     480 * kKb,
     kNoEta},
    {"Sintel 1080p (CC-BY)",
     "movies",
     qint64(1.09 * double(kGb)),
     1.0,
     "uploading",
     0,
     1,
     0,
     0,
     kNoEta},
    {"Big Buck Bunny 4K (CC-BY)",
     "movies",
     qint64(3.92 * double(kGb)),
     1.0,
     "uploading",
     0,
     7,
     0,
     0,
     kNoEta},
    {"Tears of Steel 4K (CC-BY)",
     "movies",
     qint64(6.20 * double(kGb)),
     1.0,
     "uploading",
     0,
     0,
     0,
     0,
     kNoEta},
    {"ubuntu-26.04.1-desktop-amd64.iso",
     "linux",
     qint64(6.11 * double(kGb)),
     1.0,
     "uploading",
     0,
     12,
     0,
     0,
     kNoEta},
    {"OpenStreetMap planet-260908.osm.pbf",
     "",
     qint64(81.4 * double(kGb)),
     1.0,
     "stoppedDL",
     0,
     0,
     0,
     0,
     kNoEta},
    {"NixOS minimal 26.05 x86_64.iso",
     "linux",
     qint64(1.02 * double(kGb)),
     0.227,
     "stalledDL",
     0,
     2,
     0,
     0,
     kInfiniteEta},
    {"LibreOffice 26.8.1 Linux x86_64 rpm.tar.gz",
     "linux",
     330 * kMb,
     1.0,
     "checkingUP",
     0,
     0,
     0,
     0,
     kNoEta},
    {"Elephants Dream 1080p (CC-BY)",
     "movies",
     qint64(2.03 * double(kGb)),
     0.0,
     "error",
     0,
     0,
     0,
     0,
     kNoEta},
    {"omarchy-4.0.4-x86_64.iso",
     "linux",
     qint64(1.95 * double(kGb)),
     0.0,
     "queuedDL",
     0,
     0,
     0,
     0,
     kNoEta},
    {"Spring (2019) 4K (CC-BY)",
     "movies",
     qint64(4.61 * double(kGb)),
     1.0,
     "stoppedUP",
     0,
     4,
     0,
     0,
     kNoEta},
};

constexpr int kSpecRowCount = int(std::size(kSpecRows));

/// The rail's category rows (spec §1 Fig. 1), with the save path the details pane shows.
struct DemoCategory
{
    const char *name;
    const char *savePath;
};

const DemoCategory kCategories[] = {
    {"linux", "/data/torrents/linux"},
    {"movies", "/data/torrents/movies"},
    {"tv", "/data/torrents/tv"},
    {"music", "/data/torrents/music"},
};

const char *const kTags[] = {"private", "keep", "4k"};

/// Fictional announce URLs (RFC 2606 reserves example.org / example.net).
const char *const kMoviesTracker = "udp://tracker.example.org:1337/announce";
const char *const kLinuxTracker = "udp://tracker.example.net:6969/announce";

/// A stable, unique, 40-hex infohash for row `index`: four hex digits of the row number in
/// front of the name's SHA-1. The demo library must look the same every run (the views key
/// their cursor and selection on the hash), and `sync/maindata` sends its torrents as a JSON
/// object, which Qt keeps sorted by key — putting the row number first is what makes the
/// table come out in the figure's order instead of in hash order.
QString hashFor(int index, const QString &name)
{
    const QByteArray digest =
        QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("%1").arg(index, 4, 16, QLatin1Char('0')) +
           QString::fromLatin1(digest).left(36);
}

QString savePathFor(const QString &category)
{
    for (const DemoCategory &entry : kCategories) {
        if (category == QLatin1StringView(entry.name))
            return QString::fromLatin1(entry.savePath);
    }
    return QStringLiteral("/data/torrents");
}

/// The announce URL a torrent belongs to; an empty string is the rail's `trackerless` row.
QString trackerFor(const QString &category, bool trackerless)
{
    if (trackerless)
        return QString();
    return category == QLatin1String("movies") || category == QLatin1String("tv") ||
                   category == QLatin1String("music")
               ? QString::fromLatin1(kMoviesTracker)
               : QString::fromLatin1(kLinuxTracker);
}

/// Builds one torrent object with every key TorrentModel reads, derived from the few the
/// figure fixes.
QJsonObject torrentJson(int index,
                        const QString &name,
                        const QString &category,
                        const QStringList &tags,
                        qint64 size,
                        double progress,
                        const QString &state,
                        int seeds,
                        int peers,
                        qint64 dlspeed,
                        qint64 upspeed,
                        qint64 eta,
                        bool trackerError,
                        qint64 addedOn)
{
    const QString tracker = trackerFor(category, /*trackerless=*/category.isEmpty());
    const QString savePath = savePathFor(category);
    const qint64 completed = qint64(double(size) * progress);

    QJsonObject json;
    json.insert(QStringLiteral("name"), name);
    json.insert(QStringLiteral("size"), size);
    json.insert(QStringLiteral("total_size"), size);
    json.insert(QStringLiteral("progress"), progress);
    json.insert(QStringLiteral("state"), state);
    json.insert(QStringLiteral("dlspeed"), dlspeed);
    json.insert(QStringLiteral("upspeed"), upspeed);
    json.insert(QStringLiteral("eta"), eta);
    json.insert(QStringLiteral("num_seeds"), seeds);
    json.insert(QStringLiteral("num_complete"), seeds + 4);
    json.insert(QStringLiteral("num_leechs"), peers);
    json.insert(QStringLiteral("num_incomplete"), peers + 2);
    json.insert(QStringLiteral("ratio"), progress >= 1.0 ? 1.87 : 0.21);
    json.insert(QStringLiteral("category"), category);
    json.insert(QStringLiteral("tags"), tags.join(QStringLiteral(", ")));
    json.insert(QStringLiteral("save_path"), savePath);
    json.insert(QStringLiteral("content_path"), savePath + QLatin1Char('/') + name);
    json.insert(QStringLiteral("added_on"), addedOn);
    json.insert(QStringLiteral("completion_on"), progress >= 1.0 ? addedOn + 3600 : 0);
    json.insert(QStringLiteral("tracker"), tracker);
    json.insert(QStringLiteral("trackers_count"), tracker.isEmpty() ? 0 : 2);
    json.insert(QStringLiteral("has_tracker_error"), trackerError);
    json.insert(QStringLiteral("priority"), 0);
    json.insert(QStringLiteral("seq_dl"), false);
    json.insert(QStringLiteral("f_l_piece_prio"), false);
    json.insert(QStringLiteral("dl_limit"), 0);
    json.insert(QStringLiteral("up_limit"), 0);
    json.insert(QStringLiteral("ratio_limit"), -2.0);
    json.insert(QStringLiteral("seeding_time_limit"), -2);
    json.insert(QStringLiteral("availability"), progress >= 1.0 ? 1.0 : progress + 0.4);
    json.insert(QStringLiteral("amount_left"), size - completed);
    json.insert(QStringLiteral("completed"), completed);
    json.insert(QStringLiteral("downloaded"), completed);
    json.insert(QStringLiteral("uploaded"), qint64(double(completed) * 0.6));
    json.insert(QStringLiteral("time_active"), 4210);
    json.insert(QStringLiteral("seeding_time"), progress >= 1.0 ? 3600 : 0);
    json.insert(QStringLiteral("infohash_v1"), hashFor(index, name));
    json.insert(QStringLiteral("infohash_v2"), QString());
    json.insert(QStringLiteral("magnet_uri"),
                QStringLiteral("magnet:?xt=urn:btih:%1&dn=%2").arg(hashFor(index, name), name));
    return json;
}

/// The tags the figure's rail implies: `private` on everything with a tracker, `keep` on
/// the complete ones, `4k` on the names that say so.
QStringList tagsFor(const QString &name, const QString &category, double progress)
{
    QStringList tags;
    if (!category.isEmpty())
        tags.append(QStringLiteral("private"));
    if (progress >= 1.0)
        tags.append(QStringLiteral("keep"));
    if (name.contains(QLatin1String("4K")))
        tags.append(QStringLiteral("4k"));
    return tags;
}

} // namespace

QStringList specNames()
{
    QStringList names;
    names.reserve(kSpecRowCount);
    for (const SpecRow &row : kSpecRows)
        names.append(QString::fromUtf8(row.name));
    return names;
}

QJsonObject serverState()
{
    QJsonObject state;
    state.insert(QStringLiteral("dl_info_speed"), qint64(4.21 * double(kMb)));
    state.insert(QStringLiteral("up_info_speed"), 812 * kKb);
    state.insert(QStringLiteral("dl_info_data"), 412LL * kGb);
    state.insert(QStringLiteral("up_info_data"), 188LL * kGb);
    state.insert(QStringLiteral("free_space_on_disk"), qint64(1.8 * double(kTb)));
    state.insert(QStringLiteral("use_alt_speed_limits"), false);
    state.insert(QStringLiteral("connection_status"), QStringLiteral("connected"));
    state.insert(QStringLiteral("dht_nodes"), 382);
    state.insert(QStringLiteral("total_peer_connections"), 214);
    state.insert(QStringLiteral("dl_rate_limit"), 0);
    state.insert(QStringLiteral("up_rate_limit"), 0);
    state.insert(QStringLiteral("queueing"), true);
    state.insert(QStringLiteral("refresh_interval"), 1500);
    return state;
}

QJsonObject maindata(int rows)
{
    const int count = std::max(1, rows);

    // One fixed seed: the same `rows` must give the same library every run, so a screenshot
    // taken twice is the same screenshot.
    QRandomGenerator random(0x0a5eedu);

    // A fixed "now", so added_on never drifts between two runs of the same demo.
    const qint64 now = QDateTime(QDate(2026, 9, 19), QTime(10, 41)).toSecsSinceEpoch();

    QJsonObject torrents;
    QJsonObject trackers;
    const auto announce = [&trackers](const QString &url, const QString &hash) {
        if (url.isEmpty())
            return;
        QJsonArray hashes = trackers.value(url).toArray();
        hashes.append(hash);
        trackers.insert(url, hashes);
    };

    for (int i = 0; i < count && i < kSpecRowCount; ++i) {
        const SpecRow &row = kSpecRows[i];
        const QString name = QString::fromUtf8(row.name);
        const QString category = QString::fromLatin1(row.category);
        const QString hash = hashFor(i, name);
        const bool trackerError = QLatin1StringView(row.state) == QLatin1String("error");
        torrents.insert(hash,
                        torrentJson(i,
                                    name,
                                    category,
                                    tagsFor(name, category, row.progress),
                                    row.size,
                                    row.progress,
                                    QString::fromLatin1(row.state),
                                    row.seeds,
                                    row.peers,
                                    row.dlspeed,
                                    row.upspeed,
                                    row.eta,
                                    trackerError,
                                    now - 86400LL * (i + 1)));
        announce(trackerFor(category, category.isEmpty()), hash);
    }

    // Everything past the figure: the same shapes, varied deterministically, so a 10 000-row
    // table exercises the views' scrolling, sorting and counting without a server.
    static const char *const kStates[] = {
        "downloading",
        "uploading",
        "stalledDL",
        "stalledUP",
        "stoppedUP",
        "queuedDL",
        "checkingUP",
        "error",
        "metaDL",
        "forcedUP",
    };
    for (int i = kSpecRowCount; i < count; ++i) {
        const int categoryIndex = int(random.bounded(quint32(std::size(kCategories) + 1)));
        const QString category = categoryIndex == int(std::size(kCategories))
                                     ? QString()
                                     : QString::fromLatin1(kCategories[categoryIndex].name);
        const QString state =
            QString::fromLatin1(kStates[random.bounded(quint32(std::size(kStates)))]);
        const bool complete = state != QLatin1String("downloading") &&
                              state != QLatin1String("stalledDL") &&
                              state != QLatin1String("queuedDL") && state != QLatin1String("error");
        const double progress = complete ? 1.0 : double(random.bounded(1000u)) / 1000.0;
        const QString name = QStringLiteral("demo.library.%1.%2")
                                 .arg(category.isEmpty() ? QStringLiteral("misc") : category)
                                 .arg(i, 5, 10, QLatin1Char('0'));
        const QString hash = hashFor(i, name);

        QStringList tags;
        if (random.bounded(2u) == 0)
            tags.append(QString::fromLatin1(kTags[random.bounded(quint32(std::size(kTags)))]));

        const qint64 size = qint64(random.bounded(64u) + 1) * 128 * kMb;
        const qint64 dlspeed =
            state == QLatin1String("downloading") ? qint64(random.bounded(4096u)) * kKb : 0;
        const qint64 upspeed = complete ? qint64(random.bounded(512u)) * kKb : 0;
        const qint64 eta = complete      ? kNoEta
                           : dlspeed > 0 ? qint64(random.bounded(7200u) + 30)
                                         : kInfiniteEta;

        torrents.insert(hash,
                        torrentJson(i,
                                    name,
                                    category,
                                    tags,
                                    size,
                                    progress,
                                    state,
                                    int(random.bounded(64u)),
                                    int(random.bounded(64u)),
                                    dlspeed,
                                    upspeed,
                                    eta,
                                    state == QLatin1String("error"),
                                    now - 60LL * i));
        announce(trackerFor(category, category.isEmpty()), hash);
    }

    QJsonObject categories;
    for (const DemoCategory &entry : kCategories) {
        QJsonObject category;
        category.insert(QStringLiteral("name"), QString::fromLatin1(entry.name));
        category.insert(QStringLiteral("savePath"), QString::fromLatin1(entry.savePath));
        categories.insert(QString::fromLatin1(entry.name), category);
    }

    QJsonArray tags;
    for (const char *const tag : kTags)
        tags.append(QString::fromLatin1(tag));

    QJsonObject data;
    data.insert(QStringLiteral("full_update"), true);
    data.insert(QStringLiteral("rid"), 1);
    data.insert(QStringLiteral("torrents"), torrents);
    data.insert(QStringLiteral("categories"), categories);
    data.insert(QStringLiteral("tags"), tags);
    data.insert(QStringLiteral("trackers"), trackers);
    data.insert(QStringLiteral("server_state"), serverState());
    return data;
}

// --- the details pane (spec §2 Fig. 1-4) ---------------------------------------------------

namespace {

/// The spec's own three figures, by the name of the torrent they belong to.
const char *const kBbbName = "Big Buck Bunny 4K (CC-BY)";
const char *const kArchName = "archlinux-2026.09.01-x86_64.iso";

QJsonObject fileJson(int index, const QString &name, qint64 size, double progress, int priority)
{
    QJsonObject file;
    file.insert(QStringLiteral("index"), index);
    file.insert(QStringLiteral("name"), name);
    file.insert(QStringLiteral("size"), size);
    file.insert(QStringLiteral("progress"), progress);
    file.insert(QStringLiteral("priority"), priority);
    file.insert(QStringLiteral("availability"), progress >= 1.0 ? 1.0 : progress);
    return file;
}

QJsonObject trackerJson(const QString &url,
                        int tier,
                        int status,
                        int seeds,
                        int peers,
                        qint64 nextAnnounce,
                        const QString &msg = QString())
{
    QJsonObject tracker;
    tracker.insert(QStringLiteral("url"), url);
    tracker.insert(QStringLiteral("tier"), tier);
    tracker.insert(QStringLiteral("status"), status);
    tracker.insert(QStringLiteral("num_seeds"), seeds);
    tracker.insert(QStringLiteral("num_peers"), peers);
    tracker.insert(QStringLiteral("num_leeches"), peers);
    tracker.insert(QStringLiteral("num_downloaded"), seeds > 0 ? seeds * 34 : -1);
    tracker.insert(QStringLiteral("next_announce"), nextAnnounce);
    tracker.insert(QStringLiteral("msg"), msg);
    return tracker;
}

QJsonObject peerJson(const QString &ip,
                     int port,
                     const QString &countryCode,
                     const QString &country,
                     const QString &client,
                     qint64 dlSpeed,
                     qint64 upSpeed,
                     double progress)
{
    QJsonObject peer;
    peer.insert(QStringLiteral("ip"), ip);
    peer.insert(QStringLiteral("port"), port);
    peer.insert(QStringLiteral("country_code"), countryCode);
    peer.insert(QStringLiteral("country"), country);
    peer.insert(QStringLiteral("client"), client);
    peer.insert(QStringLiteral("connection"), QStringLiteral("BT"));
    peer.insert(QStringLiteral("flags"), upSpeed > 0 ? QStringLiteral("D U") : QStringLiteral("D"));
    peer.insert(QStringLiteral("flags_desc"), QStringLiteral("D: currently downloading"));
    peer.insert(QStringLiteral("dl_speed"), dlSpeed);
    peer.insert(QStringLiteral("up_speed"), upSpeed);
    peer.insert(QStringLiteral("downloaded"), dlSpeed * 200);
    peer.insert(QStringLiteral("uploaded"), upSpeed * 200);
    peer.insert(QStringLiteral("progress"), progress);
    peer.insert(QStringLiteral("relevance"), progress * 0.5);
    return peer;
}

/// Spec §2 Fig. 2's file tree, to the byte the figure prints for each row. The root folder's
/// aggregate comes out one hundredth under the table's 3.92 GB, because the figure's own
/// per-file numbers do not add up to its folder total; the per-file values win, since those
/// are the rows a reader compares.
QJsonArray bigBuckBunnyFiles()
{
    const QString root = QStringLiteral("Big Buck Bunny 4K/");
    QJsonArray files;
    files.append(
        fileJson(0, root + QStringLiteral("bbb_sunflower_2160p_60fps.mp4"), 4144855449LL, 1.0, 6));
    files.append(
        fileJson(1, root + QStringLiteral("bbb_sunflower_1080p_30fps.mp4"), 54627532LL, 1.0, 1));
    files.append(fileJson(2, root + QStringLiteral("subs/bbb.en.srt"), 9216LL, 0.0, 0));
    files.append(fileJson(3, root + QStringLiteral("subs/bbb.pt.srt"), 9216LL, 0.0, 0));
    files.append(fileJson(4, root + QStringLiteral("LICENSE.txt"), 1024LL, 1.0, 1));
    return files;
}

/// Spec §2 Fig. 3: three announce URLs (the middle one timing out) and DHT/PeX. The hosts
/// are the fictional ones the rest of the demo library announces to.
QJsonArray archTrackers()
{
    QJsonArray trackers;
    trackers.append(trackerJson(
        QStringLiteral("https://tracker.example.net:443/announce"), 0, 2, 1204, 88, 1620));
    trackers.append(trackerJson(QStringLiteral("udp://tracker.example.org:1337/announce"),
                                1,
                                4,
                                -1,
                                -1,
                                240,
                                QStringLiteral("timed out")));
    trackers.append(
        trackerJson(QStringLiteral("udp://open.example.si:80/announce"), 2, 2, 640, 41, 0));
    trackers.append(trackerJson(QStringLiteral("** [DHT] **"), -1, 2, 22, 0, 0));
    trackers.append(trackerJson(QStringLiteral("** [PeX] **"), -1, 2, 9, 0, 0));
    return trackers;
}

/// Spec §2 Fig. 4, with the figure's own speeds and progress.
QJsonObject archPeers()
{
    QJsonObject peers;
    peers.insert(QStringLiteral("185.21.216.140:51413"),
                 peerJson(QStringLiteral("185.21.216.140"),
                          51413,
                          QStringLiteral("DE"),
                          QStringLiteral("Germany"),
                          QStringLiteral("qBittorrent 5.2.3"),
                          1069547,
                          0,
                          1.0));
    peers.insert(QStringLiteral("2a01:4f8:c0c::1:6881"),
                 peerJson(QStringLiteral("2a01:4f8:c0c::1"),
                          6881,
                          QStringLiteral("FI"),
                          QStringLiteral("Finland"),
                          QStringLiteral("Transmission 4.1.1"),
                          761856,
                          12288,
                          1.0));
    peers.insert(QStringLiteral("91.121.58.7:41221"),
                 peerJson(QStringLiteral("91.121.58.7"),
                          41221,
                          QStringLiteral("FR"),
                          QStringLiteral("France"),
                          QStringLiteral("libtorrent 2.0.14"),
                          411648,
                          0,
                          0.884));
    peers.insert(QStringLiteral("77.54.113.202:6889"),
                 peerJson(QStringLiteral("77.54.113.202"),
                          6889,
                          QStringLiteral("PT"),
                          QStringLiteral("Portugal"),
                          QStringLiteral("Deluge 2.2.0"),
                          120832,
                          100352,
                          0.640));

    QJsonObject answer;
    answer.insert(QStringLiteral("full_update"), true);
    answer.insert(QStringLiteral("rid"), 1);
    answer.insert(QStringLiteral("show_flags"), true);
    answer.insert(QStringLiteral("peers"), peers);
    return answer;
}

bool looksLikeOneFile(const QString &name)
{
    static const QStringList suffixes{QStringLiteral(".iso"),
                                      QStringLiteral(".gz"),
                                      QStringLiteral(".xz"),
                                      QStringLiteral(".bz2"),
                                      QStringLiteral(".pbf"),
                                      QStringLiteral(".zip"),
                                      QStringLiteral(".img")};
    for (const QString &suffix : suffixes) {
        if (name.endsWith(suffix, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

/// A plausible file list for a row the spec does not draw: one file for anything that looks
/// like an image or an archive, a small folder tree otherwise.
QJsonArray generatedFiles(const omaseed::core::Torrent &torrent)
{
    QJsonArray files;
    if (looksLikeOneFile(torrent.name) || torrent.size <= 0) {
        files.append(fileJson(0, torrent.name, torrent.size, torrent.progress, 1));
        return files;
    }

    const QString root = torrent.name + QLatin1Char('/');
    const qint64 extras = 1024LL * 1024;
    const qint64 main = std::max(qint64(1), torrent.size - 3 * extras);
    files.append(
        fileJson(0, root + torrent.name + QStringLiteral(".mkv"), main, torrent.progress, 1));
    files.append(fileJson(
        1, root + QStringLiteral("extras/behind-the-scenes.mkv"), 2 * extras, torrent.progress, 1));
    files.append(fileJson(2, root + QStringLiteral("extras/poster.png"), extras / 4, 1.0, 1));
    files.append(fileJson(3, root + QStringLiteral("LICENSE.txt"), 1024, 1.0, 1));
    return files;
}

QJsonArray generatedTrackers(const omaseed::core::Torrent &torrent)
{
    QJsonArray trackers;
    if (!torrent.tracker.isEmpty()) {
        trackers.append(
            trackerJson(torrent.tracker,
                        0,
                        torrent.hasTrackerError ? 4 : 2,
                        torrent.hasTrackerError ? -1 : torrent.numComplete,
                        torrent.hasTrackerError ? -1 : torrent.numIncomplete,
                        torrent.hasTrackerError ? 240 : 1620,
                        torrent.hasTrackerError ? QStringLiteral("timed out") : QString()));
    }
    trackers.append(trackerJson(QStringLiteral("** [DHT] **"), -1, 2, torrent.numSeeds, 0, 0));
    trackers.append(trackerJson(QStringLiteral("** [PeX] **"), -1, 2, 0, 0, 0));
    trackers.append(trackerJson(QStringLiteral("** [LSD] **"), -1, 0, 0, 0, 0));
    return trackers;
}

QJsonObject generatedPeers(const omaseed::core::Torrent &torrent)
{
    // Seeded from the torrent's own hash, so the same row always shows the same swarm.
    QRandomGenerator random(quint32(qHash(torrent.hash)));
    static const char *const kClients[] = {"qBittorrent 5.2.3",
                                           "Transmission 4.1.1",
                                           "libtorrent 2.0.14",
                                           "Deluge 2.2.0",
                                           "rTorrent 0.15.3"};
    static const char *const kCodes[] = {"DE", "FI", "FR", "PT", "NL", "US", "JP", "BR"};
    static const char *const kCountries[] = {"Germany",
                                             "Finland",
                                             "France",
                                             "Portugal",
                                             "Netherlands",
                                             "United States",
                                             "Japan",
                                             "Brazil"};

    const int wanted = std::clamp(torrent.numLeechs, 0, 8);
    QJsonObject peers;
    for (int i = 0; i < wanted; ++i) {
        const int which = int(random.bounded(quint32(std::size(kCodes))));
        // 198.51.100.0/24 and 203.0.113.0/24 are RFC 5737 documentation ranges.
        const QString ip =
            QStringLiteral("%1.%2")
                .arg(i % 2 == 0 ? QStringLiteral("198.51.100") : QStringLiteral("203.0.113"))
                .arg(random.bounded(1u, 254u));
        const int port = int(random.bounded(6881u, 65000u));
        const qint64 dl =
            torrent.dlspeed > 0 ? qint64(random.bounded(quint32(kKb), quint32(2 * kMb))) : 0;
        const qint64 up = torrent.upspeed > 0 ? qint64(random.bounded(quint32(64 * kKb))) : 0;
        peers.insert(
            QStringLiteral("%1:%2").arg(ip).arg(port),
            peerJson(ip,
                     port,
                     QString::fromLatin1(kCodes[which]),
                     QString::fromLatin1(kCountries[which]),
                     QString::fromLatin1(kClients[random.bounded(quint32(std::size(kClients)))]),
                     dl,
                     up,
                     double(random.bounded(1000u)) / 1000.0));
    }

    QJsonObject answer;
    answer.insert(QStringLiteral("full_update"), true);
    answer.insert(QStringLiteral("rid"), 1);
    answer.insert(QStringLiteral("show_flags"), true);
    answer.insert(QStringLiteral("peers"), peers);
    return answer;
}

} // namespace

QJsonObject details(const omaseed::core::Torrent &torrent)
{
    QJsonObject result;
    if (torrent.hash.isEmpty())
        return result;

    const bool isArch = torrent.name == QLatin1StringView(kArchName);
    const bool isBbb = torrent.name == QLatin1StringView(kBbbName);

    // Pieces: a power-of-two piece size that lands in the few-thousand range a real torrent
    // of this size would use. Spec §2 Fig. 1 prints "1 264 × 1 MiB · 617 have"; the figure's
    // own numbers are kept for that one row.
    qint64 pieceSize = 1024 * kKb;
    while (pieceSize > 16 * kKb && torrent.size / pieceSize < 512)
        pieceSize /= 2;
    int pieceCount = int(std::max(qint64(1), (torrent.size + pieceSize - 1) / pieceSize));
    int piecesHave = int(double(pieceCount) * torrent.progress);
    if (isArch) {
        pieceSize = 1024 * kKb;
        pieceCount = 1264;
        piecesHave = 617;
    }

    QJsonObject properties;
    properties.insert(QStringLiteral("name"), torrent.name);
    properties.insert(QStringLiteral("save_path"), torrent.savePath + QLatin1Char('/'));
    properties.insert(QStringLiteral("infohash_v1"), torrent.infohashV1);
    properties.insert(QStringLiteral("infohash_v2"), torrent.infohashV2);
    properties.insert(QStringLiteral("total_size"), torrent.size);
    properties.insert(QStringLiteral("total_downloaded"), torrent.downloaded);
    properties.insert(QStringLiteral("total_uploaded"), torrent.uploaded);
    properties.insert(QStringLiteral("total_wasted"), isArch ? 2202009LL : torrent.size / 640);
    properties.insert(QStringLiteral("piece_size"), pieceSize);
    properties.insert(QStringLiteral("pieces_num"), pieceCount);
    properties.insert(QStringLiteral("pieces_have"), piecesHave);
    // The API's availability is the number of distributed copies, not a fraction.
    properties.insert(QStringLiteral("availability"),
                      isArch ? 38.2 : double(torrent.numComplete) + torrent.progress);
    properties.insert(QStringLiteral("seeds"), torrent.numSeeds);
    properties.insert(QStringLiteral("seeds_total"), isArch ? 1204 : torrent.numComplete);
    properties.insert(QStringLiteral("peers"), torrent.numLeechs);
    properties.insert(QStringLiteral("peers_total"), isArch ? 88 : torrent.numIncomplete);
    properties.insert(QStringLiteral("time_elapsed"), isArch ? 862 : torrent.timeActive);
    properties.insert(QStringLiteral("created_by"), QStringLiteral("mktorrent 1.1"));
    properties.insert(QStringLiteral("comment"),
                      isArch ? QStringLiteral("Arch Linux 2026.09.01 release")
                             : QStringLiteral("%1 · shared with omaseed").arg(torrent.name));
    // Spec §2 Fig. 1 has no "Created on" row, so the demo leaves the date out.
    properties.insert(QStringLiteral("creation_date"), -1);

    result.insert(QStringLiteral("hash"), torrent.hash);
    result.insert(QStringLiteral("properties"), properties);
    result.insert(QStringLiteral("files"), isBbb ? bigBuckBunnyFiles() : generatedFiles(torrent));
    result.insert(QStringLiteral("trackers"), isArch ? archTrackers() : generatedTrackers(torrent));
    result.insert(QStringLiteral("peers"), isArch ? archPeers() : generatedPeers(torrent));
    return result;
}

} // namespace omaseed::app::DemoData
