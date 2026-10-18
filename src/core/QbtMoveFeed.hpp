#pragma once

// QbtMoveFeed — a MoveJob fed by qBittorrent (board 42 §11, DECISIONS C15, #96).
//
// MoveJob speaks in neutral terms; this is the qBittorrent side of it:
//
// - sendRequested(hashes, location) → torrents/setLocation, its answer → onSendResult();
// - SyncEngine::synced → onSynced(); a server lost → setConnected(false), back → true;
// - while the job runs, ProblemLog reads the log on every poll (watchJob, C15 M15) and its
//   "Failed to move torrent" lines → onFailed();
// - the room before each send through FreeSpaceProbe (M5), and the bytes already at a
//   torrent's target, read on this machine when it sees the server's disks (M14).
//
// When the engines branch takes this in, the feed is what sits behind EngineClient: another
// engine brings its own.

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>

namespace omaseed::core {

class FreeSpaceProbe;
class MoveJob;
class SyncEngine;

class QbtMoveFeed : public QObject
{
    Q_OBJECT

public:
    /// Ties `job` to `engine` (and its client and problem log). Neither is owned.
    QbtMoveFeed(SyncEngine *engine, MoveJob *job, QObject *parent = nullptr);

    /// Room and growth are read through `probe` (not owned); nullptr reads neither.
    void setFreeSpace(const FreeSpaceProbe *probe);

private:
    void send(const QStringList &hashes, const QString &location);
    void synced(bool fullUpdate);
    void watch();
    qint64 bytesAt(const QString &hash, const QString &target);

    QPointer<SyncEngine> m_engine;
    QPointer<MoveJob> m_job;
    const FreeSpaceProbe *m_probe = nullptr;
    /// bytesAt() walks a folder: once every 30 s per torrent at most.
    QHash<QString, QPair<qint64, QElapsedTimer>> m_grown;
};

} // namespace omaseed::core
