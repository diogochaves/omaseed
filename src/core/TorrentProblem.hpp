#pragma once

#include "core/Torrent.hpp"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace omaseed::core {

/// One warning qBittorrent wrote to its log about a torrent's files, taken apart (docs/28 §2,
/// #54; docs/32 §2, #55). The API never says why a torrent is in error, nor that a move, a
/// rename, a web seed or a .torrent copy failed; these lines are the only place it does:
///
///   File error alert. Torrent: "<name>". File: "<path>". Reason: "<prefix> file_open (<path>)
///   error: Permission denied"
///   Failed to restore torrent. … Torrent: "<name>". Reason: "<prefix> fast resume rejected.
///   file_stat(<path>): mismatching file size"
///
/// qBittorrent translates its part of the line, so nothing here reads its English words: the
/// quoted segments are what it fills in, and the last one is libtorrent's own text, which is
/// never translated. libtorrent 2.1 starts that text with the first 6 hex characters of the
/// torrent's info hash (v2 when it has one, otherwise v1); 2.0 puts the torrent's name there.
///
/// docs/32 §2's lines (#55), as qBittorrent 5.2.3 writes them:
///
///   Failed to move torrent. Torrent: "<name>". Source: "<from>". Destination: "<to>".
///   Reason: "<prefix> storage move failed. file_rename (<path>): Permission denied"
///   File rename failed. Torrent: "<name>", file: "<path in the torrent>", reason: "<message>"
///   URL seed connection failed. Torrent: "<name>". URL: "<url>". Error: "<prefix> url seed
///   (<url>) failed: Host not found (authoritative)"
///   Received error message from URL seed. Torrent: "<name>". URL: "<a file's url>".
///   Message: "404 File not found"
///   Failed to export torrent. Torrent: "<name>". Destination: "<…/name.torrent>". Reason:
///   "<message>"
///   Failed to remove partfile. Torrent: "<name>". Reason: "<message>".
///   Failed to remove torrent content. Torrent: "<name>". Error: "<message>"
///
/// A move and a URL seed connection end in libtorrent's text, so they read the same in any
/// language. The rest carry none: a URL seed's message and an export are known by their shape
/// (a URL; an absolute path ending in .torrent), a rename by a file path under the torrent's
/// name (or its English words), and the part file and content lines by their English words
/// only.
struct ProblemLine
{
    enum class Kind {
        Unknown,        ///< not a line about a torrent's files
        FileError,      ///< "File error alert": libtorrent could not open, read or write a file
        RestoreFailed,  ///< "Failed to restore torrent": fast resume rejected (missing files)
        TorrentErrored, ///< "Torrent errored" (English logs only): the error code's text alone
        MoveFailed,     ///< "Failed to move torrent": the files stayed in `source`
        RenameFailed,   ///< "File rename failed": `torrentFile` kept its name
        UrlSeedFailed,  ///< a web seed (`url`) failed to connect, or answered an error
        ExportFailed,   ///< "Failed to export torrent": the .torrent copy (`file`) wasn't written
        PartfileFailed, ///< "Failed to remove partfile" (English logs only)
        ContentRemovalFailed, ///< "Failed to remove torrent content" (English logs only)
    };

    Kind kind = Kind::Unknown;
    QStringList quoted;     ///< every "…" segment, in order; the torrent's name is among them
    QString file;           ///< the full path libtorrent was using, when the line has one
    QString hashPrefix;     ///< 6 lowercase hex characters (libtorrent 2.1), or empty
    QString libtorrentName; ///< libtorrent 2.0's name in place of the prefix, or empty
    QString operation;      ///< file_open, file_write, file_stat…, or empty
    QString systemMessage;  ///< "Permission denied", "mismatching file size"…
    QString source;         ///< MoveFailed: the folder its files are still in
    QString destination;    ///< MoveFailed: the folder they were meant to go to
    QString torrentFile;    ///< RenameFailed: the file's path inside the torrent
    QString url;            ///< UrlSeedFailed: the web seed's URL, or a file's under it

    /// Whether this is one of docs/32's failures: a move, rename, web seed, export, part file
    /// or content removal, rather than a reason for an error.
    bool isFailure() const { return kind >= Kind::MoveFailed; }

    /// Takes one log message apart; a line that is not about a torrent's files gives Unknown.
    static ProblemLine parse(const QString &message);
};

/// What omaseed shows for one errored torrent (docs/28 §4–§5).
struct TorrentProblem
{
    bool found = false; ///< a log line was matched; false is S4, "didn't log a reason"
    bool missingFiles = false;
    QString headline; ///< "Can't write to its save folder"
    /// The cause, untranslated: noAccess, readOnly, diskFull, missing, diskError,
    /// nameTooLong, badName, openRefused, missingFiles, other, notLogged.
    QString causeKey;
    QString cause;          ///< the table's short word: "no access", "disk full"…
    QString detail;         ///< "Permission denied, opening x.bin", in libtorrent's words
    QString advice;         ///< a line on what it may mean ("may be damaged"), or empty
    QString file;           ///< the full path, or empty
    QString folder;         ///< the file's folder, or empty
    QDateTime loggedAt;     ///< the newest matching line
    int count = 0;          ///< matching lines with the same file and message
    QString line;           ///< the newest matching line, whole, for "copy the line"
    bool ambiguous = false; ///< S5: another torrent of the same name may own the line
    /// In order: "location", "start", "recheck", "rename", and "checkDisk" (#108: advice with
    /// no key, nothing the app can do itself); "addAgain" for a hybrid's recheck error (C17);
    /// a stuck move's "bothFolders" and "location".
    QStringList actions;
    /// docs/44 D13: a finished torrent whose files never left its incomplete folder (causeKey
    /// "stuckMove"): `destination` is its save path, the place they never reached, `source`
    /// the incomplete folder it seeds from.
    bool stuckMove = false;
    QString destination;
    QString source;

    QVariantMap toMap() const;
    bool operator==(const TorrentProblem &other) const = default;
};

/// What omaseed shows for a failure that left a torrent working (docs/32 §4–§5, #55): a move
/// or a rename that didn't happen, a part file left behind. General shows it in yellow, under
/// #54's red block when the torrent is also in error.
struct TorrentFailure
{
    QString kind;           ///< "move", "rename" or "partfile"; empty for none
    QString headline;       ///< "The move to log55/dest didn't happen"
    QString detail;         ///< "Permission denied, moving log55-move.bin"
    QString folder;         ///< where things are now: "Its files are still in …"
    QString destination;    ///< a move: where it was meant to go, for the location sheet
    QString torrentFile;    ///< a rename: the (first) file that kept its name, for Files
    QDateTime loggedAt;     ///< the newest line behind it
    int count = 0;          ///< a move: the times it failed the same way
    QString line;           ///< the newest line, whole, for "copy the line"
    bool ambiguous = false; ///< another torrent of the same name may own the line
    QStringList actions;    ///< in order: "location", "rename", "folder"
    QStringList keys;       ///< ProblemLog::lineKey() of every line behind it, for a dismissal

    bool isEmpty() const { return kind.isEmpty(); }
    QVariantMap toMap() const;
    bool operator==(const TorrentFailure &other) const = default;
};

/// docs/32 W1: one URL seed line about a torrent, for its HTTP sources tab.
struct WebSeedLogLine
{
    QString url;    ///< the line's URL: the seed's own, or a file's under it
    QString status; ///< ProblemMatch::webSeedStatusText()
    QDateTime at;

    bool operator==(const WebSeedLogLine &other) const = default;
};

/// Ties log lines to torrents and words them.
namespace ProblemMatch {

/// Every "…" segment of a log message, in order; a quote left open runs to the end. What
/// qBittorrent fills in, a torrent's name among it, is always quoted (docs/32 §2).
QStringList quotedSegments(const QString &message);

/// The 6 lowercase hex characters libtorrent 2.1 starts its text with for `torrent`: of its
/// v2 hash when it has one, otherwise v1.
QString hashPrefix(const Torrent &torrent);

/// The torrents among `candidates` that `line` is about. The hash prefix decides first, then
/// the name, then the file's path under a save, download or content path. When the line
/// still fits more than one torrent, all of them come back and `ambiguous` is set (D6 a).
QStringList match(const ProblemLine &line, const QList<Torrent> &candidates, bool *ambiguous);

/// The plain words and actions for a matched line (docs/28 §5).
TorrentProblem describe(const ProblemLine &line);

/// What an errored torrent with no line shows (S4).
TorrentProblem notLogged(bool missingFiles);

/// Board 44 §2 (DECISIONS C17 D3, #115): a hybrid (v1+v2) torrent that went into error just
/// after a check, with nothing logged: libtorrent 2.1.2's torrent_inconsistent_hashes. Starting
/// it or restarting qBittorrent brings it back; only adding it again ("addAgain") clears it.
TorrentProblem hybridRecheck();

/// docs/44 D13 (DECISIONS C17, #121): finished (progress 1), not moving, with a download path
/// (qBittorrent's incomplete folder) and its content path still under it. qBittorrent's move
/// into the save path failed and it kept seeding from the incomplete folder, in no error state.
/// All from torrents/info, so it works on any server.
bool isStuckMove(const Torrent &torrent);

/// The red problem for a stuck move (board 44 M1): "Finished, but its files never reached
/// DESTINATION", the reason from `line` (the newest "Failed to move torrent" line about it;
/// null when the log has none: no reason guessed) and where it seeds from (`detail`), and that
/// partial copies may be left in the destination (`advice`). Actions: "bothFolders",
/// "location".
TorrentProblem describeStuckMove(const Torrent &torrent, const ProblemLine *line);

/// #108: the first part of `path` that a common target filesystem (NTFS, exFAT, FAT, the ones
/// a Linux disk shares with Windows) would refuse as a name: one with `< > : " \ | ? *` or a
/// control character, one ending in a dot or a space, a reserved device name (CON, PRN, AUX,
/// NUL, COM1–9, LPT1–9, with or without an extension), or one over 255 bytes. Empty when every
/// part is a name those disks take, so an EINVAL on it is not about the name.
QString nameTheDiskRejects(const QString &path);

/// Whether `path` is `root` or lies under it, as the server writes paths.
bool isUnder(const QString &path, const QString &root);

/// docs/32 F1: a move that didn't happen. The headline names the destination's last two
/// folders; the detail is libtorrent's reason and operation, as board 28 words them.
TorrentFailure describeMove(const ProblemLine &line);

/// docs/32 F2: files that kept their names. `lines` are the torrent's rename failures still
/// standing, one per file, oldest first; `savePath` is where the torrent's files are.
TorrentFailure describeRename(const QList<ProblemLine> &lines, const QString &savePath);

/// docs/32 F3: a part file that couldn't be removed, from a torrent saved in `savePath`.
TorrentFailure describePartfile(const ProblemLine &line, const QString &savePath);

/// "/a/b/log55/dest/" -> "log55/dest": the last two folders of a path, as F1's headline names
/// a destination.
QString lastTwoFolders(const QString &path);

/// docs/32 W1: a web seed's failure in its own words, short: "host not found", "404 File not
/// found", "connection refused".
QString webSeedStatusText(const ProblemLine &line);

/// Whether a URL seed line's `lineUrl` is about the web seed `seedUrl`: the same URL, or a
/// file's under a seed that ends in "/" (a multi-file torrent's seed names its folder).
bool isWebSeedUrl(const QString &lineUrl, const QString &seedUrl);

} // namespace ProblemMatch

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::TorrentProblem)
