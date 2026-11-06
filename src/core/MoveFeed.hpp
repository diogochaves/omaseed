#pragma once

// MoveFeed — a MoveJob fed by its server, whatever its engine (board 42 §11, DECISIONS C15,
// #96; board 47 §4, C25 K21–K23).
//
// MoveJob speaks in neutral terms; this ties it to SyncEngine and its EngineClient:
//
// - sendRequested(hashes, location) → EngineClient::setLocation(), its answer → onSendResult();
// - SyncEngine::synced → onSynced(); a server lost → setConnected(false), back → true;
// - a failed move → onFailed(), from one source per engine (setFeatures):
//   - the log (qBittorrent): while the job runs, ProblemLog reads it on every poll
//     (watchJob, C15 M15) and its "Failed to move torrent" lines come back as jobMoveFailed;
//   - the torrent (EngineFeatures::moveStopsOnFailure: Transmission, Deluge): after its send
//     answered, the torrent in error while its folder is still the old one; the engine's
//     text is kept for the block and read for the cause ("Couldn't move 'A' to 'B':
//     Permission denied (13)" is noAccess);
// - the room before each send through FreeSpaceProbe (M5), or, where omaseed can't see the
//   disks and the engine reads any folder's free space (roomOnServer), as the server last
//   read it for the job's targets; and the bytes already at a torrent's target, read on this
//   machine when it sees the server's disks (L10, M14).
//
// An engine that holds every answer while it moves (EngineFeatures::movesHoldAnswers,
// Transmission, K22) gets one torrent at a time: a second move queued behind the first would
// keep the poll waiting until both had ended. Meanwhile the torrent it is moving reads as
// moving (EngineClient::busyMoving), SyncEngine::busyTick keeps M14's stuck clock running, and
// when the job calls the move stuck the engine stops counting it as busy, so the next silence
// is C12's "didn't answer" again.

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QSet>

namespace omaseed::core {

class FreeSpaceProbe;
class MoveJob;
class SyncEngine;
struct EngineFeatures;

class MoveFeed : public QObject
{
    Q_OBJECT

public:
    /// Where a failed move is learnt (board 47 §4).
    enum class Failures {
        Log,     ///< the engine's log (qBittorrent)
        Torrent, ///< the torrent going into error after its send (Transmission, Deluge)
    };

    /// Ties `job` to `engine` (and its client and problem log). Neither is owned.
    MoveFeed(SyncEngine *engine, MoveJob *job, QObject *parent = nullptr);

    /// Room and growth are read through `probe` (not owned); nullptr reads neither.
    void setFreeSpace(const FreeSpaceProbe *probe);
    /// The server's engine: where failures come from, how many go at once, where room is
    /// read. qBittorrent's ways until told.
    void setFeatures(const EngineFeatures &features);
    Failures failures() const { return m_failures; }

    /// The cause key (TorrentProblem's: noAccess, diskFull…) and the system's words in an
    /// engine's text about a failed move: "Couldn't move 'A' to 'B': Permission denied (13)"
    /// → {"noAccess", "Permission denied"}.
    static QPair<QString, QString> causeOf(const QString &engineText);

private:
    void send(const QStringList &hashes, const QString &location);
    void synced(bool fullUpdate);
    void watch();
    /// The torrent source: a send that answered, then the torrent in error, still at home.
    void readFailures();
    /// The server's free space for the targets still to send to (roomOnServer).
    void readRoom();
    qint64 room(const QString &path) const;
    qint64 bytesAt(const QString &hash, const QString &target);
    bool seesDisks() const;

    QPointer<SyncEngine> m_engine;
    QPointer<MoveJob> m_job;
    const FreeSpaceProbe *m_probe = nullptr;
    Failures m_failures = Failures::Log;
    bool m_holdsAnswers = false;
    bool m_roomOnServer = false;
    /// bytesAt() walks a folder: once every 30 s per torrent at most.
    QHash<QString, QPair<qint64, QElapsedTimer>> m_grown;
    /// The torrent source: sends the server took, by hash, until they land or fail.
    QSet<QString> m_answered;
    /// roomOnServer: free bytes by target folder as the server read them, and when.
    QHash<QString, QPair<qint64, QElapsedTimer>> m_room;
    QSet<QString> m_roomAsked;
};

} // namespace omaseed::core
