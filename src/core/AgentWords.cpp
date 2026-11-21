#include "core/AgentWords.hpp"

#include "core/AgentDeny.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>

#include <algorithm>

namespace omaseed::core::AgentWords {

namespace {

/// The reads, the help (C45 V1, V2) and the skins' words (C37 U3, U5, U6).
const QStringList &builtWords()
{
    static const QStringList words{QStringLiteral("help"),
                                   QStringLiteral("status"),
                                   QStringLiteral("errors"),
                                   QStringLiteral("log"),
                                   QStringLiteral("moves"),
                                   QStringLiteral("check"),
                                   QStringLiteral("skin"),
                                   QStringLiteral("skill"),
                                   QStringLiteral("do")};
    return words;
}

/// Words taken by the table before they are built, so a file named like one already opens as
/// ./word (C45 V2). None since `do` was built (C45 V15, #292).
const QStringList &reservedWords()
{
    static const QStringList words;
    return words;
}

/// skin's subwords and how many positionals each takes after it (C37 U3, U6).
int positionalsOf(const QString &subword)
{
    if (subword == QLatin1String("list"))
        return 0;
    if (subword == QLatin1String("clone"))
        return 2;
    if (subword == QLatin1String("use"))
        return 1;
    return -1;
}

/// skill's words besides a page (DECISIONS C48, docs/65 §5).
const QStringList &skillWords()
{
    static const QStringList words{
        QStringLiteral("link"), QStringLiteral("unlink"), QStringLiteral("status")};
    return words;
}

QString skillTakes(const QStringList &pages)
{
    return QStringLiteral("omaseed: skill takes a page (%1), link, unlink or status. omaseed "
                          "help skill says how.")
        .arg(pages.join(QStringLiteral(", ")));
}

QString skinTakes()
{
    return QStringLiteral("omaseed: skin takes list, clone or use. omaseed help skin says how.");
}

/// The options each read takes besides --json and --help (C45 design 3). status takes no
/// --server: it lists every server already.
Request usage(const QString &word, bool json, const QString &message)
{
    Request request;
    request.kind = Request::Kind::Usage;
    request.word = word;
    request.json = json;
    request.message = message;
    return request;
}

Request page(const QString &word, bool json, const QString &which)
{
    Request request;
    request.kind = Request::Kind::Page;
    request.word = word;
    request.json = json;
    request.page = which;
    return request;
}

/// A whole number in [low, high], or false.
bool number(const QString &text, qint64 low, qint64 high, qint64 *out)
{
    bool ok = false;
    const qint64 value = text.toLongLong(&ok);
    if (!ok || value < low || value > high)
        return false;
    *out = value;
    return true;
}

/// One option's value, checked; false when it isn't one the option takes.
bool accept(const QString &name, const QString &value, QJsonObject *options)
{
    qint64 n = 0;
    if (name == QLatin1String("level")) {
        if (!levels().contains(value))
            return false;
        options->insert(name, value);
        return true;
    }
    if (name == QLatin1String("lines")) {
        if (!number(value, 1, kMaxLines, &n))
            return false;
        options->insert(name, static_cast<int>(n));
        return true;
    }
    if (name == QLatin1String("landed")) {
        if (!number(value, 1, kMaxLanded, &n))
            return false;
        options->insert(name, static_cast<int>(n));
        return true;
    }
    if (name == QLatin1String("since")) {
        // Ids are JSON numbers: whole, and exact in a double up to 2^53.
        if (!number(value, 0, (qint64(1) << 53), &n))
            return false;
        options->insert(name, n);
        return true;
    }
    if (value.trimmed().isEmpty())
        return false;
    // A hash is hex, and every engine writes it in lower case; the app matches the prefix.
    options->insert(name, name == QLatin1String("torrent") ? value.toLower() : value);
    return true;
}

QString jsonString(const QJsonValue &value)
{
    const QByteArray array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}

/// What `do` never does, as an agent may spell it (C45 V14): parsed as a call the app answers
/// `never`, so the words are the board's (docs/62 §5; assumed, PLAN Q4).
const QStringList &neverWords()
{
    static const QStringList words{QStringLiteral("delete"),
                                   QStringLiteral("rename"),
                                   QStringLiteral("category"),
                                   QStringLiteral("categories"),
                                   QStringLiteral("tag"),
                                   QStringLiteral("tags"),
                                   QStringLiteral("label"),
                                   QStringLiteral("labels"),
                                   QStringLiteral("add"),
                                   QStringLiteral("settings"),
                                   QStringLiteral("server"),
                                   QStringLiteral("servers"),
                                   QStringLiteral("credentials"),
                                   QStringLiteral("quit")};
    return words;
}

/// `help agents [--for AGENT] [--json]` (C50 GT7): answered by the CLI (Words.cpp), which
/// asks the running omaseed for its server's address.
Request parseHelpAgents(const QStringList &rest, bool json)
{
    const QString command = QStringLiteral("help agents");
    const QString takes =
        QStringLiteral("omaseed: help agents takes --for %1. omaseed help help says how.")
            .arg(AgentDeny::agents().join(QLatin1Char('|')));
    QJsonObject options;
    for (qsizetype i = 0; i < rest.size(); ++i) {
        const QString argument = rest.at(i);
        if (argument == QLatin1String("--json"))
            continue;
        if (argument == QLatin1String("--help") || argument == QLatin1String("-h"))
            return page(QStringLiteral("help"), json, QStringLiteral("help"));
        QString value;
        if (argument == QLatin1String("--for") && i + 1 < rest.size())
            value = rest.at(++i);
        else if (argument.startsWith(QLatin1String("--for=")))
            value = argument.mid(6);
        else
            return usage(command, json, takes);
        if (options.contains(QStringLiteral("for")) || !AgentDeny::agents().contains(value))
            return usage(command, json, takes);
        options.insert(QStringLiteral("for"), value);
    }
    Request request;
    request.kind = Request::Kind::Local;
    request.word = QStringLiteral("help");
    request.subword = QStringLiteral("agents");
    request.json = json;
    request.options = options;
    return request;
}

QString doTakes()
{
    return QStringLiteral("omaseed: do takes an action and its torrents: omaseed do recheck "
                          "3f9a1c0e. omaseed do --list says what it may do.");
}

/// `do <action> <hash…> [--to FOLDER] [--dry-run] [--server NAME] [--json]`, `do --list`
/// (C45 V15, C50 GT5). The options JSON: {action, torrents, to, move, dryRun, list, server}.
Request parseDo(const QStringList &rest, bool json)
{
    const QString word = QStringLiteral("do");
    // C50 GT1: no wording of a request turns a remove into a delete.
    if (rest.contains(QLatin1String("--delete-files")))
        return usage(word, json, deleteFilesWords());
    QJsonObject options;
    QStringList positionals;
    for (qsizetype i = 0; i < rest.size(); ++i) {
        const QString argument = rest.at(i);
        if (argument == QLatin1String("--json"))
            continue;
        if (!argument.isEmpty() && !argument.startsWith(QLatin1Char('-'))) {
            positionals << argument;
            continue;
        }
        if (argument == QLatin1String("--list") || argument == QLatin1String("--dry-run")) {
            const QString key = argument == QLatin1String("--list") ? QStringLiteral("list")
                                                                    : QStringLiteral("dryRun");
            if (options.contains(key))
                return usage(word, json, doesNotTake(word, argument));
            options.insert(key, true);
            continue;
        }
        const qsizetype equals = argument.indexOf(QLatin1Char('='));
        const QString name = argument.startsWith(QLatin1String("--"))
                                 ? argument.mid(2, equals < 0 ? -1 : equals - 2)
                                 : QString();
        // C50 GT4: --plan FILE, or - for stdin; the CLI reads it (planFile) and sends its text.
        const QString key = name == QLatin1String("plan") ? QStringLiteral("planFile") : name;
        if ((name != QLatin1String("to") && name != QLatin1String("server") &&
             name != QLatin1String("plan")) ||
            options.contains(key))
            return usage(word, json, doesNotTake(word, argument));
        QString value;
        QString given = argument;
        if (equals >= 0) {
            value = argument.mid(equals + 1);
        } else {
            if (i + 1 >= rest.size() || (rest.at(i + 1).startsWith(QLatin1Char('-')) &&
                                         rest.at(i + 1) != QLatin1String("-")))
                return usage(word, json, doesNotTake(word, argument));
            value = rest.at(++i);
            given += QLatin1Char(' ') + value;
        }
        if (value.trimmed().isEmpty())
            return usage(word, json, doesNotTake(word, given));
        options.insert(key, value);
    }

    Request request;
    request.kind = Request::Kind::Call;
    request.word = word;
    request.json = json;
    if (options.value(QStringLiteral("list")).toBool()) {
        // `do --list` takes nothing else but --json.
        if (!positionals.isEmpty())
            return usage(word, json, doesNotTake(word, positionals.first()));
        for (const QString &key : options.keys())
            if (key != QLatin1String("list"))
                return usage(
                    word,
                    json,
                    doesNotTake(word,
                                key == QLatin1String("dryRun")     ? QStringLiteral("--dry-run")
                                : key == QLatin1String("planFile") ? QStringLiteral("--plan")
                                                                   : QStringLiteral("--") + key));
        request.options = options;
        return request;
    }
    if (options.contains(QStringLiteral("planFile"))) {
        // A plan names its actions, torrents and folders itself (C50 GT4).
        if (!positionals.isEmpty())
            return usage(word, json, doesNotTake(word, positionals.first()));
        if (options.contains(QStringLiteral("to")))
            return usage(word, json, doesNotTake(word, QStringLiteral("--to")));
        request.options = options;
        return request;
    }
    if (positionals.isEmpty())
        return usage(word, json, doTakes());
    const QString action = positionals.takeFirst();
    // Torrents are hashes or prefixes of 8 or more, as given; the app matches them (V15).
    QStringList torrents;
    for (const QString &hash : std::as_const(positionals))
        torrents << hash.toLower();
    options.insert(QStringLiteral("action"), action);
    if (neverWords().contains(action)) {
        // C45 V14: the app answers never, whatever follows.
        options.remove(QStringLiteral("to"));
        options.insert(QStringLiteral("torrents"), QJsonArray::fromStringList(torrents));
        request.options = options;
        return request;
    }
    static const QStringList fixes{QStringLiteral("retry"),
                                   QStringLiteral("start-here"),
                                   QStringLiteral("recheck"),
                                   QStringLiteral("reannounce"),
                                   QStringLiteral("dismiss")};
    static const QStringList serverWide{QStringLiteral("pause"), QStringLiteral("resume")};
    static const QStringList withFolder{QStringLiteral("move"), QStringLiteral("locate")};
    const bool known = fixes.contains(action) || serverWide.contains(action) ||
                       withFolder.contains(action) || action == QLatin1String("move-back") ||
                       action == QLatin1String("remove");
    if (!known)
        return usage(word, json, doesNotTake(word, action));
    // --to is a move's and locate's, and theirs it must be (C45 V15, C50 GT2).
    if (withFolder.contains(action) != options.contains(QStringLiteral("to")))
        return usage(word,
                     json,
                     withFolder.contains(action)
                         ? QStringLiteral("omaseed: do %1 takes --to FOLDER. omaseed help do says "
                                          "how.")
                               .arg(action)
                         : doesNotTake(word, QStringLiteral("--to")));
    if (serverWide.contains(action)) {
        // C36 M10's hold is the whole server's: no torrents (assumed, PLAN Q2).
        if (!torrents.isEmpty())
            return usage(word,
                         json,
                         QStringLiteral("omaseed: do %1 takes no torrents: it holds the whole "
                                        "server, as the window's %1 does.")
                             .arg(action));
    } else if (action == QLatin1String("move-back")) {
        // C18: a move back names the move (omaseed moves gives its id), never torrents.
        if (torrents.size() != 1)
            return usage(word,
                         json,
                         QStringLiteral("omaseed: do move-back takes one move's id: omaseed "
                                        "moves lists them."));
        options.insert(QStringLiteral("move"), positionals.first());
        torrents.clear();
    } else if (torrents.isEmpty()) {
        return usage(word,
                     json,
                     QStringLiteral("omaseed: do %1 takes the torrents' hashes, or 8 or more of "
                                    "their first characters: omaseed errors gives them.")
                         .arg(action));
    }
    if (!torrents.isEmpty())
        options.insert(QStringLiteral("torrents"), QJsonArray::fromStringList(torrents));
    request.options = options;
    return request;
}

} // namespace

QStringList levels()
{
    return {QStringLiteral("critical"),
            QStringLiteral("warning"),
            QStringLiteral("info"),
            QStringLiteral("normal")};
}

QStringList optionsOf(const QString &word)
{
    if (word == QLatin1String("errors"))
        return {QStringLiteral("server")};
    if (word == QLatin1String("log"))
        return {QStringLiteral("level"),
                QStringLiteral("lines"),
                QStringLiteral("since"),
                QStringLiteral("torrent"),
                QStringLiteral("server")};
    if (word == QLatin1String("moves"))
        return {QStringLiteral("landed"), QStringLiteral("server")};
    if (word == QLatin1String("check"))
        return {QStringLiteral("theme")};
    return {};
}

bool isWord(const QString &argument)
{
    return builtWords().contains(argument) || reservedWords().contains(argument);
}

bool isReserved(const QString &word)
{
    return reservedWords().contains(word);
}

Request parse(const QStringList &arguments, const QStringList &skillPages)
{
    const QString word = arguments.value(0);
    const QStringList rest = arguments.mid(1);
    const bool json = rest.contains(QLatin1String("--json"));
    if (!isWord(word))
        return usage(word, json, noSuchWord(word));
    if (isReserved(word))
        return usage(word, json, notYet(word));

    // `omaseed help`, `omaseed help WORD`, `omaseed help --help` (C45 design 3).
    if (word == QLatin1String("help")) {
        if (rest.isEmpty())
            return page(word, false, QString());
        if (rest.size() == 1 &&
            (rest.first() == QLatin1String("--help") || rest.first() == QLatin1String("-h")))
            return page(word, false, word);
        // C50 GT7: `help agents [--for AGENT]`, the deny rules for an agent's own settings.
        if (rest.first() == QLatin1String("agents"))
            return parseHelpAgents(rest.mid(1), json);
        if (rest.size() == 1 && !rest.first().startsWith(QLatin1Char('-'))) {
            const QString which = rest.first();
            if (!isWord(which))
                return usage(word, false, noSuchWord(which));
            if (isReserved(which))
                return usage(word, false, notYet(which));
            return page(word, false, which);
        }
        const auto odd = std::find_if(rest.cbegin(), rest.cend(), [](const QString &a) {
            return a.startsWith(QLatin1Char('-'));
        });
        return usage(word, json, doesNotTake(word, odd != rest.cend() ? *odd : rest.at(1)));
    }

    // `omaseed WORD --help` is the word's page, whatever else it was given.
    if (rest.contains(QLatin1String("--help")) || rest.contains(QLatin1String("-h")))
        return page(word, json, word);
    if (word == QLatin1String("do"))
        return parseDo(rest, json);

    // C37 U3, U5, U6: check and skin take positionals, and skill its page or link words (C48);
    // any other word, none.
    const bool takesPositionals = word == QLatin1String("check") || word == QLatin1String("skin") ||
                                  word == QLatin1String("skill");
    const QStringList takes = optionsOf(word);
    QJsonObject options;
    QStringList positionals;
    for (qsizetype i = 0; i < rest.size(); ++i) {
        const QString argument = rest.at(i);
        if (argument == QLatin1String("--json"))
            continue;
        if (takesPositionals && !argument.isEmpty() && !argument.startsWith(QLatin1Char('-'))) {
            positionals << argument;
            continue;
        }
        if (!argument.startsWith(QLatin1String("--")) || argument.size() < 3)
            return usage(word, json, doesNotTake(word, argument));
        const qsizetype equals = argument.indexOf(QLatin1Char('='));
        const QString name = argument.mid(2, equals < 0 ? -1 : equals - 2);
        if (!takes.contains(name) || options.contains(name))
            return usage(word, json, doesNotTake(word, argument));
        QString value;
        QString given = argument;
        if (equals >= 0) {
            value = argument.mid(equals + 1);
        } else {
            if (i + 1 >= rest.size() || rest.at(i + 1).startsWith(QLatin1String("--")))
                return usage(word, json, doesNotTake(word, argument));
            value = rest.at(++i);
            given += QLatin1Char(' ') + value;
        }
        if (!accept(name, value, &options))
            return usage(word, json, doesNotTake(word, given));
    }

    Request request;
    request.kind = Request::Kind::Call;
    request.word = word;
    request.json = json;
    request.options = options;
    if (word == QLatin1String("check")) {
        if (positionals.isEmpty())
            return usage(word,
                         json,
                         QStringLiteral("omaseed: check takes a skin's name: omaseed check warm. "
                                        "omaseed help check says how."));
        if (positionals.size() > 1)
            return usage(word, json, doesNotTake(word, positionals.at(1)));
        request.kind = Request::Kind::Local;
        request.positionals = positionals;
    } else if (word == QLatin1String("skin")) {
        const QString subword = positionals.value(0);
        const int count = positionalsOf(subword);
        if (count < 0)
            return usage(word, json, skinTakes());
        request.subword = subword;
        request.positionals = positionals.mid(1);
        if (request.positionals.size() < count)
            return usage(request.command(), json, skinTakes());
        if (request.positionals.size() > count)
            return usage(request.command(),
                         json,
                         QStringLiteral("omaseed: %1 doesn't take \"%2\". omaseed help skin says "
                                        "what it takes.")
                             .arg(request.command(), request.positionals.at(count)));
        if (subword == QLatin1String("use")) {
            // C37 U6: the running omaseed draws it; Words saves it when none is running.
            request.options.insert(QStringLiteral("name"), request.positionals.first());
        } else {
            request.kind = Request::Kind::Local;
        }
    } else if (word == QLatin1String("skill")) {
        // DECISIONS C48 (docs/65 §5): none is the map, one is a page or link, unlink, status.
        // Every form reads or writes files: no display, bus or running omaseed.
        const QString subword = positionals.value(0);
        if (!subword.isEmpty() && !skillPages.contains(subword) && !skillWords().contains(subword))
            return usage(word, json, skillTakes(skillPages));
        request.subword = subword;
        if (positionals.size() > 1)
            return usage(
                request.command(), json, doesNotTake(request.command(), positionals.at(1)));
        request.kind = Request::Kind::Local;
    }
    return request;
}

bool isSkillPrint(const Request &request)
{
    return request.word == QLatin1String("skill") && !skillWords().contains(request.subword);
}

QString method(const QString &word)
{
    if (word == QLatin1String("skin use"))
        return QStringLiteral("UseSkin");
    if (word == QLatin1String("status"))
        return QStringLiteral("Status");
    if (word == QLatin1String("errors"))
        return QStringLiteral("Errors");
    if (word == QLatin1String("log"))
        return QStringLiteral("Log");
    if (word == QLatin1String("moves"))
        return QStringLiteral("Moves");
    if (word == QLatin1String("do"))
        return QStringLiteral("Do");
    return QString();
}

QString helpPage(const QString &qtHelp)
{
    QStringList lines = qtHelp.split(QLatin1Char('\n'));
    const qsizetype options = lines.indexOf(QStringLiteral("Options:"));
    // The description sits between Qt's usage line and its options; anything else is kept.
    QStringList head = lines.mid(1, options < 0 ? 1 : options - 1);
    while (!head.isEmpty() && head.last().trimmed().isEmpty())
        head.removeLast();
    QStringList page{
        QStringLiteral("Usage: omaseed [options] [magnet: links or .torrent files...]"),
        QStringLiteral("       omaseed <word> [arguments] [--json]"),
    };
    page << head;
    // C45 V2 (docs/62 §2): the words, above Qt's options; the skins' after the library's
    // (C37 U3, U5, U6); do's two lines as docs/62 §2 draws them (V15).
    page << QString() << QStringLiteral("Your library, from the running omaseed:")
         << QStringLiteral("  status     the server, what it is doing, how many need you")
         << QStringLiteral("  errors     every torrent that needs you, why, and its fix")
         << QStringLiteral("  log        qBittorrent's log: warnings and worse, the last 50 lines")
         << QStringLiteral("  moves      moves running now, and the last ones that landed")
         << QStringLiteral("  do         a fix the window offers, if you let agents act")
         << QStringLiteral("             (Settings › General › Agents; moves always ask you)")
         << QString() << QStringLiteral("Your skins, in ~/.config/omaseed/skins/:")
         << QStringLiteral("  skin list    every skin, where it lives, whether it reads")
         << QStringLiteral("  skin clone   start one from a copy: omaseed skin clone console warm")
         << QStringLiteral("  check        what a skin reads and drops, light and dark pictures, "
                           "no display")
         << QStringLiteral("  skin use     draw it in the open window, and keep it") << QString()
         << QStringLiteral("Every word takes --json. omaseed help <word> says more.")
         // DECISIONS C48 AS8: the link first, the reading second.
         << QStringLiteral("Coding agents: omaseed skill link, or omaseed skill to read the "
                           "guide.")
         << QString();
    if (options >= 0)
        page << lines.mid(options);
    QString text = page.join(QLatin1Char('\n'));
    if (!text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    return text;
}

QString wordPage(const QString &word)
{
    // C45 V2 (docs/62 §2): one shape for every word: Usage, what it is, who answers, its
    // options, its exits (V13).
    static const QString json =
        QStringLiteral("  --json          one JSON object; omaseed skill library describes its "
                       "fields\n");
    static const QString onlyCurrent =
        QStringLiteral("  --server NAME   a server by its name or id; only the one omaseed is on "
                       "can\n"
                       "                  answer (omaseed status lists them)\n");
    if (word == QLatin1String("help"))
        return QStringLiteral("Usage: omaseed help [word]\n"
                              "       omaseed help agents [--for claude|codex|pi|gemini] "
                              "[--json]\n"
                              "This page, or one word's. help agents prints deny rules for a "
                              "coding\n"
                              "agent's own settings (Claude Code's by default), filled with the "
                              "server\n"
                              "omaseed is on: omaseed's Agents settings bind only its own words. "
                              "It\n"
                              "prints them and never writes them.\n"
                              "Exit: 0 printed · 2 usage\n");
    if (word == QLatin1String("status"))
        return QStringLiteral("Usage: omaseed status [--json]\n"
                              "The server omaseed is on: whether it answers, its speeds and "
                              "alternative limits,\n"
                              "the rail's five questions, moves running, a pause held, and the "
                              "other servers\n"
                              "as last seen. The running omaseed answers.\n") +
               json +
               QStringLiteral("Exit: 0 answered (whether the server answers or not) · 2 usage ·\n"
                              "      3 omaseed isn't running\n");
    if (word == QLatin1String("errors"))
        return QStringLiteral("Usage: omaseed errors [--server NAME] [--json]\n"
                              "Every torrent that needs you, why, and the window's fix for it: "
                              "the rail's\n"
                              "\"needs you\". The running omaseed answers, for the server it is "
                              "on.\n") +
               onlyCurrent + json +
               QStringLiteral("Exit: 0 answered (whatever it found) · 2 usage · 3 omaseed isn't "
                              "running ·\n"
                              "      4 that server can't answer now\n");
    if (word == QLatin1String("log"))
        return QStringLiteral(
                   "Usage: omaseed log [--level LEVEL] [--lines N] [--since ID] [--torrent HASH]\n"
                   "                   [--server NAME] [--json]\n"
                   "qBittorrent's log as the running omaseed reads it, oldest first: warnings "
                   "and\n"
                   "worse, the last 50 lines. Only qBittorrent keeps a log omaseed can read.\n"
                   "  --level LEVEL   normal, info, warning or critical: that level and worse\n"
                   "                  (warning)\n"
                   "  --lines N       the last N, up to 2000 (50)\n"
                   "  --since ID      the lines from ID on: pass the last answer's \"next\"\n"
                   "  --torrent HASH  only the lines about it: its hash, or 8 or more of its "
                   "first\n"
                   "                  characters\n") +
               onlyCurrent + json +
               QStringLiteral("Exit: 0 answered · 2 usage · 3 omaseed isn't running · 4 that "
                              "server can't\n"
                              "      answer now (not the one omaseed is on, unreachable, no log, "
                              "an unknown\n"
                              "      torrent)\n");
    if (word == QLatin1String("moves"))
        return QStringLiteral("Usage: omaseed moves [--landed N] [--server NAME] [--json]\n"
                              "The move running now, and the last ones that landed with what a "
                              "move back\n"
                              "would do. Any server's, from what omaseed keeps for it. The "
                              "running omaseed\n"
                              "answers.\n"
                              "  --landed N      the last N that landed, up to 20 (5)\n"
                              "  --server NAME   a server by its name or id (omaseed status "
                              "lists them)\n") +
               json +
               QStringLiteral("Exit: 0 answered · 2 usage · 3 omaseed isn't running · 4 that "
                              "server can't\n"
                              "      answer now\n");
    if (word == QLatin1String("do"))
        return QStringLiteral(
                   "Usage: omaseed do <action> <hash...> [--to FOLDER] [--dry-run] [--server "
                   "NAME]\n"
                   "                  [--json]\n"
                   "       omaseed do --plan FILE|- [--dry-run] [--server NAME] [--json]\n"
                   "       omaseed do --list [--json]\n"
                   "A fix the window offers, run for you by the running omaseed, if Settings ›\n"
                   "General › Agents lets agents act. It waits for the server's answer.\n"
                   "  fixes           retry, start-here, recheck, reannounce, dismiss <hash...>;\n"
                   "                  pause, resume (the whole server, no hashes)\n"
                   "  locate          locate <hash...> --to FOLDER: a torrent whose files are\n"
                   "                  missing, pointed at the folder that holds them, nothing\n"
                   "                  moved, then rechecked (a fix)\n"
                   "  moves           move <hash...> --to FOLDER, move-back <move>: always ask\n"
                   "  remove          remove <hash...>: only a torrent whose files are gone,\n"
                   "                  never its files; kept 30 days to add back: always asks\n"
                   "  <hash>          a torrent's hash, or 8 or more of its first characters\n"
                   "  <move>          a landed move's id (omaseed moves), or 8 or more of its\n"
                   "                  first characters\n"
                   "  --plan FILE|-   a JSON plan of lines, each an action and its torrents:\n"
                   "                  every line checked first, one ask for the lot\n"
                   "  --dry-run       every check, nothing asked, nothing done\n"
                   "  --list          what the person lets agents do now\n"
                   "  --server NAME   only the server omaseed is on can act\n") +
               json +
               QStringLiteral("Never through do: delete, rename, categories and tags, adds, "
                              "settings,\n"
                              "servers, quit. No flag deletes files.\n"
                              "Exit: 0 done · 2 usage · 3 omaseed isn't running · 4 that server "
                              "can't act\n"
                              "      now · 5 not done: not allowed, never, refused, not "
                              "answered, not\n"
                              "      removable or not found · 6 the server refused it\n"
                              "Your agent's own settings are the wall: omaseed help agents.\n");
    if (word == QLatin1String("check"))
        return QStringLiteral(
                   "Usage: omaseed check <skin> [--theme NAME] [--json]\n"
                   "What a skin reads and what it drops, and why; the fonts it lacks; where "
                   "text\n"
                   "had to be lifted to stay readable; and a light and a dark picture of it, "
                   "the\n"
                   "person's theme for its own lightness and a stock Omarchy theme for the "
                   "other,\n"
                   "in ~/.cache/omaseed/check/. No display, no running omaseed: it draws "
                   "offscreen.\n"
                   "  --theme NAME    one picture, under that installed Omarchy theme\n") +
               json +
               QStringLiteral("Exit: 0 drawn · 1 drawn whole as Omarchy's (it doesn't read) · "
                              "2 usage\n");
    if (word == QLatin1String("skin"))
        return QStringLiteral(
                   "Usage: omaseed skin list [--json]\n"
                   "       omaseed skin clone <from> <name> [--json]\n"
                   "       omaseed skin use <name> [--json]\n"
                   "The person's skins, one folder each in ~/.config/omaseed/skins/.\n"
                   "  list            every skin, built in first: where it lives, whether it "
                   "reads\n"
                   "  clone           a new skin, <name>, from a copy of <from> with every "
                   "token\n"
                   "                  written out; never over a folder that is there\n"
                   "  use             the open omaseed draws it and keeps it; with none "
                   "running, it\n"
                   "                  is saved for the next start\n") +
               json +
               QStringLiteral("Exit: 0 done · 1 the skin doesn't read · 2 usage, no such skin, "
                              "a bad name or\n"
                              "      one taken · 3 an omaseed that can't answer · 5 not "
                              "written\n");
    if (word == QLatin1String("skill"))
        return QStringLiteral(
                   "Usage: omaseed skill [page] [--json]\n"
                   "       omaseed skill link | unlink | status [--json]\n"
                   "omaseed's guide for coding agents, a skill: the map, then a page of it "
                   "when the\n"
                   "task needs one (the map's last line names them). No one answers: it reads "
                   "and\n"
                   "writes files, with no display and no running omaseed.\n"
                   "  link            put the skill where coding agents look for one\n"
                   "  unlink          take out only the links omaseed made\n"
                   "  status          where it is linked, for each agent\n") +
               QStringLiteral("  --json          one JSON object: the page as text, or each "
                              "agent's folder\n") +
               QStringLiteral("Exit: 0 done · 2 usage · 5 not written\n");
    return QString();
}

QString doesNotTake(const QString &word, const QString &argument)
{
    return QStringLiteral("omaseed: %1 doesn't take \"%2\". omaseed help %1 says what it takes.")
        .arg(word, argument);
}

QString noSuchWord(const QString &argument)
{
    return QStringLiteral("omaseed: there's no word \"%1\". omaseed --help lists them.")
        .arg(argument);
}

QString deleteFilesWords()
{
    // C50 GT1, board 67's mock.
    return QStringLiteral("omaseed do never deletes files: remove takes the torrent off the list "
                          "and nothing else.");
}

bool isNeverWord(const QString &action)
{
    return neverWords().contains(action);
}

QString notYet(const QString &word)
{
    return QStringLiteral(
               "omaseed: %1 isn't in this omaseed yet. omaseed --help lists the words it has.")
        .arg(word);
}

int exitFor(const QString &code)
{
    // C45 V13: one table for the family; C37's 0, 1 and 2 keep their meaning.
    if (code.isEmpty())
        return 0;
    static const QStringList usage{
        QStringLiteral("usage"), QStringLiteral("unknown-server"), QStringLiteral("ambiguous")};
    static const QStringList unreached{QStringLiteral("not-running"),
                                       QStringLiteral("too-old"),
                                       QStringLiteral("no-bus"),
                                       QStringLiteral("no-reply")};
    static const QStringList cannotNow{QStringLiteral("not-current"),
                                       QStringLiteral("unreachable"),
                                       QStringLiteral("no-log"),
                                       QStringLiteral("unknown-torrent"),
                                       QStringLiteral("first-sync")};
    static const QStringList notDone{QStringLiteral("not-allowed"),
                                     QStringLiteral("never"),
                                     QStringLiteral("refused"),
                                     QStringLiteral("no-answer"),
                                     // C50 GT1, GT2: remove and locate's refusals.
                                     QStringLiteral("not-removable"),
                                     QStringLiteral("not-found")};
    // C37 U3, U5, U6: the skins' words.
    static const QStringList skinUsage{QStringLiteral("unknown-skin"),
                                       QStringLiteral("bad-name"),
                                       QStringLiteral("skin-exists"),
                                       QStringLiteral("no-theme")};
    if (usage.contains(code) || skinUsage.contains(code))
        return 2;
    if (code == QLatin1String("not-written"))
        return 5;
    if (code == QLatin1String("unreadable"))
        return 1;
    if (unreached.contains(code))
        return 3;
    if (cannotNow.contains(code))
        return 4;
    if (notDone.contains(code))
        return 5;
    if (code == QLatin1String("failed"))
        return 6;
    return 1;
}

QJsonValue isoUtc(const QDateTime &at)
{
    if (!at.isValid())
        return QJsonValue::Null;
    return at.toTimeZone(QTimeZone::UTC).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss'Z'"));
}

QByteArray orderedJson(const QList<std::pair<QString, QJsonValue>> &fields)
{
    QString out(QLatin1Char('{'));
    for (qsizetype i = 0; i < fields.size(); ++i) {
        if (i > 0)
            out += QLatin1Char(',');
        out += jsonString(fields.at(i).first) + QLatin1Char(':') + jsonString(fields.at(i).second);
    }
    out += QLatin1Char('}');
    return out.toUtf8();
}

QByteArray failure(const QString &command,
                   const QString &code,
                   const QString &message,
                   const QDateTime &at,
                   const QString &omaseedVersion)
{
    QList<std::pair<QString, QJsonValue>> fields{
        {QStringLiteral("v"), 1},
        {QStringLiteral("command"), command},
        {QStringLiteral("at"), isoUtc(at)},
    };
    if (!omaseedVersion.isEmpty())
        fields.append({QStringLiteral("omaseed"), omaseedVersion});
    fields.append(
        {QStringLiteral("error"),
         QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}});
    return orderedJson(fields);
}

} // namespace omaseed::core::AgentWords
