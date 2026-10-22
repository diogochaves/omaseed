#pragma once

#include "core/TorrentProblem.hpp"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantMap>

#include <chrono>

namespace omaseed::core {

class QbtClient;
class TorrentModel;

/// What qBittorrent's log says about the torrents: why errored ones are in error (docs/28,
/// #54), and the failures that left one working (docs/32, #55).
///
/// It reads `log/main` (warnings and critical lines, only those after the last id it saw)
/// after an applied poll: at most every few seconds while a torrent is errored, and at once
/// when one goes into error; every 15 s while the window shows; and on every poll for 30 s
/// after a rename or a delete omaseed asked for (watchRename(), watchDelete()); a move is a
/// job (DECISIONS C16 L9), read for its whole life (watchJob()). Each line is taken apart and tied
/// to its torrent (TorrentProblem), and what it says goes into the torrent model: `problem` and
/// `statusCause` for an error, `failure` for a move, rename or part file that didn't happen (D1).
/// An errored torrent with no line gets "didn't log a reason" once the log has been read.
///
/// A failure stays until it is fixed (the save path, or the file's name, changed), dismissed
/// (dismiss(), kept by the caller in state.json per server through dismissed() and
/// setDismissed()), or qBittorrent restarts (D3, D4). Web seed lines go to the HTTP sources
/// tab (webSeedLines(), D5) and the newest export failure to its setting (exportFailure(),
/// D6). None of them is a notice (D8): a failure omaseed itself asked for is told once, at
/// once, through watchedFailure() (D2).
///
/// qBittorrent keeps its log in memory and numbers it from 0 again after a restart. A full
/// update means the server may have restarted, so the log is read again from the start; a
/// read that starts at the top also forgets the dismissals of lines that are no longer there.
class ProblemLog : public QObject
{
    Q_OBJECT

public:
    explicit ProblemLog(QObject *parent = nullptr);

    /// The server to read the log of. Not owned; changing it forgets every line, and the
    /// dismissals with them.
    void setClient(QbtClient *client);
    /// The rows to watch and to write problems into. Not owned.
    void setTorrents(TorrentModel *torrents);

    /// The shortest time between two reads of the log while torrents stay errored; a torrent
    /// going into error reads it at once.
    void setInterval(std::chrono::milliseconds interval) { m_interval = interval; }
    /// docs/32: how often the log is read for failures while the window shows (15 s).
    void setIdleInterval(std::chrono::milliseconds interval) { m_idleInterval = interval; }
    /// docs/32 D2: how long a watch reads the log on every poll (30 s).
    void setWatchDuration(std::chrono::milliseconds duration) { m_watchDuration = duration; }
    /// Whether the window shows: failures are looked for every 15 s only then.
    void setVisible(bool visible) { m_visible = visible; }

    /// What `log/main` returned, straight in, as a read that started at lastId(): the tests
    /// feed lines through this.
    void applyEntries(const QJsonArray &entries);
    /// The last log id read; -1 before the first read, and after reset().
    int lastId() const { return m_lastId; }
    /// Lines kept that are about a torrent's files.
    int lineCount() const { return int(m_lines.size()); }
    /// The problem shown for `hash`; `found` false and an empty headline when there is none.
    TorrentProblem problem(const QString &hash) const { return m_problems.value(hash); }

    // ---- docs/32 (#55) --------------------------------------------------------------------

    /// The failure shown for `hash`, empty when there is none.
    TorrentFailure failure(const QString &hash) const { return m_failures.value(hash); }
    /// D3: hides the failure shown for `hash`, every line behind it, for good.
    void dismiss(const QString &hash);
    /// omaseed asked qBittorrent to recheck `hashes` (C17 D3): a hybrid that goes into error
    /// after it, with nothing logged, is libtorrent's torrent_inconsistent_hashes.
    void noteRechecked(const QStringList &hashes);
    /// D4: the dismissed lines, as lineKey()s, for the caller to keep per server.
    QStringList dismissed() const;
    /// What dismissed() said last time, for this server. Applies at once.
    void setDismissed(const QStringList &keys);
    /// A line's lasting name: its id, its time and a hash of its text. qBittorrent numbers its
    /// lines from 0 again when it restarts, so the id alone would name another line then.
    static QString lineKey(int id, qint64 timestamp, const QString &message);

    /// W1: the URL seed lines about `hash`, oldest first.
    QList<WebSeedLogLine> webSeedLines(const QString &hash) const
    {
        return m_webSeedLines.value(hash);
    }
    /// E1: the newest export failure: `folder` (the export folder it tried), `text` ("The last
    /// copy failed: …"), `line`. Empty when there is none.
    QVariantMap exportFailure() const { return m_exportFailure; }

    /// D2: omaseed asked qBittorrent to rename `files` files of `hash`.
    void watchRename(const QString &hash, int files);
    /// D2: omaseed asked qBittorrent to delete `hashes` with their files. Call it before the
    /// request: the torrents' names are taken from the model now, while they are still there.
    void watchDelete(const QStringList &hashes);
    /// Whether a watch is on: the log is read on every poll.
    bool isWatching() const { return !m_watches.isEmpty() || !m_jobHashes.isEmpty(); }

    /// C15 M15 (board 42 §5), C16 L9: a move is running with these torrents. The log is
    /// read on every poll for as long as it lasts, not D2's 30 s, and every "Failed to move
    /// torrent" line written since `since` about one of them is told once, through
    /// jobMoveFailed(). The failure still shows as D1's block. No footer note: the job tells.
    /// Called again with the job's torrents as they change; an empty list ends it.
    void watchJob(const QStringList &hashes, const QDateTime &since);

    /// The paths of `hash`'s files, as torrents/files lists them: a rename failure whose file
    /// is no longer among them was fixed. The tests feed it; with a server, it asks itself.
    void applyFiles(const QString &hash, const QStringList &paths);

public Q_SLOTS:
    /// One poll was applied: SyncEngine::synced. `mayRead` false (a demo, applied with the
    /// engine stopped) works out who is errored but never asks the server.
    void onSynced(bool fullUpdate, bool mayRead = true);
    /// Forgets every line and the last id; the next read starts from the top of the log.
    void reset();

Q_SIGNALS:
    /// A torrent went into `error` while omaseed was watching (not one already errored at the
    /// first sync), and the log has been read since: `problem` is what it says, found or not.
    void torrentErrored(const QString &hash,
                        const QString &name,
                        const omaseed::core::TorrentProblem &problem);
    /// docs/32 N1: a rename or delete omaseed asked for failed: the footer's note
    /// ("b.txt kept its old name: Permission denied"). `hashes` are the torrents it is
    /// about that are still in the list (none after a delete).
    void watchedFailure(const QString &text, const QStringList &hashes);
    /// C15 M15: qBittorrent logged that `hash`, one of watchJob()'s, didn't move. `reason`
    /// is libtorrent's text ("Permission denied"), `causeKey` TorrentProblem's (noAccess,
    /// readOnly, diskFull…), `line` the whole line.
    void jobMoveFailed(const QString &hash,
                       const QDateTime &at,
                       const QString &reason,
                       const QString &causeKey,
                       const QString &line);
    /// webSeedLines() or exportFailure() changed.
    void logLinesChanged();
    /// dismissed() changed: a dismissal, or a read from the top that let old ones go.
    void dismissedChanged();

private:
    struct Line
    {
        int id = 0;
        QDateTime at;
        QString message;
        QString key;
        quint64 read = 0; ///< which read brought it, for the watches
        ProblemLine parsed;
    };

    enum class Action { Rename, Delete };
    struct Watch
    {
        Action action = Action::Rename;
        QStringList hashes;
        QStringList names; ///< the torrents' names when the watch began
        int total = 0;     ///< torrents (delete) or files (rename) asked for
        quint64 fromRead = 0;
        QElapsedTimer age;
        QSet<QString> counted; ///< lineKey()s already told
        QSet<QString> failed;  ///< hashes, names or files that failed
    };

    void updateErrored(bool fullUpdate);
    void read();
    void applyRead(const QJsonArray &entries, quint64 serial, bool fromStart);
    void rematch();
    void publish(const QString &hash, const TorrentProblem &problem);

    /// The library by name and by the hash prefix libtorrent prints, as rows.
    struct Index
    {
        QHash<QString, QList<int>> byName;
        QHash<QString, QList<int>> byPrefix;
    };
    Index buildIndex() const;
    /// The torrents a failure line is about, among those added before it was written.
    QStringList failureMatch(const Line &line, const Index &index, bool *ambiguous) const;

    void updateFailures();
    void updateLogLines();
    void tellWatches(quint64 serial);
    void tellJob(quint64 serial);
    void endWatches();
    void checkFiles();
    bool failureTouched() const;

    QPointer<QbtClient> m_client;
    QPointer<TorrentModel> m_torrents;
    std::chrono::milliseconds m_interval{5000};
    std::chrono::milliseconds m_idleInterval{15000};
    std::chrono::milliseconds m_watchDuration{30000};
    bool m_visible = true;

    QList<Line> m_lines;
    int m_lastId = -1;
    bool m_reading = false;
    bool m_readOnce = false; ///< the log has been read since the errored set last changed
    QElapsedTimer m_sinceRead;
    quint64 m_issued = 0;     ///< reads asked for (or fed in), numbered from 1
    quint64 m_generation = 0; ///< reset() count: a read from before one is dropped

    QSet<QString> m_errored; ///< hashes in error or missingFiles
    /// C17 D3 (board 28's hybrid error): when each torrent was last seen checking, or asked
    /// to recheck (noteRechecked), and the errored ones that went into error soon after.
    QHash<QString, QDateTime> m_checkedAt;
    QSet<QString> m_erroredAfterCheck;
    QSet<QString> m_newlyErrored; ///< went into error while watched, notice not sent yet
    bool m_seenFirstSync = false;
    QHash<QString, TorrentProblem> m_problems;

    QHash<QString, TorrentFailure> m_failures;
    QSet<QString> m_dismissed;      ///< lineKey()s
    QSet<QString> m_settled;        ///< lineKey()s of failures seen fixed, this session
    QSet<QString> m_candidates;     ///< hashes some failure line is about: their changes matter
    QSet<QString> m_renameStanding; ///< hashes with a rename failure not yet seen fixed
    QHash<QString, QStringList> m_files; ///< torrents/files paths, by hash
    QSet<QString> m_filesAsked;          ///< torrents/files on its way, by hash
    /// Hashes omaseed asked to rename this session, and the first read after it asked.
    QHash<QString, quint64> m_renamed;
    QHash<QString, QList<WebSeedLogLine>> m_webSeedLines;
    QVariantMap m_exportFailure;
    QList<Watch> m_watches;
    QSet<QString> m_jobHashes;
    QDateTime m_jobSince;
    QSet<QString> m_jobTold; ///< lineKey()s already told to the job
};

} // namespace omaseed::core
