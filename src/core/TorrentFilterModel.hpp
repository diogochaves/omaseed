#pragma once

// TorrentFilterModel — the rail and the search box, as a proxy over TorrentModel.
//
// The five filters (status group, category, tag, tracker host, free text) are ANDed. The
// category, tag and tracker filters are three-valued: unset means "any", and a set-but-empty
// string means "the torrents with none" — the rail's `none` row (spec §1).
//
// A tracker row lists every torrent that has any announce URL on that host — the same
// membership TrackerModel counts for the rail, and what qBittorrent's own WebUI filters on
// — not only the torrents whose current tracker is on it. The trackerless row lists the
// torrents with no tracker at all (`trackers_count == 0`, qBittorrent 5's rule and the one
// FilterCounts counts with); one whose trackers all fail stays under its hosts only.
// Without a TrackerModel attached (tests), a host row falls back to the current tracker's
// host.
//
// Sorting is a total order: the chosen role first, then name, then hash. Without the
// tie-break, two rows with the same speed would swap places on every 1.5 s poll.

#include "core/Torrent.hpp"

#include <QCollator>
#include <QItemSelectionModel>
#include <QPointer>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVariant>

#include <optional>

namespace omaseed::core {

class TorrentModel;
class TrackerModel;

class TorrentFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(StatusFilter statusFilter READ statusFilter WRITE setStatusFilter NOTIFY
                   statusFilterChanged)
    Q_PROPERTY(QVariant category READ category WRITE setCategory RESET clearCategory NOTIFY
                   categoryChanged)
    Q_PROPERTY(QVariant tag READ tag WRITE setTag RESET clearTag NOTIFY tagChanged)
    Q_PROPERTY(
        QVariant tracker READ tracker WRITE setTracker RESET clearTracker NOTIFY trackerChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(int sortRole READ sortRole WRITE setSortRole NOTIFY sortRoleChanged)
    Q_PROPERTY(
        QString sortRoleName READ sortRoleName WRITE setSortByRoleName NOTIFY sortRoleChanged)
    Q_PROPERTY(Qt::SortOrder sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    /// The rail's status rows (spec §1 Fig. 1), All first.
    enum StatusFilter {
        All,
        Downloading,
        Seeding,
        Completed,
        Stopped,
        Active,
        Stalled,
        Checking,
        Errored,
    };
    Q_ENUM(StatusFilter)

    explicit TorrentFilterModel(QObject *parent = nullptr);

    StatusFilter statusFilter() const { return m_statusFilter; }
    void setStatusFilter(StatusFilter filter);

    QVariant category() const;
    void setCategory(const QVariant &category);
    Q_INVOKABLE void clearCategory();

    QVariant tag() const;
    void setTag(const QVariant &tag);
    Q_INVOKABLE void clearTag();

    QVariant tracker() const;
    void setTracker(const QVariant &tracker);
    Q_INVOKABLE void clearTracker();

    /// The rail's tracker rows, whose membership the tracker filter follows.
    void setTrackerModel(TrackerModel *model);

    QString text() const { return m_text; }
    void setText(const QString &text);

    int sortRole() const { return m_sortRole; }
    void setSortRole(int role);
    QString sortRoleName() const;
    Q_INVOKABLE void setSortByRoleName(const QString &roleName);

    Qt::SortOrder sortOrder() const { return m_sortOrder; }
    void setSortOrder(Qt::SortOrder order);

    int count() const { return rowCount(); }

    /// The proxy row showing `hash`, or -1.
    Q_INVOKABLE int rowOfHash(const QString &hash) const;
    /// The hash on proxy row `row`, or an empty string.
    Q_INVOKABLE QString hashAt(int row) const;
    /// Selects rows `from` to `to` (either order, clamped) in `selection`, a selection model
    /// over this proxy, as one range: one selectionChanged instead of one per row. The table's
    /// per-row loop froze the window for seconds on a 3,450-torrent library (ctrl+a), long
    /// enough for Hyprland's "not responding".
    Q_INVOKABLE void selectRows(QItemSelectionModel *selection, int from, int to) const;

    /// The flag a StatusFilter stands for; StatusGroup::NoGroup for All.
    static StatusGroup groupFor(StatusFilter filter);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

Q_SIGNALS:
    void statusFilterChanged();
    void categoryChanged();
    void tagChanged();
    void trackerChanged();
    void textChanged();
    void sortRoleChanged();
    void sortOrderChanged();
    void countChanged();

private:
    void refilter();
    void resort();
    /// Reloads m_trackerHashes for the current tracker filter; true when it changed.
    bool reloadTrackerHashes();
    /// Whether the tracker filter goes by TrackerModel membership rather than the host of
    /// the current tracker.
    bool usesTrackerMembership() const;

    StatusFilter m_statusFilter = All;
    std::optional<QString> m_category;
    std::optional<QString> m_tag;
    std::optional<QString> m_tracker;
    QPointer<TrackerModel> m_trackerModel;
    QSet<QString> m_trackerHashes; ///< the filtered host's torrents, from m_trackerModel
    QString m_text;
    int m_sortRole = 0; ///< resolved to TorrentModel::NameRole on the first setSourceModel
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    QCollator m_collator;
};

} // namespace omaseed::core
