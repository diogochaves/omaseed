#pragma once

// AgentAnswers — what the running omaseed answers an agent's word (DECISIONS C45 V4, V6, V8):
// the envelope, the options a method takes, the match of `--server`, and each read's JSON,
// made from plain input structs that app::AgentService fills from App's models. Pure Qt
// Core, so `tst_agentanswers` pins every field.
//
// - Every answer starts v, command, at, omaseed (C45 V4); times are AgentWords::isoUtc's
//   (UTC, seconds, Z; null when unknown), bytes are integers, lists are always there, ids are
//   never translated.
// - Every free text an answer carries (a problem's words, a server's reason) goes through
//   AnnounceMask::maskText (C45 V7): a tracker's passkey never leaves in an answer.
// - Addresses are host:port only. Nothing here reads a username, a URL or a secret.

#include "core/engines/TorrentStatus.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace omaseed::core::AgentAnswers {

/// What a method was asked, with the defaults filled in (C45 V10, V11).
struct Options
{
    QString server;    ///< "" for the server omaseed is on
    QString level;     ///< log: normal, info, warning or critical
    int lines = 0;     ///< log: the last N
    qint64 since = -1; ///< log: from this id on; -1 not given
    QString torrent;   ///< log: a hash or a prefix, lower case
    int landed = 0;    ///< moves: the last N that landed
};

/// The options JSON a method was called with, read for `command`, or the usage words.
struct ParsedOptions
{
    Options options;
    QString usage; ///< "" when the options are good
};

/// `json` is the Agent interface's options argument (C45 V3): an object, all keys optional,
/// "" taken as {}. An unknown key, a key `command` doesn't take or a wrong type is usage, in
/// AgentWords::doesNotTake's words.
ParsedOptions parseOptions(const QString &command, const QString &json);

/// A saved server, for `--server` (C45 V6).
struct ServerRef
{
    QString id;
    QString name;
};

/// The server `--server NAME` names: its id, or a failure's code and words.
struct ServerMatch
{
    QString id;
    QString code; ///< "" matched; "unknown-server"
    QString message;
};

/// C45 V6: the id (exact, braces optional), then the name (exact), then the one name that
/// matches ignoring case. None, or more than one by case, is `unknown-server`.
ServerMatch matchServer(const QList<ServerRef> &servers, const QString &query);

/// C45 V6's not-current words: "omaseed is on CURRENT, not NAME. WORD reads only the server
/// omaseed is on."
QString notCurrentWords(const QString &current, const QString &name, const QString &word);

/// The current server's trouble in ErrorWords' words (C12), as the window's notice says it.
struct Problem
{
    QString headline;
    QString line;
    int retryIn = 0; ///< seconds until the next try; 0 none
};

/// The server omaseed is on (C45 V8): every read's `server`.
struct Server
{
    QString id;
    QString name;
    QString engine;  ///< "qbittorrent" | "transmission" | "deluge"
    QString version; ///< "5.3.0", "" before a contact
    QString address; ///< host:port
    bool reachable = false;
    QString connection; ///< "connected" | "firewalled" | "disconnected"
    std::optional<Problem> problem;
};

/// Another server, or the current one, as the bar's menu lists them (BarMenu::serversJson).
struct ListedServer
{
    QString id;
    QString name;
    QString engine;
    bool current = false;
    QString state;  ///< ok · down · refused · first-sync · unknown
    int count = -1; ///< the torrents last seen; -1 never
    QDateTime seen; ///< invalid: never
    QString reason;
};

/// The move running now (MoveController::job), for status.
struct MoveNow
{
    QString title;
    QString to;
    int there = 0;
    int of = 0;
};

/// Everything status says (C45 V8, design 11).
struct StatusInput
{
    QDateTime at;
    QString omaseed; ///< this omaseed's version
    bool windowShown = false;
    Server server;
    qint64 down = 0; ///< bytes per second
    qint64 up = 0;
    QString altPick = QStringLiteral("off"); ///< off · turtle · burst
    int all = 0;
    int needsYou = 0;
    int arriving = 0;
    int landedToday = 0;
    int givingBack = 0;
    int resting = 0;
    int movesRunning = 0;
    int movesQueued = 0;
    std::optional<MoveNow> moveNow;
    int paused = 0; ///< the torrents a Pause Torrents holds (C36 M10)
    QList<ListedServer> servers;
};

/// One log line tied to a torrent (ProblemLog::linesNaming), for errors.
struct LogLine
{
    qint64 id = 0;
    QDateTime at;
    int level = 4; ///< qBittorrent's: 1 normal, 2 info, 4 warning, 8 critical
    QString message;
};

/// One torrent the rail says needs you (C45 V9, design 12), as AgentService reads it.
struct ErrorTorrent
{
    QString hash;
    QString name;
    bool problem = false; ///< a red problem shows (ProblemLog::problem); false: a failure only
    bool stuckMove = false;
    bool missingFiles = false;
    QString problemCause; ///< TorrentProblem::causeKey
    QString failureLine;  ///< TorrentFailure::line, read for a cause when there is no problem
    QString fact;         ///< RowWords' (wordsOfHash)
    QString sentence;
    QString fixWords;
    QString fixAction; ///< RowWords::fixAction: "problem:start", "failure:location", ""
    QDateTime since;   ///< the problem's or the failure's loggedAt
    TorrentPhase phase = TorrentPhase::Unknown;
    QString savePath;
    QList<LogLine> log; ///< newest last; dropped on an engine without a log
};

/// Everything errors says (C45 V9).
struct ErrorsInput
{
    QDateTime at;
    QString omaseed;
    Server server;
    QList<ErrorTorrent> torrents;
    int dismissed = 0; ///< torrents whose failure was dismissed in the window
};

/// "stuck-move", "missing-files", "error" or "failure" (design 12).
QString kindOf(const ErrorTorrent &torrent);
/// The problem's causeKey, or for a failure the cause its line gives
/// (ProblemMatch::describe), "" when there is none.
QString causeOf(const ErrorTorrent &torrent);
/// What `omaseed do` would run for a fix id of RowWords::fixWords (C45 V9, V15).
struct FixDo
{
    QString action; ///< "retry", "start-here", "move", "recheck"; "" for none
    QStringList needs;
};
FixDo fixDo(const QString &fixAction);
/// The lower-case name of a phase: "errored", "stopped", "seeding"…
QString phaseName(TorrentPhase phase);

/// The envelope's first four fields (C45 V4), for an answer's own fields to follow.
QList<std::pair<QString, QJsonValue>>
envelope(const QString &command, const QDateTime &at, const QString &omaseed);

/// `server` as every read carries it: problem null while it answers, its words masked.
QJsonObject serverJson(const Server &server);
/// The servers' list: `seen` ISO (null for never), every reason masked.
QJsonArray serversJson(const QList<ListedServer> &servers);

/// status's one JSON object.
QByteArray status(const StatusInput &input);

/// errors' one JSON object (C45 V9): `server`, `torrents[]`, `dismissed`. A torrent's `log`
/// is [] unless the server runs qBittorrent; at most 3 lines.
QByteArray errors(const ErrorsInput &input);

} // namespace omaseed::core::AgentAnswers
