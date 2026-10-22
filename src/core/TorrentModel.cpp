#include "core/TorrentModel.hpp"

#include "core/Format.hpp"

#include <QJsonValue>
#include <QSet>
#include <QTimeZone>
#include <QVariant>

#include <algorithm>

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

const QHash<QString, QList<int>> &keyToRoles()
{
    using M = TorrentModel;
    static const QHash<QString, QList<int>> map{
        {QStringLiteral("name"), {M::NameRole}},
        {QStringLiteral("size"), {M::SizeRole, M::SizeTextRole}},
        {QStringLiteral("total_size"), {M::TotalSizeRole, M::TotalSizeTextRole}},
        {QStringLiteral("progress"),
         {M::ProgressRole,
          M::ProgressTextRole,
          M::ProgressPercentRole,
          M::StatusGroupsRole,
          M::EtaTextRole,
          M::EtaLongTextRole}},
        // The ETA texts follow the state too (Torrent::hasEta), not only the `eta` key.
        {QStringLiteral("state"),
         {M::StateRole,
          M::StateTextRole,
          M::StateColorRoleRole,
          M::StatusGroupsRole,
          M::EtaTextRole,
          M::EtaLongTextRole}},
        {QStringLiteral("dlspeed"), {M::DlspeedRole, M::DlspeedTextRole, M::StatusGroupsRole}},
        {QStringLiteral("upspeed"), {M::UpspeedRole, M::UpspeedTextRole, M::StatusGroupsRole}},
        {QStringLiteral("eta"), {M::EtaRole, M::EtaTextRole, M::EtaLongTextRole}},
        {QStringLiteral("num_seeds"), {M::NumSeedsRole, M::SeedsPeersTextRole, M::SeedsTextRole}},
        {QStringLiteral("num_leechs"), {M::NumLeechsRole, M::SeedsPeersTextRole, M::PeersTextRole}},
        {QStringLiteral("num_complete"), {M::NumCompleteRole, M::SeedsTextRole}},
        {QStringLiteral("num_incomplete"), {M::NumIncompleteRole, M::PeersTextRole}},
        {QStringLiteral("ratio"), {M::RatioRole, M::RatioTextRole}},
        {QStringLiteral("category"), {M::CategoryRole}},
        {QStringLiteral("tags"), {M::TagsRole, M::TagsTextRole}},
        {QStringLiteral("save_path"), {M::SavePathRole}},
        {QStringLiteral("content_path"), {M::ContentPathRole}},
        {QStringLiteral("added_on"), {M::AddedOnRole, M::AddedOnTextRole, M::AddedOnShortTextRole}},
        {QStringLiteral("completion_on"), {M::CompletionOnRole, M::CompletionOnTextRole}},
        {QStringLiteral("last_activity"), {M::LastActivityRole, M::LastActivityTextRole}},
        {QStringLiteral("tracker"), {M::TrackerRole, M::TrackerHostRole}},
        {QStringLiteral("trackers_count"), {M::TrackersCountRole}},
        {QStringLiteral("has_tracker_error"), {M::HasTrackerErrorRole}},
        {QStringLiteral("priority"), {M::PriorityRole, M::PriorityTextRole}},
        {QStringLiteral("private"), {M::IsPrivateRole}},
        {QStringLiteral("seq_dl"), {M::SeqDlRole}},
        {QStringLiteral("f_l_piece_prio"), {M::FirstLastPiecePrioRole}},
        {QStringLiteral("dl_limit"), {M::DlLimitRole, M::DlLimitTextRole}},
        {QStringLiteral("up_limit"), {M::UpLimitRole, M::UpLimitTextRole}},
        {QStringLiteral("ratio_limit"), {M::RatioLimitRole}},
        {QStringLiteral("seeding_time_limit"), {M::SeedingTimeLimitRole}},
        {QStringLiteral("inactive_seeding_time_limit"), {M::InactiveSeedingTimeLimitRole}},
        {QStringLiteral("share_limit_action"), {M::ShareLimitActionRole}},
        {QStringLiteral("availability"), {M::AvailabilityRole, M::AvailabilityTextRole}},
        {QStringLiteral("amount_left"), {M::AmountLeftRole, M::AmountLeftTextRole}},
        {QStringLiteral("completed"), {M::CompletedRole}},
        {QStringLiteral("downloaded"), {M::DownloadedRole, M::DownloadedTextRole}},
        {QStringLiteral("uploaded"), {M::UploadedRole, M::UploadedTextRole}},
        {QStringLiteral("time_active"), {M::TimeActiveRole, M::TimeActiveTextRole}},
        {QStringLiteral("seeding_time"), {M::SeedingTimeRole, M::SeedingTimeTextRole}},
        {QStringLiteral("infohash_v1"), {M::InfohashV1Role}},
        {QStringLiteral("infohash_v2"), {M::InfohashV2Role}},
        {QStringLiteral("magnet_uri"), {M::MagnetUriRole}},
    };
    return map;
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
        return int(t.state);
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
        return t.eta;
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
        return t.ratioLimit;
    case SeedingTimeLimitRole:
        return t.seedingTimeLimit;
    case InactiveSeedingTimeLimitRole:
        return t.inactiveSeedingTimeLimit;
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
        // C20 (#107): stopped by omaseed, it says so; the cause stays in General's block.
        if (t.state == TorrentState::Error && m_errorStops.contains(t.hash))
            return tr("error · stopped");
        return m_problems.value(t.hash).value(QStringLiteral("cause")).toString();
    case FailureRole:
        return m_failures.value(t.hash);
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
    };
    return names;
}

QList<int> TorrentModel::rolesForJsonKey(const QString &key)
{
    return keyToRoles().value(key);
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

void TorrentModel::applyFullUpdate(const QJsonObject &torrents)
{
    m_lastChange = Change{true, {}, {}, m_revision};
    beginResetModel();
    m_rows.clear();
    m_indexByHash.clear();
    m_problems.clear();
    m_failures.clear();
    m_rows.reserve(torrents.size());
    for (auto it = torrents.constBegin(); it != torrents.constEnd(); ++it) {
        Torrent t;
        t.hash = it.key();
        t.applyJson(it.value().toObject());
        m_indexByHash.insert(t.hash, int(m_rows.size()));
        m_rows.append(std::move(t));
    }
    endResetModel();
    ++m_revision;
    Q_EMIT countChanged();
    Q_EMIT updated();
}

void TorrentModel::applyPartial(const QJsonObject &torrents, const QJsonArray &removed)
{
    bool touchedAnything = false;
    m_lastChange = Change{false, {}, {}, m_revision};

    // 1. Removals first, so an add that reuses a freed hash cannot collide.
    if (!removed.isEmpty()) {
        QList<int> rows;
        rows.reserve(removed.size());
        QSet<QString> named;
        for (const QJsonValue &value : removed) {
            const QString hash = value.toString();
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
    QJsonObject additions;
    for (auto it = torrents.constBegin(); it != torrents.constEnd(); ++it) {
        const int row = indexOfHash(it.key());
        if (row < 0) {
            additions.insert(it.key(), it.value());
            continue;
        }
        const QStringList changedKeys = m_rows[row].applyJson(it.value().toObject());
        if (changedKeys.isEmpty())
            continue;
        m_lastChange.changed.append(it.key());
        QList<int> roles{TorrentRole};
        for (const QString &key : changedKeys) {
            for (const int role : rolesForJsonKey(key)) {
                if (!roles.contains(role))
                    roles.append(role);
            }
        }
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
        for (auto it = additions.constBegin(); it != additions.constEnd(); ++it) {
            Torrent t;
            t.hash = it.key();
            t.applyJson(it.value().toObject());
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
    Q_EMIT dataChanged(index(row), index(row), {ProblemRole, StatusCauseRole});
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
            Q_EMIT dataChanged(index(row), index(row), {StatusCauseRole});
    }
    Q_EMIT problemsChanged();
}

bool TorrentModel::isErrorStopped(const QString &hash) const
{
    return m_errorStops.contains(hash) && torrent(hash).state == TorrentState::Error;
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
