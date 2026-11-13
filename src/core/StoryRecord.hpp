#pragma once

// RenameHistory — the renames the details pane's story tells (board 56, DECISIONS C38, #236).
//
// One per server, saved beside the move history (C38 R9, C18). It keeps, per torrent, the last
// rename lines: the torrent's name changed by omaseed or seen changed in a poll (R1, R4), and
// omaseed's own file and folder renames (R3; the sync has no file names). Nothing here words a
// line: a record holds what the story needs to word it later.
//
// - record() folds a rename into the torrent's newest record when R6 says so, and drops a run
//   that ends on the name it started with.
// - Each record gets an id, unique within the history. record() returns the id of the record
//   the rename landed in (new or folded into), 0 when it left no line. A rename that
//   qBittorrent answers OK and then logs as failed (C2 D2, R3) takes its line back with
//   remove(hash, id). When that rename had folded into an earlier one, the whole folded line
//   goes: the failure line tells the story from there (R3).
// - prune() drops the torrents that left the server (R9); at most kMaxPerTorrent each, no age.
//
// Pure data, no Qt object: the controller owns one per server and saves it through its hooks.

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

namespace omaseed::core {

struct RenameRecord
{
    enum class What { Torrent, File, Folder, Files };
    enum class By { Omaseed, Elsewhere };

    int id = 0;   ///< set by RenameHistory::record(), unique within the history
    QDateTime at; ///< UTC: the server's OK, or the poll that saw it (R1, R4)
    QString hash;
    What what = What::Torrent;
    /// The old and new names. A file's or folder's own name, not its path (R3's OLD → NEW);
    /// for a Files run, the first file's.
    QString was;
    QString now;
    int count = 1; ///< files renamed: 1, or a Files run's total
    /// The folder it sits in ("" at the top). For a Files run, the one folder all share; ""
    /// when they span several (then `folders` says how many).
    QString in;
    int folders = 1; ///< a Files run's distinct folders (R3: "in K folders" when more than 1)
    By by = By::Omaseed;
    bool disk = false; ///< R7: Transmission renamed its folder on disk too (K28)

    QJsonObject toJson() const;
    /// False in `ok` (when given) for an object that isn't a record (no hash, no time, an
    /// unknown what or by).
    static RenameRecord fromJson(const QJsonObject &object, bool *ok = nullptr);

    bool operator==(const RenameRecord &other) const = default;
};

class RenameHistory
{
public:
    /// DECISIONS C38 R9: the last 10 rename lines per torrent, no age limit.
    static constexpr int kMaxPerTorrent = 10;
    /// DECISIONS C38 R6: renames of the same name within 10 minutes fold.
    static constexpr int kFoldSeconds = 10 * 60;

    /// A rename (its `id` is ignored). Returns the id of the record it is in now, folded or
    /// new; 0 when it leaves no line (no hash, no change, or a fold back to where it began).
    int record(const RenameRecord &rename);
    /// Takes `id` back from `hash`'s lines. True when it was there.
    bool remove(const QString &hash, int id);
    /// After a full sync: drops every torrent not in `hashesInLibrary` (R9). True when
    /// something went.
    bool prune(const QSet<QString> &hashesInLibrary);

    /// `hash`'s lines, newest first.
    QList<RenameRecord> recordsFor(const QString &hash) const;
    bool isEmpty() const { return m_byHash.isEmpty(); }

    /// One flat array: each torrent's records newest first, torrents by hash.
    QJsonArray toJson() const;
    /// Skips what isn't a record; keeps each torrent's newest kMaxPerTorrent.
    static RenameHistory fromJson(const QJsonArray &array);

private:
    QHash<QString, QList<RenameRecord>> m_byHash; ///< newest first
    int m_nextId = 1;
};

} // namespace omaseed::core
