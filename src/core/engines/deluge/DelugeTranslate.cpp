#include "core/engines/deluge/DelugeTranslate.hpp"

#include "core/MagnetLink.hpp"
#include "core/PieceStates.hpp"
#include "core/TorrentFile.hpp"

#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace omaseed::core::DelugeTranslate {

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
    return value.isDouble() ? qint64(std::llround(value.toDouble())) : fallback;
}

double real(const QJsonObject &json, const char *name, double fallback = 0.0)
{
    const QJsonValue value = json.value(key(name));
    return value.isDouble() ? value.toDouble() : fallback;
}

bool flag(const QJsonObject &json, const char *name)
{
    const QJsonValue value = json.value(key(name));
    return value.isBool() ? value.toBool() : value.toDouble() != 0.0;
}

QString text(const QJsonObject &json, const char *name)
{
    return json.value(key(name)).toString();
}

/// A date in seconds since the epoch; 0 (or nothing) is no date.
QDateTime date(const QJsonObject &json, const char *name)
{
    const qint64 seconds = number(json, name);
    return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds) : QDateTime();
}

/// A speed or limit in KiB/s (Deluge's unit) as bytes/s; -1 and 0 are none.
qint64 kibToBytes(double kib)
{
    return kib > 0.0 ? qint64(std::llround(kib * 1024.0)) : 0;
}

QString joined(const QString &folder, const QString &name)
{
    if (folder.isEmpty())
        return name;
    return folder.endsWith(QLatin1Char('/')) ? folder + name : folder + QLatin1Char('/') + name;
}

bool hasMetadata(const QJsonObject &torrent)
{
    // A magnet waiting for its metadata has no pieces yet (and no files, size 0).
    return !has(torrent, "num_pieces") || number(torrent, "num_pieces") > 0;
}

/// Settings › Deluge (E6, #87): the core.conf keys its sections read and write under Deluge's
/// own names (the rest go by app/preferences' words, so the speed surfaces and Basics read them
/// as on qBittorrent). A Count or a Real takes -1 for "no limit" where `none` says so, else a
/// value from `min`; a read-only key is never written.
enum class SettingKind { Flag, Count, Real, Text };

struct Setting
{
    const char *field;
    SettingKind kind;
    double min = 0;
    bool none = false; ///< -1 is "no limit"
};

const QList<Setting> &ownSettings()
{
    using K = SettingKind;
    static const QList<Setting> settings{
        // speed
        {"max_download_speed_per_torrent", K::Real, 1, true},
        {"max_upload_speed_per_torrent", K::Real, 1, true},
        {"ignore_limits_on_local_network", K::Flag},
        {"rate_limit_ip_overhead", K::Flag},
        // discovery
        {"utpex", K::Flag},
        {"lsd", K::Flag},
        // connection
        {"random_port", K::Flag},
        {"upnp", K::Flag},
        {"natpmp", K::Flag},
        {"max_connections_global", K::Count, 1, true},
        {"max_connections_per_torrent", K::Count, 1, true},
        {"max_upload_slots_global", K::Count, 1, true},
        {"max_upload_slots_per_torrent", K::Count, 1, true},
        // queue
        {"queue_new_to_top", K::Flag},
        {"max_active_limit", K::Count, 0, true},
        {"max_active_downloading", K::Count, 0, true},
        {"max_active_seeding", K::Count, 0, true},
        {"dont_count_slow_torrents", K::Flag},
        {"auto_manage_prefer_seeds", K::Flag},
        {"share_ratio_limit", K::Real, 0, true},
        {"seed_time_ratio_limit", K::Real, 0, true},
        {"seed_time_limit", K::Count, 0, true},
        // downloads and files
        {"move_completed", K::Flag},
        {"move_completed_path", K::Text},
        {"copy_torrent_file", K::Flag},
        {"torrentfiles_location", K::Text},
        {"del_copy_torrent_file", K::Flag},
        {"add_paused", K::Flag},
        {"pre_allocate_storage", K::Flag},
        {"prioritize_first_last_pieces", K::Flag},
        {"sequential_download", K::Flag},
    };
    return settings;
}

const Setting *ownSetting(const QString &name)
{
    for (const Setting &setting : ownSettings()) {
        if (name == QLatin1String(setting.field))
            return &setting;
    }
    return nullptr;
}

/// Deluge's encryption policy (enc_in_policy, enc_out_policy): 0 forced, 1 enabled, 2 disabled,
/// in Settings' one choice by those words.
const QStringList &encryptionWords()
{
    static const QStringList words{
        QStringLiteral("forced"), QStringLiteral("enabled"), QStringLiteral("disabled")};
    return words;
}

/// Deluge's `pieces` values (torrent.get_pieces_info).
constexpr int kPieceDownloading = 2;
constexpr int kPieceHave = 3;

/// Deluge's file priorities (the UI's set; libtorrent's 0–7).
constexpr int kDelugeSkip = 0;
constexpr int kDelugeLow = 1;
constexpr int kDelugeNormal = 4;
constexpr int kDelugeHigh = 7;

} // namespace

// --- fields ---------------------------------------------------------------------------------

QStringList listFields()
{
    // scripts/dev/deluge-fixtures.py's LIST_FIELDS: keep the two alike.
    return {QStringLiteral("name"),
            QStringLiteral("state"),
            QStringLiteral("total_size"),
            QStringLiteral("total_wanted"),
            QStringLiteral("progress"),
            QStringLiteral("download_payload_rate"),
            QStringLiteral("upload_payload_rate"),
            QStringLiteral("eta"),
            QStringLiteral("num_seeds"),
            QStringLiteral("total_seeds"),
            QStringLiteral("num_peers"),
            QStringLiteral("total_peers"),
            QStringLiteral("ratio"),
            QStringLiteral("label"),
            QStringLiteral("save_path"),
            QStringLiteral("move_completed"),
            QStringLiteral("move_completed_path"),
            QStringLiteral("is_finished"),
            QStringLiteral("paused"),
            QStringLiteral("time_added"),
            QStringLiteral("completed_time"),
            QStringLiteral("time_since_transfer"),
            QStringLiteral("tracker"),
            QStringLiteral("tracker_status"),
            QStringLiteral("queue"),
            QStringLiteral("sequential_download"),
            QStringLiteral("prioritize_first_last"),
            QStringLiteral("super_seeding"),
            QStringLiteral("is_auto_managed"),
            QStringLiteral("private"),
            QStringLiteral("max_download_speed"),
            QStringLiteral("max_upload_speed"),
            QStringLiteral("stop_at_ratio"),
            QStringLiteral("stop_ratio"),
            QStringLiteral("remove_at_ratio"),
            QStringLiteral("distributed_copies"),
            QStringLiteral("total_remaining"),
            QStringLiteral("total_done"),
            QStringLiteral("all_time_download"),
            QStringLiteral("total_uploaded"),
            QStringLiteral("total_payload_upload"),
            QStringLiteral("active_time"),
            QStringLiteral("seeding_time"),
            QStringLiteral("message"),
            QStringLiteral("num_pieces")};
}

QStringList propertiesFields()
{
    return {QStringLiteral("hash"),
            QStringLiteral("name"),
            QStringLiteral("save_path"),
            QStringLiteral("comment"),
            QStringLiteral("creator"),
            QStringLiteral("total_size"),
            QStringLiteral("all_time_download"),
            QStringLiteral("total_uploaded"),
            QStringLiteral("num_pieces"),
            QStringLiteral("piece_length"),
            QStringLiteral("pieces"),
            QStringLiteral("distributed_copies"),
            QStringLiteral("num_seeds"),
            QStringLiteral("total_seeds"),
            QStringLiteral("num_peers"),
            QStringLiteral("total_peers"),
            QStringLiteral("active_time"),
            QStringLiteral("max_connections"),
            QStringLiteral("last_seen_complete"),
            QStringLiteral("seeding_time"),
            QStringLiteral("completed_time"),
            QStringLiteral("next_announce"),
            QStringLiteral("private"),
            QStringLiteral("state"),
            QStringLiteral("is_finished")};
}

QStringList filesFields()
{
    return {QStringLiteral("files"),
            QStringLiteral("file_priorities"),
            QStringLiteral("file_progress"),
            QStringLiteral("piece_length")};
}

QStringList trackersFields()
{
    return {
        QStringLiteral("trackers"), QStringLiteral("tracker"), QStringLiteral("tracker_status")};
}

QStringList peersFields()
{
    return {QStringLiteral("peers")};
}

QStringList piecesFields()
{
    return {QStringLiteral("pieces"),
            QStringLiteral("num_pieces"),
            QStringLiteral("is_finished"),
            QStringLiteral("state")};
}

QStringList sessionStatusKeys()
{
    return {QStringLiteral("total_payload_download"),
            QStringLiteral("total_payload_upload"),
            QStringLiteral("has_incoming_connections")};
}

QStringList configKeys()
{
    return {QStringLiteral("max_download_speed"),
            QStringLiteral("max_upload_speed"),
            QStringLiteral("download_location"),
            QStringLiteral("listen_ports"),
            QStringLiteral("dht"),
            QStringLiteral("stop_seed_at_ratio"),
            QStringLiteral("stop_seed_ratio"),
            QStringLiteral("remove_seed_at_ratio")};
}

// --- the poll -------------------------------------------------------------------------------

QList<QJsonObject> rows(const QJsonValue &byHash)
{
    QList<QJsonObject> list;
    const QJsonObject torrents = byHash.toObject();
    list.reserve(torrents.size());
    for (auto it = torrents.constBegin(); it != torrents.constEnd(); ++it) {
        QJsonObject row = it.value().toObject();
        row.insert(QStringLiteral("hash"), it.key().toLower());
        list.append(row);
    }
    return list;
}

Poll poll(const QJsonValue &updateUi)
{
    Poll p;
    const QJsonObject result = updateUi.toObject();
    p.connected = result.value(QStringLiteral("connected")).toBool();
    if (!p.connected)
        return p;
    p.torrents = rows(result.value(QStringLiteral("torrents")));
    p.stats = result.value(QStringLiteral("stats")).toObject();
    const QJsonObject filters = result.value(QStringLiteral("filters")).toObject();
    p.labelPlugin = filters.contains(QStringLiteral("label"));
    for (const QJsonValue &entry : filters.value(QStringLiteral("label")).toArray()) {
        const QString name = entry.toArray().at(0).toString();
        // "" counts the torrents with none and "All" every torrent: neither is a label.
        if (!name.isEmpty() && name != QLatin1String("All"))
            p.labels.append(name);
    }
    p.labels.sort();
    return p;
}

// --- one torrent ----------------------------------------------------------------------------

TorrentStatus status(const QJsonObject &torrent)
{
    using P = TorrentPhase;
    using F = TorrentFlag;
    TorrentStatus s;
    const QString state = text(torrent, "state");
    if (state.isEmpty())
        return s;
    s.word = state;
    const bool finished = flag(torrent, "is_finished");
    const bool metadata = hasMetadata(torrent);
    if (finished)
        s.flags |= F::Finished;

    if (state == QLatin1String("Error")) {
        // Deluge's error pauses the torrent; `message` says why (C25 K24, §F).
        s.phase = P::Errored;
    } else if (state == QLatin1String("Paused")) {
        s.phase = P::Stopped;
    } else if (state == QLatin1String("Checking")) {
        s.phase = P::Checking;
    } else if (state == QLatin1String("Allocating")) {
        s.phase = P::Allocating;
    } else if (state == QLatin1String("Moving")) {
        s.phase = P::Moving; // with its own % in `progress` (board 47 E, K21)
    } else if (state == QLatin1String("Queued")) {
        s.phase = finished ? P::Seeding : metadata ? P::Downloading : P::Metadata;
        s.flags |= F::Queued;
    } else if (state == QLatin1String("Downloading")) {
        s.phase = metadata ? P::Downloading : P::Metadata;
        if (number(torrent, "download_payload_rate") == 0)
            s.flags |= F::Stalled;
    } else if (state == QLatin1String("Seeding")) {
        s.phase = P::Seeding;
        s.flags |= F::Finished;
        if (number(torrent, "upload_payload_rate") == 0)
            s.flags |= F::Stalled;
    } else {
        s.phase = P::Unknown;
    }

    // Out of the queue's hands (auto_managed off) and running: started past the queue
    // (engine matrix §4.5).
    const bool running =
        s.phase == P::Downloading || s.phase == P::Seeding || s.phase == P::Metadata;
    if (running && !s.has(F::Queued) && has(torrent, "is_auto_managed") &&
        !flag(torrent, "is_auto_managed"))
        s.flags |= F::Forced;
    return s;
}

QString contentRoot(const QJsonObject &json)
{
    const QJsonArray files = json.value(QStringLiteral("files")).toArray();
    if (files.isEmpty())
        return {};
    QString root;
    for (const QJsonValue &value : files) {
        const QString path = value.toObject().value(QStringLiteral("path")).toString();
        const qsizetype slash = path.indexOf(QLatin1Char('/'));
        if (slash < 0)
            return files.size() == 1 ? path : QString(); // one file, in no folder
        const QString top = path.left(slash);
        if (root.isEmpty())
            root = top;
        else if (root != top)
            return {};
    }
    return root;
}

TorrentPatch torrent(const QJsonObject &json, const Context &context)
{
    using F = TorrentField;
    TorrentPatch p(text(json, "hash").toLower());
    const bool metadata = hasMetadata(json);
    const bool finished = flag(json, "is_finished");

    if (has(json, "name"))
        p.set(&Torrent::name, F::Name, text(json, "name"));
    if (has(json, "total_wanted"))
        p.set(&Torrent::size, F::Size, number(json, "total_wanted"));
    if (has(json, "total_size"))
        p.set(&Torrent::totalSize, F::TotalSize, metadata ? number(json, "total_size") : -1);
    if (has(json, "progress"))
        p.set(&Torrent::progress,
              F::Progress,
              metadata ? std::clamp(real(json, "progress") / 100.0, 0.0, 1.0) : 0.0);
    if (has(json, "state"))
        p.set(&Torrent::status, F::Status, status(json));
    if (has(json, "download_payload_rate"))
        p.set(&Torrent::dlspeed, F::DlSpeed, number(json, "download_payload_rate"));
    if (has(json, "upload_payload_rate"))
        p.set(&Torrent::upspeed, F::UpSpeed, number(json, "upload_payload_rate"));
    if (has(json, "eta")) {
        const qint64 eta = number(json, "eta"); // 0: none (finished, or nothing coming in)
        p.set(&Torrent::eta, F::Eta, eta > 0 ? std::optional<qint64>(eta) : std::nullopt);
    }
    if (has(json, "num_seeds"))
        p.set(&Torrent::numSeeds, F::NumSeeds, int(number(json, "num_seeds")));
    if (has(json, "total_seeds") && number(json, "total_seeds", -1) >= 0)
        p.set(&Torrent::numComplete, F::NumComplete, int(number(json, "total_seeds")));
    if (has(json, "num_peers"))
        p.set(&Torrent::numLeechs, F::NumLeechs, int(number(json, "num_peers")));
    if (has(json, "total_peers") && number(json, "total_peers", -1) >= 0)
        p.set(&Torrent::numIncomplete, F::NumIncomplete, int(number(json, "total_peers")));
    if (has(json, "ratio")) {
        const double ratio = real(json, "ratio", -1.0); // -1: nothing to divide by yet
        p.set(&Torrent::ratio, F::Ratio, ratio < 0.0 ? 0.0 : ratio);
    }
    if (has(json, "label") || !context.labelPlugin)
        // The Label plugin's one label per torrent, in the categories place (C25 B). With the
        // plugin off there is none, whatever stale `label` deluge-web still sends.
        p.set(
            &Torrent::category, F::Category, context.labelPlugin ? text(json, "label") : QString());
    if (has(json, "save_path")) {
        const QString current = text(json, "save_path");
        // Deluge's inverse model (engine matrix §4.3): `save_path` is where it is now, and
        // `move_completed_path` where it goes when done. omaseed's save path is the end, its
        // download path the folder it waits in meanwhile.
        const bool goesOnWhenDone = !finished && flag(json, "move_completed") &&
                                    !text(json, "move_completed_path").isEmpty();
        p.set(&Torrent::savePath,
              F::SavePath,
              goesOnWhenDone ? text(json, "move_completed_path") : current);
        p.set(&Torrent::downloadPath, F::DownloadPath, goesOnWhenDone ? current : QString());
        if (!context.contentRoot.isEmpty())
            p.set(&Torrent::contentPath, F::ContentPath, joined(current, context.contentRoot));
    }
    if (has(json, "time_added"))
        p.set(&Torrent::addedOn, F::AddedOn, date(json, "time_added"));
    if (has(json, "completed_time"))
        p.set(&Torrent::completionOn, F::CompletionOn, date(json, "completed_time"));
    if (has(json, "time_since_transfer")) {
        const qint64 since = number(json, "time_since_transfer", -1); // -1: never
        const qint64 now =
            context.nowSecs > 0 ? context.nowSecs : QDateTime::currentSecsSinceEpoch();
        p.set(&Torrent::lastActivity,
              F::LastActivity,
              since >= 0 ? QDateTime::fromSecsSinceEpoch(now - since) : QDateTime());
    }
    if (has(json, "tracker"))
        p.set(&Torrent::tracker, F::Tracker, text(json, "tracker"));
    if (has(json, "trackers"))
        p.set(&Torrent::trackersCount,
              F::TrackersCount,
              int(json.value(QStringLiteral("trackers")).toArray().size()));
    if (has(json, "tracker_status"))
        // Only the tracker in use says how it went (board 47 H).
        p.set(&Torrent::hasTrackerError,
              F::HasTrackerError,
              text(json, "tracker_status").startsWith(QLatin1String("Error")));
    if (has(json, "queue")) {
        const qint64 queue = number(json, "queue", -1); // -1: out of the queue (finished)
        p.set(&Torrent::priority, F::QueuePosition, queue >= 0 ? int(queue) + 1 : 0);
    }
    if (has(json, "sequential_download"))
        p.set(&Torrent::seqDl, F::SeqDl, flag(json, "sequential_download"));
    if (has(json, "prioritize_first_last"))
        p.set(&Torrent::firstLastPiecePrio,
              F::FirstLastPiecePrio,
              flag(json, "prioritize_first_last"));
    if (has(json, "super_seeding"))
        p.set(&Torrent::superSeeding, F::SuperSeeding, flag(json, "super_seeding"));
    if (has(json, "private") && metadata)
        p.set(&Torrent::isPrivate, F::IsPrivate, flag(json, "private"));
    if (has(json, "max_download_speed"))
        p.set(&Torrent::dlLimit, F::DlLimit, kibToBytes(real(json, "max_download_speed", -1)));
    if (has(json, "max_upload_speed"))
        p.set(&Torrent::upLimit, F::UpLimit, kibToBytes(real(json, "max_upload_speed", -1)));
    if (has(json, "stop_at_ratio"))
        // A torrent keeps its own ratio limit, copied from the server's when it was added: no
        // "the server's" left to follow (D, K18).
        p.set(&Torrent::ratioLimit,
              F::RatioLimit,
              flag(json, "stop_at_ratio") ? RatioLimit::of(real(json, "stop_ratio"))
                                          : RatioLimit::unlimited());
    if (has(json, "remove_at_ratio"))
        // At the limit it stops, or it is removed keeping its files (K18).
        p.set(&Torrent::shareLimitAction,
              F::ShareLimitAction,
              flag(json, "remove_at_ratio") ? QStringLiteral("Remove") : QStringLiteral("Stop"));
    if (has(json, "distributed_copies") && real(json, "distributed_copies", -1) >= 0)
        p.set(&Torrent::availability, F::Availability, real(json, "distributed_copies"));
    if (has(json, "total_remaining"))
        p.set(&Torrent::amountLeft, F::AmountLeft, number(json, "total_remaining"));
    if (has(json, "total_done"))
        p.set(&Torrent::completed, F::Completed, number(json, "total_done"));
    if (has(json, "all_time_download"))
        p.set(&Torrent::downloaded, F::Downloaded, number(json, "all_time_download"));
    if (has(json, "total_uploaded"))
        p.set(&Torrent::uploaded, F::Uploaded, number(json, "total_uploaded"));
    if (has(json, "total_payload_upload"))
        p.set(&Torrent::uploadedSession, F::UploadedSession, number(json, "total_payload_upload"));
    if (has(json, "active_time"))
        p.set(&Torrent::timeActive, F::TimeActive, number(json, "active_time"));
    if (has(json, "seeding_time"))
        p.set(&Torrent::seedingTime, F::SeedingTime, number(json, "seeding_time"));
    if (!p.hash.isEmpty())
        p.set(&Torrent::infohashV1, F::InfohashV1, p.hash);
    if (has(json, "message") && has(json, "state"))
        // "OK" unless in error; the error's own words otherwise (C25 K24).
        p.set(&Torrent::errorMessage,
              F::ErrorMessage,
              text(json, "state") == QLatin1String("Error") ? text(json, "message") : QString());
    return p;
}

QStringList trackerUrls(const QJsonObject &json)
{
    QStringList urls;
    if (has(json, "trackers")) {
        for (const QJsonValue &value : json.value(QStringLiteral("trackers")).toArray()) {
            const QString url = value.toObject().value(QStringLiteral("url")).toString();
            if (!url.isEmpty() && !urls.contains(url))
                urls.append(url);
        }
        return urls;
    }
    if (!text(json, "tracker").isEmpty())
        urls.append(text(json, "tracker"));
    return urls;
}

bool isStopped(const QJsonObject &torrent)
{
    // Not `paused`: libtorrent pauses a queued torrent too, and a resumed error isn't paused.
    const QString state = text(torrent, "state");
    return state == QLatin1String("Paused") || state == QLatin1String("Error");
}

bool isPaused(const QJsonObject &torrent)
{
    const QString state = text(torrent, "state");
    if (state == QLatin1String("Paused"))
        return true;
    return state == QLatin1String("Error") && (!has(torrent, "paused") || flag(torrent, "paused"));
}

// --- the server -----------------------------------------------------------------------------

ServerStatePatch serverState(const QJsonObject &stats, const QJsonObject &sessionStatus)
{
    ServerStatePatch s;
    if (has(stats, "download_rate"))
        s.dlSpeed = number(stats, "download_rate");
    if (has(stats, "upload_rate"))
        s.upSpeed = number(stats, "upload_rate");
    if (has(sessionStatus, "total_payload_download"))
        s.dlSession = number(sessionStatus, "total_payload_download");
    if (has(sessionStatus, "total_payload_upload"))
        s.upSession = number(sessionStatus, "total_payload_upload");
    if (has(stats, "free_space") && number(stats, "free_space", -1) >= 0)
        s.freeSpaceOnDisk = number(stats, "free_space");
    // libtorrent's own reading, as qBittorrent words it: incoming connections, or none yet.
    const QJsonObject &incoming = has(stats, "has_incoming_connections") ? stats : sessionStatus;
    if (has(incoming, "has_incoming_connections"))
        s.connectionStatus = flag(incoming, "has_incoming_connections")
                                 ? QStringLiteral("connected")
                                 : QStringLiteral("firewalled");
    if (has(stats, "dht_nodes"))
        s.dhtNodes = int(number(stats, "dht_nodes"));
    if (has(stats, "num_connections"))
        s.peerConnections = int(number(stats, "num_connections"));
    if (has(stats, "max_download"))
        s.dlRateLimit = kibToBytes(real(stats, "max_download", -1));
    if (has(stats, "max_upload"))
        s.upRateLimit = kibToBytes(real(stats, "max_upload", -1));
    s.queueing = true;        // Deluge's queue is always there (auto-managed torrents)
    s.altSpeedLimits = false; // no turtle (K20)
    // No all-time totals (K34): omaseed counts them from when it began watching.
    return s;
}

GlobalLimits globalLimits(const QJsonObject &config)
{
    GlobalLimits limits;
    if (has(config, "max_download_speed"))
        limits.download = kibToBytes(real(config, "max_download_speed", -1));
    if (has(config, "max_upload_speed"))
        limits.upload = kibToBytes(real(config, "max_upload_speed", -1));
    return limits;
}

QVariantMap speedPreferences(const QJsonObject &config)
{
    QVariantMap prefs;
    if (has(config, "max_download_speed"))
        prefs.insert(QStringLiteral("dl_limit"),
                     kibToBytes(real(config, "max_download_speed", -1)));
    if (has(config, "max_upload_speed"))
        prefs.insert(QStringLiteral("up_limit"), kibToBytes(real(config, "max_upload_speed", -1)));
    if (has(config, "download_location"))
        prefs.insert(QStringLiteral("save_path"), text(config, "download_location"));
    if (has(config, "listen_ports")) {
        const QJsonArray ports = config.value(QStringLiteral("listen_ports")).toArray();
        if (!ports.isEmpty())
            prefs.insert(QStringLiteral("listen_port"), ports.at(0).toInt());
    }
    if (has(config, "dht"))
        prefs.insert(QStringLiteral("dht"), flag(config, "dht"));
    if (has(config, "stop_seed_at_ratio"))
        prefs.insert(QStringLiteral("max_ratio_enabled"), flag(config, "stop_seed_at_ratio"));
    if (has(config, "stop_seed_ratio"))
        prefs.insert(QStringLiteral("max_ratio"), real(config, "stop_seed_ratio"));
    if (has(config, "remove_seed_at_ratio"))
        prefs.insert(QStringLiteral("max_ratio_act"), flag(config, "remove_seed_at_ratio") ? 1 : 0);
    // Settings › Deluge (E6): the rest by Deluge's own names.
    for (const Setting &setting : ownSettings()) {
        if (!has(config, setting.field))
            continue;
        const QString name = key(setting.field);
        switch (setting.kind) {
        case SettingKind::Flag:
            prefs.insert(name, flag(config, setting.field));
            break;
        case SettingKind::Count:
            prefs.insert(name, int(number(config, setting.field, -1)));
            break;
        case SettingKind::Real:
            prefs.insert(name, real(config, setting.field, -1));
            break;
        case SettingKind::Text:
            prefs.insert(name, text(config, setting.field));
            break;
        }
    }
    // One choice for both ways: the outgoing policy, which is what peers meet first.
    if (has(config, "enc_out_policy")) {
        const qint64 policy = number(config, "enc_out_policy", -1);
        if (policy >= 0 && policy < encryptionWords().size())
            prefs.insert(QStringLiteral("encryption"), encryptionWords().at(int(policy)));
    }
    // K15: the plugins Deluge has on, for Settings › Deluge › Plugins (read only).
    if (has(config, "enabled_plugins")) {
        QStringList plugins;
        for (const QJsonValue &plugin : config.value(QStringLiteral("enabled_plugins")).toArray())
            plugins.append(plugin.toString());
        plugins.sort(Qt::CaseInsensitive);
        prefs.insert(QStringLiteral("enabled_plugins"), plugins);
    }
    return prefs;
}

QJsonObject configSet(const QVariantMap &changes, QString *why)
{
    QJsonObject set;
    const auto refuse = [why](const QString &reason) {
        if (why)
            *why = reason;
        return QJsonObject();
    };
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        const QString &name = it.key();
        const QVariant &value = it.value();
        bool ok = true;
        if (name == QLatin1String("dl_limit") || name == QLatin1String("up_limit")) {
            const qint64 bytes = value.toLongLong(&ok);
            if (!ok || bytes < 0)
                return refuse(QStringLiteral("%1: not a speed").arg(name));
            set.insert(name == QLatin1String("dl_limit") ? QStringLiteral("max_download_speed")
                                                         : QStringLiteral("max_upload_speed"),
                       speedKiB(bytes));
        } else if (name == QLatin1String("save_path")) {
            const QString path = value.toString();
            if (path.isEmpty())
                return refuse(QStringLiteral("save_path: empty"));
            set.insert(QStringLiteral("download_location"), path);
        } else if (name == QLatin1String("listen_port")) {
            const int port = value.toInt(&ok);
            if (!ok || port < 1 || port > 65535)
                return refuse(QStringLiteral("listen_port: %1 isn't a port").arg(value.toString()));
            set.insert(QStringLiteral("listen_ports"), QJsonArray{port, port});
        } else if (name == QLatin1String("dht")) {
            set.insert(QStringLiteral("dht"), value.toBool());
        } else if (name == QLatin1String("max_ratio_enabled")) {
            set.insert(QStringLiteral("stop_seed_at_ratio"), value.toBool());
        } else if (name == QLatin1String("max_ratio")) {
            const double ratio = value.toDouble(&ok);
            if (!ok || ratio < 0.0)
                return refuse(QStringLiteral("max_ratio: not a ratio"));
            set.insert(QStringLiteral("stop_seed_ratio"), ratio);
        } else if (name == QLatin1String("max_ratio_act")) {
            const int act = value.toInt(&ok);
            if (!ok || (act != 0 && act != 1)) // stop, or remove keeping the files (K18)
                return refuse(
                    QStringLiteral("max_ratio_act: Deluge stops or removes, keeping the files"));
            set.insert(QStringLiteral("remove_seed_at_ratio"), act == 1);
        } else if (name == QLatin1String("encryption")) {
            const qsizetype policy = encryptionWords().indexOf(value.toString());
            if (policy < 0)
                return refuse(
                    QStringLiteral("encryption is one of %1, not %2")
                        .arg(encryptionWords().join(QStringLiteral(", ")), value.toString()));
            set.insert(QStringLiteral("enc_in_policy"), int(policy));
            set.insert(QStringLiteral("enc_out_policy"), int(policy));
        } else if (const Setting *setting = ownSetting(name)) {
            switch (setting->kind) {
            case SettingKind::Flag:
                set.insert(name, value.toBool());
                break;
            case SettingKind::Count:
            case SettingKind::Real: {
                const double amount = value.toDouble(&ok);
                const bool none = setting->none && amount == -1.0;
                if (!ok || (!none && amount < setting->min) ||
                    (setting->kind == SettingKind::Count && amount != std::floor(amount)))
                    return refuse(setting->none
                                      ? QStringLiteral("%1 is -1 (no limit) or from %2, not %3")
                                            .arg(name)
                                            .arg(setting->min)
                                            .arg(value.toString())
                                      : QStringLiteral("%1 is from %2, not %3")
                                            .arg(name)
                                            .arg(setting->min)
                                            .arg(value.toString()));
                if (setting->kind == SettingKind::Count)
                    set.insert(name, int(amount));
                else
                    set.insert(name, amount);
                break;
            }
            case SettingKind::Text:
                set.insert(name, value.toString());
                break;
            }
        } else {
            return refuse(QStringLiteral("%1: not one of Deluge's settings here").arg(name));
        }
    }
    return set;
}

// --- details --------------------------------------------------------------------------------

TorrentProperties properties(const QJsonObject &json)
{
    TorrentProperties p;
    p.hash = text(json, "hash").toLower();
    p.infohashV1 = p.hash;
    p.name = text(json, "name");
    p.savePath = text(json, "save_path");
    p.createdBy = text(json, "creator");
    p.comment = text(json, "comment");
    p.totalSize = number(json, "total_size");
    p.totalDownloaded = number(json, "all_time_download");
    p.totalUploaded = number(json, "total_uploaded");
    p.piecesNum = int(number(json, "num_pieces"));
    p.pieceSize = number(json, "piece_length");
    p.piecesHave = PieceStates::count(pieceStates(json), PieceStates::Have);
    if (has(json, "distributed_copies") && real(json, "distributed_copies", -1) >= 0)
        p.availability = real(json, "distributed_copies");
    p.seeds = int(number(json, "num_seeds"));
    p.seedsTotal = int(std::max<qint64>(0, number(json, "total_seeds")));
    p.peers = int(number(json, "num_peers"));
    p.peersTotal = int(std::max<qint64>(0, number(json, "total_peers")));
    if (has(json, "num_seeds") && has(json, "num_peers"))
        p.connections = p.seeds + p.peers;
    p.connectionsLimit = int(std::max<qint64>(0, number(json, "max_connections", -1)));
    p.timeElapsed = number(json, "active_time");
    p.seedingTime = number(json, "seeding_time");
    p.completedOn = date(json, "completed_time");
    p.reannounce = std::max<qint64>(0, number(json, "next_announce"));
    if (has(json, "last_seen_complete")) {
        // 0: no peer has shown a whole copy. Said as "never" only while it isn't whole here.
        const QDateTime seen = date(json, "last_seen_complete");
        if (seen.isValid() || !flag(json, "is_finished"))
            p.lastSeen = seen;
    }
    if (has(json, "private") && hasMetadata(json))
        p.isPrivate = flag(json, "private");
    // Not reported by Deluge, so left out rather than shown as nothing (C25 K29): when it was
    // created, the bytes thrown away, average speeds, a v2 hash.
    return p;
}

QList<ContentFile> files(const QJsonObject &json)
{
    const QJsonArray files = json.value(QStringLiteral("files")).toArray();
    const QJsonArray priorities = json.value(QStringLiteral("file_priorities")).toArray();
    const QJsonArray progress = json.value(QStringLiteral("file_progress")).toArray();
    const qint64 pieceLength = number(json, "piece_length");
    QList<ContentFile> list;
    list.reserve(files.size());
    for (qsizetype i = 0; i < files.size(); ++i) {
        const QJsonObject file = files.at(i).toObject();
        ContentFile entry;
        entry.index = int(number(file, "index", i));
        entry.path = text(file, "path");
        entry.size = number(file, "size");
        const qsizetype at = entry.index.value_or(int(i));
        entry.progress =
            at < progress.size() ? std::clamp(progress.at(at).toDouble(), 0.0, 1.0) : 0.0;
        if (at < priorities.size())
            entry.priority = filePriority(priorities.at(at).toInt(kDelugeNormal));
        if (pieceLength > 0 && has(file, "offset")) {
            const qint64 offset = number(file, "offset");
            const qint64 last = offset + std::max<qint64>(1, entry.size) - 1;
            entry.pieces = std::pair<int, int>(int(offset / pieceLength), int(last / pieceLength));
        }
        list.append(entry);
    }
    return list;
}

int filePriority(int delugePriority)
{
    if (delugePriority <= kDelugeSkip)
        return int(FilePriority::Skip);
    if (delugePriority < kDelugeNormal)
        return int(FilePriority::Low);
    if (delugePriority == kDelugeNormal)
        return int(FilePriority::Normal);
    return int(FilePriority::High);
}

int delugePriority(FilePriority priority)
{
    switch (priority) {
    case FilePriority::Skip:
        return kDelugeSkip;
    case FilePriority::Low:
        return kDelugeLow;
    case FilePriority::High:
    case FilePriority::Maximal: // Deluge's highest is high (C25 K17)
        return kDelugeHigh;
    case FilePriority::Normal:
    case FilePriority::Mixed:
        break;
    }
    return kDelugeNormal;
}

QList<TrackerEntry> trackers(const QJsonObject &json)
{
    const QString current = text(json, "tracker");
    const QString said = text(json, "tracker_status");
    QList<TrackerEntry> list;
    bool placed = false;
    const QJsonArray all = json.value(QStringLiteral("trackers")).toArray();
    for (qsizetype i = 0; i < all.size(); ++i) {
        const QJsonObject tracker = all.at(i).toObject();
        TrackerEntry entry;
        entry.url = text(tracker, "url");
        entry.tier = int(number(tracker, "tier", -1));
        entry.status = int(TrackerStatus::NotContacted);
        // The status is the tracker in use's; before one answered, the first one tried's.
        const bool inUse = current.isEmpty() ? i == 0 : entry.url == current;
        if (inUse && !placed && !said.isEmpty()) {
            placed = true;
            if (said.startsWith(QLatin1String("Announce OK"))) {
                entry.status = int(TrackerStatus::Working);
            } else if (said.startsWith(QLatin1String("Announce Sent"))) {
                entry.status = int(TrackerStatus::Updating);
            } else if (said.startsWith(QLatin1String("Error"))) {
                entry.status = int(TrackerStatus::NotWorking);
                entry.message = said.mid(said.indexOf(QLatin1Char(':')) + 1).trimmed();
            } else if (said.startsWith(QLatin1String("Warning"))) {
                entry.status = int(TrackerStatus::Working);
                entry.message = said.mid(said.indexOf(QLatin1Char(':')) + 1).trimmed();
            } else {
                entry.message = said;
            }
        }
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
        // "ip:port", or "[v6]:port".
        patch.address = text(peer, "ip");
        const qsizetype colon = patch.address.lastIndexOf(QLatin1Char(':'));
        if (colon > 0) {
            QString ip = patch.address.left(colon);
            if (ip.startsWith(QLatin1Char('[')) && ip.endsWith(QLatin1Char(']')))
                ip = ip.mid(1, ip.size() - 2);
            patch.ip = ip;
            patch.port = patch.address.mid(colon + 1).toInt();
        }
        patch.client = text(peer, "client");
        const QString country = text(peer, "country").trimmed();
        if (!country.isEmpty())
            patch.countryCode = country.toLower();
        patch.dlSpeed = number(peer, "down_speed");
        patch.upSpeed = number(peer, "up_speed");
        patch.progress = std::clamp(real(peer, "progress"), 0.0, 1.0);
        delta.peers.append(patch);
    }
    return delta;
}

QByteArray pieceStates(const QJsonObject &json)
{
    const int count = int(number(json, "num_pieces"));
    const QJsonValue pieces = json.value(QStringLiteral("pieces"));
    if (pieces.isArray()) {
        const QJsonArray list = pieces.toArray();
        QByteArray states(list.size(), char(PieceStates::Missing));
        for (qsizetype i = 0; i < list.size(); ++i) {
            const int piece = list.at(i).toInt();
            if (piece == kPieceHave)
                states[i] = char(PieceStates::Have);
            else if (piece == kPieceDownloading)
                states[i] = char(PieceStates::Downloading);
        }
        return states;
    }
    // Deluge sends none while seeding (every piece is had) and before the metadata.
    if (count > 0 && (flag(json, "is_finished") || text(json, "state") == QLatin1String("Seeding")))
        return QByteArray(count, char(PieceStates::Have));
    return {};
}

// --- labels ---------------------------------------------------------------------------------

CategoryPatch category(const QString &label, const QJsonObject &options)
{
    CategoryPatch patch;
    patch.name = label;
    const bool moves = flag(options, "apply_move_completed") && flag(options, "move_completed");
    patch.savePath = moves ? text(options, "move_completed_path") : QString();
    return patch;
}

QString labelName(const QString &name, QString *why)
{
    // The Label plugin lower-cases a label and takes a–z, 0–9, `_`, `-` and `.` (checked on
    // 2.2.0; its message leaves the dot out).
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_.-]+$"));
    const QString label = name.trimmed().toLower();
    if (label.isEmpty()) {
        if (why)
            *why = QStringLiteral("a label can't be empty");
        return {};
    }
    if (!valid.match(label).hasMatch()) {
        if (why)
            *why = QStringLiteral("Deluge's labels take only a–z, 0–9, _, - and .: %1").arg(name);
        return {};
    }
    return label;
}

// --- metadata -------------------------------------------------------------------------------

std::optional<TorrentMetadata>
metadata(const QJsonValue &answer, const QString &magnet, bool *unreadable)
{
    if (unreadable)
        *unreadable = false;
    const QJsonArray pair = answer.toArray();
    const QString encoded = pair.size() > 1 ? pair.at(1).toString() : QString();
    if (encoded.isEmpty())
        return std::nullopt; // still looking: the call's timeout passed
    const QByteArray info = QByteArray::fromBase64(encoded.toLatin1());
    // The info dictionary alone: wrapped as a .torrent's, it reads as any other.
    QString why;
    const TorrentFile file =
        TorrentFile::parse(QByteArrayLiteral("d4:info") + info + QByteArrayLiteral("e"), &why);
    if (!file.isValid()) {
        if (unreadable)
            *unreadable = true;
        return std::nullopt;
    }
    TorrentMetadata metadata;
    metadata.infoHashV1 = file.infoHashV1;
    metadata.infoHashV2 = file.infoHashV2;
    metadata.torrentId = file.infoHashV1; // Deluge keys torrents by their v1 hash
    metadata.name = file.name;
    metadata.totalSize = file.totalSize;
    metadata.pieceLength = file.pieceLength;
    metadata.isPrivate = file.isPrivate;
    metadata.multiFile = file.multiFile;
    metadata.files = file.files;
    metadata.trackers = MagnetLink::parse(magnet).trackers; // the info has none
    return metadata;
}

// --- what it can do -------------------------------------------------------------------------

EngineFeatures features(const ServerInfo &, bool labelPlugin)
{
    // Board 47's A–K table, Deluge's column (DECISIONS C25); 2.1 is the minimum (K8), so
    // nothing here depends on the version yet. The Label plugin is read at connect and on
    // every poll (its filter in web.update_ui's tree).
    EngineFeatures f;
    f.categories = labelPlugin;        // B, K13–K15: the Label plugin's labels
    f.categorySavePaths = labelPlugin; // B: a label's move-completed folder
    f.categoriesAreLabels = true;      // B: called labels, lower case; off says why (K15)
    f.tags = false;                    // B: none
    f.tagsAreLabels = false;
    // C, K17: skip · low · normal · high.
    f.filePriorities = {int(FilePriority::Skip),
                        int(FilePriority::Low),
                        int(FilePriority::Normal),
                        int(FilePriority::High)};
    // D, K18: a ratio, then stop, or remove keeping the files.
    f.shareLimitActions = {QStringLiteral("Stop"), QStringLiteral("Remove")};
    f.seedingTimeLimit = false;  // D, K18: no time limits
    f.idleLimit = false;         // D, K18: nor one without traffic
    f.altSpeeds = false;         // D, K20: the Scheduler plugin is a week grid; no turtle
    f.queue = true;              // matrix §4.5: core.queue_up/down
    f.forceStart = true;         // matrix §4.5: auto_managed off, then resume
    f.sequentialDownload = true; // I
    f.firstLastPiece = true;     // I
    f.superSeeding = true;       // I
    f.autoManagement = false;    // I: none (a label's folder is the nearest)
    f.fetchMetadata = true;      // G, K26 (checked, §3 and research/87): by repeated calls
    f.addOptions = true;         // G, K27: name and skip check at add
    f.addContentLayout = false;  // G, K27: no layout
    f.v2Only = false;            // G, K25: magnets need btih
    f.renameOnDisk = false;      // G, K28: the name shown only
    f.moveProgress = true;       // E, K21: its own % while Moving
    f.moveStopsOnFailure = true; // E, F: Error with its words, paused
    f.pausesOnError = true;      // F, K24: paused; a failed move's error needs a recheck
    f.movesShowAtOnce = true;    // E: Moving from the send on; never Moving = not moved
    f.moveEndsWhenDone = true;   // E: a move turns move_completed off
    f.listFolders = true;        // E, L13: core.glob
    f.movesHoldAnswers = false;  // E: it answers while it moves
    f.roomOnServer = true;       // E, K23: core.get_free_space on any path
    f.moveReplacesFile = false;  // E, M8: libtorrent's dont_replace keeps the file there
    f.peerControl = true;        // H: core.connect_peer
    f.peerBan = false;           // H: no ban
    f.webSeeds = false;          // H: none reported; the tab never appears
    f.editWebSeeds = false;      // H
    f.engineLog = false;         // I, K30: no log face, no ~
    f.qbtSettings = false;       // I, J
    f.sessionSettings = true;    // J, K31: Settings › Deluge, from core.conf (E6)
    f.allTimeTotals = false;     // K, K34: what omaseed saw, from when it began watching
    f.torrentsBackup = false;    // I, K33
    f.rss = false;               // I
    f.addAgain = false;          // I: no .torrent to add back over the API
    f.search = false;            // I (#145)
    f.createTorrent = false;     // I (#146): core.create_torrent, not driven yet
    // H, K29: tracker_status is the one in use's; the others are listed with their tiers.
    f.trackerStatusInUseOnly = true;
    return f;
}

// --- the way out ----------------------------------------------------------------------------

double speedKiB(qint64 bytesPerSec)
{
    return bytesPerSec > 0 ? double(bytesPerSec) / 1024.0 : -1.0;
}

QJsonArray trackersWith(const QJsonArray &trackers, const QStringList &urls)
{
    QJsonArray out;
    QStringList known;
    int lastTier = -1;
    for (const QJsonValue &value : trackers) {
        const QJsonObject tracker = value.toObject();
        const QString url = tracker.value(QStringLiteral("url")).toString();
        const int tier = tracker.value(QStringLiteral("tier")).toInt();
        known.append(url);
        lastTier = std::max(lastTier, tier);
        out.append(QJsonObject{{QStringLiteral("url"), url}, {QStringLiteral("tier"), tier}});
    }
    for (const QString &url : urls) {
        if (url.isEmpty() || known.contains(url))
            continue;
        known.append(url);
        out.append(QJsonObject{{QStringLiteral("url"), url}, {QStringLiteral("tier"), ++lastTier}});
    }
    return out;
}

QJsonArray trackersWithout(const QJsonArray &trackers, const QStringList &urls)
{
    QJsonArray out;
    for (const QJsonValue &value : trackers) {
        const QJsonObject tracker = value.toObject();
        const QString url = tracker.value(QStringLiteral("url")).toString();
        if (urls.contains(url))
            continue;
        out.append(
            QJsonObject{{QStringLiteral("url"), url},
                        {QStringLiteral("tier"), tracker.value(QStringLiteral("tier")).toInt()}});
    }
    return out;
}

} // namespace omaseed::core::DelugeTranslate
