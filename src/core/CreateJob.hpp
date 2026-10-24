#pragma once

// CreateJob — one torrent made from a folder on the server's qBittorrent (#146, DECISIONS C27,
// board 49 §1): the task's life from addTask to the .torrent in hand, and for a public one its
// add, seeding from where the files are.
//
//   starting   addTask is out (every field: startSeeding off, no torrentFilePath, CR12)
//   queued     qBittorrent holds it behind another task
//   hashing    running: `progress` is a whole percent (CR13)
//   fetching   finished: torrentFile, then deleteTask
//   adding     a public one: torrents/add of those bytes with seedMode at the folder's parent,
//              not stopped, no automatic management, the original layout, the category
//   done       the bytes are here (and, public, added; `failure` AddFailed when the add was
//              refused: the .torrent is kept all the same, C27 §7)
//   failed     addTask refused, the task failed (its words), the server forgot the task (404:
//              qBittorrent restarted while hashing), or the file couldn't be fetched
//   cancelled  cancel(): deleteTask, which stops a running task
//
// The status reply echoes the trackers in full: nothing from it is logged, and errorText()
// goes through AnnounceMask. A task already running (omaseed restarted) is taken up with
// resume(). The polls go every `pollInterval` (500 ms), one at a time; a lost poll is asked
// again, a 404 ends it.

#include "core/QbtClient.hpp"
#include "core/TorrentFile.hpp"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include <chrono>
#include <functional>

class QTimer;

namespace omaseed::core {

class CreateJob : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Starting, Queued, Hashing, Fetching, Adding, Done, Failed, Cancelled };
    Q_ENUM(State)
    enum class Failure { None, Refused, Hashing, Restarted, Lost, AddFailed };
    Q_ENUM(Failure)

    struct Request
    {
        QbtClient::CreateTask task;
        /// The torrent's name: the folder's (or the file's) last part.
        QString name;
        /// Public (CR12): added with seedMode once made. Private (CR7): never added.
        bool addAfter = false;
        QString category;
        /// The server's Web API version, for how seedMode is spelled.
        QString apiVersion;
        /// The content's size when this machine read it; -1 when unknown.
        qint64 totalBytes = -1;
    };

    explicit CreateJob(QbtClient *client, QObject *parent = nullptr);

    void setPollInterval(std::chrono::milliseconds interval);
    /// Tests set the time in ms; the job reads a monotonic clock otherwise.
    void setClock(std::function<qint64()> clock);

    void start(const Request &request);
    /// Takes up a task already on the server (omaseed restarted while it ran): polls at once.
    void resume(const Request &request, const QString &taskId, qint64 startedAtMs);
    /// Stops it: deleteTask once the id is known.
    void cancel();

    State state() const { return m_state; }
    Failure failure() const { return m_failure; }
    const Request &request() const { return m_request; }
    QString taskId() const { return m_taskId; }
    /// 0–100 while hashing, 100 once made.
    int progress() const { return m_progress; }
    /// The piece size the task used (status when finished, else the .torrent's).
    qint64 pieceSize() const { return m_pieceSize; }
    qint64 startedAtMs() const { return m_startedAt; }
    qint64 elapsedMs() const;
    /// How long it took from start to the file in hand.
    qint64 tookMs() const { return m_tookMs; }
    /// qBittorrent's words for the failure, passkeys masked; "" when none.
    QString errorText() const { return m_errorText; }
    QByteArray bytes() const { return m_bytes; }
    const TorrentFile &torrent() const { return m_torrent; }
    /// Running on the server, or about to be.
    bool isActive() const;
    bool isOver() const;

Q_SIGNALS:
    void changed();
    /// The server took the task: its id, to be kept while it runs.
    void taskStarted(const QString &taskId);
    /// Done, failed or cancelled.
    void finished();

private:
    qint64 now() const;
    void poll();
    void arm();
    void fetch();
    void add();
    void fail(Failure failure, const QString &text);
    void setState(State state);

    QPointer<QbtClient> m_client;
    QTimer *m_timer = nullptr;
    std::function<qint64()> m_clock;
    Request m_request;
    State m_state = State::Idle;
    Failure m_failure = Failure::None;
    QString m_taskId;
    QString m_errorText;
    QByteArray m_bytes;
    TorrentFile m_torrent;
    int m_progress = 0;
    qint64 m_pieceSize = 0;
    qint64 m_startedAt = 0;
    qint64 m_tookMs = 0;
    bool m_cancelAsked = false;
    bool m_inFlight = false;
    /// Moves on every start, so an answer to an earlier task is dropped.
    int m_generation = 0;
};

} // namespace omaseed::core
