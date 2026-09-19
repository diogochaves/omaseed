#include "core/TorrentModel.hpp"

#include "core/Format.hpp"

#include <QJsonValue>
#include <QSet>
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
        {QStringLiteral("total_size"), {M::TotalSizeRole}},
        {QStringLiteral("progress"),
         {M::ProgressRole, M::ProgressTextRole, M::ProgressPercentRole, M::StatusGroupsRole}},
        {QStringLiteral("state"),
         {M::StateRole, M::StateTextRole, M::StateColorRoleRole, M::StatusGroupsRole}},
        {QStringLiteral("dlspeed"), {M::DlspeedRole, M::DlspeedTextRole, M::StatusGroupsRole}},
        {QStringLiteral("upspeed"), {M::UpspeedRole, M::UpspeedTextRole, M::StatusGroupsRole}},
        {QStringLiteral("eta"), {M::EtaRole, M::EtaTextRole, M::EtaLongTextRole}},
        {QStringLiteral("num_seeds"), {M::NumSeedsRole, M::SeedsPeersTextRole}},
        {QStringLiteral("num_leechs"), {M::NumLeechsRole, M::SeedsPeersTextRole}},
        {QStringLiteral("num_complete"), {M::NumCompleteRole}},
        {QStringLiteral("num_incomplete"), {M::NumIncompleteRole}},
        {QStringLiteral("ratio"), {M::RatioRole, M::RatioTextRole}},
        {QStringLiteral("category"), {M::CategoryRole}},
        {QStringLiteral("tags"), {M::TagsRole, M::TagsTextRole}},
        {QStringLiteral("save_path"), {M::SavePathRole}},
        {QStringLiteral("content_path"), {M::ContentPathRole}},
        {QStringLiteral("added_on"), {M::AddedOnRole, M::AddedOnTextRole}},
        {QStringLiteral("completion_on"), {M::CompletionOnRole, M::CompletionOnTextRole}},
        {QStringLiteral("tracker"), {M::TrackerRole, M::TrackerHostRole}},
        {QStringLiteral("trackers_count"), {M::TrackersCountRole}},
        {QStringLiteral("has_tracker_error"), {M::HasTrackerErrorRole}},
        {QStringLiteral("priority"), {M::PriorityRole}},
        {QStringLiteral("seq_dl"), {M::SeqDlRole}},
        {QStringLiteral("f_l_piece_prio"), {M::FirstLastPiecePrioRole}},
        {QStringLiteral("dl_limit"), {M::DlLimitRole}},
        {QStringLiteral("up_limit"), {M::UpLimitRole}},
        {QStringLiteral("ratio_limit"), {M::RatioLimitRole}},
        {QStringLiteral("seeding_time_limit"), {M::SeedingTimeLimitRole}},
        {QStringLiteral("availability"), {M::AvailabilityRole}},
        {QStringLiteral("amount_left"), {M::AmountLeftRole}},
        {QStringLiteral("completed"), {M::CompletedRole}},
        {QStringLiteral("downloaded"), {M::DownloadedRole}},
        {QStringLiteral("uploaded"), {M::UploadedRole}},
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
{}

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
        return t.isComplete() ? QStringLiteral("—") : Format::etaShort(t.eta);
    case EtaLongTextRole:
        return t.isComplete() ? QStringLiteral("—") : Format::eta(t.eta);
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
    case TorrentRole:
        return QVariant::fromValue(t);
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
        {PriorityRole, "priority"},
        {SeqDlRole, "seqDl"},
        {FirstLastPiecePrioRole, "firstLastPiecePrio"},
        {DlLimitRole, "dlLimit"},
        {UpLimitRole, "upLimit"},
        {RatioLimitRole, "ratioLimit"},
        {SeedingTimeLimitRole, "seedingTimeLimit"},
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
        {TorrentRole, "torrent"},
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
    const int row = indexOfHash(hash);
    return row < 0 ? emptyTorrent() : m_rows.at(row);
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
    beginResetModel();
    m_rows.clear();
    m_indexByHash.clear();
    m_rows.reserve(torrents.size());
    for (auto it = torrents.constBegin(); it != torrents.constEnd(); ++it) {
        Torrent t;
        t.hash = it.key();
        t.applyJson(it.value().toObject());
        m_indexByHash.insert(t.hash, int(m_rows.size()));
        m_rows.append(std::move(t));
    }
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT updated();
}

void TorrentModel::applyPartial(const QJsonObject &torrents, const QJsonArray &removed)
{
    bool touchedAnything = false;

    // 1. Removals first, so an add that reuses a freed hash cannot collide.
    if (!removed.isEmpty()) {
        QList<int> rows;
        rows.reserve(removed.size());
        for (const QJsonValue &value : removed) {
            const int row = indexOfHash(value.toString());
            if (row >= 0)
                rows.append(row);
        }
        if (!rows.isEmpty()) {
            std::sort(rows.begin(), rows.end(), std::greater<int>());
            int i = 0;
            while (i < rows.size()) {
                const int last = rows.at(i);
                int j = i;
                while (j + 1 < rows.size() && rows.at(j + 1) == rows.at(j) - 1)
                    ++j;
                const int first = rows.at(j);
                beginRemoveRows(QModelIndex(), first, last);
                m_rows.remove(first, last - first + 1);
                endRemoveRows();
                i = j + 1;
            }
            m_indexByHash.clear();
            for (int row = 0; row < m_rows.size(); ++row)
                m_indexByHash.insert(m_rows.at(row).hash, row);
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
            m_rows.append(std::move(t));
        }
        endInsertRows();
        touchedAnything = true;
        Q_EMIT countChanged();
    }

    if (touchedAnything)
        Q_EMIT updated();
}

void TorrentModel::emitRunsFor(const QList<QPair<int, QList<int>>> &touched)
{
    QList<QPair<int, QList<int>>> sorted = touched;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

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
}

void TorrentModel::clear()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    m_indexByHash.clear();
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT updated();
}

} // namespace omaseed::core
