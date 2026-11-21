#pragma once

// AgentWords — the words an agent (or a person) types after `omaseed` to ask the running
// omaseed about the library (DECISIONS C45 V1, V2; C37 U10's family): the table, the
// argument parser, the help page and each word's page, the failure object and the exit map.
//
// - Only the first argument can be a word (C37 U10): `omaseed ./status`, a magnet or
//   `--hidden status` are links and options, as they always were.
// - `help`, `status`, `errors`, `log`, `moves`, `check`, `skin` and `skill` (C48) are built. `do`
// is
//   reserved: it answers usage until its word exists (the actions patch).
// - `check <skin>` and `skin list | clone <from> <name> | use <name>` take positionals
//   (C37 U3, U5, U6), `skill [page | link | unlink | status]` one (C48); every other word
//   takes options only.
// - The parser turns a word's arguments into the options JSON the Agent interface takes
//   (C45 V3): only what was given; the running omaseed fills in the defaults.
// - Every answer and failure is one JSON object led by v, command, at (and omaseed when the
//   app made it), C45 V4. `orderedJson` keeps that order; QJsonObject sorts its keys.
//
// Pure Qt Core; `tst_agentwords` has a case per rule.

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

namespace omaseed::core::AgentWords {

/// The defaults and limits of the reads (C45 V10, V11), for the parser and the answers.
inline constexpr int kDefaultLines = 50;
inline constexpr int kMaxLines = 2000;
inline constexpr int kDefaultLanded = 5;
inline constexpr int kMaxLanded = 20;
inline constexpr int kReplyTimeoutMs = 30000;
/// The levels `--level` takes, from the most to the least said; "warning" is the default.
QStringList levels();
inline const char *const kDefaultLevel = "warning";

/// True when `argument` is a word of the table, built or reserved (C37 U10: compared whole,
/// so a path or a link is never one).
bool isWord(const QString &argument);
/// True for a word whose handler isn't in this omaseed yet (`do`).
bool isReserved(const QString &word);

/// What a command line that starts with a word asks for.
struct Request
{
    enum class Kind {
        Call,  ///< ask the running omaseed: `method` with `options`
        Page,  ///< print `page` (empty: the help page) and exit 0
        Usage, ///< print `message` as a usage failure and exit 2
        Local, ///< the CLI answers itself (check, skin list, skin clone, skill; C37 U3, U5, C48)
    };
    Kind kind = Kind::Usage;
    QString word;            ///< the word ("skin" for every skin subword)
    bool json = false;       ///< `--json` was given (anywhere, so a usage failure is JSON too)
    QString page;            ///< Page: the word whose page, or "" for the help page
    QJsonObject options;     ///< Call, Local: server, level, … theme, name, as given
    QString message;         ///< Usage: the words
    QString subword;         ///< skin's: list, clone or use; skill's page or link, unlink,
                             ///< status; "" for every other word
    QStringList positionals; ///< check's skin; clone's from and name; use's name

    /// The envelope's "command": the word, then its subword ("skin list").
    QString command() const { return subword.isEmpty() ? word : word + QLatin1Char(' ') + subword; }
};

/// `arguments` are the command line after the program, the word first. `skillPages` are the
/// pages `skill` takes (integration::AgentGuide::pages(); core reads no resource, C48).
Request parse(const QStringList &arguments, const QStringList &skillPages = {});
/// True for `skill` and `skill PAGE`, the prints; false for link, unlink, status and any
/// other word (C48).
bool isSkillPrint(const Request &request);

/// The options a word takes, by name without "--" (C45 design 3): server, level, lines,
/// since, torrent, landed, as each word has them; none for status and help.
QStringList optionsOf(const QString &word);

/// The Agent interface's method for a command: "Status", "Errors", "Log", "Moves",
/// "UseSkin" (`skin use`); "" else.
QString method(const QString &command);

/// The help page: Qt's `helpText()` with its first line replaced by omaseed's two usage
/// lines and the words' block inserted after the description (C45 V2, docs/62 §2).
QString helpPage(const QString &qtHelp);
/// One word's page (Usage, what it is, who answers, the options, Exit); "" for no word.
QString wordPage(const QString &word);

/// The usage failures' words (C45 V13).
QString doesNotTake(const QString &word, const QString &argument);
QString noSuchWord(const QString &argument);
QString notYet(const QString &word);

/// The exit for an error code (C45 V13): 2 usage, unknown-server, ambiguous, and the skins'
/// unknown-skin, bad-name, skin-exists, no-theme; 3 not-running, too-old, no-bus, no-reply;
/// 4 not-current, unreachable, no-log, unknown-torrent, first-sync; 5 the actions' (with
/// C50's not-removable and not-found) and not-written; 6 failed. 0 for "" (answered), 1 for
/// unreadable and a code it doesn't know.
int exitFor(const QString &code);

/// ISO 8601 in UTC with seconds and Z ("2026-11-12T14:03:11Z"); null when invalid (C45 V4).
QJsonValue isoUtc(const QDateTime &at);

/// One JSON object, compact, its top-level keys in the order given (nested values as
/// QJsonDocument writes them).
QByteArray orderedJson(const QList<std::pair<QString, QJsonValue>> &fields);

/// The failure object (C45 V4): v, command, at, then omaseed when `omaseedVersion` isn't
/// empty (the app made it; the CLI's own failures have none), then error {code, message}.
QByteArray failure(const QString &command,
                   const QString &code,
                   const QString &message,
                   const QDateTime &at,
                   const QString &omaseedVersion = QString());

} // namespace omaseed::core::AgentWords
