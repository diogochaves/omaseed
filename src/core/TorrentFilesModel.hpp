#pragma once

// TorrentFilesModel — the details pane's Files tab (spec §2 Fig. 2, docs/DECISIONS.md N1).
//
// `torrents/files` sends a flat array of files whose `name` is the full path inside the
// torrent ("Big Buck Bunny 4K/subs/bbb.en.srt"). The figure draws that as a tree: the
// folders are synthesised from the `/` separators, a folder row sums its children's bytes,
// shows their weighted progress, and shows a priority only when every file under it agrees
// ("mixed" otherwise).
//
// The model is a QAbstractListModel over the *visible* rows of that tree, not a
// QAbstractItemModel: a folded folder simply drops its subtree from the row list. This is
// what the plan's T3.4 note calls "a flat model with `expanded` gates" — a ListView can draw
// it with `depth` indentation, and `h`/`l` fold by calling setExpanded() on the cursor row.
// Fold state is remembered per folder path, so it survives a refresh of the file list.
//
// N1 adds what qBittorrent's WebUI does with a torrent's content, all of it on the same
// visible-rows list:
//   * a name filter (`filterText`, docs/12 F4), matched against a file's path inside the
//     torrent and ignoring case: `|` separates alternatives ("mkv|mp4"), a file must hold
//     every space-separated word of one of them, and `*` in a word stands for any run of
//     characters ("*.srt", "bbb*srt"). Otherwise a word is a plain substring, so "subs/"
//     finds a folder's files, and it folds as the library's search does (DECISIONS C22):
//     `.`, `_` and `-` in the word and in the path count as a space ("big.buck" finds
//     "Big_Buck_Bunny"). A `*` word is matched on the path as it is. A folder shows when one
//     of its files does, and every folder is open while a filter is typed. A folder then acts
//     on the files it shows, not on the hidden ones.
//   * hiding the skipped files (`hideSkipped`, #9): a file at priority skip fails the
//     filter, so a folder whose every file is skipped goes too. Unlike a typed filter it
//     leaves the folds alone, and it is a preference, not the torrent's: clear() keeps it.
//     A change of priority under it (a key, the 2 s poll) hides or shows the file at once.
//   * a sort (`sortKey`, `sortDescending`): siblings are sorted inside their folder, so the
//     tree survives. By name, folders come first in both directions (qBittorrent's content
//     view does the same); by any number, folders and files mix. "" is the torrent's own
//     order.
//   * a selection of rows (`space`/`v` in the pane), kept by path so it survives a refresh.
//     The priority keys and the priority menu act on it, or on the cursor row alone.
//   * remaining bytes, availability and the piece range, per file and summed per folder.
//
// applyFiles() keeps the tree when the shape (the same paths in the same order) has not
// changed and emits dataChanged() for the rows whose numbers moved; only a different shape
// resets the model. That is what lets the 2 s poll run without dropping the cursor.

#include <QAbstractListModel>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace omaseed::core {

/// qBittorrent's file priorities, plus the synthetic one a folder shows when its files
/// disagree. The numbers are the API's own (`torrents/filePrio`).
enum class FilePriority {
    Mixed = -1,
    Skip = 0,
    Normal = 1,
    High = 6,
    Maximal = 7,
};

/// "skip", "normal", "high", "maximal", "mixed" (spec §2 Fig. 2's Priority column).
QString filePriorityText(int priority);

class TorrentFilesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY countChanged)
    /// How many files pass the filter (all of them while there is none): the "n" of "n of m".
    Q_PROPERTY(int matchCount READ matchCount NOTIFY countChanged)
    /// docs/12 F1's summary line: "5 files · 2 folders · 3.91 GB · 2 skipped". Folders are
    /// the synthesised ones, skipped files are the ones at priority 0, and the size is every
    /// file's, whatever the filter shows.
    Q_PROPERTY(int folderCount READ folderCount NOTIFY summaryChanged)
    Q_PROPERTY(int skippedCount READ skippedCount NOTIFY summaryChanged)
    Q_PROPERTY(qint64 totalSize READ totalSize NOTIFY summaryChanged)
    Q_PROPERTY(QString totalSizeText READ totalSizeText NOTIFY summaryChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(bool hideSkipped READ hideSkipped WRITE setHideSkipped NOTIFY hideSkippedChanged)
    /// "" (the torrent's order), "name", "size", "progress", "remaining", "priority",
    /// "availability", "pieces".
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortChanged)
    /// Rows the user selected, folders counted once each.
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    /// The head of the tick column (#42, docs/27 D1/D4), over every file the filter shows
    /// (hidden skipped files included): 2 none skipped, 1 some, 0 all; 2 with no files.
    Q_PROPERTY(int headWanted READ headWanted NOTIFY headWantedChanged)
    /// How many files the head covers, for its tooltip.
    Q_PROPERTY(int headFileCount READ headFileCount NOTIFY headWantedChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1, ///< the leaf name only; the tree carries the folders
        PathRole,                    ///< the full path inside the torrent
        SizeRole,
        SizeTextRole,
        ProgressRole,
        ProgressTextRole,
        PriorityRole,
        PriorityTextRole,
        IsFolderRole,
        DepthRole,
        ExpandedRole,
        FileIndexesRole,  ///< the API file ids under this row (one for a file)
        AvailabilityRole, ///< 0..1, the share of the row's pieces some peer has; -1 unknown
        AvailabilityTextRole,
        RemainingRole, ///< bytes still to download; a skipped file has none
        RemainingTextRole,
        PieceFirstRole, ///< -1 when the server sent no piece_range
        PieceLastRole,
        PiecesTextRole, ///< "0–3859", or "3860" for a one-piece file
        SelectedRole,
        WantedRole, ///< the tick (#42): 2 nothing under the row skipped, 1 some, 0 all
    };
    Q_ENUM(Role)

    explicit TorrentFilesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_visible.size()); }
    /// How many real files the torrent has, folders excluded.
    int fileCount() const { return m_fileCount; }
    int matchCount() const { return m_matchCount; }
    int folderCount() const { return m_folderCount; }
    int skippedCount() const { return m_skippedCount; }
    int headWanted() const { return m_headWanted; }
    int headFileCount() const { return m_headFileCount; }
    /// The API file ids the head tick covers: every file the typed filter matches (all of
    /// them with no filter), whether hideSkipped shows it or not.
    Q_INVOKABLE QVariantList headFileIndexes() const;
    /// The priority of the file with API id `fileIndex`, or -1 when there is none.
    Q_INVOKABLE int filePriority(int fileIndex) const;
    /// Every file's priority by API id, in one pass.
    QHash<int, int> priorityByFileIndex() const;
    qint64 totalSize() const { return m_totalSize; }
    QString totalSizeText() const;

    /// Applies a `torrents/files` array. Same shape as last time -> rows update in place;
    /// anything else -> a reset.
    void applyFiles(const QJsonArray &files);
    /// Empties the model for another torrent: the filter goes too, the sort and hideSkipped
    /// stay.
    void clear();

    // --- filter and sort -------------------------------------------------------------
    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &text);
    bool hideSkipped() const { return m_hideSkipped; }
    void setHideSkipped(bool hide);
    QString sortKey() const { return m_sortKey; }
    void setSortKey(const QString &key);
    bool sortDescending() const { return m_sortDescending; }
    void setSortDescending(bool descending);
    /// The header click and the sort menu: a new key starts in its natural direction (text
    /// and pieces ascending, numbers largest first); the current key flips direction.
    Q_INVOKABLE void sortBy(const QString &key);
    /// A column header's click (#51 D14): a new key starts as sortBy() starts it, a second
    /// click flips it, and a third goes back to the torrent's own order (the `s` menu's
    /// "torrent order"), so the header alone reaches every order.
    Q_INVOKABLE void cycleSort(const QString &key);
    /// The direction a key starts in: false for text and pieces (and the torrent order),
    /// true for the numbers, largest first.
    static bool startsDescending(const QString &key);

    // --- folding ---------------------------------------------------------------------
    Q_INVOKABLE bool isFolder(int row) const;
    /// True on an open folder. Every folder reads open while a filter is typed.
    Q_INVOKABLE bool isExpanded(int row) const;
    /// No-op on a file row, while a filter is typed, or when the folder is already in that
    /// state.
    Q_INVOKABLE void setExpanded(int row, bool expanded);
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void expandAll();
    Q_INVOKABLE void collapseAll();
    /// The row of `row`'s parent folder, or -1 at the top level (`h` on a file).
    Q_INVOKABLE int parentRow(int row) const;
    /// The visible row showing `path`, or -1.
    Q_INVOKABLE int rowOfPath(const QString &path) const;

    // --- selection -------------------------------------------------------------------
    int selectedCount() const { return int(m_selected.size()); }
    Q_INVOKABLE bool isSelected(int row) const;
    Q_INVOKABLE void setSelected(int row, bool selected);
    Q_INVOKABLE void toggleSelected(int row);
    /// Selects every visible row from `from` to `to`, both included, in either order.
    Q_INVOKABLE void selectRange(int from, int to);
    Q_INVOKABLE void selectAll();
    /// docs/12 F3's `ctrl+a`: every file the filter shows, folded away or not (a folder counts
    /// through its files, so it is not selected itself); with no filter, every file. Adds to
    /// the selection.
    Q_INVOKABLE void selectAllFiles();
    Q_INVOKABLE void clearSelection();

    // --- priorities ------------------------------------------------------------------
    /// The API file ids under `row`: one for a file, the whole subtree for a folder (only
    /// the files that pass the filter, while one is typed).
    Q_INVOKABLE QVariantList fileIndexes(int row) const;
    /// What an action applies to: the selection's files when there is a selection, the
    /// cursor row's otherwise. Sorted, no duplicates.
    Q_INVOKABLE QVariantList targetFileIndexes(int cursorRow) const;
    Q_INVOKABLE int priority(int row) const;
    /// The one priority every target file shares, or -1 ("mixed") when they differ.
    Q_INVOKABLE int targetPriority(int cursorRow) const;
    /// qBittorrent's "by shown file order": the target files, in the order the list shows
    /// them, split in thirds — maximal, high, normal (a group is at least one file). Returns
    /// `[{priority, fileIndexes}]`, one entry per group that has files.
    Q_INVOKABLE QVariantList shownOrderGroups(int cursorRow) const;
    /// Sets `priority` on the files `fileIndexes` names, locally, before the server confirms
    /// it (and instead of it, in demo mode). Folders re-aggregate.
    Q_INVOKABLE void setLocalPriority(const QVariantList &fileIndexes, int priority);

    /// The full path of a row inside the torrent, for the `o` open and `y` copy actions.
    Q_INVOKABLE QString pathAt(int row) const;
    /// The leaf name alone, without the folder's trailing `/`: what `r` offers to edit.
    Q_INVOKABLE QString nameAt(int row) const;
    /// Every file's path inside the torrent, shown or not: what a batch rename must not land
    /// on (#11).
    Q_INVOKABLE QStringList filePaths() const;
    /// What a batch rename covers (#11, docs/18 B1 and E1): the selected files the filter
    /// shows, in the tab's order, else every file it shows. Never folders.
    Q_INVOKABLE QStringList renameTargets() const;
    /// How many selected rows are files the filter shows (`r` opens the rename sheet from 2).
    Q_INVOKABLE int selectedFileCount() const;
    /// Moves `oldPath` (a file, or a folder and everything under it) to `newPath` locally,
    /// for demo mode's rename. Keeps the fold state and selection of what moved.
    Q_INVOKABLE void renameLocal(const QString &oldPath, const QString &newPath);

    /// The next priority up/down the ladder skip -> normal -> high -> maximal. A "mixed"
    /// folder raises to high and lowers to normal, the way the WebUI settles a mixed set.
    Q_INVOKABLE static int raisedPriority(int priority);
    Q_INVOKABLE static int loweredPriority(int priority);
    Q_INVOKABLE static QString priorityText(int priority) { return filePriorityText(priority); }

    /// Where a file of the torrent sits on the server's disk. `relativePath` is a row's
    /// path; `contentPath` and `savePath` are the torrent's (sync/maindata). The files'
    /// names are relative to the folder that holds the torrent's top entry: that is
    /// `contentPath` less the top entry when it ends with it (which also follows qBittorrent's
    /// incomplete-files folder), and `savePath` otherwise.
    static QString
    serverPathOf(const QString &contentPath, const QString &savePath, const QString &relativePath);

    /// Whether `path` passes the filter `filter`, by the rules above. An empty filter, or one
    /// of nothing but separators, passes everything.
    static bool filterMatches(const QString &filter, const QString &path);

Q_SIGNALS:
    void countChanged();
    void filterTextChanged();
    void hideSkippedChanged();
    void sortChanged();
    void selectionChanged();
    void summaryChanged();
    void headWantedChanged();
    /// The same rows changed places (a sort, or a poll moving the number sorted by). The
    /// cursor is a row number, so the view re-finds its file by path (rowOfPath).
    void reordered();

private:
    struct Node
    {
        QString name; ///< leaf name
        QString path; ///< full path inside the torrent
        int depth = 0;
        bool isFolder = false;
        int fileIndex = -1; ///< the API's `index`, -1 on a folder
        int order = 0;      ///< position in the API array (a folder: its first file's)
        qint64 size = 0;
        qint64 remaining = 0;
        double progress = 0.0;
        int priority = int(FilePriority::Normal);
        double availability = -1.0;
        int pieceFirst = -1;
        int pieceLast = -1;
        bool matches = true;    ///< passes the filter and hideSkipped (a folder: one file does)
        QList<int> children;    ///< indexes into m_nodes
        QList<int> fileIndexes; ///< API ids in this subtree
        QList<int> shownIds;    ///< the ids of fileIndexes that pass the filter
        int files = 1;          ///< files in this subtree (a file: itself)
        int skippedFiles = 0;   ///< of those, at priority skip
        int parent = -1;
    };

    /// One word of the filter: a plain lower-case substring, or a `*` pattern.
    struct FilterWord
    {
        QString plain;
        QRegularExpression pattern; ///< valid only when the word has a `*`
        bool wildcard = false;
    };
    /// The filter's `|` alternatives, each a list of words that must all match.
    using Filter = QList<QList<FilterWord>>;
    static Filter parseFilter(const QString &text);
    static bool matches(const Filter &filter, const QString &path);

    /// The paths, in order, that a given `torrents/files` array would build. Two arrays with
    /// the same shape can be merged in place.
    static QStringList shapeOf(const QJsonArray &files);
    static void readFile(Node &node, const QJsonObject &file);

    void rebuild(const QJsonArray &files);
    void updateInPlace(const QJsonArray &files);
    void aggregate(int node);
    void applyFilter();
    bool matchFilter(int node, const Filter &filter);
    QList<int> sortedChildren(const QList<int> &children) const;
    bool lessThan(int left, int right) const;
    QList<int> computeVisible() const;
    void rebuildVisible();
    /// Recomputes the row list after a change of fold, filter, sort or values: a reset when
    /// the set of rows changed, row moves (or one layoutChanged on a very long list) when
    /// only their order did. True when anything changed.
    bool relayout();
    /// Brings m_visible to `next`, which holds the same node indexes in another order.
    void reorderVisible(const QList<int> &next);
    /// Files under the target rows, in the order the tree is drawn (sort applied, folds
    /// ignored), filter respected.
    QList<int> targetLeaves(int cursorRow) const;
    void collectLeaves(int node, QList<int> &out) const;
    /// The API's file index → the leaf's node index.
    QHash<int, int> leavesByFileIndex() const;
    void emitAllChanged(const QList<int> &roles);
    /// Recounts folders, skipped files and the total size; emits summaryChanged() on a change.
    void updateSummary();
    /// Recounts the head tick (headWanted, headFileCount); emits headWantedChanged().
    void updateHeadWanted();

    QList<Node> m_nodes; ///< the tree, parents before children
    QList<int> m_roots;
    QList<int> m_visible; ///< node indexes, in draw order
    QStringList m_shape;
    QJsonArray m_files;     ///< the last array, for renameLocal()
    QSet<QString> m_folded; ///< folder paths the user closed
    QSet<QString> m_selected;
    int m_fileCount = 0;
    int m_matchCount = 0;
    int m_folderCount = 0;
    int m_skippedCount = 0;
    int m_headWanted = 2;
    int m_headFileCount = 0;
    qint64 m_totalSize = 0;
    QString m_filterText;
    bool m_hideSkipped = false;
    QString m_sortKey;
    bool m_sortDescending = false;
};

} // namespace omaseed::core
