#pragma once

// TorrentStory — "what happened" in a torrent's details (board 46 `details()`'s `.tl`,
// DECISIONS C24 H6): the story tab's timeline, newest first (C32 D15), one line each with its
// time, a marker kind, the event and a dim line under it.
//
// Only what omaseed knows for sure goes in: when it was added (and where), each move the
// history keeps for it (C18 D8: the last 20 moves or 30 days), when it finished downloading,
// and the error (docs/28) and the failure (docs/32) at the time qBittorrent's log wrote them,
// a move an engine dropped without a word (C25, Deluge) when omaseed saw it fail, each
// rename omaseed's own record keeps (DECISIONS C38: omaseed's renames, and the torrent's name
// seen changed in a poll while it ran; never a file renamed elsewhere, which it can't see), and
// each filing edit and place it keeps (C43, board 58: a category or label, tags or
// Transmission's labels, by omaseed or seen in a poll; a save path seen changed outside omaseed
// or by the server itself). Added's dim line is the category it came with (C43 Y5).
// The now is the details' head sentence, not a line here. Pure Qt Core; `tst_torrentstory`.

#include "core/StoryRecord.hpp"
#include "core/Torrent.hpp"

#include <QDateTime>
#include <QList>
#include <QString>

namespace omaseed::core {

struct StoryMove
{
    QDateTime at;
    QString to;     ///< the torrent's save path after it
    int others = 0; ///< the other torrents that moved with it
    QString folder; ///< a folder move's title ("games"), "" for a selection
};

struct StoryInput
{
    Torrent torrent;
    QList<StoryMove> moves;
    /// The error block's headline (TorrentProblem) and when the log wrote it; "" for none.
    QString problem;
    QDateTime problemAt;
    /// DECISIONS C25 K24: the engine whose own words the problem is (TorrentProblem::engine),
    /// "" for qBittorrent's log.
    QString problemEngine;
    /// C25 K24: that engine paused it rather than stopped it (Deluge).
    bool problemPaused = false;
    /// The failure block's headline (TorrentFailure) and when the log wrote it; "" for none.
    QString failure;
    QDateTime failureAt;
    /// DECISIONS C25 (Deluge): a move the engine took and dropped without a word (MoveJob's
    /// cause "silent"): where it was meant to go, when omaseed saw it fail, and the engine;
    /// "" for none. No log knows it, so it is the move job's to tell.
    QString notMovedTo;
    QDateTime notMovedAt;
    QString notMovedEngine;
    /// DECISIONS C38, C43: the torrent's entries in the story's record (StoryRecord::recordsFor),
    /// newest first: its renames, filing edits and places.
    QList<StoryEntry> entries;
    QDateTime now;
    /// The engine groups torrents in categories (EngineFeatures::categories): without them
    /// "Added" says nothing about one (C14 E4, Transmission).
    bool categories = true;
    /// They are Deluge's labels (EngineFeatures::categoriesAreLabels, C25 K13): "Added with
    /// no label", "Labelled music".
    bool categoriesAreLabels = false;
    /// Tags are Transmission's labels (EngineFeatures::tagsAreLabels): "Labelled live"
    /// (C43 Y3).
    bool tagsAreLabels = false;
};

struct StoryEvent
{
    enum class Kind {
        Plain,  ///< added, finished: the state's colour
        Move,   ///< a move: the accent
        Warn,   ///< a failure the log tells: yellow
        Error,  ///< the error: red
        Rename, ///< a rename (C38 R2): the accent, as a move
        Edit,   ///< a filing edit (C43 Y4): the accent, as a move
    };
    QDateTime at;
    QString time; ///< "09:15", "yesterday 09:15", "12 Oct 09:15", "12 Oct 2025"
    Kind kind = Kind::Plain;
    QString title;
    QString detail;
    /// Two at the same second read in this order, the higher one above (C43 Y7, C38): 0 the
    /// rest, 1 a filing edit, 2 a place seen in a poll, 3 a rename.
    int rank = 0;
};

class TorrentStory
{
public:
    /// The events, newest first (C32 D15); an event with no time is left out.
    static QList<StoryEvent> events(const StoryInput &input);

    /// When `at` happened, seen from `now` (both local): "09:15" today, "yesterday 09:15",
    /// "12 Oct 09:15" this year, "12 Oct 2025" before it.
    static QString timeWords(const QDateTime &at, const QDateTime &now);

    /// The marker kind's name for QML: "plain", "move", "warn", "error", "rename",
    /// "edit".
    static QString kindName(StoryEvent::Kind kind);
};

} // namespace omaseed::core
