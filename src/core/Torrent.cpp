#include "core/Torrent.hpp"

#include <QHash>
#include <QJsonValue>
#include <QUrl>

namespace omaseed::core {

namespace {

/// Copies one JSON key into `field` when it is present and different, and records the key.
/// Absent keys are left alone: that is what makes a partial diff a merge.
template<class T, class Convert>
void take(
    const QJsonObject &json, QLatin1StringView key, T &field, Convert convert, QStringList &changed)
{
    const auto it = json.constFind(key);
    if (it == json.constEnd())
        return;
    T next = convert(it.value());
    if (next == field)
        return;
    field = std::move(next);
    changed.append(QString(key));
}

qint64 asInteger(const QJsonValue &value)
{
    return value.toInteger(0);
}

int asInt(const QJsonValue &value)
{
    return int(value.toInteger(0));
}

double asDouble(const QJsonValue &value)
{
    return value.toDouble(0.0);
}

bool asBool(const QJsonValue &value)
{
    return value.toBool(false);
}

QString asString(const QJsonValue &value)
{
    return value.toString();
}

QStringList asTags(const QJsonValue &value)
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

QDateTime asTimestamp(const QJsonValue &value)
{
    const qint64 seconds = value.toInteger(0);
    return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds) : QDateTime();
}

struct StateName
{
    TorrentState state;
    QLatin1StringView key;
};

/// The full list of `state` strings of WebUI API 2.x, newest spellings first.
const StateName *stateTable(int &count)
{
    static const StateName table[] = {
        {TorrentState::Downloading, QLatin1StringView("downloading")},
        {TorrentState::StalledDownload, QLatin1StringView("stalledDL")},
        {TorrentState::MetaDownload, QLatin1StringView("metaDL")},
        {TorrentState::ForcedMetaDownload, QLatin1StringView("forcedMetaDL")},
        {TorrentState::ForcedDownload, QLatin1StringView("forcedDL")},
        {TorrentState::QueuedDownload, QLatin1StringView("queuedDL")},
        {TorrentState::CheckingDownload, QLatin1StringView("checkingDL")},
        {TorrentState::Uploading, QLatin1StringView("uploading")},
        {TorrentState::StalledUpload, QLatin1StringView("stalledUP")},
        {TorrentState::ForcedUpload, QLatin1StringView("forcedUP")},
        {TorrentState::QueuedUpload, QLatin1StringView("queuedUP")},
        {TorrentState::CheckingUpload, QLatin1StringView("checkingUP")},
        {TorrentState::StoppedDownload, QLatin1StringView("stoppedDL")},
        {TorrentState::StoppedUpload, QLatin1StringView("stoppedUP")},
        {TorrentState::PausedDownload, QLatin1StringView("pausedDL")},
        {TorrentState::PausedUpload, QLatin1StringView("pausedUP")},
        {TorrentState::Error, QLatin1StringView("error")},
        {TorrentState::MissingFiles, QLatin1StringView("missingFiles")},
        {TorrentState::CheckingResumeData, QLatin1StringView("checkingResumeData")},
        {TorrentState::Moving, QLatin1StringView("moving")},
        {TorrentState::Allocating, QLatin1StringView("allocating")},
        {TorrentState::Unknown, QLatin1StringView("unknown")},
    };
    count = int(std::size(table));
    return table;
}

} // namespace

QString torrentStateKey(TorrentState state)
{
    int count = 0;
    const StateName *table = stateTable(count);
    for (int i = 0; i < count; ++i) {
        if (table[i].state == state)
            return QString(table[i].key);
    }
    return QStringLiteral("unknown");
}

TorrentState torrentStateFromKey(QStringView key)
{
    int count = 0;
    const StateName *table = stateTable(count);
    for (int i = 0; i < count; ++i) {
        if (key == table[i].key)
            return table[i].state;
    }
    return TorrentState::Unknown;
}

QString trackerlessKey()
{
    return QStringLiteral("trackerless");
}

QString trackerHost(const QString &trackerUrl)
{
    const QString url = trackerUrl.trimmed();
    if (url.isEmpty())
        return trackerlessKey();

    // qBittorrent's pseudo-trackers come as "** [DHT] **", "** [PeX] **", "** [LSD] **".
    if (url.startsWith(QLatin1String("**"))) {
        const qsizetype open = url.indexOf(QLatin1Char('['));
        const qsizetype close = url.indexOf(QLatin1Char(']'), open + 1);
        if (open >= 0 && close > open)
            return url.mid(open + 1, close - open - 1).trimmed();
        return url;
    }

    const QString host = QUrl(url).host();
    return host.isEmpty() ? url : host;
}

QStringList Torrent::applyJson(const QJsonObject &json)
{
    QStringList changed;
    changed.reserve(8);

    take(json, QLatin1StringView("name"), name, asString, changed);
    take(json, QLatin1StringView("size"), size, asInteger, changed);
    take(json, QLatin1StringView("total_size"), totalSize, asInteger, changed);
    take(json, QLatin1StringView("progress"), progress, asDouble, changed);
    take(
        json,
        QLatin1StringView("state"),
        state,
        [](const QJsonValue &value) { return torrentStateFromKey(value.toString()); },
        changed);

    take(json, QLatin1StringView("dlspeed"), dlspeed, asInteger, changed);
    take(json, QLatin1StringView("upspeed"), upspeed, asInteger, changed);
    take(json, QLatin1StringView("eta"), eta, asInteger, changed);

    take(json, QLatin1StringView("num_seeds"), numSeeds, asInt, changed);
    take(json, QLatin1StringView("num_complete"), numComplete, asInt, changed);
    take(json, QLatin1StringView("num_leechs"), numLeechs, asInt, changed);
    take(json, QLatin1StringView("num_incomplete"), numIncomplete, asInt, changed);
    take(json, QLatin1StringView("ratio"), ratio, asDouble, changed);

    take(json, QLatin1StringView("category"), category, asString, changed);
    take(json, QLatin1StringView("tags"), tags, asTags, changed);
    take(json, QLatin1StringView("save_path"), savePath, asString, changed);
    take(json, QLatin1StringView("content_path"), contentPath, asString, changed);

    take(json, QLatin1StringView("added_on"), addedOn, asTimestamp, changed);
    take(json, QLatin1StringView("completion_on"), completionOn, asTimestamp, changed);
    take(json, QLatin1StringView("last_activity"), lastActivity, asTimestamp, changed);

    take(json, QLatin1StringView("tracker"), tracker, asString, changed);
    take(json, QLatin1StringView("trackers_count"), trackersCount, asInt, changed);
    take(json, QLatin1StringView("has_tracker_error"), hasTrackerError, asBool, changed);

    take(json, QLatin1StringView("priority"), priority, asInt, changed);
    take(json, QLatin1StringView("seq_dl"), seqDl, asBool, changed);
    take(json, QLatin1StringView("f_l_piece_prio"), firstLastPiecePrio, asBool, changed);
    take(json, QLatin1StringView("super_seeding"), superSeeding, asBool, changed);
    take(json, QLatin1StringView("auto_tmm"), autoTmm, asBool, changed);
    take(json, QLatin1StringView("private"), isPrivate, asBool, changed);
    take(json, QLatin1StringView("dl_limit"), dlLimit, asInteger, changed);
    take(json, QLatin1StringView("up_limit"), upLimit, asInteger, changed);
    take(json, QLatin1StringView("ratio_limit"), ratioLimit, asDouble, changed);
    take(json, QLatin1StringView("seeding_time_limit"), seedingTimeLimit, asInteger, changed);
    take(json,
         QLatin1StringView("inactive_seeding_time_limit"),
         inactiveSeedingTimeLimit,
         asInteger,
         changed);
    take(json, QLatin1StringView("share_limit_action"), shareLimitAction, asString, changed);

    take(json, QLatin1StringView("availability"), availability, asDouble, changed);
    take(json, QLatin1StringView("amount_left"), amountLeft, asInteger, changed);
    take(json, QLatin1StringView("completed"), completed, asInteger, changed);
    take(json, QLatin1StringView("downloaded"), downloaded, asInteger, changed);
    take(json, QLatin1StringView("uploaded"), uploaded, asInteger, changed);
    take(json, QLatin1StringView("uploaded_session"), uploadedSession, asInteger, changed);
    take(json, QLatin1StringView("time_active"), timeActive, asInteger, changed);
    take(json, QLatin1StringView("seeding_time"), seedingTime, asInteger, changed);

    take(json, QLatin1StringView("infohash_v1"), infohashV1, asString, changed);
    take(json, QLatin1StringView("infohash_v2"), infohashV2, asString, changed);
    take(json, QLatin1StringView("magnet_uri"), magnetUri, asString, changed);

    return changed;
}

StatusGroups Torrent::statusGroups() const
{
    StatusGroups groups;

    const bool isUpState =
        state == TorrentState::Uploading || state == TorrentState::StalledUpload ||
        state == TorrentState::ForcedUpload || state == TorrentState::QueuedUpload ||
        state == TorrentState::CheckingUpload || state == TorrentState::StoppedUpload ||
        state == TorrentState::PausedUpload;

    switch (state) {
    case TorrentState::Downloading:
    case TorrentState::StalledDownload:
    case TorrentState::MetaDownload:
    case TorrentState::ForcedMetaDownload:
    case TorrentState::ForcedDownload:
    case TorrentState::QueuedDownload:
    case TorrentState::CheckingDownload:
    case TorrentState::StoppedDownload:
    case TorrentState::PausedDownload:
        groups |= StatusGroup::Downloading;
        break;
    default:
        break;
    }

    switch (state) {
    case TorrentState::Uploading:
    case TorrentState::StalledUpload:
    case TorrentState::ForcedUpload:
    case TorrentState::QueuedUpload:
    case TorrentState::CheckingUpload:
        groups |= StatusGroup::Seeding;
        break;
    default:
        break;
    }

    if (isUpState || progress >= 1.0)
        groups |= StatusGroup::Completed;

    switch (state) {
    case TorrentState::StoppedDownload:
    case TorrentState::StoppedUpload:
    case TorrentState::PausedDownload:
    case TorrentState::PausedUpload:
        groups |= StatusGroup::Stopped;
        break;
    case TorrentState::StalledDownload:
    case TorrentState::StalledUpload:
        groups |= StatusGroup::Stalled;
        break;
    case TorrentState::CheckingDownload:
    case TorrentState::CheckingUpload:
    case TorrentState::CheckingResumeData:
        groups |= StatusGroup::Checking;
        break;
    case TorrentState::Error:
    case TorrentState::MissingFiles:
        groups |= StatusGroup::Errored;
        break;
    default:
        break;
    }

    const bool forcedOrMoving =
        state == TorrentState::Downloading || state == TorrentState::Uploading ||
        state == TorrentState::ForcedDownload || state == TorrentState::ForcedMetaDownload ||
        state == TorrentState::ForcedUpload;
    if (dlspeed > 0 || upspeed > 0 || forcedOrMoving)
        groups |= StatusGroup::Active;

    return groups;
}

bool Torrent::inGroup(StatusGroup group) const
{
    return statusGroups().testFlag(group);
}

QString Torrent::stateText() const
{
    switch (state) {
    case TorrentState::Downloading:
    case TorrentState::ForcedDownload:
        return QStringLiteral("downloading");
    case TorrentState::MetaDownload:
    case TorrentState::ForcedMetaDownload:
        return QStringLiteral("metadata");
    case TorrentState::StalledDownload:
        return QStringLiteral("stalled");
    case TorrentState::QueuedDownload:
    case TorrentState::QueuedUpload:
        return QStringLiteral("queued");
    case TorrentState::CheckingDownload:
    case TorrentState::CheckingUpload:
    case TorrentState::CheckingResumeData:
    case TorrentState::Allocating:
        return QStringLiteral("checking");
    // A finished torrent with nobody to upload to is normal, not stuck: "stalled" is kept
    // for downloads that are.
    case TorrentState::Uploading:
    case TorrentState::ForcedUpload:
    case TorrentState::StalledUpload:
        return QStringLiteral("seeding");
    case TorrentState::StoppedDownload:
    case TorrentState::PausedDownload:
        return QStringLiteral("stopped");
    case TorrentState::StoppedUpload:
    case TorrentState::PausedUpload:
        return QStringLiteral("completed");
    case TorrentState::Error:
        return QStringLiteral("error");
    // Fixed differently from an error (move the files back, or recheck), so it says so; the
    // rail still counts it under errored.
    case TorrentState::MissingFiles:
        return QStringLiteral("missing files");
    case TorrentState::Moving:
        return QStringLiteral("moving");
    case TorrentState::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QString Torrent::stateRole() const
{
    const QString text = stateText();
    if (text == QLatin1String("downloading") || text == QLatin1String("metadata"))
        return QStringLiteral("stateDownloading");
    if (text == QLatin1String("seeding") || text == QLatin1String("completed"))
        return QStringLiteral("stateSeeding");
    if (text == QLatin1String("stalled"))
        return QStringLiteral("stateStalled");
    if (text == QLatin1String("checking") || text == QLatin1String("moving"))
        return QStringLiteral("stateChecking");
    if (text == QLatin1String("queued"))
        return QStringLiteral("stateQueued");
    if (text == QLatin1String("error") || text == QLatin1String("missing files"))
        return QStringLiteral("stateError");
    return QStringLiteral("stateStopped");
}

QString Torrent::seedsPeersText() const
{
    return QStringLiteral("%1 / %2").arg(numSeeds).arg(numLeechs);
}

QString Torrent::trackerHostKey() const
{
    return trackerHost(tracker);
}

bool Torrent::isTrackerless() const
{
    return trackersCount == 0 && tracker.trimmed().isEmpty();
}

bool Torrent::isComplete() const
{
    return progress >= 1.0;
}

bool Torrent::hasEta() const
{
    if (isComplete())
        return false;
    switch (state) {
    case TorrentState::Downloading:
    case TorrentState::ForcedDownload:
    case TorrentState::MetaDownload:
    case TorrentState::ForcedMetaDownload:
    case TorrentState::StalledDownload:
        return true;
    default:
        return false;
    }
}

} // namespace omaseed::core
