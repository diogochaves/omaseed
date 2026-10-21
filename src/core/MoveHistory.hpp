#pragma once

// MoveHistory — the moves that finished, kept after their summary closes (board 45 D7–D8,
// DECISIONS C18, #133).
//
// A job's record used to end with its summary: closing the band took "move back" away for
// good. This keeps, per server, the last moves that landed: each torrent's save path before
// and after, the time, the title. The torrent's details, the log and the palette read it to
// offer move back after the close.
//
// What a move back of one record does is check(): per torrent, never per move (C18 D7).
// - A torrent still where that move put it goes back.
// - One moved again by a later move here is named with that move: undo that one first, and
//   it is back where this record expects it. Only the move that put it where it is now is
//   named, so undoing in reverse order works one step at a time.
// - One that isn't where any later move put it was moved outside omaseed (qBittorrent's
//   Web UI, another tool): it stays.
// - One no longer in the library is gone.
//
// Pure data, no Qt object: the controller owns one per server and saves it through its hooks.

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace omaseed::core {

struct MoveRecord
{
    QString id;      ///< unique within the history
    QString kind;    ///< "folder", "selection", "retry"
    QString title;   ///< "games", "Arcade Set 01", "3 torrents"
    QString newPath; ///< where it went: the folder's new path, or the one folder ("" when each
                     ///< kept its place below a new root)
    QDateTime startedAt;
    QDateTime finishedAt;
    QHash<QString, QString> from;  ///< save path before, by hash
    QHash<QString, QString> to;    ///< save path after, by hash
    QHash<QString, QString> names; ///< the torrent's name then, by hash
    QStringList hadAutoTmm;        ///< automatic management was on before the move
    int carried = 0;               ///< loose files carried after the torrents (a folder's)
    QString carriedTo;             ///< where they went

    QJsonObject toJson() const;
    static MoveRecord fromJson(const QJsonObject &object);
};

class MoveHistory
{
public:
    /// D8: the last 20 moves or 30 days, whichever ends first.
    static constexpr int kMaxMoves = 20;
    static constexpr int kMaxDays = 30;

    /// A new move, newest first; what falls past D8's limits at `now` goes.
    void add(MoveRecord record, const QDateTime &now);
    /// Drops what is past D8's limits at `now`. True when something went.
    bool prune(const QDateTime &now);

    /// Newest first.
    const QList<MoveRecord> &records() const { return m_records; }
    bool isEmpty() const { return m_records.isEmpty(); }
    const MoveRecord *find(const QString &id) const;
    MoveRecord *find(const QString &id);
    const MoveRecord *newest() const;
    /// The newest record that moved `hash`; null for none.
    const MoveRecord *lastFor(const QString &hash) const;

    /// A move back of `id` landed for `hashes`: they leave the record, and a record with
    /// none left goes. True when something changed.
    bool backDone(const QString &id, const QStringList &hashes);

    struct BackCheck
    {
        QStringList goBack; ///< still where the record put them, sorted
        /// Moved again by a later move here: that move's id, and the hashes, newest move first.
        QList<QPair<QString, QStringList>> movedAgain;
        QStringList outside; ///< moved since by something else
        QStringList gone;    ///< no longer in the library
        int stay() const;    ///< every one that doesn't go back
    };
    /// What moving `id` back would do, with `savePaths` the library's save path by hash now.
    /// Only `only`, when given (the details' "this one only").
    BackCheck check(const QString &id,
                    const QHash<QString, QString> &savePaths,
                    const QStringList &only = {}) const;

    QJsonArray toJson() const;
    static MoveHistory fromJson(const QJsonArray &array);

private:
    int indexOf(const QString &id) const;

    QList<MoveRecord> m_records;
};

} // namespace omaseed::core
