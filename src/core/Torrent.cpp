#include "core/Torrent.hpp"

#include "core/engines/TorrentPatch.hpp"

#include <QUrl>

namespace omaseed::core {

namespace {

/// Copies one field from the patch when the patch carries it and it differs, and records it.
/// Absent fields are left alone: that is what makes a partial patch a merge.
template<class T>
void take(const TorrentPatch &patch,
          TorrentField field,
          T Torrent::*member,
          Torrent &torrent,
          TorrentFields &changed)
{
    if (!patch.has(field))
        return;
    const T &next = patch.values.*member;
    if (torrent.*member == next)
        return;
    torrent.*member = next;
    changed.set(field);
}

} // namespace

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

TorrentFields Torrent::apply(const TorrentPatch &patch)
{
    TorrentFields changed;
    take(patch, TorrentField::Name, &Torrent::name, *this, changed);
    take(patch, TorrentField::Size, &Torrent::size, *this, changed);
    take(patch, TorrentField::TotalSize, &Torrent::totalSize, *this, changed);
    take(patch, TorrentField::Progress, &Torrent::progress, *this, changed);
    take(patch, TorrentField::Status, &Torrent::status, *this, changed);
    take(patch, TorrentField::DlSpeed, &Torrent::dlspeed, *this, changed);
    take(patch, TorrentField::UpSpeed, &Torrent::upspeed, *this, changed);
    take(patch, TorrentField::Eta, &Torrent::eta, *this, changed);
    take(patch, TorrentField::NumSeeds, &Torrent::numSeeds, *this, changed);
    take(patch, TorrentField::NumComplete, &Torrent::numComplete, *this, changed);
    take(patch, TorrentField::NumLeechs, &Torrent::numLeechs, *this, changed);
    take(patch, TorrentField::NumIncomplete, &Torrent::numIncomplete, *this, changed);
    take(patch, TorrentField::Ratio, &Torrent::ratio, *this, changed);
    take(patch, TorrentField::Category, &Torrent::category, *this, changed);
    take(patch, TorrentField::Tags, &Torrent::tags, *this, changed);
    take(patch, TorrentField::SavePath, &Torrent::savePath, *this, changed);
    take(patch, TorrentField::ContentPath, &Torrent::contentPath, *this, changed);
    take(patch, TorrentField::DownloadPath, &Torrent::downloadPath, *this, changed);
    take(patch, TorrentField::AddedOn, &Torrent::addedOn, *this, changed);
    take(patch, TorrentField::CompletionOn, &Torrent::completionOn, *this, changed);
    take(patch, TorrentField::LastActivity, &Torrent::lastActivity, *this, changed);
    take(patch, TorrentField::Tracker, &Torrent::tracker, *this, changed);
    take(patch, TorrentField::TrackersCount, &Torrent::trackersCount, *this, changed);
    take(patch, TorrentField::HasTrackerError, &Torrent::hasTrackerError, *this, changed);
    take(patch, TorrentField::QueuePosition, &Torrent::priority, *this, changed);
    take(patch, TorrentField::SeqDl, &Torrent::seqDl, *this, changed);
    take(patch, TorrentField::FirstLastPiecePrio, &Torrent::firstLastPiecePrio, *this, changed);
    take(patch, TorrentField::SuperSeeding, &Torrent::superSeeding, *this, changed);
    take(patch, TorrentField::AutoTmm, &Torrent::autoTmm, *this, changed);
    take(patch, TorrentField::IsPrivate, &Torrent::isPrivate, *this, changed);
    take(patch, TorrentField::DlLimit, &Torrent::dlLimit, *this, changed);
    take(patch, TorrentField::UpLimit, &Torrent::upLimit, *this, changed);
    take(patch, TorrentField::RatioLimit, &Torrent::ratioLimit, *this, changed);
    take(patch, TorrentField::SeedingTimeLimit, &Torrent::seedingTimeLimit, *this, changed);
    take(patch,
         TorrentField::InactiveSeedingTimeLimit,
         &Torrent::inactiveSeedingTimeLimit,
         *this,
         changed);
    take(patch, TorrentField::ShareLimitAction, &Torrent::shareLimitAction, *this, changed);
    take(patch, TorrentField::Availability, &Torrent::availability, *this, changed);
    take(patch, TorrentField::AmountLeft, &Torrent::amountLeft, *this, changed);
    take(patch, TorrentField::Completed, &Torrent::completed, *this, changed);
    take(patch, TorrentField::Downloaded, &Torrent::downloaded, *this, changed);
    take(patch, TorrentField::Uploaded, &Torrent::uploaded, *this, changed);
    take(patch, TorrentField::UploadedSession, &Torrent::uploadedSession, *this, changed);
    take(patch, TorrentField::TimeActive, &Torrent::timeActive, *this, changed);
    take(patch, TorrentField::SeedingTime, &Torrent::seedingTime, *this, changed);
    take(patch, TorrentField::InfohashV1, &Torrent::infohashV1, *this, changed);
    take(patch, TorrentField::InfohashV2, &Torrent::infohashV2, *this, changed);
    take(patch, TorrentField::MagnetUri, &Torrent::magnetUri, *this, changed);
    return changed;
}

TorrentPatch TorrentPatch::of(const Torrent &torrent, TorrentField field)
{
    TorrentPatch patch(torrent.hash);
    patch.values = torrent;
    patch.fields.set(field);
    return patch;
}

// The rules below are the one place a state turns into what the views show (docs/36 §4.1).
// For qBittorrent they give exactly Q16's results for each of its states; QbtTranslate maps
// the states to phases and flags.

StatusGroups Torrent::statusGroups() const
{
    StatusGroups groups;
    const TorrentPhase phase = status.phase;
    const bool finished = status.has(TorrentFlag::Finished);

    // Wanting data, or stopped or checking before it had it all.
    if (phase == TorrentPhase::Downloading || phase == TorrentPhase::Metadata ||
        ((phase == TorrentPhase::Checking || phase == TorrentPhase::Stopped) && !finished))
        groups |= StatusGroup::Downloading;

    if (phase == TorrentPhase::Seeding || (phase == TorrentPhase::Checking && finished))
        groups |= StatusGroup::Seeding;

    if (finished || progress >= 1.0)
        groups |= StatusGroup::Completed;

    switch (phase) {
    case TorrentPhase::Stopped:
        groups |= StatusGroup::Stopped;
        break;
    case TorrentPhase::Checking:
    case TorrentPhase::Loading:
        groups |= StatusGroup::Checking;
        break;
    case TorrentPhase::Errored:
    case TorrentPhase::Missing:
        groups |= StatusGroup::Errored;
        break;
    case TorrentPhase::Downloading:
    case TorrentPhase::Seeding:
        if (status.has(TorrentFlag::Stalled))
            groups |= StatusGroup::Stalled;
        break;
    default:
        break;
    }

    // Moving data, or allowed to: running and not waiting for peers or a queue slot, or
    // forced past the queue.
    const bool running = (phase == TorrentPhase::Downloading || phase == TorrentPhase::Seeding) &&
                         !status.has(TorrentFlag::Stalled) && !status.has(TorrentFlag::Queued);
    if (dlspeed > 0 || upspeed > 0 || running || status.has(TorrentFlag::Forced))
        groups |= StatusGroup::Active;

    return groups;
}

bool Torrent::inGroup(StatusGroup group) const
{
    return statusGroups().testFlag(group);
}

QString Torrent::stateText() const
{
    switch (status.phase) {
    case TorrentPhase::Downloading:
        if (status.has(TorrentFlag::Queued))
            return QStringLiteral("queued");
        if (status.has(TorrentFlag::Stalled))
            return QStringLiteral("stalled");
        return QStringLiteral("downloading");
    case TorrentPhase::Metadata:
        return QStringLiteral("metadata");
    // A finished torrent with nobody to upload to is normal, not stuck: "stalled" is kept
    // for downloads that are.
    case TorrentPhase::Seeding:
        return status.has(TorrentFlag::Queued) ? QStringLiteral("queued")
                                               : QStringLiteral("seeding");
    case TorrentPhase::Checking:
    case TorrentPhase::Loading:
    case TorrentPhase::Allocating:
        return QStringLiteral("checking");
    case TorrentPhase::Stopped:
        return status.has(TorrentFlag::Finished) ? QStringLiteral("completed")
                                                 : QStringLiteral("stopped");
    case TorrentPhase::Errored:
        return QStringLiteral("error");
    // Fixed differently from an error (move the files back, or recheck), so it says so; the
    // rail still counts it under errored.
    case TorrentPhase::Missing:
        return QStringLiteral("missing files");
    case TorrentPhase::Moving:
        return QStringLiteral("moving");
    case TorrentPhase::Unknown:
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
    return (status.phase == TorrentPhase::Downloading && !status.has(TorrentFlag::Queued)) ||
           status.phase == TorrentPhase::Metadata;
}

} // namespace omaseed::core
