#pragma once

// SkippedOnDisk — what a torrent's skipped files still hold on the disk (docs/44 §2,
// DECISIONS C17 D1–D4, #118).
//
// Wanting a file makes libtorrent create it at its full length, even on a stopped torrent; on
// ntfs3, exFAT and FAT that allocates the whole file, and skipping it later keeps it (#116).
// qBittorrent never deletes a skipped file and its API can't see one, so this reads each
// skipped file on this machine: its allocated size (st_blocks, not its length, so a sparse
// file on ext4, btrfs or the newer ntfs reads as the little it costs), and whether it holds a
// downloaded piece (D3: such a file needs a recheck once it is gone, and on a hybrid that
// recheck hits libtorrent 2.1.2's torrent_inconsistent_hashes, #115).
//
// `read` is the pure half: a `torrents/files` array, the torrent's `torrents/pieceStates`, and
// where each file is on this machine ("" when it can't see the disk, D4). `refresh` runs it on
// the thread pool and says `readingChanged`; it is called when the Files tab opens and after a
// priority change, never polled (docs/44 §7).
//
// `plan` is what "Free the space…" would do (D3): delete every skipped file that holds space
// and no downloaded piece, only inside the content path, not shared with another torrent
// (board 26's guard), not held open by a program (/proc/*/fd); a file with downloaded pieces
// is deleted too on a v1-only or v2-only torrent, with a recheck after the start, and left on
// a hybrid.

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

namespace omaseed::core {

/// One skipped file of a torrent, as this machine reads it.
struct SkippedFile
{
    int index = -1;
    QString path;      ///< inside the torrent, as torrents/files names it
    QString localPath; ///< on this machine; empty when the disk can't be seen
    qint64 size = 0;
    qint64 allocated = -1; ///< bytes the file takes on its disk; -1 not there, or unseen
    double progress = 0;
    int pieceFirst = -1;
    int pieceLast = -1;
    /// Some piece in its range is downloaded (or its progress says so): libtorrent's record
    /// still says it has those pieces once the file is gone (#115's second trap).
    bool holdsPieces = false;

    bool holdsSpace() const { return allocated > 0; }
};

/// Every skipped file of one torrent, and what they add up to.
struct SkippedReading
{
    QString hash;
    bool seen = false; ///< the files were read on this machine (D4: false is a NAS unmounted)
    QList<SkippedFile> files;
    qint64 held = 0;       ///< bytes the skipped files take
    int holding = 0;       ///< skipped files that take any
    int partlyWritten = 0; ///< D4: skipped files with progress above 0 %
    qint64 torrentSize = 0;

    /// D2: the band shows when skipped files hold more than 1 GiB or 5 % of the torrent.
    bool bandShown() const;
    /// The allocated bytes per file id, for the Files tab's column (D1).
    QHash<int, qint64> allocatedByIndex() const;
};

/// What "Free the space…" does with each skipped file that holds space (D3).
struct FreeItem
{
    enum class Verdict {
        Delete,  ///< deleted
        Open,    ///< a program has it open: on Linux the space would only come back later
        Pieces,  ///< holds downloaded pieces on a hybrid: a recheck there hits #115
        Shared,  ///< another torrent uses it (board 26's guard)
        Outside, ///< not inside this torrent's content path
    };
    SkippedFile file;
    Verdict verdict = Verdict::Delete;
    QString openBy; ///< Open: the program's name ("mpv")
};

struct FreePlan
{
    QList<FreeItem> items; ///< every skipped file that holds space, deletes first
    qint64 freed = 0;      ///< what the deletes give back
    int deletes = 0;
    /// A file with downloaded pieces goes (v1-only or v2-only): recheck after the start.
    bool recheck = false;
    bool hybrid = false;
    int leftWithPieces = 0; ///< a hybrid's: freed only by adding it again
    qint64 leftWithPiecesBytes = 0;

    QStringList deletePaths() const;
    QStringList piecesPaths() const; ///< the local paths of the Pieces items
};

class SkippedOnDisk : public QObject
{
    Q_OBJECT

public:
    /// The bytes a file takes on its disk (st_blocks × 512), -1 when it is not a regular file.
    using AllocatedReader = std::function<qint64(const QString &localPath)>;
    /// The local path of a torrent's file, from its path inside the torrent; "" unseen.
    using LocalPathOf = std::function<QString(const QString &pathInTorrent)>;

    explicit SkippedOnDisk(AllocatedReader reader = readAllocated, QObject *parent = nullptr);

    /// The skipped files of `files` (a torrents/files array), with `pieceStates` (one
    /// PieceStates::State per piece; empty: progress alone decides). Without `localPathOf`
    /// nothing is read on disk and `seen` is false.
    static SkippedReading read(const QString &hash,
                               const QJsonArray &files,
                               const QByteArray &pieceStates,
                               qint64 torrentSize,
                               const LocalPathOf &localPathOf,
                               const AllocatedReader &reader);

    /// D3's plan. `contentLocal` is the torrent's content path on this machine; `sharedRoots`
    /// the local content paths of every other torrent of the library; `openBy` what
    /// openFileHolders() found for the files.
    static FreePlan plan(const SkippedReading &reading,
                         bool hybrid,
                         const QString &contentLocal,
                         const QStringList &sharedRoots,
                         const QHash<QString, QString> &openBy);

    /// Which of `paths` a running program holds open, and its name: every /proc/<pid>/fd link
    /// that can be read (another user's are not, and are skipped). `procRoot` is for tests.
    static QHash<QString, QString> openFileHolders(const QSet<QString> &paths,
                                                   const QString &procRoot = QStringLiteral(
                                                       "/proc"));

    /// lstat: st_blocks × 512 for a regular file, -1 for anything else or nothing there.
    static qint64 readAllocated(const QString &localPath);

    /// The last reading; empty (no hash) before the first.
    const SkippedReading &reading() const { return m_reading; }
    /// Reads again on the thread pool. A reading for another hash, or an older one, never
    /// replaces a newer one.
    void refresh(const QString &hash,
                 const QJsonArray &files,
                 const QByteArray &pieceStates,
                 qint64 torrentSize,
                 const LocalPathOf &localPathOf);
    /// Reads at once, on this thread (the tests, and a plan made just before it runs).
    void refreshNow(const QString &hash,
                    const QJsonArray &files,
                    const QByteArray &pieceStates,
                    qint64 torrentSize,
                    const LocalPathOf &localPathOf);
    void clear();

Q_SIGNALS:
    void readingChanged();

private:
    void apply(const SkippedReading &reading, quint64 serial);

    AllocatedReader m_reader;
    SkippedReading m_reading;
    quint64 m_serial = 0;
};

} // namespace omaseed::core
