#pragma once

// AgentText — an answer's JSON as a person reads it in a terminal (DECISIONS C45 V12, docs/62
// §4): the CLI turns the app's JSON into these lines, so `--json` and a `busctl` call of the
// Agent interface print the same bytes, and the lines are only ever made from what the JSON
// says. Speeds and sizes are core/Format's, times TorrentStory::timeWords'.
//
// Colour only when the style asks (the CLI: a tty and no NO_COLOR): red 31, yellow 33, green
// 32 for connected, blue 34 for a move running, bold 1. The width wraps the free text under
// itself. Counts use one/many pairs, never a `%n` string (C41). Pure Qt Core.

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace omaseed::core::AgentText {

/// How the lines are drawn; tests pin each field.
struct TextStyle
{
    bool colour = false;
    int width = 80;
    QDateTime now; ///< "today" for the times; invalid: the current time
};

/// The answer's lines for its `command`, each ending in "\n"; "" for a command with no
/// lines yet (the CLI then prints the JSON).
QString lines(const QJsonObject &answer, const TextStyle &style);

/// status's lines (C45 V8, docs/62 §4's mock).
QString status(const QJsonObject &answer, const TextStyle &style);

/// errors' lines (C45 V9, docs/62 V9's mock): the head, each torrent's block, the dismissed.
QString errors(const QJsonObject &answer, const TextStyle &style);

/// log's lines (C45 V10, docs/62 V10's mock): the head, where qBittorrent's memory begins, and
/// each line's time, level and message, wrapped under the message.
QString log(const QJsonObject &answer, const TextStyle &style);

/// moves' lines (C45 V11, docs/62 V11's mock): the head, each running move in the band's
/// words, each landed one with what a move back would do, and how many more are kept.
QString moves(const QJsonObject &answer, const TextStyle &style);

/// do's answer (C45 V15, docs/62 §5's mock): "Allowed." when it asked, the outcome strip's
/// words, and a line per torrent the server refused (red).
QString doAnswer(const QJsonObject &answer, const TextStyle &style);
/// `do --list` (C50 GT3): the Agents row's lines as they stand, then what is never done.
QString doList(const QJsonObject &answer, const TextStyle &style);
/// `do --dry-run` (C50 GT5): what would happen, nothing done.
QString doDryRun(const QJsonObject &answer, const TextStyle &style);

/// `do --plan` (C50 GT4, GT5; board 67 §6): the plan's head, what each line would do or did
/// (done, failed, left unticked), and its last line.
QString doPlan(const QJsonObject &answer, const TextStyle &style);

/// A failure's words for stderr, wrapped at the width.
QString failure(const QString &message, const TextStyle &style);

/// `text` broken at spaces so no line is longer than `width` (a longer word stays whole);
/// the first line starts with `first`, the others with `rest`; a "\n" in `text` starts a
/// line too. Each line ends in "\n".
QString wrap(const QString &text, int width, const QString &first, const QString &rest);

/// "qBittorrent", "Transmission", "Deluge" for an engine's id; the id itself otherwise.
QString engineName(const QString &engine);

} // namespace omaseed::core::AgentText
