#pragma once

// TorrentStory — "what happened" in a torrent's details (board 46 `details()`'s `.tl`,
// DECISIONS C24 H6): the story tab's timeline, newest first (C32 D15), one line each with its
// time, a marker kind, the event and a dim line under it.
//
// Only what omaseed knows for sure goes in: when it was added (and where), each move the
// history keeps for it (C18 D8: the last 20 moves or 30 days), when it finished downloading,
// and the error (docs/28) and the failure (docs/32) at the time qBittorrent's log wrote them.
// The now is the details' head sentence, not a line here. Pure Qt Core; `tst_torrentstory`.

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
    /// The failure block's headline (TorrentFailure) and when the log wrote it; "" for none.
    QString failure;
    QDateTime failureAt;
    QDateTime now;
};

struct StoryEvent
{
    enum class Kind {
        Plain, ///< added, finished: the state's colour
        Move,  ///< a move: the accent
        Warn,  ///< a failure the log tells: yellow
        Error, ///< the error: red
    };
    QDateTime at;
    QString time; ///< "09:15", "yesterday 09:15", "12 Oct 09:15", "12 Oct 2025"
    Kind kind = Kind::Plain;
    QString title;
    QString detail;
};

class TorrentStory
{
public:
    /// The events, newest first (C32 D15); an event with no time is left out.
    static QList<StoryEvent> events(const StoryInput &input);

    /// When `at` happened, seen from `now` (both local): "09:15" today, "yesterday 09:15",
    /// "12 Oct 09:15" this year, "12 Oct 2025" before it.
    static QString timeWords(const QDateTime &at, const QDateTime &now);

    /// The marker kind's name for QML: "plain", "move", "warn", "error".
    static QString kindName(StoryEvent::Kind kind);
};

} // namespace omaseed::core
