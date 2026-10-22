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
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

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
        IsPrivateRole,
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
        // docs/28 (#54): why an errored torrent is in error, from qBittorrent's log. Not in
        // maindata: ProblemLog sets them with setProblem().
        ProblemRole,     ///< TorrentProblem::toMap(), or an empty map
        StatusCauseRole, ///< the Status column's short cause ("no access"), or empty
        // docs/32 (#55): a move, rename or part file that failed while the torrent kept
        // working. ProblemLog sets it with setFailure().
        FailureRole, ///< TorrentFailure::toMap(), or an empty map
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

    /// docs/28: what ProblemLog worked out for `hash` from the server's log (TorrentProblem::
    /// toMap()); an empty map takes it away. Emits dataChanged() for ProblemRole and
    /// StatusCauseRole on that row only, then problemsChanged(). A removed torrent, a full
    /// update and clear() drop every problem with the rows.
    void setProblem(const QString &hash, const QVariantMap &problem);
    QVariantMap problem(const QString &hash) const { return m_problems.value(hash); }
    /// docs/32: the failure ProblemLog shows for `hash` (TorrentFailure::toMap()), the same
    /// way: dataChanged() for FailureRole, then problemsChanged(); dropped with the row.
    void setFailure(const QString &hash, const QVariantMap &failure);
    QVariantMap failure(const QString &hash) const { return m_failures.value(hash); }

    /// docs/DECISIONS.md C20 (#107): the errored torrents omaseed stopped. qBittorrent ranks
    /// the error above the stop and reports `state: error` either way, so App remembers the
    /// stop it sent and hands the set over here; a full update keeps it, since it is not the
    /// server's. While one of them is in error its Status column reads "error · stopped"
    /// (StatusCauseRole). Emits dataChanged() on the rows that changed, then problemsChanged().
    void setErrorStops(const QSet<QString> &hashes);
    /// Whether `hash` is in error and in the set setErrorStops() was given.
    bool isErrorStopped(const QString &hash) const;

    /// What the last applyFullUpdate(), applyPartial() or clear() did (#36), so code that keeps
    /// its own state per torrent can follow the rows a poll touched instead of walking the
    /// library. An idle poll leaves both lists empty.
    struct Change
    {
        bool full = false;   ///< every row was replaced (a full update, or clear())
        QStringList changed; ///< merged with a changed key, or added; empty when `full`
        QStringList removed; ///< dropped by `torrents_removed`
        quint64 since = 0;   ///< revision() before it
    };
    const Change &lastChange() const { return m_lastChange; }
    /// Goes up by one with every `updated()`: a cheap "did anything change" for a cache.
    quint64 revision() const { return m_revision; }
    /// Whether lastChange() is all that changed since `revision`, a revision() taken earlier:
    /// a partial apply that started there. Otherwise (a full update, a clear(), or a change
    /// the caller missed) the caller walks the whole library again.
    bool lastChangeFollows(quint64 revision) const
    {
        return !m_lastChange.full && m_lastChange.since == revision;
    }

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
    /// Brackets the dataChanged() runs of one applyPartial() (#48). `rows` are every touched
    /// row, ascending, already holding their new values; `roles` are every role they changed,
    /// never widened to "all" as a wide run's dataChanged() is. A sorting proxy can hold its
    /// per-run re-sort through the runs and sort once at runsAnnounced(), instead of once per
    /// run.
    void runsAboutToBeAnnounced(const QList<int> &rows, const QList<int> &roles);
    void runsAnnounced();
    /// setProblem() changed what a row says about its error.
    void problemsChanged();

private:
    void emitRunsFor(const QList<QPair<int, QList<int>>> &touched);
    /// Drops rows first..last and moves the index of every row above them, between a
    /// beginRemoveRows() and its endRemoveRows().
    void removeRun(int first, int last);
    void scheduleMidnight();

    QList<Torrent> m_rows;
    QHash<QString, int> m_indexByHash;
    QHash<QString, QVariantMap> m_problems; ///< docs/28: by hash, only errored torrents
    QHash<QString, QVariantMap> m_failures; ///< docs/32: by hash, only those with a failure
    QSet<QString> m_errorStops;             ///< C20: the errored torrents omaseed stopped
    Change m_lastChange;
    quint64 m_revision = 0;
    QTimer *m_midnight = nullptr; ///< fires once a day, just after local midnight
};

} // namespace omaseed::core
