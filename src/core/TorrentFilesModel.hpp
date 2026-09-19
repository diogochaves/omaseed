#pragma once

// TorrentFilesModel — the details pane's Files tab (spec §2 Fig. 2).
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
// applyFiles() keeps the tree when the shape (the same paths in the same order) has not
// changed and emits dataChanged() for the rows whose numbers moved; only a different shape
// resets the model. That is what lets the 2 s poll run without dropping the cursor.

#include <QAbstractListModel>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QString>
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
        AvailabilityRole, ///< -1 on a folder
    };
    Q_ENUM(Role)

    explicit TorrentFilesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_visible.size()); }
    /// How many real files the torrent has, folders excluded.
    int fileCount() const { return m_fileCount; }

    /// Applies a `torrents/files` array. Same shape as last time -> rows update in place;
    /// anything else -> a reset.
    void applyFiles(const QJsonArray &files);
    void clear();

    // --- folding ---------------------------------------------------------------------
    Q_INVOKABLE bool isFolder(int row) const;
    Q_INVOKABLE bool isExpanded(int row) const;
    /// No-op on a file row, or when the folder is already in that state.
    Q_INVOKABLE void setExpanded(int row, bool expanded);
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void expandAll();
    Q_INVOKABLE void collapseAll();

    // --- priorities ------------------------------------------------------------------
    /// The API file ids under `row`: one for a file, the whole subtree for a folder.
    Q_INVOKABLE QVariantList fileIndexes(int row) const;
    Q_INVOKABLE int priority(int row) const;
    /// The full path of a row inside the torrent, for the `o` open and `y` copy actions.
    Q_INVOKABLE QString pathAt(int row) const;
    /// The next priority up/down the ladder skip -> normal -> high -> maximal. A "mixed"
    /// folder raises to high and lowers to normal, the way the WebUI settles a mixed set.
    Q_INVOKABLE static int raisedPriority(int priority);
    Q_INVOKABLE static int loweredPriority(int priority);
    Q_INVOKABLE static QString priorityText(int priority) { return filePriorityText(priority); }

Q_SIGNALS:
    void countChanged();

private:
    struct Node
    {
        QString name; ///< leaf name
        QString path; ///< full path inside the torrent
        int depth = 0;
        bool isFolder = false;
        int fileIndex = -1; ///< the API's `index`, -1 on a folder
        qint64 size = 0;
        double progress = 0.0;
        int priority = int(FilePriority::Normal);
        double availability = -1.0;
        QList<int> children;    ///< indexes into m_nodes
        QList<int> fileIndexes; ///< API ids in this subtree
        int parent = -1;
    };

    /// The paths, in order, that a given `torrents/files` array would build. Two arrays with
    /// the same shape can be merged in place.
    static QStringList shapeOf(const QJsonArray &files);

    void rebuild(const QJsonArray &files);
    void updateInPlace(const QJsonArray &files);
    void aggregate(int node);
    void rebuildVisible();

    QList<Node> m_nodes; ///< the tree, parents before children
    QList<int> m_roots;
    QList<int> m_visible; ///< node indexes, in draw order
    QStringList m_shape;
    QSet<QString> m_folded; ///< folder paths the user closed
    int m_fileCount = 0;
};

} // namespace omaseed::core
