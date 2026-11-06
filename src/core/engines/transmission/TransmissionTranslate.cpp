#include "core/engines/transmission/TransmissionTranslate.hpp"

#include "core/PieceStates.hpp"

#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <limits>

namespace omaseed::core::TransmissionTranslate {

namespace {

QString key(const char *name)
{
    return QString::fromLatin1(name);
}

bool has(const QJsonObject &json, const char *name)
{
    return json.contains(key(name));
}

qint64 number(const QJsonObject &json, const char *name, qint64 fallback = 0)
{
    const QJsonValue value = json.value(key(name));
    return value.isDouble() ? qint64(value.toDouble()) : fallback;
}

/// A date in seconds since the epoch; 0 (or nothing) is no date.
QDateTime date(const QJsonObject &json, const char *name)
{
    const qint64 seconds = number(json, name);
    return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds) : QDateTime();
}

/// Transmission's tr_activity, rpc-spec §3.3 `status`.
enum Status {
    Stopped = 0,
    CheckWait = 1,
    Check = 2,
    DownloadWait = 3,
    Download = 4,
    SeedWait = 5,
    Seed = 6,
};

/// tr_stat's `error`: 1 and 2 are a tracker's, only 3 is the torrent's own (engine matrix §4.4).
constexpr int kLocalError = 3;
constexpr int kTrackerError = 2;

/// tr_ratiolimit and tr_idlelimit: the session's, the torrent's own, none.
constexpr int kLimitGlobal = 0;
constexpr int kLimitOwn = 1;

/// tr_priority_t.
constexpr int kPriorityLow = -1;
constexpr int kPriorityHigh = 1;

/// tr_tracker_state: TR_TRACKER_ACTIVE is an announce under way.
constexpr int kAnnounceActive = 3;

RatioLimit ratioLimit(int mode, double value)
{
    if (mode == kLimitGlobal)
        return RatioLimit::global();
    if (mode == kLimitOwn)
        return RatioLimit::of(value);
    return RatioLimit::unlimited();
}

MinutesLimit minutesLimit(int mode, qint64 value)
{
    if (mode == kLimitGlobal)
        return MinutesLimit::global();
    if (mode == kLimitOwn)
        return MinutesLimit::of(value);
    return MinutesLimit::unlimited();
}

/// The largest count any tracker reported (-1 means it didn't); nothing when none did.
std::optional<int> swarmCount(const QJsonArray &stats, const char *name)
{
    std::optional<int> best;
    for (const QJsonValue &value : stats) {
        const int count = int(number(value.toObject(), name, -1));
        if (count >= 0 && (!best || count > *best))
            best = count;
    }
    return best;
}

QString joined(const QString &folder, const QString &name)
{
    if (folder.isEmpty())
        return name;
    return folder.endsWith(QLatin1Char('/')) ? folder + name : folder + QLatin1Char('/') + name;
}

/// tracker_list as tiers of URLs.
QList<QStringList> tiersOf(const QString &list)
{
    QList<QStringList> tiers;
    QStringList tier;
    for (const QString &raw : list.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            if (!tier.isEmpty())
                tiers.append(tier);
            tier.clear();
            continue;
        }
        tier.append(line);
    }
    if (!tier.isEmpty())
        tiers.append(tier);
    return tiers;
}

QString listOf(const QList<QStringList> &tiers)
{
    QStringList parts;
    for (const QStringList &tier : tiers) {
        if (!tier.isEmpty())
            parts.append(tier.join(QLatin1Char('\n')));
    }
    return parts.isEmpty() ? QString() : parts.join(QStringLiteral("\n\n")) + QLatin1Char('\n');
}

} // namespace

// --- what to ask for ------------------------------------------------------------------------

QStringList sessionFields()
{
    return {QStringLiteral("units"),
            QStringLiteral("download_dir"),
            QStringLiteral("download_queue_enabled"),
            QStringLiteral("seed_queue_enabled"),
            QStringLiteral("incomplete_dir"),
            QStringLiteral("incomplete_dir_enabled"),
            QStringLiteral("speed_limit_down"),
            QStringLiteral("speed_limit_down_enabled"),
            QStringLiteral("speed_limit_up"),
            QStringLiteral("speed_limit_up_enabled"),
            QStringLiteral("alt_speed_enabled"),
            QStringLiteral("alt_speed_down"),
            QStringLiteral("alt_speed_up"),
            QStringLiteral("alt_speed_time_enabled"),
            QStringLiteral("alt_speed_time_begin"),
            QStringLiteral("alt_speed_time_end"),
            QStringLiteral("alt_speed_time_day")};
}

QStringList listFields()
{
    return {QStringLiteral("id"),
            QStringLiteral("hash_string"),
            QStringLiteral("name"),
            QStringLiteral("status"),
            QStringLiteral("error"),
            QStringLiteral("error_string"),
            QStringLiteral("metadata_percent_complete"),
            QStringLiteral("percent_done"),
            QStringLiteral("size_when_done"),
            QStringLiteral("total_size"),
            QStringLiteral("left_until_done"),
            QStringLiteral("have_valid"),
            QStringLiteral("rate_download"),
            QStringLiteral("rate_upload"),
            QStringLiteral("eta"),
            QStringLiteral("is_stalled"),
            QStringLiteral("peers_sending_to_us"),
            QStringLiteral("peers_getting_from_us"),
            QStringLiteral("webseeds_sending_to_us"),
            QStringLiteral("upload_ratio"),
            QStringLiteral("labels"),
            QStringLiteral("download_dir"),
            QStringLiteral("added_date"),
            QStringLiteral("done_date"),
            QStringLiteral("activity_date"),
            QStringLiteral("trackers"),
            QStringLiteral("tracker_stats"),
            QStringLiteral("queue_position"),
            QStringLiteral("sequential_download"),
            QStringLiteral("is_private"),
            QStringLiteral("download_limit"),
            QStringLiteral("download_limited"),
            QStringLiteral("upload_limit"),
            QStringLiteral("upload_limited"),
            QStringLiteral("seed_ratio_limit"),
            QStringLiteral("seed_ratio_mode"),
            QStringLiteral("seed_idle_limit"),
            QStringLiteral("seed_idle_mode"),
            QStringLiteral("downloaded_ever"),
            QStringLiteral("uploaded_ever"),
            QStringLiteral("seconds_downloading"),
            QStringLiteral("seconds_seeding"),
            QStringLiteral("magnet_link")};
}

QStringList propertiesFields()
{
    return {QStringLiteral("hash_string"),
            QStringLiteral("name"),
            QStringLiteral("download_dir"),
            QStringLiteral("creator"),
            QStringLiteral("comment"),
            QStringLiteral("total_size"),
            QStringLiteral("downloaded_ever"),
            QStringLiteral("uploaded_ever"),
            QStringLiteral("corrupt_ever"),
            QStringLiteral("piece_count"),
            QStringLiteral("piece_size"),
            QStringLiteral("pieces"),
            QStringLiteral("peers_connected"),
            QStringLiteral("peer_limit"),
            QStringLiteral("peers_sending_to_us"),
            QStringLiteral("peers_getting_from_us"),
            QStringLiteral("tracker_stats"),
            QStringLiteral("seconds_downloading"),
            QStringLiteral("seconds_seeding"),
            QStringLiteral("date_created"),
            QStringLiteral("done_date"),
            QStringLiteral("is_private"),
            QStringLiteral("metadata_percent_complete")};
}

QStringList filesFields()
{
    return {QStringLiteral("files"), QStringLiteral("file_stats")};
}

QStringList trackersFields()
{
    return {QStringLiteral("tracker_stats")};
}

QStringList peersFields()
{
    return {QStringLiteral("peers")};
}

QStringList webSeedsFields()
{
    return {QStringLiteral("webseeds")};
}

QStringList piecesFields()
{
    return {QStringLiteral("pieces"), QStringLiteral("piece_count")};
}

// --- the way in -----------------------------------------------------------------------------

Session session(const QJsonObject &sessionGet)
{
    Session s;
    const qint64 bytes =
        number(sessionGet.value(QStringLiteral("units")).toObject(), "speed_bytes", s.speedBytes);
    if (bytes > 0)
        s.speedBytes = bytes;
    if (has(sessionGet, "download_queue_enabled") || has(sessionGet, "seed_queue_enabled"))
        s.queueing = sessionGet.value(QStringLiteral("download_queue_enabled")).toBool() ||
                     sessionGet.value(QStringLiteral("seed_queue_enabled")).toBool();
    if (sessionGet.value(QStringLiteral("incomplete_dir_enabled")).toBool())
        s.incompleteDir = sessionGet.value(QStringLiteral("incomplete_dir")).toString();
    return s;
}

QList<QJsonObject> rows(const QJsonObject &result)
{
    const QJsonArray torrents = result.value(QStringLiteral("torrents")).toArray();
    QList<QJsonObject> objects;
    if (torrents.isEmpty())
        return objects;
    if (torrents.first().isObject()) {
        objects.reserve(torrents.size());
        for (const QJsonValue &value : torrents)
            objects.append(value.toObject());
        return objects;
    }
    // format "table": the first row names the keys, each other row is one torrent.
    const QJsonArray keys = torrents.first().toArray();
    objects.reserve(torrents.size() - 1);
    for (qsizetype row = 1; row < torrents.size(); ++row) {
        const QJsonArray values = torrents.at(row).toArray();
        QJsonObject object;
        for (qsizetype i = 0; i < keys.size() && i < values.size(); ++i)
            object.insert(keys.at(i).toString(), values.at(i));
        objects.append(object);
    }
    return objects;
}

TorrentStatus status(const QJsonObject &torrent)
{
    using P = TorrentPhase;
    using F = TorrentFlag;
    static const QString words[] = {QStringLiteral("stopped"),
                                    QStringLiteral("check_wait"),
                                    QStringLiteral("checking"),
                                    QStringLiteral("download_wait"),
                                    QStringLiteral("downloading"),
                                    QStringLiteral("seed_wait"),
                                    QStringLiteral("seeding")};

    TorrentStatus s;
    const int code = int(number(torrent, "status", -1));
    if (code < Stopped || code > Seed)
        return s; // Unknown, no word
    s.word = words[code];

    const bool hasMetadata = number(torrent, "metadata_percent_complete", 1) >= 1;
    const bool done =
        hasMetadata && has(torrent, "left_until_done") && number(torrent, "left_until_done") == 0;
    if (done)
        s.flags |= F::Finished;

    if (number(torrent, "error") == kLocalError) {
        // A local error stops the torrent: Errored, its own words beside it (C25 K24).
        s.phase = P::Errored;
        s.word = QStringLiteral("error");
        return s;
    }

    const bool stalled = torrent.value(QStringLiteral("is_stalled")).toBool();
    switch (Status(code)) {
    case Stopped:
        s.phase = P::Stopped;
        break;
    case CheckWait:
        s.phase = P::Checking;
        s.flags |= F::Queued;
        break;
    case Check:
        s.phase = P::Checking;
        break;
    case DownloadWait:
        s.phase = hasMetadata ? P::Downloading : P::Metadata;
        s.flags |= F::Queued;
        break;
    case Download:
        s.phase = hasMetadata ? P::Downloading : P::Metadata;
        // Nothing coming in and nobody to get it from, or idle past the stall timer.
        if (stalled ||
            (number(torrent, "rate_download") == 0 && number(torrent, "peers_sending_to_us") == 0 &&
             number(torrent, "webseeds_sending_to_us") == 0))
            s.flags |= F::Stalled;
        break;
    case SeedWait:
        s.phase = P::Seeding;
        s.flags |= F::Queued | F::Finished;
        break;
    case Seed:
        s.phase = P::Seeding;
        s.flags |= F::Finished;
        if (stalled ||
            (number(torrent, "rate_upload") == 0 && number(torrent, "peers_getting_from_us") == 0))
            s.flags |= F::Stalled;
        break;
    }
    return s;
}

TorrentPatch torrent(const QJsonObject &json, const Session &session)
{
    using F = TorrentField;
    TorrentPatch p(json.value(QStringLiteral("hash_string")).toString().toLower());
    const bool hasMetadata = number(json, "metadata_percent_complete", 1) >= 1;

    if (has(json, "name"))
        p.set(&Torrent::name, F::Name, json.value(QStringLiteral("name")).toString());
    if (has(json, "size_when_done"))
        p.set(&Torrent::size, F::Size, number(json, "size_when_done"));
    if (has(json, "total_size"))
        p.set(&Torrent::totalSize, F::TotalSize, hasMetadata ? number(json, "total_size") : -1);
    if (has(json, "percent_done"))
        p.set(&Torrent::progress,
              F::Progress,
              hasMetadata ? json.value(QStringLiteral("percent_done")).toDouble() : 0.0);
    if (has(json, "status"))
        p.set(&Torrent::status, F::Status, status(json));
    if (has(json, "rate_download"))
        p.set(&Torrent::dlspeed, F::DlSpeed, number(json, "rate_download"));
    if (has(json, "rate_upload"))
        p.set(&Torrent::upspeed, F::UpSpeed, number(json, "rate_upload"));
    if (has(json, "eta")) {
        const qint64 eta = number(json, "eta", -1); // -1 not available, -2 unknown
        p.set(&Torrent::eta, F::Eta, eta >= 0 ? std::optional<qint64>(eta) : std::nullopt);
    }
    if (has(json, "peers_sending_to_us"))
        p.set(&Torrent::numSeeds, F::NumSeeds, int(number(json, "peers_sending_to_us")));
    if (has(json, "peers_getting_from_us"))
        p.set(&Torrent::numLeechs, F::NumLeechs, int(number(json, "peers_getting_from_us")));
    if (has(json, "upload_ratio")) {
        const double ratio = json.value(QStringLiteral("upload_ratio")).toDouble();
        // TR_RATIO_INF is -2 (uploaded with nothing downloaded), TR_RATIO_NA -1.
        p.set(&Torrent::ratio,
              F::Ratio,
              ratio <= -2.0 ? std::numeric_limits<double>::infinity()
              : ratio < 0.0 ? 0.0
                            : ratio);
    }
    if (has(json, "labels")) {
        QStringList labels;
        for (const QJsonValue &label : json.value(QStringLiteral("labels")).toArray())
            labels.append(label.toString());
        p.set(&Torrent::tags, F::Tags, labels); // labels in the tags place (C14 E5, C25 B)
    }
    if (has(json, "download_dir")) {
        const QString folder = json.value(QStringLiteral("download_dir")).toString();
        p.set(&Torrent::savePath, F::SavePath, folder);
        // Unfinished torrents sit in the incomplete folder when it is on (matrix §4.3).
        const bool done =
            has(json, "left_until_done") && number(json, "left_until_done") == 0 && hasMetadata;
        const QString where =
            !session.incompleteDir.isEmpty() && !done ? session.incompleteDir : folder;
        if (has(json, "name"))
            p.set(&Torrent::contentPath,
                  F::ContentPath,
                  joined(where, json.value(QStringLiteral("name")).toString()));
        p.set(&Torrent::downloadPath,
              F::DownloadPath,
              !session.incompleteDir.isEmpty() ? session.incompleteDir : QString());
    }
    if (has(json, "added_date"))
        p.set(&Torrent::addedOn, F::AddedOn, date(json, "added_date"));
    if (has(json, "done_date"))
        p.set(&Torrent::completionOn, F::CompletionOn, date(json, "done_date"));
    if (has(json, "activity_date"))
        p.set(&Torrent::lastActivity, F::LastActivity, date(json, "activity_date"));
    if (has(json, "trackers"))
        p.set(&Torrent::trackersCount,
              F::TrackersCount,
              int(json.value(QStringLiteral("trackers")).toArray().size()));
    if (has(json, "tracker_stats")) {
        const QJsonArray stats = json.value(QStringLiteral("tracker_stats")).toArray();
        QString current;
        bool announced = false;
        bool worked = false;
        for (const QJsonValue &value : stats) {
            const QJsonObject stat = value.toObject();
            if (!stat.value(QStringLiteral("has_announced")).toBool())
                continue;
            announced = true;
            if (stat.value(QStringLiteral("last_announce_succeeded")).toBool()) {
                worked = true;
                if (current.isEmpty())
                    current = stat.value(QStringLiteral("announce")).toString();
            }
        }
        p.set(&Torrent::tracker, F::Tracker, current);
        // Every tracker that answered failed, or one refused it (tr_stat error 2).
        p.set(&Torrent::hasTrackerError,
              F::HasTrackerError,
              (announced && !worked) || number(json, "error") == kTrackerError);
        if (const auto seeds = swarmCount(stats, "seeder_count"))
            p.set(&Torrent::numComplete, F::NumComplete, *seeds);
        if (const auto leeches = swarmCount(stats, "leecher_count"))
            p.set(&Torrent::numIncomplete, F::NumIncomplete, *leeches);
    }
    if (has(json, "queue_position"))
        p.set(&Torrent::priority,
              F::QueuePosition,
              session.queueing ? int(number(json, "queue_position")) + 1 : -1);
    if (has(json, "sequential_download"))
        p.set(
            &Torrent::seqDl, F::SeqDl, json.value(QStringLiteral("sequential_download")).toBool());
    if (has(json, "is_private") && hasMetadata)
        p.set(&Torrent::isPrivate, F::IsPrivate, json.value(QStringLiteral("is_private")).toBool());
    if (has(json, "download_limited"))
        p.set(&Torrent::dlLimit,
              F::DlLimit,
              json.value(QStringLiteral("download_limited")).toBool()
                  ? number(json, "download_limit") * session.speedBytes
                  : 0);
    if (has(json, "upload_limited"))
        p.set(&Torrent::upLimit,
              F::UpLimit,
              json.value(QStringLiteral("upload_limited")).toBool()
                  ? number(json, "upload_limit") * session.speedBytes
                  : 0);
    if (has(json, "seed_ratio_mode"))
        p.set(&Torrent::ratioLimit,
              F::RatioLimit,
              ratioLimit(int(number(json, "seed_ratio_mode")),
                         json.value(QStringLiteral("seed_ratio_limit")).toDouble()));
    if (has(json, "seed_idle_mode"))
        p.set(&Torrent::inactiveSeedingTimeLimit,
              F::InactiveSeedingTimeLimit,
              minutesLimit(int(number(json, "seed_idle_mode")), number(json, "seed_idle_limit")));
    if (has(json, "left_until_done"))
        p.set(&Torrent::amountLeft, F::AmountLeft, number(json, "left_until_done"));
    if (has(json, "have_valid"))
        p.set(&Torrent::completed, F::Completed, number(json, "have_valid"));
    if (has(json, "downloaded_ever"))
        p.set(&Torrent::downloaded, F::Downloaded, number(json, "downloaded_ever"));
    if (has(json, "uploaded_ever"))
        p.set(&Torrent::uploaded, F::Uploaded, number(json, "uploaded_ever"));
    if (has(json, "seconds_downloading") && has(json, "seconds_seeding"))
        p.set(&Torrent::timeActive,
              F::TimeActive,
              number(json, "seconds_downloading") + number(json, "seconds_seeding"));
    if (has(json, "seconds_seeding"))
        p.set(&Torrent::seedingTime, F::SeedingTime, number(json, "seconds_seeding"));
    if (has(json, "hash_string"))
        p.set(&Torrent::infohashV1, F::InfohashV1, p.hash);
    if (has(json, "magnet_link"))
        p.set(&Torrent::magnetUri,
              F::MagnetUri,
              json.value(QStringLiteral("magnet_link")).toString());
    if (has(json, "error"))
        p.set(&Torrent::errorMessage,
              F::ErrorMessage,
              number(json, "error") == kLocalError
                  ? json.value(QStringLiteral("error_string")).toString()
                  : QString());
    return p;
}

QStringList trackerUrls(const QJsonObject &json)
{
    QStringList urls;
    for (const QJsonValue &value : json.value(QStringLiteral("trackers")).toArray()) {
        const QString url = value.toObject().value(QStringLiteral("announce")).toString();
        if (!url.isEmpty() && !urls.contains(url))
            urls.append(url);
    }
    return urls;
}

ServerStatePatch serverState(const QJsonObject &stats,
                             const QJsonObject &sessionGet,
                             const std::optional<QJsonObject> &freeSpace)
{
    ServerStatePatch s;
    // Transmission reports no network state like libtorrent's connection_status (its port test
    // asks a site outside): "" says so, rather than the "disconnected" a missing value reads.
    s.connectionStatus = QString();
    if (has(stats, "download_speed"))
        s.dlSpeed = number(stats, "download_speed");
    if (has(stats, "upload_speed"))
        s.upSpeed = number(stats, "upload_speed");
    const QJsonObject current = stats.value(QStringLiteral("current_stats")).toObject();
    if (has(current, "downloaded_bytes"))
        s.dlSession = number(current, "downloaded_bytes");
    if (has(current, "uploaded_bytes"))
        s.upSession = number(current, "uploaded_bytes");
    const QJsonObject cumulative = stats.value(QStringLiteral("cumulative_stats")).toObject();
    if (has(cumulative, "downloaded_bytes")) {
        s.allTimeDownload = number(cumulative, "downloaded_bytes");
        s.allTimeUpload = number(cumulative, "uploaded_bytes");
        if (*s.allTimeDownload > 0)
            s.globalRatio = double(*s.allTimeUpload) / double(*s.allTimeDownload);
    }

    const Session session = TransmissionTranslate::session(sessionGet);
    if (has(sessionGet, "alt_speed_enabled")) {
        const bool alt = sessionGet.value(QStringLiteral("alt_speed_enabled")).toBool();
        s.altSpeedLimits = alt;
        const GlobalLimits limits = globalLimits(sessionGet);
        s.dlRateLimit = alt ? limits.altDownload.value_or(0) : limits.download.value_or(0);
        s.upRateLimit = alt ? limits.altUpload.value_or(0) : limits.upload.value_or(0);
    }
    if (has(sessionGet, "download_queue_enabled") || has(sessionGet, "seed_queue_enabled"))
        s.queueing = session.queueing;
    if (freeSpace && has(*freeSpace, "size_bytes") && number(*freeSpace, "size_bytes") >= 0)
        s.freeSpaceOnDisk = number(*freeSpace, "size_bytes");
    return s;
}

GlobalLimits globalLimits(const QJsonObject &sessionGet)
{
    const qint64 unit = session(sessionGet).speedBytes;
    const auto limit = [&sessionGet, unit](const char *value, const char *enabled) {
        return sessionGet.value(key(enabled)).toBool() ? number(sessionGet, value) * unit : 0;
    };
    GlobalLimits limits;
    if (has(sessionGet, "speed_limit_down_enabled"))
        limits.download = limit("speed_limit_down", "speed_limit_down_enabled");
    if (has(sessionGet, "speed_limit_up_enabled"))
        limits.upload = limit("speed_limit_up", "speed_limit_up_enabled");
    if (has(sessionGet, "alt_speed_down"))
        limits.altDownload = number(sessionGet, "alt_speed_down") * unit;
    if (has(sessionGet, "alt_speed_up"))
        limits.altUpload = number(sessionGet, "alt_speed_up") * unit;
    return limits;
}

QVariantMap speedPreferences(const QJsonObject &sessionGet)
{
    QVariantMap prefs;
    const GlobalLimits limits = globalLimits(sessionGet);
    if (limits.download)
        prefs.insert(QStringLiteral("dl_limit"), *limits.download);
    if (limits.upload)
        prefs.insert(QStringLiteral("up_limit"), *limits.upload);
    if (limits.altDownload)
        prefs.insert(QStringLiteral("alt_dl_limit"), *limits.altDownload);
    if (limits.altUpload)
        prefs.insert(QStringLiteral("alt_up_limit"), *limits.altUpload);
    if (has(sessionGet, "alt_speed_time_enabled"))
        prefs.insert(QStringLiteral("scheduler_enabled"),
                     sessionGet.value(QStringLiteral("alt_speed_time_enabled")).toBool());
    if (has(sessionGet, "alt_speed_time_begin") && has(sessionGet, "alt_speed_time_end") &&
        has(sessionGet, "alt_speed_time_day")) {
        const qint64 begin = number(sessionGet, "alt_speed_time_begin");
        const qint64 end = number(sessionGet, "alt_speed_time_end");
        prefs.insert(QStringLiteral("schedule_from_hour"), int(begin / 60));
        prefs.insert(QStringLiteral("schedule_from_min"), int(begin % 60));
        prefs.insert(QStringLiteral("schedule_to_hour"), int(end / 60));
        prefs.insert(QStringLiteral("schedule_to_min"), int(end % 60));
        prefs.insert(QStringLiteral("scheduler_day_mask"),
                     int(number(sessionGet, "alt_speed_time_day")));
    }
    if (has(sessionGet, "download_dir"))
        prefs.insert(QStringLiteral("save_path"),
                     sessionGet.value(QStringLiteral("download_dir")).toString());
    return prefs;
}

QJsonObject speedSessionSet(const QVariantMap &changes, const QJsonObject &sessionGet, QString *why)
{
    const qint64 unit = session(sessionGet).speedBytes;
    const auto minutes = [&changes, &sessionGet](
                             const char *hourKey, const char *minuteKey, const char *field) {
        const qint64 was = number(sessionGet, field);
        const qint64 hour =
            changes.contains(key(hourKey)) ? changes.value(key(hourKey)).toLongLong() : was / 60;
        const qint64 minute = changes.contains(key(minuteKey))
                                  ? changes.value(key(minuteKey)).toLongLong()
                                  : was % 60;
        return std::clamp<qint64>(hour * 60 + minute, 0, 24 * 60 - 1);
    };
    QJsonObject params;
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        const QString &name = it.key();
        const qint64 value = it.value().toLongLong();
        if (name == QLatin1String("dl_limit") || name == QLatin1String("up_limit")) {
            const QString field = name == QLatin1String("dl_limit")
                                      ? QStringLiteral("speed_limit_down")
                                      : QStringLiteral("speed_limit_up");
            params.insert(field + QStringLiteral("_enabled"), value > 0);
            if (value > 0)
                params.insert(field, speedUnits(value, unit));
        } else if (name == QLatin1String("alt_dl_limit")) {
            params.insert(QStringLiteral("alt_speed_down"), speedUnits(value, unit));
        } else if (name == QLatin1String("alt_up_limit")) {
            params.insert(QStringLiteral("alt_speed_up"), speedUnits(value, unit));
        } else if (name == QLatin1String("scheduler_enabled")) {
            params.insert(QStringLiteral("alt_speed_time_enabled"), it.value().toBool());
        } else if (name == QLatin1String("schedule_from_hour") ||
                   name == QLatin1String("schedule_from_min")) {
            params.insert(
                QStringLiteral("alt_speed_time_begin"),
                minutes("schedule_from_hour", "schedule_from_min", "alt_speed_time_begin"));
        } else if (name == QLatin1String("schedule_to_hour") ||
                   name == QLatin1String("schedule_to_min")) {
            params.insert(QStringLiteral("alt_speed_time_end"),
                          minutes("schedule_to_hour", "schedule_to_min", "alt_speed_time_end"));
        } else if (name == QLatin1String("scheduler_day_mask")) {
            params.insert(QStringLiteral("alt_speed_time_day"), int(value & 127));
        } else if (name == QLatin1String("save_path")) {
            params.insert(QStringLiteral("download_dir"), it.value().toString());
        } else {
            if (why != nullptr)
                *why = QStringLiteral("Transmission has no setting for %1").arg(name);
            return {};
        }
    }
    return params;
}

TorrentProperties properties(const QJsonObject &json, qint64 nowSecs)
{
    TorrentProperties p;
    p.hash = json.value(QStringLiteral("hash_string")).toString().toLower();
    p.infohashV1 = p.hash;
    p.name = json.value(QStringLiteral("name")).toString();
    p.savePath = json.value(QStringLiteral("download_dir")).toString();
    p.createdBy = json.value(QStringLiteral("creator")).toString();
    p.comment = json.value(QStringLiteral("comment")).toString();
    p.totalSize = number(json, "total_size");
    p.totalDownloaded = number(json, "downloaded_ever");
    p.totalUploaded = number(json, "uploaded_ever");
    p.totalWasted = number(json, "corrupt_ever");
    p.piecesNum = int(number(json, "piece_count"));
    p.pieceSize = number(json, "piece_size");
    p.piecesHave = PieceStates::count(pieceStates(json), PieceStates::Have);
    p.seeds = int(number(json, "peers_sending_to_us"));
    p.peers = int(number(json, "peers_getting_from_us"));
    if (has(json, "peers_connected"))
        p.connections = int(number(json, "peers_connected"));
    p.connectionsLimit = int(number(json, "peer_limit"));
    const QJsonArray stats = json.value(QStringLiteral("tracker_stats")).toArray();
    p.seedsTotal = swarmCount(stats, "seeder_count").value_or(0);
    p.peersTotal = swarmCount(stats, "leecher_count").value_or(0);
    qint64 next = 0;
    for (const QJsonValue &value : stats) {
        const qint64 at = number(value.toObject(), "next_announce_time");
        if (at > nowSecs && (next == 0 || at < next))
            next = at;
    }
    p.reannounce = next > 0 ? next - nowSecs : 0;
    p.timeElapsed = number(json, "seconds_downloading") + number(json, "seconds_seeding");
    p.seedingTime = number(json, "seconds_seeding");
    p.createdOn = date(json, "date_created");
    p.completedOn = date(json, "done_date");
    if (has(json, "is_private") && number(json, "metadata_percent_complete", 1) >= 1)
        p.isPrivate = json.value(QStringLiteral("is_private")).toBool();
    // Not reported by Transmission, so left out rather than shown as nothing (C25 K29): a
    // complete copy last seen, distributed copies, average speeds, a v2 hash.
    return p;
}

QList<ContentFile> files(const QJsonObject &json)
{
    const QJsonArray files = json.value(QStringLiteral("files")).toArray();
    const QJsonArray stats = json.value(QStringLiteral("file_stats")).toArray();
    QList<ContentFile> list;
    list.reserve(files.size());
    for (qsizetype i = 0; i < files.size(); ++i) {
        const QJsonObject file = files.at(i).toObject();
        ContentFile entry;
        entry.index = int(i); // the file's position is its index for files_wanted & co.
        entry.path = file.value(QStringLiteral("name")).toString();
        entry.size = number(file, "length");
        const qint64 completed = number(file, "bytes_completed");
        entry.progress = entry.size > 0 ? double(completed) / double(entry.size) : 1.0;
        if (i < stats.size()) {
            const QJsonObject stat = stats.at(i).toObject();
            entry.priority = filePriority(stat.value(QStringLiteral("wanted")).toBool(true),
                                          int(number(stat, "priority")));
        }
        if (has(file, "begin_piece") && has(file, "end_piece")) {
            // end_piece is one past the file's last piece.
            const int begin = int(number(file, "begin_piece"));
            const int end = int(number(file, "end_piece"));
            entry.pieces = std::pair<int, int>(begin, std::max(begin, end - 1));
        }
        list.append(entry);
    }
    return list;
}

int filePriority(bool wanted, int priority)
{
    if (!wanted)
        return int(FilePriority::Skip);
    if (priority <= kPriorityLow)
        return int(FilePriority::Low);
    if (priority >= kPriorityHigh)
        return int(FilePriority::High);
    return int(FilePriority::Normal);
}

QList<TrackerEntry> trackers(const QJsonObject &json)
{
    QList<TrackerEntry> list;
    for (const QJsonValue &value : json.value(QStringLiteral("tracker_stats")).toArray()) {
        const QJsonObject stat = value.toObject();
        TrackerEntry entry;
        entry.url = stat.value(QStringLiteral("announce")).toString();
        entry.tier = int(number(stat, "tier", -1));
        const bool announced = stat.value(QStringLiteral("has_announced")).toBool();
        const bool succeeded = stat.value(QStringLiteral("last_announce_succeeded")).toBool();
        if (number(stat, "announce_state") == kAnnounceActive)
            entry.status = int(TrackerStatus::Updating);
        else if (!announced)
            entry.status = int(TrackerStatus::NotContacted);
        else if (succeeded)
            entry.status = int(TrackerStatus::Working);
        else if (stat.value(QStringLiteral("last_announce_timed_out")).toBool())
            entry.status = int(TrackerStatus::Unreachable);
        else
            entry.status = int(TrackerStatus::NotWorking);
        const auto count = [&stat](const char *name) -> std::optional<int> {
            const qint64 n = number(stat, name, -1);
            return n >= 0 ? std::optional<int>(int(n)) : std::nullopt;
        };
        entry.seeds = count("seeder_count");
        entry.leeches = count("leecher_count");
        entry.downloaded = count("download_count");
        if (announced)
            entry.peers = int(number(stat, "last_announce_peer_count"));
        const qint64 next = number(stat, "next_announce_time");
        if (next > 0)
            entry.nextAnnounce = next;
        if (announced && !succeeded)
            entry.message = stat.value(QStringLiteral("last_announce_result")).toString();
        list.append(entry);
    }
    return list;
}

PeersDelta peers(const QJsonObject &json)
{
    PeersDelta delta;
    delta.full = true; // the whole list, every time
    for (const QJsonValue &value : json.value(QStringLiteral("peers")).toArray()) {
        const QJsonObject peer = value.toObject();
        PeerPatch patch;
        const QString ip = peer.value(QStringLiteral("address")).toString();
        const int port = int(number(peer, "port"));
        patch.address = ip.contains(QLatin1Char(':')) ? QStringLiteral("[%1]:%2").arg(ip).arg(port)
                                                      : QStringLiteral("%1:%2").arg(ip).arg(port);
        patch.ip = ip;
        patch.port = port;
        patch.client = peer.value(QStringLiteral("client_name")).toString();
        patch.connection = peer.value(QStringLiteral("is_utp")).toBool() ? QStringLiteral("μTP")
                                                                         : QStringLiteral("BT");
        patch.flags = peer.value(QStringLiteral("flag_str")).toString();
        patch.dlSpeed = number(peer, "rate_to_client");
        patch.upSpeed = number(peer, "rate_to_peer");
        if (has(peer, "bytes_to_client"))
            patch.downloaded = number(peer, "bytes_to_client");
        if (has(peer, "bytes_to_peer"))
            patch.uploaded = number(peer, "bytes_to_peer");
        patch.progress = peer.value(QStringLiteral("progress")).toDouble();
        delta.peers.append(patch);
    }
    return delta;
}

QStringList webSeeds(const QJsonObject &json)
{
    QStringList urls;
    for (const QJsonValue &value : json.value(QStringLiteral("webseeds")).toArray())
        urls.append(value.toString());
    return urls;
}

QByteArray pieceStates(const QJsonObject &json)
{
    const int count = int(number(json, "piece_count"));
    if (count <= 0)
        return {};
    const QByteArray bits =
        QByteArray::fromBase64(json.value(QStringLiteral("pieces")).toString().toLatin1());
    QByteArray states(count, char(PieceStates::Missing));
    for (int i = 0; i < count; ++i) {
        const qsizetype byte = i / 8;
        if (byte < bits.size() && (quint8(bits.at(byte)) & (0x80 >> (i % 8))) != 0)
            states[i] = char(PieceStates::Have);
    }
    return states;
}

EngineFeatures features(const ServerInfo &)
{
    // Board 47's A–K table, Transmission's column (DECISIONS C25); 4.1 is the minimum (C14 E3),
    // so nothing here depends on the version yet.
    EngineFeatures f;
    f.categories = false;        // B, K16: bandwidth groups carry no folder; left out
    f.categorySavePaths = false; // B
    f.tags = true;               // B, K14: labels, in the tags place (C14 E5)
    // C, K17: skip · low · normal · high.
    f.filePriorities = {int(FilePriority::Skip),
                        int(FilePriority::Low),
                        int(FilePriority::Normal),
                        int(FilePriority::High)};
    f.shareLimitActions = {};    // D, K18: a limit always stops the torrent
    f.seedingTimeLimit = false;  // D, K18: ratio and idle time only
    f.altSpeeds = true;          // D, K20: turtle mode and its clock
    f.queue = true;              // matrix §4.5: queue_move_up/down
    f.forceStart = true;         // matrix §4.5: torrent_start_now (no forced mark comes back)
    f.sequentialDownload = true; // 4.1
    f.firstLastPiece = false;    // I
    f.superSeeding = false;      // I
    f.autoManagement = false;    // I
    f.fetchMetadata = false;     // G, K26: a stopped magnet never connects
    f.addOptions = false;        // G, K27: no name, layout, first and last, skip check at add
    f.renameOnDisk = true;       // G, K28: renaming renames its folder on disk
    f.peerControl = false;       // H: neither add nor ban
    f.editWebSeeds = false;      // H, K29: read only
    f.engineLog = false;         // I, K30: no log face, no ~
    f.qbtSettings = false;       // I, J
    f.torrentsBackup = false;    // I, K33
    f.rss = false;               // I
    f.addAgain = false;          // I: C17 D3's re-add is libtorrent's hybrid fix
    f.search = false;            // I (#145)
    f.createTorrent = false;     // I (#146): transmission-create is a command only
    return f;
}

// --- the way out ----------------------------------------------------------------------------

qint64 speedUnits(qint64 bytesPerSec, qint64 speedBytes)
{
    if (bytesPerSec <= 0)
        return 0;
    const qint64 unit = speedBytes > 0 ? speedBytes : 1000;
    return (bytesPerSec + unit - 1) / unit;
}

QString trackerListWith(const QString &list, const QStringList &urls)
{
    QList<QStringList> tiers = tiersOf(list);
    QStringList known;
    for (const QStringList &tier : std::as_const(tiers))
        known += tier;
    for (const QString &raw : urls) {
        const QString url = raw.trimmed();
        if (url.isEmpty() || known.contains(url))
            continue;
        known.append(url);
        tiers.append(QStringList{url});
    }
    return listOf(tiers);
}

QString trackerListWithout(const QString &list, const QStringList &urls)
{
    QList<QStringList> tiers = tiersOf(list);
    for (QStringList &tier : tiers) {
        for (const QString &url : urls)
            tier.removeAll(url.trimmed());
    }
    return listOf(tiers);
}

} // namespace omaseed::core::TransmissionTranslate
