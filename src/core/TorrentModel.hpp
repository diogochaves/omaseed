#pragma once

// TorrentModel — one row per torrent, keyed by hash, in arrival order.
//
// sync/maindata sends a full map once and then only the torrents (and inside them only the
// keys) that moved. applyPartial() therefore never resets: it merges, emits one dataChanged()
// per contiguous run of touched rows, and only removes what `torrents_removed` names.
//
// Rows are appended in the order they arrive and never reordered — a full update comes out
// of a JSON object, which iterates by key, so its rows are in hash order; sorting belongs to
// TorrentFilterModel, so a sort change costs nothing here and row indices stay stable for the
// views that remember a cursor.

#include "core/Torrent.hpp"

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>
#include <QTimer>

namespace omaseed::core {

class TorrentModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        HashRole = Qt::UserRole + 1,
        NameRole,
        CategoryRole,
        TagsRole,
        TagsTextRole,
        SizeRole,
        SizeTextRole,
        TotalSizeRole,
        ProgressRole,
        ProgressTextRole,
        ProgressPercentRole,
        StateRole,
        StateTextRole,
        StateColorRoleRole, ///< the Theme colour role name, e.g. "stateDownloading"
        StatusGroupsRole,
        DlspeedRole,
        DlspeedTextRole,
        UpspeedRole,
        UpspeedTextRole,
        EtaRole,
        EtaTextRole,
        EtaLongTextRole,
        NumSeedsRole,
        NumCompleteRole,
        NumLeechsRole,
        NumIncompleteRole,
        SeedsPeersTextRole,
        RatioRole,
        RatioTextRole,
        SavePathRole,
        ContentPathRole,
        AddedOnRole,
        AddedOnTextRole,
        CompletionOnRole,
        CompletionOnTextRole,
        TrackerRole,
        TrackerHostRole,
        TrackersCountRole,
        HasTrackerErrorRole,
        PriorityRole,
        SeqDlRole,
        FirstLastPiecePrioRole,
        DlLimitRole,
        UpLimitRole,
        RatioLimitRole,
        SeedingTimeLimitRole,
        InactiveSeedingTimeLimitRole,
        ShareLimitActionRole,
        AvailabilityRole,
        AmountLeftRole,
        CompletedRole,
        DownloadedRole,
        UploadedRole,
        TimeActiveRole,
        TimeActiveTextRole,
        SeedingTimeRole,
        SeedingTimeTextRole,
        InfohashV1Role,
        InfohashV2Role,
        MagnetUriRole,
        // The table's optional columns (docs/10 §4): a display text for fields that only
        // had a raw role, the raw role still being what the header sorts by.
        TotalSizeTextRole,
        DownloadedTextRole,
        UploadedTextRole,
        AmountLeftTextRole,
        AvailabilityTextRole,
        DlLimitTextRole,
        UpLimitTextRole,
        PriorityTextRole,
        SeedsTextRole,        ///< "21 (1 204)": connected, then the swarm
        PeersTextRole,        ///< "42 (88)"
        AddedOnShortTextRole, ///< "today 10:41", without `AddedOnTextRole`'s "added"
        LastActivityRole,
        LastActivityTextRole,
        TorrentRole, ///< the whole value, for code that wants more than one field
    };
    Q_ENUM(Role)

    explicit TorrentModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }

    /// -1 when the hash is not in the model.
    Q_INVOKABLE int indexOfHash(const QString &hash) const;
    /// A default-constructed Torrent when the hash is unknown.
    const Torrent &torrent(const QString &hash) const;
    const Torrent &torrentAt(int row) const;
    QStringList hashes() const;

    /// Replaces everything with `torrents` (the `torrents` map of a full_update).
    void applyFullUpdate(const QJsonObject &torrents);
    /// Merges a diff: new hashes are appended, known ones merged key by key, and the hashes
    /// in `removed` dropped.
    void applyPartial(const QJsonObject &torrents, const QJsonArray &removed);
    void clear();

    /// The roles that change when the named JSON key changes, derived roles included.
    static QList<int> rolesForJsonKey(const QString &key);

    /// The texts that say "today" or "yesterday", which a new day changes with no poll.
    static QList<int> dayRelativeRoles();
    /// Milliseconds from `now` to just after the next local midnight.
    static qint64 msecsToNextMidnight(const QDateTime &now);
    /// One dataChanged over every row for dayRelativeRoles(); the midnight timer calls it.
    void refreshDayRelativeTexts();

Q_SIGNALS:
    void countChanged();
    /// Emitted once per applyPartial/applyFullUpdate that touched anything, after all the
    /// model signals, so FilterCounts and views recompute once instead of per row.
    void updated();

private:
    void emitRunsFor(const QList<QPair<int, QList<int>>> &touched);
    void scheduleMidnight();

    QList<Torrent> m_rows;
    QHash<QString, int> m_indexByHash;
    QTimer *m_midnight = nullptr; ///< fires once a day, just after local midnight
};

} // namespace omaseed::core
