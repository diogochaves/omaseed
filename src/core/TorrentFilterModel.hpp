#pragma once

// TorrentFilterModel — the rail and the search box, as a proxy over TorrentModel.
//
// The five filters (status group, category, tag, tracker host, free text) are ANDed. The
// category, tag and tracker filters are three-valued: unset means "any", and a set-but-empty
// string means "the torrents with none" — the rail's `none` row (spec §1).
//
// Two more narrow it for a folder's move (board 42, DECISIONS C15 M1, M10): `folder`, a save
// path, keeps the torrents saved there or below it ("" is any); `onlyHashes`, when not empty,
// keeps those torrents only (a move's "show").
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
//
// The sort is dynamic, and Qt re-sorts per dataChanged(): a poll that moves the sorted value
// of scattered rows (sorted by speed, many torrents busy) would re-sort, and send the table a
// layoutChanged(), once per run of changed rows. TorrentModel brackets a poll's runs, so when
// they reorder rows across more than one run, the proxy holds the dynamic sort through them
// and sorts once after (#48). A poll that leaves the order alone goes through Qt as before.
// Most rows tie on a live value (idle at 0 B/s), so the name tie-break reads a cached rank
// instead of collating two names per comparison.
//
// `holdOrder` (#51 D9) holds the row order while the pointer is over the table's rows, so a
// live re-sort never moves a row between a look and a click. The sort stays dynamic (rows
// still enter and leave the filter, and values still change): lessThan() reads each row's
// place when the hold began, by hash, so Qt's own re-sort finds nothing out of place. A
// torrent new to the library (an add) takes its sorted place among the held rows at once,
// right after the last held row the live order puts before it (DECISIONS C20, #124: never at
// the end first); one already in the library that newly matches the filter goes below the
// held ones. A poll's bracket above composes with it: its one sort reads the held places
// too. Letting go sorts once, like the bracket. An explicit change (sort role or order,
// filter text, a rail scope) applies at once: the rows sort afresh and the hold starts again
// from there.
//
// The rail's five questions (DECISIONS C24 H2, board 46) are the first filter: `question`
// keeps the torrents that answer one, AnyQuestion all of them (the library). They are
// disjoint, in this order: needs you (in error, missing files, or a problem or failure in the
// log), landed today (complete, finished since local midnight), resting (stopped), then giving
// back (complete) or arriving (not yet). "Today" is a LocalDay's; at its midnight the rows
// are filtered again.
//
// Every state the rail used to list stays in the text (C24 H2): `is:stalled` keeps the
// stalled torrents, `is:` taking the StatusFilter rows' words (isWords()) or a prefix only one
// of them has. Several are ANDed. An empty or shared prefix narrows nothing yet, so the list
// does not empty while one is typed; a word no state starts with stays a word of the query.
//
// The text is matched by words (DECISIONS C22, #125, through SearchWords): a torrent shows
// when every word of the query appears in its name, category or tags, folded the same way
// (`.`, `_`, `-` and spaces are one space), or when the query is a prefix of its hash, as
// typed. While a query is typed the rows are ranked before the sort role: the whole query as
// a phrase in the name first, then its words in order, then in any order. A hash prefix
// ranks with the phrases. Clearing the query brings the plain sort back.

#include "core/SearchWords.hpp"
#include "core/Torrent.hpp"

#include <QCollator>
#include <QHash>
#include <QItemSelectionModel>
#include <QPointer>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVariant>

#include <optional>

namespace omaseed::core {

class LocalDay;
class TorrentModel;
class TrackerModel;

class TorrentFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(StatusFilter statusFilter READ statusFilter WRITE setStatusFilter NOTIFY
                   statusFilterChanged)
    Q_PROPERTY(Question question READ question WRITE setQuestion NOTIFY questionChanged)
    Q_PROPERTY(QVariant category READ category WRITE setCategory RESET clearCategory NOTIFY
                   categoryChanged)
    Q_PROPERTY(QVariant tag READ tag WRITE setTag RESET clearTag NOTIFY tagChanged)
    Q_PROPERTY(
        QVariant tracker READ tracker WRITE setTracker RESET clearTracker NOTIFY trackerChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(QStringList onlyHashes READ onlyHashes WRITE setOnlyHashes NOTIFY onlyHashesChanged)
    Q_PROPERTY(int sortRole READ sortRole WRITE setSortRole NOTIFY sortRoleChanged)
    Q_PROPERTY(
        QString sortRoleName READ sortRoleName WRITE setSortByRoleName NOTIFY sortRoleChanged)
    Q_PROPERTY(Qt::SortOrder sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool holdOrder READ holdOrder WRITE setHoldOrder NOTIFY holdOrderChanged)

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

    /// DECISIONS C24 H2: the rail's questions, in its order; AnyQuestion is the library.
    enum Question {
        AnyQuestion,
        NeedsYou,
        Arriving,
        LandedToday,
        GivingBack,
        Resting,
    };
    Q_ENUM(Question)

    explicit TorrentFilterModel(QObject *parent = nullptr);

    StatusFilter statusFilter() const { return m_statusFilter; }
    void setStatusFilter(StatusFilter filter);

    Question question() const { return m_question; }
    void setQuestion(Question question);

    /// Where "today" comes from, for LandedToday; not owned. Without one, the system clock
    /// at each pass, and nothing re-filters at midnight.
    void setDay(LocalDay *day);

    /// The question `torrent` answers. `logged`: the log holds a problem or a failure for it
    /// (TorrentModel::hasLoggedTrouble()); `dayStartMSecs`: local midnight, today.
    static Question questionOf(const Torrent &torrent, bool logged, qint64 dayStartMSecs);
    /// The words `is:` takes: the StatusFilter rows', All's aside.
    static QStringList isWords();
    /// The StatusFilter `word` names, whole or by a prefix only it has (case-insensitive);
    /// All when none or several do.
    static StatusFilter statusFilterForWord(QStringView word);

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

    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);

    QStringList onlyHashes() const { return m_onlyHashes; }
    void setOnlyHashes(const QStringList &hashes);

    int sortRole() const { return m_sortRole; }
    void setSortRole(int role);
    QString sortRoleName() const;
    Q_INVOKABLE void setSortByRoleName(const QString &roleName);

    Qt::SortOrder sortOrder() const { return m_sortOrder; }
    void setSortOrder(Qt::SortOrder order);

    int count() const { return rowCount(); }

    /// While true, the rows keep the order they had when it was set (see the header).
    bool holdOrder() const { return m_holdOrder; }
    void setHoldOrder(bool hold);

    /// The proxy row showing `hash`, or -1.
    Q_INVOKABLE int rowOfHash(const QString &hash) const;
    /// The hash on proxy row `row`, or an empty string.
    Q_INVOKABLE QString hashAt(int row) const;
    /// Selects rows `from` to `to` (either order, clamped) in `selection`, a selection model
    /// over this proxy, as one range: one selectionChanged instead of one per row. The table's
    /// per-row loop froze the window for seconds on a 3,450-torrent library (ctrl+a), long
    /// enough for Hyprland's "not responding". Added to what is selected, or with `replace`
    /// in its place (a shift+click), still in one selectionChanged.
    Q_INVOKABLE void
    selectRows(QItemSelectionModel *selection, int from, int to, bool replace = false) const;

    /// The flag a StatusFilter stands for; StatusGroup::NoGroup for All.
    static StatusGroup groupFor(StatusFilter filter);

    void setSourceModel(QAbstractItemModel *model) override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

Q_SIGNALS:
    void statusFilterChanged();
    void questionChanged();
    void categoryChanged();
    void tagChanged();
    void trackerChanged();
    void textChanged();
    void folderChanged();
    void onlyHashesChanged();
    void sortRoleChanged();
    void sortOrderChanged();
    void countChanged();
    void holdOrderChanged();

private:
    void refilter();
    void resort();
    /// An explicit filter change (text, status, category, tag, tracker): a re-filter, and
    /// while the order is held, a fresh sort first and a new hold after.
    void applyFilterChange();
    /// Every row sorted once, now: one layoutChanged.
    void sortNow();
    /// Notes every shown row's place, by hash, for the held order, and every torrent the
    /// library holds, so a later one is known as new.
    void freezeOrder();
    /// Lets go of the held order (the places, the new rows' slots, the library then).
    void dropHeldOrder();
    /// Source row `sourceRow`'s place in the held order: its own when it was shown at the
    /// hold, a slot after the held row it follows in the live order when it is new to the
    /// library (C20), and below every held row when it only newly matches the filter.
    double heldPlace(int sourceRow) const;
    /// The live order, as lessThan() reads it without a hold (ascending).
    bool liveLessThan(const QModelIndex &left, const QModelIndex &right) const;
    /// Reloads m_trackerHashes for the current tracker filter; true when it changed.
    bool reloadTrackerHashes();
    /// Whether the tracker filter goes by TrackerModel membership rather than the host of
    /// the current tracker.
    bool usesTrackerMembership() const;
    /// TorrentModel::runsAboutToBeAnnounced(): holds the dynamic sort when the runs would
    /// re-sort more than once.
    void holdSortFor(const QList<int> &rows, const QList<int> &roles);
    /// TorrentModel::runsAnnounced(): one sort, and a re-filter if a held row's filter moved.
    void releaseSort();
    /// Whether source row `sourceRow`, mapped, no longer sits between its proxy neighbours.
    bool outOfOrder(int sourceRow) const;
    /// Source row `sourceRow`'s place in the name-then-hash order, built on first use after
    /// rows came, went or were renamed.
    int nameRank(int sourceRow) const;
    /// Source row `sourceRow`'s SearchWords::Rank against the query, cached per row.
    int matchRank(int sourceRow) const;
    /// -1, 0 or 1: `left` against `right`, two values of the sort role.
    int compareValues(const QVariant &left, const QVariant &right) const;
    void dropNameRanks()
    {
        m_nameRanks.clear();
        m_matchRanks.clear();
    }

    StatusFilter m_statusFilter = All;
    Question m_question = AnyQuestion;
    QPointer<LocalDay> m_day;
    /// The text's `is:` words, as groups a row must all be in.
    StatusGroups m_isGroups;
    /// The text without its `is:` words: what the name, category, tags and hash meet.
    QString m_queryText;
    /// Local midnight in ms since the epoch, for one filter pass (m_day's, or the clock's).
    qint64 dayStartMSecs() const;
    std::optional<QString> m_category;
    std::optional<QString> m_tag;
    std::optional<QString> m_tracker;
    QPointer<TrackerModel> m_trackerModel;
    QSet<QString> m_trackerHashes; ///< the filtered host's torrents, from m_trackerModel
    QString m_text;
    /// m_text, folded.
    SearchWords::Query m_query;
    QString m_folder; ///< normalised; "" is any
    QStringList m_onlyHashes;
    QSet<QString> m_onlyHashSet;
    int m_sortRole = 0; ///< resolved to TorrentModel::NameRole on the first setSourceModel
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    QCollator m_collator;
    QList<double> m_heldKeys;       ///< the sort role by source row, in releaseSort()
    mutable QList<int> m_nameRanks; ///< by source row; empty until a tie-break needs it
    /// By source row, -1 not yet known; used while a query is typed.
    mutable QList<qint8> m_matchRanks;
    QList<QMetaObject::Connection> m_sourceConnections;
    QHash<QString, int> m_heldPlaces; ///< the held order: proxy row by hash, while holdOrder
    QSet<QString> m_heldLibrary;      ///< every torrent in the library when the hold began
    mutable QHash<QString, double> m_heldSlots; ///< new torrents' places, found on first use
    bool m_holdOrder = false;
    bool m_sortHeld = false;
    bool m_refilterAfterHold = false;
};

} // namespace omaseed::core
