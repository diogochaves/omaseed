#pragma once

// StoryRecord — what the details pane's story tells that no engine keeps (board 56, DECISIONS
// C38, #236; board 58, C43, #248): renames, filing edits (category, tags) and places.
//
// One per server, saved beside the move history (C38 R9, C18). It keeps, per torrent, one
// newest-first list of entries:
// - names (C38): the torrent's name changed by omaseed or seen changed in a poll (R1, R4), and
//   omaseed's own file and folder renames (R3; the sync has no file names);
// - filing (C43 Y2, Y3, Y8): a category (Deluge's label) or tags put on or taken off, by omaseed
//   or seen in a poll;
// - places (C43 Y4, Y6, Y7): a save path seen changed in a poll, outside omaseed or by the
//   server itself (automatic management, a label's folder).
// Nothing here words a line: an entry holds what the story needs to word it later.
//
// - record() folds an entry into the torrent's newest entry when C38 R6 / C43 Y9 say so, and
//   drops a run that ends where it started.
// - Each entry gets an id, unique within the record. record() returns the id of the entry the
//   edit landed in (new or folded into), 0 when it left no line. A rename that qBittorrent
//   answers OK and then logs as failed (C2 D2, R3) takes its line back with remove(hash, id).
//   When that rename had folded into an earlier one, the whole folded line goes: the failure
//   line tells the story from there (R3).
// - prune() drops the torrents that left the server (R9); at most kMaxPerKind of each group
//   (names, filing, places) per torrent, no age (C43 Y10).
//
// Pure data, no Qt object: the controller owns one per server and saves it through its hooks.

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace omaseed::core {

struct StoryEntry
{
    enum class What {
        Torrent,  ///< the torrent's name (C38)
        File,     ///< one file's name
        Folder,   ///< one folder's name
        Files,    ///< a sheet run of file renames
        Category, ///< the category, or Deluge's label (C43 Y2)
        Tags,     ///< tags, or Transmission's labels (C43 Y3)
        Place,    ///< the save path (C43 Y4)
    };
    /// Omaseed: its own edit, at the server's OK. Elsewhere: seen in a poll, done outside
    /// omaseed. Server: seen in a poll, done by the server itself (a Place: automatic
    /// management, a label's folder; C43 Y7).
    enum class By { Omaseed, Elsewhere, Server };

    int id = 0;   ///< set by StoryRecord::record(), unique within the record
    QDateTime at; ///< UTC: the server's OK, or the poll that saw it (R1, R4)
    QString hash;
    What what = What::Torrent;
    /// Names: the old and new names, a file's or folder's own name, not its path (R3's
    /// OLD → NEW); for a Files run, the first file's. Category: the old and new category ("" for
    /// none). Place: the old and new save path.
    QString was;
    QString now;
    int count = 1; ///< files renamed: 1, or a Files run's total
    /// The folder it sits in ("" at the top). For a Files run, the one folder all share; ""
    /// when they span several (then `folders` says how many).
    QString in;
    int folders = 1; ///< a Files run's distinct folders (R3: "in K folders" when more than 1)
    By by = By::Omaseed;
    bool disk = false; ///< R7: Transmission renamed its folder on disk too (K28)
    /// Tags: the names put on and taken off, in the tag list's order (TagModel::lessThan).
    QStringList tagged;
    QStringList untagged;
    /// Place: false while it waits in the incomplete folder for that save path (Will land in,
    /// C43 Y4).
    bool landed = true;
    /// Place by Server: what moved it, "category" (automatic management) or "label" (Deluge's
    /// label folder), and that category's or label's name ("" for none).
    QString cause;
    QString causeName;
    /// Category, Tags: the category or tag left the server's list in that poll (C43 Y8).
    bool removed = false;

    QJsonObject toJson() const;
    /// False in `ok` (when given) for an object that isn't an entry (no hash, no time, an
    /// unknown what or by).
    static StoryEntry fromJson(const QJsonObject &object, bool *ok = nullptr);
    /// Nothing changed: tags with both lists empty, any other kind with was == now.
    bool isNoChange() const;

    bool operator==(const StoryEntry &other) const = default;
};

class StoryRecord
{
public:
    /// DECISIONS C43 Y10 (C38 R9): the last 10 of each group (names, filing, places) per
    /// torrent, no age limit.
    static constexpr int kMaxPerKind = 10;
    /// DECISIONS C38 R6, C43 Y9: edits of the same thing within 10 minutes fold.
    static constexpr int kFoldSeconds = 10 * 60;

    /// An entry (its `id` is ignored). Returns the id of the entry it is in now, folded or
    /// new; 0 when it leaves no line (no hash, no change, or a fold back to where it began).
    int record(const StoryEntry &entry);
    /// Takes `id` back from `hash`'s entries. True when it was there.
    bool remove(const QString &hash, int id);
    /// After a full sync: drops every torrent not in `hashesInLibrary` (R9). True when
    /// something went.
    bool prune(const QSet<QString> &hashesInLibrary);

    /// `hash`'s entries, newest first.
    QList<StoryEntry> recordsFor(const QString &hash) const;
    bool isEmpty() const { return m_byHash.isEmpty(); }

    /// One flat array: each torrent's entries newest first, torrents by hash.
    QJsonArray toJson() const;
    /// Skips what isn't an entry; keeps each torrent's newest kMaxPerKind of each group.
    static StoryRecord fromJson(const QJsonArray &array);

private:
    QHash<QString, QList<StoryEntry>> m_byHash; ///< newest first
    int m_nextId = 1;
};

} // namespace omaseed::core
