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

#include <chrono>

namespace omaseed::core {

class QbtClient;
class TorrentModel;

/// Why errored torrents are in error, from qBittorrent's log (docs/28, #54).
///
/// After each applied poll it looks at which torrents are in `error` or `missingFiles`. While
/// any are, it reads `log/main` (warnings and critical lines, only those after the last id it
/// saw), at most every few seconds and at once when a torrent goes into error. Each line is
/// taken apart and tied to its torrent (TorrentProblem), and what it says goes into the
/// torrent model's `problem` and `statusCause` roles. An errored torrent with no line gets
/// "didn't log a reason" once the log has been read.
///
/// qBittorrent keeps its log in memory and numbers it from 0 again after a restart. A full
/// update means the server may have restarted, so the log is read again from the start.
class ProblemLog : public QObject
{
    Q_OBJECT

public:
    explicit ProblemLog(QObject *parent = nullptr);

    /// The server to read the log of. Not owned; changing it forgets every line.
    void setClient(QbtClient *client);
    /// The rows to watch and to write problems into. Not owned.
    void setTorrents(TorrentModel *torrents);

    /// The shortest time between two reads of the log while torrents stay errored; a torrent
    /// going into error reads it at once.
    void setInterval(std::chrono::milliseconds interval) { m_interval = interval; }

    /// What `log/main` returned, straight in: the tests feed lines through this.
    void applyEntries(const QJsonArray &entries);
    /// The last log id read; -1 before the first read.
    int lastId() const { return m_lastId; }
    /// Lines kept that are about a torrent's files.
    int lineCount() const { return int(m_lines.size()); }
    /// The problem shown for `hash`; `found` false and an empty headline when there is none.
    TorrentProblem problem(const QString &hash) const { return m_problems.value(hash); }

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

private:
    struct Line
    {
        int id = 0;
        QDateTime at;
        QString message;
        ProblemLine parsed;
    };

    void updateErrored(bool fullUpdate);
    void read();
    void rematch();
    void publish(const QString &hash, const TorrentProblem &problem);

    QPointer<QbtClient> m_client;
    QPointer<TorrentModel> m_torrents;
    std::chrono::milliseconds m_interval{5000};

    QList<Line> m_lines;
    int m_lastId = -1;
    bool m_reading = false;
    bool m_readOnce = false; ///< the log has been read since the errored set last changed
    QElapsedTimer m_sinceRead;

    QSet<QString> m_errored;      ///< hashes in error or missingFiles
    QSet<QString> m_newlyErrored; ///< went into error while watched, notice not sent yet
    bool m_seenFirstSync = false;
    QHash<QString, TorrentProblem> m_problems;
};

} // namespace omaseed::core
