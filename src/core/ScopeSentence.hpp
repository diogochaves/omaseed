#pragma once

// ScopeSentence — the title bar's sentence (board 46 `titleBar()`, DECISIONS C24 H5): one line
// under the title that says, in words before numbers, what the scope holds. The library's is
// the five questions in a breath ("2 need you · 7 arriving, the next in 3 min · 2 landed
// today · giving back 41 KB/s"); a question's, a category's, a tag's or a tracker's says what
// that one holds; a folder's takes the scope line's facts (C15 M1, now the sentence); the seed
// console's and the log's say what the face shows. A part with nothing to say is left out,
// never printed as a zero.
//
// The facts are gathered torrent by torrent into ScopeFacts (TorrentFilterModel::scopeFacts()
// over the rail's scope, never the filter's words); the words are the functions below. Pure
// Qt Core; `tst_scopesentence` has a row per sentence.

#include "core/Torrent.hpp"

#include <QString>

namespace omaseed::core {

/// What a scope holds, for its sentence.
struct ScopeFacts
{
    int total = 0;
    /// One count per question (TorrentFilterModel::Question; disjoint, C24 H2).
    int needsYou = 0;
    int arriving = 0;
    int landedToday = 0;
    int givingBack = 0;
    int resting = 0;
    /// Landed today and still sharing (not stopped).
    int landedSharing = 0;
    /// Arriving torrents stalled with no seed online (the row's "waiting · no seed"): none
    /// of them can finish, so none gives the soonest or latest estimate below (#201 F14).
    int waitingSeed = 0;
    /// Seconds to the soonest and the latest arriving torrent with an estimate, among those
    /// that can finish; -1 for none.
    qint64 nextEta = -1;
    qint64 lastEta = -1;
    /// What the arriving torrents receive, and what the giving-back ones and every torrent
    /// send, in bytes per second.
    qint64 arrivingSpeed = 0;
    qint64 givingSpeed = 0;
    qint64 upSpeed = 0;
    /// The giving-back torrents' traffic, for their ratio together.
    qint64 givenUp = 0;
    qint64 givenDown = 0;
    /// Bytes of the selected files of every torrent.
    qint64 bytes = 0;

    /// Counts `torrent`, which answers `question` (a TorrentFilterModel::Question).
    void add(const Torrent &torrent, int question);
};

class ScopeSentence
{
public:
    /// The library: the questions with something to say, resting only when nothing else is.
    static QString library(const ScopeFacts &facts);
    /// One of the five questions (a TorrentFilterModel::Question); the library for any other.
    static QString question(int question, const ScopeFacts &facts);
    /// A category: its count, its size (none known: left out) and where it saves ("" leaves
    /// the place out).
    static QString category(const ScopeFacts &facts, const QString &savePath);
    /// #167 assumed 9: a tag's and a tracker's count and the kind of place.
    static QString tag(const ScopeFacts &facts);
    static QString tracker(const ScopeFacts &facts);
    /// The rows display's group head (board 46 `rows()`, DECISIONS C24 H3): the short line
    /// after a question's name and count, from the facts of the rows listed under it ("2
    /// fixes wait", "next ready in 3 min", "since midnight", "41 KB/s · ratio 1.6", "not
    /// sharing"); "" when there is nothing to say.
    static QString groupLine(int question, const ScopeFacts &facts);
    /// A folder's line in space (board 51 D12 a): "9 torrents · 1.36 GB"; the size left out
    /// while none is known.
    static QString folderLine(const ScopeFacts &facts);
    /// The rail's word for a question ("arriving"), for a sentence a question narrows too;
    /// "" for the library.
    static QString questionWord(int question);
    /// A place narrowed by a question as well: the question's word first.
    static QString narrowed(int question, const QString &sentence);

    /// A folder picked in the rail (board 46: "49 torrents · 1.32 MB on disk · 2 loose
    /// files · /home/sbx/library/games"). `savePaths` above one is said; `loose` < 1 is not.
    static QString
    folder(int torrents, qint64 bytesOnDisk, int savePaths, int loose, const QString &path);
    /// The folder while its move runs (board 46: "moving to /tmp/fast/games · 17 of 49
    /// landed").
    static QString folderMoving(const QString &newPath, int landed, int torrents);
    /// A move's torrents on show (C15 M10's "show"): how many, and how many qBittorrent has.
    static QString moveJob(int torrents, int withServer);
    /// The move sheet's torrents on show (its "show").
    static QString moveSheet(int torrents);

    /// The seed console: what goes up now, today's total ("18.4 GB today") and its scope.
    static QString seed(const QString &upSpeed, const QString &today, const QString &scope);
    /// The log face: whose log, and how many lines since when ("" for none known); then the
    /// warnings and critical lines held and "following" while new lines pull the view down.
    static QString log(const QString &server,
                       int lines,
                       const QString &since,
                       int warnings = 0,
                       int critical = 0,
                       bool following = false);
    /// Any scope while the server is lost: the list's time and the notice's headline.
    static QString lost(const QString &listTime, const QString &headline);

    /// A wait in rounded words: "40 s", "3 min", then Format::duration's "2h 41m".
    static QString soon(qint64 seconds);
};

} // namespace omaseed::core
