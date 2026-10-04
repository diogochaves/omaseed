#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace omaseed::core {

/// One library torrent as SharedData sees it: the server's own paths, never mapped to this
/// machine, and the bytes it holds on disk.
struct SharedDataRow
{
    QString hash;
    QString savePath;
    QString contentPath;
    qint64 size = 0;
};

/// Which torrents use the same data on disk (docs/26 §3, #28). qBittorrent does not know two
/// torrents share files: a cross-seed added over another torrent's folder is a stranger to it,
/// so deleting one with its files, or moving one, takes the data from under the other.
///
/// Two torrents share data when
///  - their content paths are equal (the same root folder or the same single file), or
///  - one content path lies under the other's (a season pack and one episode of it), or
///  - one has no root folder (added with "No subfolder": its content path *is* its save path)
///    and both have the same save path, and their file lists overlap. Without the file lists
///    that pair is only a candidate (`Relation::NeedsFiles`).
///
/// Paths are compared as the server writes them: a trailing separator trimmed, `\` read as
/// `/`, case-sensitive. Different paths never share, so hardlinked or reflinked copies (qui's
/// and cross-seed's link mode) are never flagged: deleting one leaves the other intact.
class SharedData
{
public:
    enum class Relation {
        None,       ///< nothing in common, as far as paths can tell
        Same,       ///< the same content path
        Nested,     ///< one content path under the other's
        NeedsFiles, ///< a no-root torrent beside another in the same save path: compare files
    };

    struct Result
    {
        /// Each selected torrent with sharers outside the selection, in selection order.
        QStringList sharing;
        /// The sharers outside the selection of each hash in `sharing`, in library order.
        QHash<QString, QStringList> outsideOf;
        /// Every sharer outside the selection, once, in library order.
        QStringList outside;
        /// Groups of two or more selected torrents that share data among themselves (D3 a:
        /// "these N share one copy of their files"), each in selection order.
        QList<QStringList> inside;
        /// Hashes whose file lists are needed to settle a `NeedsFiles` pair and were not in
        /// the `files` given to `find()`. Until they are, those pairs count as not sharing.
        QStringList needFiles;
        /// The selection's bytes on disk, shared data counted once.
        qint64 diskSize = 0;
        /// The same with every outside sharer added (what "delete those too" frees).
        qint64 diskSizeWithOutside = 0;
        /// The bytes `inside` groups would have counted twice or more.
        qint64 insideSize = 0;
    };

    /// `path` as compared: `\` to `/`, trailing `/` trimmed (but `/` stays `/`).
    static QString normalize(const QString &path);
    /// True when `path` is `parent` or lies under it at a path-component boundary.
    static bool isUnder(const QString &path, const QString &parent);
    /// Added with "No subfolder": the content path is the save path itself.
    static bool hasNoRoot(const SharedDataRow &row);
    /// How `a` and `b` relate on disk, from paths alone.
    static Relation relate(const SharedDataRow &a, const SharedDataRow &b);
    /// Whether any file of `a` is a file of `b`. `aFiles`/`bFiles` are torrents/files names:
    /// paths relative to each torrent's save path (with the root folder, when there is one).
    static bool filesOverlap(const SharedDataRow &a,
                             const QStringList &aFiles,
                             const SharedDataRow &b,
                             const QStringList &bFiles);

    /// The sharers of `selected` in `library`. `files` holds the file lists known so far
    /// (hash → torrents/files names); pairs that need one that is missing are reported in
    /// `needFiles` and count as not sharing.
    static Result find(const QStringList &selected,
                       const QList<SharedDataRow> &library,
                       const QHash<QString, QStringList> &files = {});
};

} // namespace omaseed::core
