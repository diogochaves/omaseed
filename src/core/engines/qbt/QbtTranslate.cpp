#include "core/engines/qbt/QbtTranslate.hpp"

#include "core/PieceStates.hpp"
#include "core/TorrentFile.hpp"

#include <QDateTime>
#include <QJsonValue>
#include <QVersionNumber>

#include <limits>

namespace omaseed::core::QbtTranslate {

namespace {

/// qBittorrent's "no estimate" (MAX_ETA, 100 days).
constexpr qint64 kNoEta = 8640000;
/// qBittorrent's ratio for "uploaded with nothing downloaded" (MAX_RATIO).
constexpr double kMaxRatio = 9999.0;
/// Past this, next_announce is a time, not seconds left (older servers send the latter).
constexpr qint64 kEpochThreshold = 1'000'000'000;

QLatin1StringView key(const char *name)
{
    return QLatin1StringView(name);
}

QStringList splitTags(const QJsonValue &value)
{
    const QString raw = value.toString();
    if (raw.isEmpty())
        return {};
    QStringList tags = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &tag : tags)
        tag = tag.trimmed();
    tags.removeAll(QString());
    return tags;
}

QDateTime timestamp(const QJsonValue &value)
{
    const qint64 seconds = value.toInteger(0);
    return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds) : QDateTime();
}

/// -2 the server's default, -1 none (torrents/setShareLimits' encoding).
RatioLimit ratioLimit(double wire)
{
    if (wire <= -2.0)
        return RatioLimit::global();
    if (wire < 0.0)
        return RatioLimit::unlimited();
    return RatioLimit::of(wire);
}

MinutesLimit minutesLimit(qint64 wire)
{
    if (wire <= -2)
        return MinutesLimit::global();
    if (wire < 0)
        return MinutesLimit::unlimited();
    return MinutesLimit::of(wire);
}

QStringList stringList(const QJsonValue &value)
{
    QStringList list;
    const QJsonArray array = value.toArray();
    list.reserve(array.size());
    for (const QJsonValue &item : array)
        list.append(item.toString());
    return list;
}

/// Sets `field` from `json[name]` through `convert`, when the key is there.
template<class T, class Convert>
void take(const QJsonObject &json,
          const char *name,
          TorrentPatch &patch,
          T Torrent::*member,
          TorrentField field,
          Convert convert)
{
    const auto it = json.constFind(key(name));
    if (it == json.constEnd())
        return;
    patch.set(member, field, T(convert(it.value())));
}

template<class T, class Convert>
void takeOptional(const QJsonObject &json,
                  const char *name,
                  std::optional<T> &field,
                  Convert convert)
{
    const auto it = json.constFind(key(name));
    if (it == json.constEnd())
        return;
    field = T(convert(it.value()));
}

const auto asString = [](const QJsonValue &v) { return v.toString(); };
const auto asInteger = [](const QJsonValue &v) { return v.toInteger(0); };
const auto asInt = [](const QJsonValue &v) { return int(v.toInteger(0)); };
const auto asDouble = [](const QJsonValue &v) { return v.toDouble(0.0); };
const auto asBool = [](const QJsonValue &v) { return v.toBool(false); };

struct StateWord
{
    QLatin1StringView word;
    TorrentPhase phase;
    TorrentFlags flags;
};

/// Every `state` word of WebUI API 2.x, the pre-5.0 paused* spellings and 4.x's
/// queuedForChecking included. The *UP words are the finished side.
const QList<StateWord> &stateWords()
{
    using P = TorrentPhase;
    using F = TorrentFlag;
    static const QList<StateWord> table{
        {QLatin1StringView("downloading"), P::Downloading, {}},
        {QLatin1StringView("stalledDL"), P::Downloading, F::Stalled},
        {QLatin1StringView("metaDL"), P::Metadata, {}},
        {QLatin1StringView("forcedMetaDL"), P::Metadata, F::Forced},
        {QLatin1StringView("forcedDL"), P::Downloading, F::Forced},
        {QLatin1StringView("queuedDL"), P::Downloading, F::Queued},
        {QLatin1StringView("checkingDL"), P::Checking, {}},
        {QLatin1StringView("uploading"), P::Seeding, F::Finished},
        {QLatin1StringView("stalledUP"), P::Seeding, F::Stalled | F::Finished},
        {QLatin1StringView("forcedUP"), P::Seeding, F::Forced | F::Finished},
        {QLatin1StringView("queuedUP"), P::Seeding, F::Queued | F::Finished},
        {QLatin1StringView("checkingUP"), P::Checking, F::Finished},
        {QLatin1StringView("stoppedDL"), P::Stopped, {}},
        {QLatin1StringView("stoppedUP"), P::Stopped, F::Finished},
        {QLatin1StringView("pausedDL"), P::Stopped, {}},
        {QLatin1StringView("pausedUP"), P::Stopped, F::Finished},
        {QLatin1StringView("error"), P::Errored, {}},
        {QLatin1StringView("missingFiles"), P::Missing, {}},
        {QLatin1StringView("checkingResumeData"), P::Loading, {}},
        {QLatin1StringView("queuedForChecking"), P::Checking, F::Queued},
        {QLatin1StringView("moving"), P::Moving, {}},
        {QLatin1StringView("allocating"), P::Allocating, {}},
    };
    return table;
}

} // namespace

TorrentStatus status(QStringView word)
{
    TorrentStatus status;
    status.word = word.toString();
    for (const StateWord &entry : stateWords()) {
        if (word == entry.word) {
            status.phase = entry.phase;
            status.flags = entry.flags;
            break;
        }
    }
    return status;
}

TorrentPatch torrent(const QString &hash, const QJsonObject &json)
{
    using F = TorrentField;
    using T = Torrent;
    TorrentPatch p(hash);
    take(json, "name", p, &T::name, F::Name, asString);
    take(json, "size", p, &T::size, F::Size, asInteger);
    take(json, "total_size", p, &T::totalSize, F::TotalSize, asInteger);
    take(json, "progress", p, &T::progress, F::Progress, asDouble);
    take(json, "state", p, &T::status, F::Status, [](const QJsonValue &v) {
        return status(v.toString());
    });
    take(json, "dlspeed", p, &T::dlspeed, F::DlSpeed, asInteger);
    take(json, "upspeed", p, &T::upspeed, F::UpSpeed, asInteger);
    take(json, "eta", p, &T::eta, F::Eta, [](const QJsonValue &v) -> std::optional<qint64> {
        const qint64 seconds = v.toInteger(0);
        if (seconds >= kNoEta)
            return std::nullopt;
        return seconds;
    });
    take(json, "num_seeds", p, &T::numSeeds, F::NumSeeds, asInt);
    take(json, "num_complete", p, &T::numComplete, F::NumComplete, asInt);
    take(json, "num_leechs", p, &T::numLeechs, F::NumLeechs, asInt);
    take(json, "num_incomplete", p, &T::numIncomplete, F::NumIncomplete, asInt);
    take(json, "ratio", p, &T::ratio, F::Ratio, [](const QJsonValue &v) {
        const double ratio = v.toDouble(0.0);
        return ratio >= kMaxRatio ? std::numeric_limits<double>::infinity() : ratio;
    });
    take(json, "category", p, &T::category, F::Category, asString);
    take(json, "tags", p, &T::tags, F::Tags, splitTags);
    take(json, "save_path", p, &T::savePath, F::SavePath, asString);
    take(json, "content_path", p, &T::contentPath, F::ContentPath, asString);
    take(json, "download_path", p, &T::downloadPath, F::DownloadPath, asString);
    take(json, "added_on", p, &T::addedOn, F::AddedOn, timestamp);
    take(json, "completion_on", p, &T::completionOn, F::CompletionOn, timestamp);
    take(json, "last_activity", p, &T::lastActivity, F::LastActivity, timestamp);
    take(json, "tracker", p, &T::tracker, F::Tracker, asString);
    take(json, "trackers_count", p, &T::trackersCount, F::TrackersCount, asInt);
    take(json, "has_tracker_error", p, &T::hasTrackerError, F::HasTrackerError, asBool);
    take(json, "priority", p, &T::priority, F::QueuePosition, asInt);
    take(json, "seq_dl", p, &T::seqDl, F::SeqDl, asBool);
    take(json, "f_l_piece_prio", p, &T::firstLastPiecePrio, F::FirstLastPiecePrio, asBool);
    take(json, "super_seeding", p, &T::superSeeding, F::SuperSeeding, asBool);
    take(json, "auto_tmm", p, &T::autoTmm, F::AutoTmm, asBool);
    take(json, "private", p, &T::isPrivate, F::IsPrivate, asBool);
    // 0 is no limit; older servers spelled it -1.
    const auto rate = [](const QJsonValue &v) { return std::max<qint64>(0, v.toInteger(0)); };
    take(json, "dl_limit", p, &T::dlLimit, F::DlLimit, rate);
    take(json, "up_limit", p, &T::upLimit, F::UpLimit, rate);
    take(json, "ratio_limit", p, &T::ratioLimit, F::RatioLimit, [](const QJsonValue &v) {
        return ratioLimit(v.toDouble(-2.0));
    });
    take(json,
         "seeding_time_limit",
         p,
         &T::seedingTimeLimit,
         F::SeedingTimeLimit,
         [](const QJsonValue &v) { return minutesLimit(v.toInteger(-2)); });
    take(json,
         "inactive_seeding_time_limit",
         p,
         &T::inactiveSeedingTimeLimit,
         F::InactiveSeedingTimeLimit,
         [](const QJsonValue &v) { return minutesLimit(v.toInteger(-2)); });
    take(json, "share_limit_action", p, &T::shareLimitAction, F::ShareLimitAction, asString);
    take(json, "availability", p, &T::availability, F::Availability, asDouble);
    take(json, "amount_left", p, &T::amountLeft, F::AmountLeft, asInteger);
    take(json, "completed", p, &T::completed, F::Completed, asInteger);
    take(json, "downloaded", p, &T::downloaded, F::Downloaded, asInteger);
    take(json, "uploaded", p, &T::uploaded, F::Uploaded, asInteger);
    take(json, "uploaded_session", p, &T::uploadedSession, F::UploadedSession, asInteger);
    take(json, "time_active", p, &T::timeActive, F::TimeActive, asInteger);
    take(json, "seeding_time", p, &T::seedingTime, F::SeedingTime, asInteger);
    take(json, "infohash_v1", p, &T::infohashV1, F::InfohashV1, asString);
    take(json, "infohash_v2", p, &T::infohashV2, F::InfohashV2, asString);
    take(json, "magnet_uri", p, &T::magnetUri, F::MagnetUri, asString);
    return p;
}

QList<TorrentPatch> torrents(const QJsonObject &map)
{
    QList<TorrentPatch> patches;
    patches.reserve(map.size());
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        patches.append(torrent(it.key(), it.value().toObject()));
    return patches;
}

ServerStatePatch serverState(const QJsonObject &s)
{
    ServerStatePatch state;
    takeOptional(s, "dl_info_speed", state.dlSpeed, asInteger);
    takeOptional(s, "up_info_speed", state.upSpeed, asInteger);
    takeOptional(s, "dl_info_data", state.dlSession, asInteger);
    takeOptional(s, "up_info_data", state.upSession, asInteger);
    takeOptional(s, "free_space_on_disk", state.freeSpaceOnDisk, asInteger);
    takeOptional(s, "use_alt_speed_limits", state.altSpeedLimits, asBool);
    takeOptional(s, "connection_status", state.connectionStatus, asString);
    takeOptional(s, "dht_nodes", state.dhtNodes, asInt);
    takeOptional(s, "total_peer_connections", state.peerConnections, asInt);
    takeOptional(s, "dl_rate_limit", state.dlRateLimit, asInteger);
    takeOptional(s, "up_rate_limit", state.upRateLimit, asInteger);
    takeOptional(s, "queueing", state.queueing, asBool);
    takeOptional(s, "refresh_interval", state.refreshInterval, asInt);
    takeOptional(s, "alltime_ul", state.allTimeUpload, asInteger);
    takeOptional(s, "alltime_dl", state.allTimeDownload, asInteger);
    // "2.16" in qBittorrent 5.x; a number is taken too, anything else is "unknown".
    takeOptional(s, "global_ratio", state.globalRatio, [](const QJsonValue &v) {
        if (v.isDouble())
            return v.toDouble();
        bool ok = false;
        const double ratio = v.toString().toDouble(&ok);
        return ok ? ratio : -1.0;
    });
    return state;
}

QList<CategoryPatch> categories(const QJsonObject &map)
{
    QList<CategoryPatch> list;
    list.reserve(map.size());
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        const QJsonObject object = it.value().toObject();
        CategoryPatch category;
        category.name = object.value(key("name")).toString(it.key());
        if (category.name.isEmpty())
            category.name = it.key();
        // A partial update carries only the keys that changed (e.g. just `download_path`).
        if (object.contains(key("savePath")))
            category.savePath = object.value(key("savePath")).toString();
        list.append(category);
    }
    return list;
}

QHash<QString, QStringList> trackerMap(const QJsonObject &map)
{
    QHash<QString, QStringList> trackers;
    trackers.reserve(map.size());
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        trackers.insert(it.key(), stringList(it.value()));
    return trackers;
}

SyncDelta syncDelta(const QJsonObject &data)
{
    SyncDelta delta;
    delta.full = data.value(key("full_update")).toBool(false);
    delta.torrents = torrents(data.value(key("torrents")).toObject());
    delta.removedTorrents = stringList(data.value(key("torrents_removed")));
    delta.categories = categories(data.value(key("categories")).toObject());
    delta.removedCategories = stringList(data.value(key("categories_removed")));
    delta.tags = stringList(data.value(key("tags")));
    delta.removedTags = stringList(data.value(key("tags_removed")));
    delta.trackers = trackerMap(data.value(key("trackers")).toObject());
    delta.removedTrackers = stringList(data.value(key("trackers_removed")));
    const QJsonValue state = data.value(key("server_state"));
    if (state.isObject())
        delta.serverState = serverState(state.toObject());
    const QJsonValue rid = data.value(key("rid"));
    if (rid.isDouble())
        delta.cursor = rid.toInt();
    return delta;
}

TorrentProperties properties(const QJsonObject &json)
{
    const auto number = [&json](const char *name) {
        return qint64(json.value(key(name)).toDouble());
    };
    const auto text = [&json](const char *name) { return json.value(key(name)).toString(); };
    // qBittorrent marks "not known" with -1 (or leaves the key out on an older server).
    const auto has = [&json](const char *name) {
        const QJsonValue value = json.value(key(name));
        return !value.isUndefined() && !value.isNull();
    };
    const auto when = [&](const char *name) {
        const qint64 seconds = number(name);
        return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds) : QDateTime();
    };

    TorrentProperties p;
    p.hash = text("hash");
    p.name = text("name");
    p.savePath = text("save_path");
    p.infohashV1 = text("infohash_v1");
    p.infohashV2 = text("infohash_v2");
    p.createdBy = text("created_by");
    p.comment = text("comment");
    p.totalSize = number("total_size");
    p.totalDownloaded = number("total_downloaded");
    p.totalUploaded = number("total_uploaded");
    p.totalWasted = number("total_wasted");
    p.piecesNum = int(number("pieces_num"));
    p.pieceSize = number("piece_size");
    p.piecesHave = int(number("pieces_have"));
    // -1 when there is no figure (every seeding torrent).
    const double availability = json.value(key("availability")).toDouble();
    if (availability >= 0)
        p.availability = availability;
    p.seeds = int(number("seeds"));
    p.seedsTotal = int(number("seeds_total"));
    p.peers = int(number("peers"));
    p.peersTotal = int(number("peers_total"));
    p.timeElapsed = number("time_elapsed");
    p.createdOn = when("creation_date");
    if (has("nb_connections"))
        p.connections = int(number("nb_connections"));
    p.connectionsLimit = int(number("nb_connections_limit"));
    if (has("last_seen"))
        p.lastSeen = when("last_seen");
    p.seedingTime = number("seeding_time");
    p.completedOn = when("completion_date");
    if (has("dl_speed_avg"))
        p.dlSpeedAvg = number("dl_speed_avg");
    if (has("up_speed_avg"))
        p.upSpeedAvg = number("up_speed_avg");
    p.reannounce = number("reannounce");
    // `private` (5.0+) is null until the metadata says; older servers only send
    // `is_private`, which reads false for a magnet that has no metadata yet.
    const QJsonValue privateValue = json.value(key("private"));
    const QJsonValue isPrivate = json.value(key("is_private"));
    if (privateValue.isBool())
        p.isPrivate = privateValue.toBool();
    else if (privateValue.isUndefined() && isPrivate.isBool() && p.piecesNum > 0)
        p.isPrivate = isPrivate.toBool();
    return p;
}

std::optional<int> filePriority(int wire)
{
    switch (wire) {
    case 0:
        return int(FilePriority::Skip);
    case 6:
        return int(FilePriority::High);
    case 7:
        return int(FilePriority::Maximal);
    default:
        // 1 is normal; libtorrent's in-between levels read as normal too.
        if (wire >= 1 && wire <= 5)
            return int(FilePriority::Normal);
        return std::nullopt;
    }
}

QList<ContentFile> files(const QJsonArray &array)
{
    QList<ContentFile> list;
    list.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject file = value.toObject();
        ContentFile entry;
        const QJsonValue index = file.value(key("index"));
        if (index.isDouble())
            entry.index = index.toInt();
        entry.path = file.value(key("name")).toString();
        entry.size = qint64(file.value(key("size")).toDouble());
        entry.progress = file.value(key("progress")).toDouble();
        const QJsonValue priority = file.value(key("priority"));
        if (priority.isDouble())
            entry.priority = filePriority(priority.toInt());
        const double availability = file.value(key("availability")).toDouble(-1.0);
        if (availability >= 0.0)
            entry.availability = availability;
        const QJsonArray range = file.value(key("piece_range")).toArray();
        if (range.size() == 2)
            entry.pieces = std::pair{range.at(0).toInt(-1), range.at(1).toInt(-1)};
        list.append(entry);
    }
    return list;
}

QList<TrackerEntry> trackers(const QJsonArray &array, std::optional<qint64> nowSecs)
{
    const qint64 now = nowSecs.value_or(QDateTime::currentSecsSinceEpoch());
    const auto count = [](const QJsonValue &value) -> std::optional<int> {
        const int n = value.toInt(-1);
        if (n < 0)
            return std::nullopt;
        return n;
    };
    QList<TrackerEntry> list;
    list.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject json = value.toObject();
        TrackerEntry entry;
        entry.url = json.value(key("url")).toString();
        if (entry.url.isEmpty())
            continue;
        // DHT, PeX and LSD come as "** [DHT] **" rows.
        if (entry.url == QLatin1String("** [DHT] **"))
            entry.source = TrackerSource::Dht;
        else if (entry.url == QLatin1String("** [PeX] **"))
            entry.source = TrackerSource::Pex;
        else if (entry.url == QLatin1String("** [LSD] **"))
            entry.source = TrackerSource::Lsd;
        entry.tier = json.value(key("tier")).toInt(-1);
        entry.status = json.value(key("status")).toInt(int(TrackerStatus::NotContacted));
        entry.seeds = count(json.value(key("num_seeds")));
        entry.peers = count(json.value(key("num_peers")));
        entry.leeches = count(json.value(key("num_leeches")));
        entry.downloaded = count(json.value(key("num_downloaded")));
        // A time in 5.x; seconds left on older servers.
        const qint64 next = qint64(json.value(key("next_announce")).toDouble());
        if (next >= kEpochThreshold)
            entry.nextAnnounce = next;
        else if (next > 0)
            entry.nextAnnounce = now + next;
        entry.message = json.value(key("msg")).toString();
        list.append(entry);
    }
    return list;
}

PeersDelta peers(const QJsonObject &answer)
{
    PeersDelta delta;
    delta.full = answer.value(key("full_update")).toBool();
    const QJsonValue rid = answer.value(key("rid"));
    if (rid.isDouble())
        delta.cursor = rid.toInt();
    const QJsonObject map = answer.value(key("peers")).toObject();
    delta.peers.reserve(map.size());
    const auto asQint64 = [](const QJsonValue &v) { return qint64(v.toDouble()); };
    const auto asPeerInt = [](const QJsonValue &v) { return v.toInt(); };
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        const QJsonObject json = it.value().toObject();
        PeerPatch peer;
        peer.address = it.key();
        takeOptional(json, "ip", peer.ip, asString);
        takeOptional(json, "port", peer.port, asPeerInt);
        takeOptional(json, "country", peer.country, asString);
        takeOptional(json, "country_code", peer.countryCode, asString);
        takeOptional(json, "client", peer.client, asString);
        takeOptional(json, "connection", peer.connection, asString);
        takeOptional(json, "flags", peer.flags, asString);
        takeOptional(json, "flags_desc", peer.flagsDesc, asString);
        takeOptional(json, "dl_speed", peer.dlSpeed, asQint64);
        takeOptional(json, "up_speed", peer.upSpeed, asQint64);
        takeOptional(json, "downloaded", peer.downloaded, asQint64);
        takeOptional(json, "uploaded", peer.uploaded, asQint64);
        takeOptional(json, "progress", peer.progress, asDouble);
        takeOptional(json, "relevance", peer.relevance, asDouble);
        delta.peers.append(peer);
    }
    delta.removed = stringList(answer.value(key("peers_removed")));
    return delta;
}

QStringList webSeeds(const QJsonArray &array)
{
    QStringList urls;
    urls.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QString url = value.toObject().value(key("url")).toString();
        if (!url.isEmpty())
            urls.append(url);
    }
    return urls;
}

QByteArray pieceStates(const QJsonArray &array)
{
    QByteArray states(array.size(), char(PieceStates::Missing));
    for (qsizetype i = 0; i < array.size(); ++i) {
        switch (array.at(i).toInt(0)) {
        case 1:
            states[i] = char(PieceStates::Downloading);
            break;
        case 2:
            states[i] = char(PieceStates::Have);
            break;
        default:
            break;
        }
    }
    return states;
}

QList<LogEntry> log(const QJsonArray &array)
{
    QList<LogEntry> entries;
    entries.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject json = value.toObject();
        LogEntry entry;
        entry.id = json.value(key("id")).toInt(-1);
        entry.timestamp = qint64(json.value(key("timestamp")).toDouble());
        entry.type = json.value(key("type")).toInt();
        entry.message = json.value(key("message")).toString();
        entries.append(entry);
    }
    return entries;
}

TorrentDetailsData details(const QJsonObject &json)
{
    TorrentDetailsData data;
    data.hash = json.value(key("hash")).toString();
    data.properties = properties(json.value(key("properties")).toObject());
    data.files = files(json.value(key("files")).toArray());
    data.trackers = trackers(json.value(key("trackers")).toArray());
    data.peers = peers(json.value(key("peers")).toObject());
    data.webSeeds = webSeeds(json.value(key("webSeeds")).toArray());
    data.pieceStates = pieceStates(json.value(key("pieceStates")).toArray());
    return data;
}

std::optional<TorrentMetadata> metadata(const QJsonObject &body)
{
    const QJsonObject info = body.value(key("info")).toObject();
    const QString name = info.value(key("name")).toString();
    if (info.isEmpty() || name.isEmpty())
        return std::nullopt;

    TorrentMetadata metadata;
    metadata.name = name;
    metadata.infoHashV1 = body.value(key("infohash_v1")).toString().toLower();
    metadata.infoHashV2 = body.value(key("infohash_v2")).toString().toLower();
    metadata.torrentId = body.value(key("hash")).toString().toLower();
    if (metadata.torrentId.isEmpty())
        metadata.torrentId = TorrentFile::torrentIdFor(metadata.infoHashV1, metadata.infoHashV2);
    metadata.pieceLength = info.value(key("piece_length")).toInteger();
    const QJsonValue isPrivate = info.value(key("private"));
    metadata.isPrivate = isPrivate.isBool() ? isPrivate.toBool() : isPrivate.toInt() != 0;

    // A multi-file torrent's paths start with its folder, `<info.name>/`; a single file's path
    // is its name. TorrentFileEntry keeps paths below the folder, as TorrentFile does.
    const QString root = name + QLatin1Char('/');
    const QJsonArray files = info.value(key("files")).toArray();
    for (const QJsonValue &value : files) {
        const QJsonObject file = value.toObject();
        QString path = file.value(key("path")).toString();
        if (path.startsWith(root)) {
            path.remove(0, root.size());
            metadata.multiFile = true;
        }
        const qint64 length = file.value(key("length")).toInteger();
        metadata.files.append({path, length});
        metadata.totalSize += length;
    }
    if (metadata.files.isEmpty()) {
        const qint64 length = info.value(key("length")).toInteger();
        metadata.files.append({name, length});
        metadata.totalSize = length;
    }
    if (metadata.files.size() > 1)
        metadata.multiFile = true;

    for (const QJsonValue &value : body.value(key("trackers")).toArray()) {
        const QString url = value.toObject().value(key("url")).toString();
        if (!url.isEmpty() && !metadata.trackers.contains(url))
            metadata.trackers.append(url);
    }
    return metadata;
}

EngineFeatures features(const QString &apiVersion)
{
    const QVersionNumber version = QVersionNumber::fromString(apiVersion);
    const auto atLeast = [&version](int major, int minor, int micro) {
        return !version.isNull() && version >= QVersionNumber(major, minor, micro);
    };

    EngineFeatures f;
    f.categories = true;
    f.categorySavePaths = true;
    f.tags = true;
    f.filePriorities = {int(FilePriority::Skip),
                        int(FilePriority::Normal),
                        int(FilePriority::High),
                        int(FilePriority::Maximal)};
    f.queue = true;
    f.forceStart = true;
    f.sequentialDownload = true;
    f.firstLastPiece = true;
    f.superSeeding = true;
    f.autoManagement = true;
    f.engineLog = true;
    f.addOptions = true;
    f.peerControl = true;
    f.editWebSeeds = true;
    f.exportTorrent = true;
    f.seedingTimeLimit = true;
    // qBittorrent's own surfaces (docs/36 §4.4, C14 E7): Settings › qBittorrent with its
    // export and import, torrents export and import (C9's file holds its rows), RSS.
    f.qbtSettings = true;
    f.torrentsBackup = true;
    f.rss = true;
    // torrents/fetchMetadata, and file priorities on an add (C5).
    f.fetchMetadata = atLeast(2, 11, 9);
    // The shareLimitAction field: an older server rejects an unknown parameter.
    if (atLeast(2, 15, 1)) {
        f.shareLimitActions = {QStringLiteral("Default"),
                               QStringLiteral("Stop"),
                               QStringLiteral("Remove"),
                               QStringLiteral("RemoveWithContent"),
                               QStringLiteral("EnableSuperSeeding")};
    }
    return f;
}

} // namespace omaseed::core::QbtTranslate
