#pragma once

// SelectionHold — a big widening of a torrent's selection waits a moment before it is sent
// (board 44 §3, DECISIONS C17 D5–D7, which change N1; #119).
//
// The tick itself makes libtorrent create every newly wanted file at its full length, even on
// a stopped torrent (#116), so only not sending it protects: a misclick undone inside the
// window never reaches qBittorrent. The rows change at once (TorrentFilesModel's local
// priority); only the request waits.
//
//   * what waits (D5, D6): a change that wants files now skipped, adding more than 1 GiB, or
//     wanting the whole torrent, running or stopped alike. A skip alone never waits;
//   * the window (D7): 3 s, starting again with each change;
//   * several changes: what is made while one waits joins it; the net change per file is what
//     is sent, and nothing at all when it nets to zero;
//   * undo gives back each file's priority as the server has it, and sends nothing;
//   * leaving (another torrent, the pane closed, a quit) sends what waits: `flush`.
//
// The hold knows the server's priorities from before its first change (`original`), so a file
// ticked and unticked inside the window is no change at all.

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace omaseed::core {

class SelectionHold : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 kThresholdBytes = 1024LL * 1024 * 1024; ///< D6
    static constexpr int kWindowMs = 3000;                          ///< D7

    explicit SelectionHold(QObject *parent = nullptr);

    /// D5–D6: whether setting `ids` to `priority` on a torrent whose files are now at
    /// `current` (id → priority) with `sizes` (id → bytes) must wait: it wants files now
    /// skipped, adding more than 1 GiB of them or leaving none of the torrent skipped.
    static bool mustWait(const QHash<int, int> &current,
                         const QHash<int, qint64> &sizes,
                         const QList<int> &ids,
                         int priority);

    bool waiting() const { return !m_target.isEmpty(); }
    QString hash() const { return m_hash; }
    /// Joins the change to what waits (or starts a hold), restarting the window. `current`
    /// is every file's priority before this change. A change that nets everything to zero
    /// ends the hold with nothing to send.
    void add(const QString &hash,
             const QList<int> &ids,
             int priority,
             const QHash<int, int> &current,
             const QHash<int, qint64> &sizes);

    /// What each file the hold changes is to become: the rows keep it over the polls.
    QHash<int, int> targets() const { return m_target; }
    /// The net change: file ids by the priority to send.
    QMap<int, QList<int>> net() const;
    /// What undo puts back on the rows: file ids by their priority on the server.
    QMap<int, QList<int>> originals() const;
    /// The files the net change wants that were skipped, and their bytes (the band's words).
    int addedFiles() const;
    qint64 addedBytes() const;
    /// When the window ends; invalid while nothing waits.
    QDateTime deadline() const { return m_deadline; }
    int windowMs() const { return m_windowMs; }
    /// Only the tests shorten it.
    void setWindowMs(int ms);

    /// Ends the hold and hands back its net change (empty when nothing waits).
    QMap<int, QList<int>> take();
    /// Ends the hold, sending nothing.
    void clear();

Q_SIGNALS:
    void changed();
    /// The window ran out: send `take()`.
    void due();

private:
    QString m_hash;
    QHash<int, int> m_original; ///< the server's priority of every file the hold touched
    QHash<int, int> m_target;   ///< what each of those is to become
    QHash<int, qint64> m_sizes;
    QDateTime m_deadline;
    int m_windowMs = kWindowMs;
    QTimer *m_timer = nullptr;
};

} // namespace omaseed::core
