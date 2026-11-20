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

/// A failure's words for stderr, wrapped at the width.
QString failure(const QString &message, const TextStyle &style);

/// `text` broken at spaces so no line is longer than `width` (a longer word stays whole);
/// the first line starts with `first`, the others with `rest`. Each line ends in "\n".
QString wrap(const QString &text, int width, const QString &first, const QString &rest);

/// "qBittorrent", "Transmission", "Deluge" for an engine's id; the id itself otherwise.
QString engineName(const QString &engine);

} // namespace omaseed::core::AgentText
