#pragma once

// AddFilesModel — the add sheet's file picker (docs/11 D1): the files of a `.torrent` that is
// not on the server yet, as a foldable tree with a priority per file and folder, chosen before
// the add.
//
// The tree itself is TorrentFilesModel's, the one the details pane's Files tab draws, fed
// from the parsed metainfo instead of `torrents/files`. This proxy keeps the priorities the
// user picks (there is no server to hold them yet) and adds one role, `wanted`, for the
// check mark: 0 when every file under the row is skipped, 2 when none is, 1 in between. The
// file ids are positions in TorrentFile::files, which is qBittorrent's own numbering (pad
// files left out), so `priorityGroups()` can go straight to `torrents/filePrio` once the
// torrent is on the server.

#include "core/TorrentFile.hpp"

#include <QIdentityProxyModel>
#include <QList>
#include <QMap>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace omaseed::core {

class TorrentFilesModel;

class AddFilesModel : public QIdentityProxyModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// How many files the torrent has (folders excluded).
    Q_PROPERTY(int fileCount READ fileCount NOTIFY choicesChanged)
    /// How many of them are not skipped, and their bytes.
    Q_PROPERTY(int wantedCount READ wantedCount NOTIFY choicesChanged)
    Q_PROPERTY(qint64 wantedSize READ wantedSize NOTIFY choicesChanged)
    /// "3 of 8 files · 9.2 GB", the sheet's line above the tree.
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY choicesChanged)
    /// True once any file is not at "normal": the add then has to set priorities afterwards.
    Q_PROPERTY(bool customised READ isCustomised NOTIFY choicesChanged)

public:
    /// The extra role, after TorrentFilesModel's own.
    enum Role {
        WantedRole = Qt::UserRole + 100, ///< 0 all skipped, 1 some, 2 none skipped
        RenamedRole,                     ///< the row's own name differs from the metainfo's
        OriginalNameRole,                ///< the metainfo's name for the row (no "/")
    };
    Q_ENUM(Role)

    explicit AddFilesModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }
    int fileCount() const { return int(m_files.size()); }
    int wantedCount() const;
    qint64 wantedSize() const;
    QString summaryText() const;
    bool isCustomised() const;

    /// Shows `files` (TorrentFile::files), every one at "normal", every folder open.
    void load(const QList<TorrentFileEntry> &files);
    Q_INVOKABLE void clear();

    // --- the tree (TorrentFilesModel's, forwarded) -----------------------------------
    Q_INVOKABLE bool isFolder(int row) const;
    Q_INVOKABLE bool isExpanded(int row) const;
    Q_INVOKABLE void setExpanded(int row, bool expanded);
    /// How deep a row sits (0 at the top), for `h` to find the folder a row is in.
    Q_INVOKABLE int depthAt(int row) const;

    // --- choosing ----------------------------------------------------------------------
    /// The priority a row shows: the file's own, or a folder's when its files agree, else -1.
    Q_INVOKABLE int priority(int row) const;
    /// Every file under `row` to `priority` (0 skip, 1 normal, 6 high, 7 maximal).
    Q_INVOKABLE void setPriority(int row, int priority);
    /// `space`: a row with anything wanted under it is skipped whole, otherwise it goes back
    /// to "normal" — a check box over the subtree.
    Q_INVOKABLE void toggleWanted(int row);
    /// `+` / `-`: one step up or down skip → normal → high → maximal, on the whole row.
    Q_INVOKABLE void raise(int row);
    Q_INVOKABLE void lower(int row);
    /// A click on a row's priority (manual box M17): normal → high → maximal → normal, on the
    /// row's wanted files only, so a folder's skipped files stay skipped. A mixed row starts
    /// over at normal; a row with nothing wanted comes back at normal (the click reads as
    /// "stop skipping this").
    Q_INVOKABLE void cyclePriority(int row);
    /// The header's "all" (every skipped file back to normal, the others keep their
    /// priority) and "none" (every file skipped).
    Q_INVOKABLE void setAllWanted(bool wanted);

    // --- renaming (manual box M18) -----------------------------------------------------
    /// Why `name` cannot be `row`'s new name ("" when it can): empty, ".", "..", a "/" or
    /// "\", or a sibling already called that.
    Q_INVOKABLE QString renameError(int row, const QString &name) const;
    /// Renames a file, or a folder (every path under it follows). False, and nothing
    /// changes, when `renameError()` says no. Renaming to the current name does nothing.
    Q_INVOKABLE bool rename(int row, const QString &name);
    /// The row's name without a folder's "/", for the inline field to start from.
    Q_INVOKABLE QString plainName(int row) const;
    /// A row's path inside the torrent (after the renames), and the row showing a path (-1
    /// when none does, e.g. inside a folded folder): the tree keeps its cursor on a row the
    /// rename moved.
    Q_INVOKABLE QString pathOf(int row) const { return pathAt(row); }
    Q_INVOKABLE int rowOfPath(const QString &path) const;
    /// The renames in the order they were made, each `{kind: "file"|"folder", from, to}` with
    /// paths inside the torrent (no root folder): replayed on the server after the add,
    /// through `torrents/renameFile` / `torrents/renameFolder`.
    Q_INVOKABLE QVariantList renameList() const { return m_renames; }
    /// A file's id and metainfo path, for App to find the server's root folder in front of
    /// the paths (`{index, path}`, or empty with no files).
    Q_INVOKABLE QVariantMap renameAnchor() const;

    /// One priority per file, in file-id order.
    QList<int> priorities() const { return m_priorities; }
    /// The same, for QML and for the request map the sheet builds.
    Q_INVOKABLE QVariantList priorityList() const;

    /// The ids to send to `torrents/filePrio`, grouped by priority, leaving out "normal"
    /// (what qBittorrent gives every file on add). Empty when nothing was changed. Takes a
    /// `priorityList()` so App can work from the sheet's request map alone.
    static QMap<int, QList<int>> priorityGroups(const QVariantList &priorities);

Q_SIGNALS:
    void countChanged();
    void choicesChanged();

private:
    /// Pushes m_priorities into the tree (same shape, so it updates rows in place).
    void applyToTree();
    QList<int> idsAt(int row) const;
    /// Sets each of `ids` to `priority`; true when any changed (the caller then applies).
    bool setIds(const QList<int> &ids, int priority);

    /// A row's path as shown (after renames), and its metainfo path.
    QString pathAt(int row) const;
    QString originalPathAt(int row) const;

    TorrentFilesModel *m_tree = nullptr;
    QList<TorrentFileEntry> m_files;
    QList<int> m_priorities;
    /// Each file's path after the renames so far (m_files keeps the metainfo's).
    QList<QString> m_paths;
    QVariantList m_renames;
};

} // namespace omaseed::core
