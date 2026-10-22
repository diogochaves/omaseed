#include "core/TorrentModel.hpp"

#include "core/Format.hpp"

#include <QSet>
#include <QTimeZone>
#include <QVariant>

#include <algorithm>
#include <array>
#include <limits>

namespace omaseed::core {

namespace {

/// Beyond this many roles a narrow dataChanged() stops paying for itself, so the run is
/// announced with an empty role list, which means "every role".
constexpr int kMaxNarrowRoles = 12;

const Torrent &emptyTorrent()
{
    static const Torrent value;
    return value;
}

/// The roles each field feeds, derived roles included.
const std::array<QList<int>, std::size_t(TorrentField::Count)> &fieldRoles()
{
    using M = TorrentModel;
    using F = TorrentField;
    static const auto table = [] {
        std::array<QList<int>, std::size_t(F::Count)> t;
        const auto put = [&t](F field, QList<int> roles) {
            t[std::size_t(field)] = std::move(roles);
        };
        put(F::Name, {M::NameRole});
        put(F::Size, {M::SizeRole, M::SizeTextRole});
        put(F::TotalSize, {M::TotalSizeRole, M::TotalSizeTextRole});
        put(F::Progress,
            {M::ProgressRole,
             M::ProgressTextRole,
             M::ProgressPercentRole,
             M::StatusGroupsRole,
             M::EtaTextRole,
             M::EtaLongTextRole});
        // The ETA texts follow the state too (Torrent::hasEta), not only the eta.
        put(F::Status,
            {M::StateRole,
             M::StateTextRole,
             M::StateColorRoleRole,
             M::StatusGroupsRole,
             M::EtaTextRole,
             M::EtaLongTextRole});
        put(F::DlSpeed, {M::DlspeedRole, M::DlspeedTextRole, M::StatusGroupsRole});
        put(F::UpSpeed, {M::UpspeedRole, M::UpspeedTextRole, M::StatusGroupsRole});
        put(F::Eta, {M::EtaRole, M::EtaTextRole, M::EtaLongTextRole});
        put(F::NumSeeds, {M::NumSeedsRole, M::SeedsPeersTextRole, M::SeedsTextRole});
        put(F::NumLeechs, {M::NumLeechsRole, M::SeedsPeersTextRole, M::PeersTextRole});
        put(F::NumComplete, {M::NumCompleteRole, M::SeedsTextRole});
        put(F::NumIncomplete, {M::NumIncompleteRole, M::PeersTextRole});
        put(F::Ratio, {M::RatioRole, M::RatioTextRole});
        put(F::Category, {M::CategoryRole});
        put(F::Tags, {M::TagsRole, M::TagsTextRole});
        put(F::SavePath, {M::SavePathRole});
        put(F::ContentPath, {M::ContentPathRole});
        put(F::AddedOn, {M::AddedOnRole, M::AddedOnTextRole, M::AddedOnShortTextRole});
        put(F::CompletionOn, {M::CompletionOnRole, M::CompletionOnTextRole});
        put(F::LastActivity, {M::LastActivityRole, M::LastActivityTextRole});
        put(F::Tracker, {M::TrackerRole, M::TrackerHostRole});
        put(F::TrackersCount, {M::TrackersCountRole});
        put(F::HasTrackerError, {M::HasTrackerErrorRole});
        put(F::QueuePosition, {M::PriorityRole, M::PriorityTextRole});
        put(F::IsPrivate, {M::IsPrivateRole});
        put(F::SeqDl, {M::SeqDlRole});
        put(F::FirstLastPiecePrio, {M::FirstLastPiecePrioRole});
        put(F::DlLimit, {M::DlLimitRole, M::DlLimitTextRole});
        put(F::UpLimit, {M::UpLimitRole, M::UpLimitTextRole});
        put(F::RatioLimit, {M::RatioLimitRole});
        put(F::SeedingTimeLimit, {M::SeedingTimeLimitRole});
        put(F::InactiveSeedingTimeLimit, {M::InactiveSeedingTimeLimitRole});
        put(F::ShareLimitAction, {M::ShareLimitActionRole});
        put(F::Availability, {M::AvailabilityRole, M::AvailabilityTextRole});
        put(F::AmountLeft, {M::AmountLeftRole, M::AmountLeftTextRole});
        put(F::Completed, {M::CompletedRole});
        put(F::Downloaded, {M::DownloadedRole, M::DownloadedTextRole});
        put(F::Uploaded, {M::UploadedRole, M::UploadedTextRole});
        put(F::TimeActive, {M::TimeActiveRole, M::TimeActiveTextRole});
        put(F::SeedingTime, {M::SeedingTimeRole, M::SeedingTimeTextRole});
        put(F::InfohashV1, {M::InfohashV1Role});
        put(F::InfohashV2, {M::InfohashV2Role});
        put(F::MagnetUri, {M::MagnetUriRole});
        // Fields no role shows on its own (only TorrentRole, which every change carries):
        // DownloadPath, SuperSeeding, AutoTmm, UploadedSession.
        return t;
    }();
    return table;
}

} // namespace

TorrentModel::TorrentModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_midnight(new QTimer(this))
{
    // "today 10:41" is only worked out when a row changes; at midnight it has to become
    // "yesterday 10:41" with nothing from the server to prompt it.
    m_midnight->setSingleShot(true);
    m_midnight->setTimerType(Qt::VeryCoarseTimer);
    connect(m_midnight, &QTimer::timeout, this, [this]() {
        refreshDayRelativeTexts();
        scheduleMidnight();
    });
    scheduleMidnight();
}

QList<int> TorrentModel::dayRelativeRoles()
{
    return {AddedOnTextRole, AddedOnShortTextRole, CompletionOnTextRole, LastActivityTextRole};
}

qint64 TorrentModel::msecsToNextMidnight(const QDateTime &now)
{
    const QDateTime midnight(now.date().addDays(1), QTime(0, 0), now.timeZone());
    // A second late, so the new date is already current when the texts are recomputed.
    return std::max<qint64>(1000, now.msecsTo(midnight) + 1000);
}

void TorrentModel::scheduleMidnight()
{
    m_midnight->start(std::chrono::milliseconds(msecsToNextMidnight(QDateTime::currentDateTime())));
}

void TorrentModel::refreshDayRelativeTexts()
{
    if (m_rows.isEmpty())
        return;
    Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), dayRelativeRoles());
}

int TorrentModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant TorrentModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Torrent &t = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return t.name;
    case HashRole:
        return t.hash;
    case CategoryRole:
        return t.category;
    case TagsRole:
        return t.tags;
    case TagsTextRole:
        return t.tags.join(QStringLiteral(", "));
    case SizeRole:
        return t.size;
    case SizeTextRole:
        return Format::size(t.size);
    case TotalSizeRole:
        return t.totalSize;
    case ProgressRole:
        return t.progress;
    case ProgressTextRole:
        return Format::percentCompact(t.progress);
    case ProgressPercentRole:
        return Format::percent(t.progress);
    case StateRole:
        return t.status.word;
    case StateTextRole:
        return t.stateText();
    case StateColorRoleRole:
        return t.stateRole();
    case StatusGroupsRole:
        return int(t.statusGroups().toInt());
    case DlspeedRole:
        return t.dlspeed;
    case DlspeedTextRole:
        return Format::speed(t.dlspeed);
    case UpspeedRole:
        return t.upspeed;
    case UpspeedTextRole:
        return Format::speed(t.upspeed);
    case EtaRole:
        return t.eta.value_or(kNoEstimateSortKey);
    case EtaTextRole:
        return t.hasEta() ? Format::etaShort(t.eta) : QStringLiteral("—");
    case EtaLongTextRole:
        return t.hasEta() ? Format::eta(t.eta) : QStringLiteral("—");
    case NumSeedsRole:
        return t.numSeeds;
    case NumCompleteRole:
        return t.numComplete;
    case NumLeechsRole:
        return t.numLeechs;
    case NumIncompleteRole:
        return t.numIncomplete;
    case SeedsPeersTextRole:
        return t.seedsPeersText();
    case RatioRole:
        return t.ratio;
    case RatioTextRole:
        return Format::ratio(t.ratio);
    case SavePathRole:
        return t.savePath;
    case ContentPathRole:
        return t.contentPath;
    case AddedOnRole:
        return t.addedOn;
    case AddedOnTextRole:
        return Format::added(t.addedOn);
    case CompletionOnRole:
        return t.completionOn;
    case CompletionOnTextRole:
        return Format::relativeTime(t.completionOn);
    case TrackerRole:
        return t.tracker;
    case TrackerHostRole:
        return t.trackerHostKey();
    case TrackersCountRole:
        return t.trackersCount;
    case HasTrackerErrorRole:
        return t.hasTrackerError;
    case IsPrivateRole:
        return t.isPrivate;
    case PriorityRole:
        return t.priority;
    case SeqDlRole:
        return t.seqDl;
    case FirstLastPiecePrioRole:
        return t.firstLastPiecePrio;
    case DlLimitRole:
        return t.dlLimit;
    case UpLimitRole:
        return t.upLimit;
    case RatioLimitRole:
        return t.ratioLimit.code();
    case SeedingTimeLimitRole:
        return t.seedingTimeLimit.code();
    case InactiveSeedingTimeLimitRole:
        return t.inactiveSeedingTimeLimit.code();
    case ShareLimitActionRole:
        return t.shareLimitAction;
    case AvailabilityRole:
        return t.availability;
    case AmountLeftRole:
        return t.amountLeft;
    case CompletedRole:
        return t.completed;
    case DownloadedRole:
        return t.downloaded;
    case UploadedRole:
        return t.uploaded;
    case TimeActiveRole:
        return t.timeActive;
    case TimeActiveTextRole:
        return Format::duration(t.timeActive);
    case SeedingTimeRole:
        return t.seedingTime;
    case SeedingTimeTextRole:
        return Format::duration(t.seedingTime);
    case InfohashV1Role:
        return t.infohashV1;
    case InfohashV2Role:
        return t.infohashV2;
    case MagnetUriRole:
        return t.magnetUri;
    case TotalSizeTextRole:
        return t.totalSize < 0 ? QStringLiteral("—") : Format::size(t.totalSize);
    case DownloadedTextRole:
        return Format::size(t.downloaded);
    case UploadedTextRole:
        return Format::size(t.uploaded);
    case AmountLeftTextRole:
        return Format::size(t.amountLeft);
    case AvailabilityTextRole:
        // qBittorrent sends -1 while it has no metadata to count pieces against.
        return t.availability < 0 ? QStringLiteral("—") : QString::number(t.availability, 'f', 2);
    case DlLimitTextRole:
        return Format::limit(t.dlLimit);
    case UpLimitTextRole:
        return Format::limit(t.upLimit);
    case PriorityTextRole:
        // 0 is "not queued" (a finished or forced torrent), -1 "queueing is off".
        return t.priority > 0 ? QString::number(t.priority) : QStringLiteral("—");
    case SeedsTextRole:
        return QStringLiteral("%1 (%2)").arg(Format::count(std::max(0, t.numSeeds)),
                                             Format::count(std::max(0, t.numComplete)));
    case PeersTextRole:
        return QStringLiteral("%1 (%2)").arg(Format::count(std::max(0, t.numLeechs)),
                                             Format::count(std::max(0, t.numIncomplete)));
    case AddedOnShortTextRole:
        return Format::relativeTime(t.addedOn);
    case LastActivityRole:
        return t.lastActivity;
    case LastActivityTextRole:
        return Format::relativeTime(t.lastActivity);
    case TorrentRole:
        return QVariant::fromValue(t);
    case ProblemRole:
        return m_problems.value(t.hash);
    case StatusCauseRole:
        // C20 (#142): stopped, it says so ("error · stopped" didn't fit the column); the
        // cause stays in General's block and Status row.
        if (t.status.phase == TorrentPhase::Errored && m_errorStops.contains(t.hash))
            return tr("stopped");
        return m_problems.value(t.hash).value(QStringLiteral("cause")).toString();
    case FailureRole:
        return m_failures.value(t.hash);
    case StuckMoveRole:
        return m_problems.value(t.hash).value(QStringLiteral("stuckMove")).toBool();
    case ErrorStoppedRole:
        return t.status.phase == TorrentPhase::Errored && m_errorStops.contains(t.hash);
    default:
        return {};
    }
}

QHash<int, QByteArray> TorrentModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
        {HashRole, "hash"},
        {NameRole, "name"},
        {CategoryRole, "category"},
        {TagsRole, "tags"},
        {TagsTextRole, "tagsText"},
        {SizeRole, "size"},
        {SizeTextRole, "sizeText"},
        {TotalSizeRole, "totalSize"},
        {ProgressRole, "progress"},
        {ProgressTextRole, "progressText"},
        {ProgressPercentRole, "progressPercent"},
        {StateRole, "state"},
        {StateTextRole, "stateText"},
        {StateColorRoleRole, "stateRole"},
        {StatusGroupsRole, "statusGroups"},
        {DlspeedRole, "dlspeed"},
        {DlspeedTextRole, "dlspeedText"},
        {UpspeedRole, "upspeed"},
        {UpspeedTextRole, "upspeedText"},
        {EtaRole, "eta"},
        {EtaTextRole, "etaText"},
        {EtaLongTextRole, "etaLongText"},
        {NumSeedsRole, "numSeeds"},
        {NumCompleteRole, "numComplete"},
        {NumLeechsRole, "numLeechs"},
        {NumIncompleteRole, "numIncomplete"},
        {SeedsPeersTextRole, "seedsPeersText"},
        {RatioRole, "ratio"},
        {RatioTextRole, "ratioText"},
        {SavePathRole, "savePath"},
        {ContentPathRole, "contentPath"},
        {AddedOnRole, "addedOn"},
        {AddedOnTextRole, "addedOnText"},
        {CompletionOnRole, "completionOn"},
        {CompletionOnTextRole, "completionOnText"},
        {TrackerRole, "tracker"},
        {TrackerHostRole, "trackerHost"},
        {TrackersCountRole, "trackersCount"},
        {HasTrackerErrorRole, "hasTrackerError"},
        {IsPrivateRole, "isPrivate"},
        {PriorityRole, "priority"},
        {SeqDlRole, "seqDl"},
        {FirstLastPiecePrioRole, "firstLastPiecePrio"},
        {DlLimitRole, "dlLimit"},
        {UpLimitRole, "upLimit"},
        {RatioLimitRole, "ratioLimit"},
        {SeedingTimeLimitRole, "seedingTimeLimit"},
        {InactiveSeedingTimeLimitRole, "inactiveSeedingTimeLimit"},
        {ShareLimitActionRole, "shareLimitAction"},
        {AvailabilityRole, "availability"},
        {AmountLeftRole, "amountLeft"},
        {CompletedRole, "completed"},
        {DownloadedRole, "downloaded"},
        {UploadedRole, "uploaded"},
        {TimeActiveRole, "timeActive"},
        {TimeActiveTextRole, "timeActiveText"},
        {SeedingTimeRole, "seedingTime"},
        {SeedingTimeTextRole, "seedingTimeText"},
        {InfohashV1Role, "infohashV1"},
        {InfohashV2Role, "infohashV2"},
        {MagnetUriRole, "magnetUri"},
        {TotalSizeTextRole, "totalSizeText"},
        {DownloadedTextRole, "downloadedText"},
        {UploadedTextRole, "uploadedText"},
        {AmountLeftTextRole, "amountLeftText"},
        {AvailabilityTextRole, "availabilityText"},
        {DlLimitTextRole, "dlLimitText"},
        {UpLimitTextRole, "upLimitText"},
        {PriorityTextRole, "priorityText"},
        {SeedsTextRole, "seedsText"},
        {PeersTextRole, "peersText"},
        {AddedOnShortTextRole, "addedOnShortText"},
        {LastActivityRole, "lastActivity"},
        {LastActivityTextRole, "lastActivityText"},
        {TorrentRole, "torrent"},
        {ProblemRole, "problem"},
        {StatusCauseRole, "statusCause"},
        {FailureRole, "failure"},
        {ErrorStoppedRole, "errorStopped"},
        {StuckMoveRole, "stuckMove"},
    };
    return names;
}

QList<int> TorrentModel::rolesForField(TorrentField field)
{
    return fieldRoles().at(std::size_t(field));
}

int TorrentModel::indexOfHash(const QString &hash) const
{
    return m_indexByHash.value(hash, -1);
}

const Torrent &TorrentModel::torrent(const QString &hash) const
{
    // Bounds-checked like torrentAt(): QList::at() checks nothing in a Release build.
    return torrentAt(indexOfHash(hash));
}

const Torrent &TorrentModel::torrentAt(int row) const
{
    return (row < 0 || row >= m_rows.size()) ? emptyTorrent() : m_rows.at(row);
}

QStringList TorrentModel::hashes() const
{
    QStringList list;
    list.reserve(int(m_rows.size()));
    for (const Torrent &t : m_rows)
        list.append(t.hash);
    return list;
}

void TorrentModel::applyFullUpdate(const QList<TorrentPatch> &torrents)
{
    m_lastChange = Change{true, {}, {}, m_revision};
    beginResetModel();
    m_rows.clear();
    m_indexByHash.clear();
    m_problems.clear();
    m_failures.clear();
    m_rows.reserve(torrents.size());
    for (const TorrentPatch &patch : torrents) {
        Torrent t;
        t.hash = patch.hash;
        t.apply(patch);
        m_indexByHash.insert(t.hash, int(m_rows.size()));
        m_rows.append(std::move(t));
    }
    endResetModel();
    ++m_revision;
    Q_EMIT countChanged();
    Q_EMIT updated();
}

void TorrentModel::applyPartial(const QList<TorrentPatch> &torrents, const QStringList &removed)
{
    bool touchedAnything = false;
    m_lastChange = Change{false, {}, {}, m_revision};

    // 1. Removals first, so an add that reuses a freed hash cannot collide.
    if (!removed.isEmpty()) {
        QList<int> rows;
        rows.reserve(removed.size());
        QSet<QString> named;
        for (const QString &hash : removed) {
            const int row = indexOfHash(hash);
            if (row >= 0 && !named.contains(hash)) {
                named.insert(hash);
                rows.append(row);
                m_lastChange.removed.append(hash);
            }
        }
        if (!rows.isEmpty()) {
            std::sort(rows.begin(), rows.end(), std::greater<int>());
            // A hash named twice must not remove twice: the second pass would take whatever
            // row had moved into the freed slot.
            rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
            int i = 0;
            while (i < rows.size()) {
                const int last = rows.at(i);
                int j = i;
                while (j + 1 < rows.size() && rows.at(j + 1) == rows.at(j) - 1)
                    ++j;
                const int first = rows.at(j);
                beginRemoveRows(QModelIndex(), first, last);
                removeRun(first, last);
                endRemoveRows();
                i = j + 1;
            }
            touchedAnything = true;
            Q_EMIT countChanged();
        }
    }

    // 2. Merges into existing rows, collected so the signals can be batched.
    QList<QPair<int, QList<int>>> touched;
    QList<const TorrentPatch *> additions;
    QSet<QString> added;
    for (const TorrentPatch &patch : torrents) {
        const int row = indexOfHash(patch.hash);
        if (row < 0) {
            // Named twice in one delta: one new row, from the first.
            if (!added.contains(patch.hash)) {
                added.insert(patch.hash);
                additions.append(&patch);
            }
            continue;
        }
        const TorrentFields changedFields = m_rows[row].apply(patch);
        if (changedFields.isEmpty())
            continue;
        m_lastChange.changed.append(patch.hash);
        QList<int> roles{TorrentRole};
        changedFields.forEach([&roles](TorrentField field) {
            for (const int role : rolesForField(field)) {
                if (!roles.contains(role))
                    roles.append(role);
            }
        });
        touched.append({row, roles});
    }
    if (!touched.isEmpty()) {
        emitRunsFor(touched);
        touchedAnything = true;
    }

    // 3. Appends last, in one insert.
    if (!additions.isEmpty()) {
        const int first = int(m_rows.size());
        beginInsertRows(QModelIndex(), first, first + additions.size() - 1);
        for (const TorrentPatch *patch : std::as_const(additions)) {
            Torrent t;
            t.hash = patch->hash;
            t.apply(*patch);
            m_indexByHash.insert(t.hash, int(m_rows.size()));
            m_lastChange.changed.append(t.hash);
            m_rows.append(std::move(t));
        }
        endInsertRows();
        touchedAnything = true;
        Q_EMIT countChanged();
    }

    if (touchedAnything) {
        ++m_revision;
        Q_EMIT updated();
    }
}

void TorrentModel::removeRun(int first, int last)
{
    // The index is brought up to date before endRemoveRows(), not once after every run: the
    // listeners of each run (the proxy's rowsRemoved arrives inside the next run's
    // beginRemoveRows, then the table's cursor, the details pane, the seed console) look
    // torrents up by hash, and a row the index still held from before the run pointed past
    // the end of m_rows (#46). Runs go highest first, so only the rows above this one move.
    for (int row = first; row <= last; ++row) {
        m_indexByHash.remove(m_rows.at(row).hash);
        m_problems.remove(m_rows.at(row).hash);
        m_failures.remove(m_rows.at(row).hash);
    }
    m_rows.remove(first, last - first + 1);
    for (int row = first; row < m_rows.size(); ++row)
        m_indexByHash[m_rows.at(row).hash] = row;
}

void TorrentModel::emitRunsFor(const QList<QPair<int, QList<int>>> &touched)
{
    QList<QPair<int, QList<int>>> sorted = touched;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

    QList<int> rows;
    rows.reserve(sorted.size());
    QList<int> allRoles;
    for (const auto &[row, roles] : std::as_const(sorted)) {
        rows.append(row);
        for (const int role : roles) {
            if (!allRoles.contains(role))
                allRoles.append(role);
        }
    }
    Q_EMIT runsAboutToBeAnnounced(rows, allRoles);

    int i = 0;
    while (i < sorted.size()) {
        const int first = sorted.at(i).first;
        QList<int> roles = sorted.at(i).second;
        int last = first;
        int j = i + 1;
        while (j < sorted.size() && sorted.at(j).first == last + 1) {
            for (const int role : sorted.at(j).second) {
                if (!roles.contains(role))
                    roles.append(role);
            }
            last = sorted.at(j).first;
            ++j;
        }
        if (roles.size() > kMaxNarrowRoles)
            roles.clear(); // an empty list means "all roles"
        Q_EMIT dataChanged(index(first), index(last), roles);
        i = j;
    }
    Q_EMIT runsAnnounced();
}

void TorrentModel::setProblem(const QString &hash, const QVariantMap &problem)
{
    const int row = indexOfHash(hash);
    if (row < 0)
        return;
    if (problem.isEmpty()) {
        if (m_problems.remove(hash) == 0)
            return;
    } else {
        auto it = m_problems.find(hash);
        if (it != m_problems.end() && it.value() == problem)
            return;
        m_problems.insert(hash, problem);
    }
    Q_EMIT dataChanged(index(row), index(row), {ProblemRole, StatusCauseRole, StuckMoveRole});
    Q_EMIT problemsChanged();
}

void TorrentModel::setFailure(const QString &hash, const QVariantMap &failure)
{
    const int row = indexOfHash(hash);
    if (row < 0)
        return;
    if (failure.isEmpty()) {
        if (m_failures.remove(hash) == 0)
            return;
    } else {
        auto it = m_failures.find(hash);
        if (it != m_failures.end() && it.value() == failure)
            return;
        m_failures.insert(hash, failure);
    }
    Q_EMIT dataChanged(index(row), index(row), {FailureRole});
    Q_EMIT problemsChanged();
}

void TorrentModel::setErrorStops(const QSet<QString> &hashes)
{
    if (hashes == m_errorStops)
        return;
    // The rows whose Status may read differently: in one set and not the other.
    QSet<QString> touched = hashes;
    touched.unite(m_errorStops);
    touched.subtract(QSet<QString>(hashes).intersect(m_errorStops));
    m_errorStops = hashes;
    for (const QString &hash : std::as_const(touched)) {
        const int row = indexOfHash(hash);
        if (row >= 0)
            Q_EMIT dataChanged(index(row), index(row), {StatusCauseRole, ErrorStoppedRole});
    }
    Q_EMIT problemsChanged();
}

bool TorrentModel::isErrorStopped(const QString &hash) const
{
    return m_errorStops.contains(hash) && torrent(hash).status.phase == TorrentPhase::Errored;
}

void TorrentModel::clear()
{
    m_lastChange = Change{true, {}, {}, m_revision};
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    m_indexByHash.clear();
    m_problems.clear();
    m_failures.clear();
    endResetModel();
    ++m_revision;
    Q_EMIT countChanged();
    Q_EMIT updated();
}

} // namespace omaseed::core
