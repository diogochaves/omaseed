#include "core/AgentDeny.hpp"

#include "core/AgentWords.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace omaseed::core::AgentDeny {

namespace {

const QString kHostHolder = QStringLiteral("<host>");
const QString kPortHolder = QStringLiteral("<port>");

bool isLoopback(const QString &host)
{
    return host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost") ||
           host == QLatin1String("::1") || host == QLatin1String("[::1]");
}

/// The spellings of the server's address a command may carry: a loopback server is reached
/// as 127.0.0.1 and as localhost alike, so both are named (its own spelling too, when it is
/// another, such as [::1]).
QStringList addresses(const Target &target)
{
    const QString port = target.port.isEmpty() ? kPortHolder : target.port;
    if (target.host.isEmpty())
        return {kHostHolder + QLatin1Char(':') + port};
    QStringList hosts{target.host};
    if (isLoopback(target.host)) {
        hosts = QStringList{QStringLiteral("127.0.0.1"), QStringLiteral("localhost")};
        if (!hosts.contains(target.host))
            hosts << target.host;
    }
    QStringList out;
    for (const QString &host : hosts)
        out << host + QLatin1Char(':') + port;
    return out;
}

QString hostOrHolder(const Target &target)
{
    return target.host.isEmpty() ? kHostHolder : target.host;
}

QString portOrHolder(const Target &target)
{
    return target.port.isEmpty() ? kPortHolder : target.port;
}

/// A JSON string's inside, as settings.json wants it.
QString jsonQuoted(const QString &text)
{
    const QByteArray array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(2, array.size() - 4));
}

/// A Starlark string (Codex's .rules).
QString starlarkQuoted(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + out + QLatin1Char('"');
}

/// A regular expression's literal text: only the characters a JavaScript regex reads as
/// syntax are escaped (an escaped ':' is an error under the u flag).
QString regexLiteral(const QString &text)
{
    static const QString special = QStringLiteral("\\^$.|?*+()[]{}");
    QString out;
    for (const QChar c : text) {
        if (special.contains(c))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

/// The config folder relative to home ("…/.config/omaseed"), or absolute when outside it.
QString configUnderHome(const Target &target, bool *underHome)
{
    const QString home =
        target.home.endsWith(QLatin1Char('/')) ? target.home.chopped(1) : target.home;
    *underHome = !home.isEmpty() && target.configDir.startsWith(home + QLatin1Char('/'));
    return *underHome ? target.configDir.mid(home.size() + 1) : target.configDir;
}

/// One paragraph wrapped at 80 columns, ending in a newline.
QString wrapped(const QString &paragraph)
{
    QString out;
    QString line;
    for (const QString &word : paragraph.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (!line.isEmpty() && line.size() + 1 + word.size() > 80) {
            out += line + QLatin1Char('\n');
            line.clear();
        }
        line += (line.isEmpty() ? QString() : QStringLiteral(" ")) + word;
    }
    return out + line + QLatin1Char('\n');
}

QString serverWords(const Target &target)
{
    return target.serverName.isEmpty() ? QStringLiteral("your torrent server") : target.serverName;
}

/// The page's head (board 67 §5's mock): what the gate binds, and where the rules go.
QString head(const Target &target, const QString &intro)
{
    QString paragraph = QStringLiteral("omaseed's Agents settings bind only omaseed's own way "
                                       "in. Anything running as you can call %1's API, "
                                       "omaseed's bar widget on D-Bus, or edit omaseed's files.")
                            .arg(serverWords(target));
    if (!intro.isEmpty())
        paragraph += QStringLiteral(" To block that, tell your agent. ") + intro;
    return QStringLiteral("A wall, not a gate\n") + wrapped(paragraph);
}

QString nameOf(const QString &agent)
{
    if (agent == QLatin1String("codex"))
        return QStringLiteral("Codex");
    if (agent == QLatin1String("gemini"))
        return QStringLiteral("Gemini CLI");
    if (agent == QLatin1String("pi"))
        return QStringLiteral("pi");
    return QStringLiteral("Claude Code");
}

QString others(const QString &agent)
{
    QStringList names;
    for (const QString &other : agents())
        if (other != agent)
            names << other;
    QStringList titles;
    for (const QString &other : names)
        titles << nameOf(other);
    return QStringLiteral("%1: omaseed help agents --for %2\n")
        .arg(titles.join(QStringLiteral(" · ")), names.join(QLatin1Char('|')));
}

QString placeholderNote(const Rules &rules, const Target &target)
{
    if (!rules.rules.join(QLatin1Char('\n')).contains(kPortHolder))
        return QString();
    if (target.unknownWhy == QLatin1String("not-running") ||
        target.unknownWhy == QLatin1String("no-bus"))
        return wrapped(QStringLiteral("omaseed isn't running, so it can't say its server: put "
                                      "the server's address in place of %1:%2.")
                           .arg(kHostHolder, kPortHolder));
    return wrapped(QStringLiteral("omaseed didn't say its server's address: put it in place of "
                                  "%1:%2.")
                       .arg(kHostHolder, kPortHolder));
}

QString tail(const Rules &rules, const Target &target, bool matchText)
{
    QString text;
    for (const QString &why : rules.omitted)
        text += wrapped(why);
    text += placeholderNote(rules, target);
    if (!text.isEmpty())
        text += QLatin1Char('\n');
    text += others(rules.agent);
    text += matchText ? QStringLiteral("These match command text. A sandbox with a network allow "
                                       "list is the wall.\n")
                      : QStringLiteral("A sandbox with a network allow list is the wall.\n");
    return text;
}

// Claude Code: settings.json's permissions.deny.
// Read 2026-11-21: https://code.claude.com/docs/en/permissions
// - Bash(…): `*` stands for any text in its place, so a leading one matches any program, and a
//   deny rule matches every subcommand of a compound command. Patterns on arguments are
//   fragile (the page says so for curl): hence "match command text".
// - Edit(…) is gitignore syntax; `~/` is home, `//` the filesystem's root. Write(…) with a
//   path is never matched ("use Edit(…) in place of Write(…)"), so none is printed.
// - WebFetch(domain:…) takes no port: it would shut every service on the host, so none.
Rules claude(const Target &target)
{
    Rules out;
    out.agent = QStringLiteral("claude");
    out.name = QStringLiteral("Claude Code");
    out.where = QStringLiteral("~/.claude/settings.json");
    for (const QString &address : addresses(target))
        out.rules << QStringLiteral("Bash(*%1*)").arg(address);
    if (!target.busName.isEmpty())
        for (const QString &tool :
             {QStringLiteral("busctl"), QStringLiteral("gdbus"), QStringLiteral("dbus-send")})
            out.rules << QStringLiteral("Bash(*%1*%2*)").arg(tool, target.busName);
    bool underHome = false;
    const QString config = configUnderHome(target, &underHome);
    static const QString globby = QStringLiteral("*?[]\\!#");
    const bool spellable =
        !config.isEmpty() &&
        std::none_of(config.cbegin(), config.cend(), [](QChar c) { return globby.contains(c); }) &&
        config.trimmed() == config;
    if (spellable) {
        const QString root =
            underHome ? QStringLiteral("~/") + config : QStringLiteral("//") + config.mid(1);
        // Every file omaseed keeps there; skins/ and layouts/ stay the person's (GT7).
        out.rules << QStringLiteral("Edit(%1/*.json)").arg(root)
                  << QStringLiteral("Edit(%1/moves/**)").arg(root)
                  << QStringLiteral("Edit(%1/secrets/**)").arg(root);
    } else {
        out.omitted << QStringLiteral("No Edit rule: %1 has characters Claude Code's path "
                                      "patterns read as globs.")
                           .arg(target.configDir);
    }
    out.omitted << QStringLiteral("No WebFetch rule: its domain rule takes no port, so it would "
                                  "shut every service on %1.")
                       .arg(hostOrHolder(target));

    QString text = head(target,
                        QStringLiteral("For Claude Code, in its settings.json (%1 for every "
                                       "project, or a project's .claude/settings.json):")
                            .arg(out.where));
    text += QStringLiteral("\n  \"permissions\": { \"deny\": [\n");
    for (qsizetype i = 0; i < out.rules.size(); ++i)
        text += QStringLiteral("    \"%1\"%2\n")
                    .arg(jsonQuoted(out.rules.at(i)),
                         i + 1 < out.rules.size() ? QStringLiteral(",") : QString());
    text += QStringLiteral("  ] }\n\n");
    out.text = text;
    out.text += tail(out, target, true);
    return out;
}

// Codex: a rules file of prefix_rule()s.
// Read 2026-11-21: https://developers.openai.com/codex/rules (now
// https://learn.chatgpt.com/docs/agent-configuration/rules)
// - `pattern` is the command's first arguments, each a literal or a list of alternatives,
//   matched whole as an exact prefix; `bash -lc "a && b"` is split into its commands.
// - No wildcard, regex or substring: the server's address inside a URL can't be named, so no
//   curl rule; and rules match commands only, never file edits.
Rules codex(const Target &target)
{
    Rules out;
    out.agent = QStringLiteral("codex");
    out.name = QStringLiteral("Codex");
    out.where = QStringLiteral("~/.codex/rules/omaseed.rules");
    const auto list = [](const QStringList &words) {
        QStringList quoted;
        for (const QString &word : words)
            quoted << starlarkQuoted(word);
        return QStringLiteral("[") + quoted.join(QStringLiteral(", ")) + QStringLiteral("]");
    };
    const auto rule = [](const QString &pattern) {
        return QStringLiteral("prefix_rule(\n"
                              "    pattern = %1,\n"
                              "    decision = \"forbidden\",\n"
                              "    justification = \"omaseed's own words reach it: omaseed "
                              "help\",\n"
                              ")")
            .arg(pattern);
    };
    if (!target.busName.isEmpty()) {
        const QString bus = starlarkQuoted(target.busName);
        const QString calls =
            list({QStringLiteral("call"), QStringLiteral("introspect"), QStringLiteral("monitor")});
        out.rules << rule(QStringLiteral("[\"busctl\", \"--user\", %1, %2]")
                              .arg(list({QStringLiteral("call"),
                                         QStringLiteral("introspect"),
                                         QStringLiteral("tree"),
                                         QStringLiteral("get-property"),
                                         QStringLiteral("set-property"),
                                         QStringLiteral("monitor")}),
                                   bus))
                  << rule(QStringLiteral("[\"gdbus\", %1, %2, %3, %4]")
                              .arg(calls,
                                   list({QStringLiteral("--session"), QStringLiteral("-e")}),
                                   list({QStringLiteral("--dest"), QStringLiteral("-d")}),
                                   bus))
                  << rule(QStringLiteral("[\"gdbus\", %1, %2, %3]")
                              .arg(calls,
                                   list({QStringLiteral("--session"), QStringLiteral("-e")}),
                                   starlarkQuoted(QStringLiteral("--dest=") + target.busName)));
    }
    out.omitted << QStringLiteral("No rule for %1's address: Codex's rules match a command's "
                                  "first words, whole, and an address sits inside a URL.")
                       .arg(serverWords(target))
                << QStringLiteral("No rule for omaseed's files: Codex's rules match commands, "
                                  "not edits; its workspace-write sandbox keeps edits in the "
                                  "project.");
    QString text = head(target, QStringLiteral("For Codex, in %1:").arg(out.where));
    text += QLatin1Char('\n');
    for (const QString &each : out.rules)
        text += each + QLatin1Char('\n');
    text += QLatin1Char('\n');
    out.text = text + tail(out, target, true);
    return out;
}

// Gemini CLI: a policy file of [[rule]]s (tools.exclude is deprecated for these).
// Read 2026-11-21: https://geminicli.com/docs/reference/policy-engine/ ,
// https://geminicli.com/docs/tools/shell/ , https://geminicli.com/docs/tools/file-system/
// - User policies: ~/.gemini/policies/*.toml; decision "deny", priority 0–999.
// - commandRegex is tested against the arguments' JSON after `"command":"`; the shell tool
//   splits `&&`, `||`, `;` and checks each part. argsPattern is tested against the arguments'
//   JSON, where write_file and replace name the file `file_path` (relative or absolute).
// - TOML literal strings ('…'), so a backslash stays one; a path holding ', " or \ can't be
//   written that way, nor matched in JSON as written: no edit rule then.
Rules gemini(const Target &target)
{
    Rules out;
    out.agent = QStringLiteral("gemini");
    out.name = QStringLiteral("Gemini CLI");
    out.where = QStringLiteral("~/.gemini/policies/omaseed.toml");
    const auto rule = [](const QString &tool, const QString &key, const QString &pattern) {
        return QStringLiteral("[[rule]]\n"
                              "toolName = %1\n"
                              "%2 = '%3'\n"
                              "decision = \"deny\"\n"
                              "priority = 900\n"
                              "denyMessage = \"omaseed's: use omaseed's own words (omaseed "
                              "help)\"")
            .arg(tool, key, pattern);
    };
    const QString shell = QStringLiteral("\"run_shell_command\"");
    QStringList hosts;
    if (target.host.isEmpty()) {
        hosts << kHostHolder;
    } else {
        for (const QString &address : addresses(target))
            hosts << regexLiteral(address.section(QLatin1Char(':'), 0, -2));
    }
    const QString port = target.port.isEmpty() ? kPortHolder : regexLiteral(target.port);
    const QString hostPart =
        hosts.size() == 1 ? hosts.first()
                          : QLatin1Char('(') + hosts.join(QLatin1Char('|')) + QLatin1Char(')');
    out.rules << rule(shell,
                      QStringLiteral("commandRegex"),
                      QStringLiteral(".*%1:%2(?![0-9])").arg(hostPart, port));
    if (!target.busName.isEmpty())
        out.rules << rule(
            shell,
            QStringLiteral("commandRegex"),
            QStringLiteral(".*(busctl|gdbus|dbus-send).*%1").arg(regexLiteral(target.busName)));
    bool underHome = false;
    const QString config = configUnderHome(target, &underHome);
    const bool writable = !config.isEmpty() && !config.contains(QLatin1Char('\'')) &&
                          !config.contains(QLatin1Char('"')) && !config.contains(QLatin1Char('\\'));
    if (writable) {
        // Everything under it but skins/ and layouts/ (GT7), however the path is given.
        out.rules << rule(QStringLiteral("[\"write_file\", \"replace\"]"),
                          QStringLiteral("argsPattern"),
                          QStringLiteral("\"file_path\":\"[^\"]*%1/(?!skins/|layouts/)")
                              .arg(regexLiteral(config)));
    } else {
        out.omitted << QStringLiteral("No rule for omaseed's files: %1 can't be written in a "
                                      "policy's pattern.")
                           .arg(target.configDir);
    }
    QString text = head(target, QStringLiteral("For Gemini CLI, in %1:").arg(out.where));
    text += QLatin1Char('\n');
    for (const QString &each : out.rules)
        text += each + QStringLiteral("\n\n");
    out.text = text + tail(out, target, true);
    return out;
}

// pi: no rules of its own.
// Read 2026-11-21: https://github.com/earendil-works/pi (README, "Permissions &
// Containerization"): "Pi does not include a built-in permission system for restricting
// filesystem, process, network, or credential access." Its extensions can block a tool call,
// but that is code the person writes, not a rule to paste (Q14: the wall alone).
Rules pi(const Target &target)
{
    Rules out;
    out.agent = QStringLiteral("pi");
    out.name = QStringLiteral("pi");
    QString text = head(target, QString());
    text += QLatin1Char('\n') +
            wrapped(QStringLiteral("pi has no deny rules of its own: it runs with your "
                                   "permissions. Run it in a container or a sandbox (its "
                                   "docs/containerization.md) whose network allow list leaves "
                                   "out %1:%2 and whose files leave out omaseed's.")
                        .arg(hostOrHolder(target), portOrHolder(target))) +
            QLatin1Char('\n');
    out.text = text + tail(out, target, false);
    return out;
}

} // namespace

QStringList agents()
{
    return {QStringLiteral("claude"),
            QStringLiteral("codex"),
            QStringLiteral("pi"),
            QStringLiteral("gemini")};
}

Rules rulesFor(const QString &agent, const Target &target)
{
    if (agent == QLatin1String("codex"))
        return codex(target);
    if (agent == QLatin1String("gemini"))
        return gemini(target);
    if (agent == QLatin1String("pi"))
        return pi(target);
    return claude(target);
}

QByteArray json(const Rules &rules, const Target &target, const QDateTime &at)
{
    const QJsonValue server = target.host.isEmpty()
                                  ? QJsonValue(QJsonValue::Null)
                                  : QJsonValue(target.host + QLatin1Char(':') + target.port);
    return AgentWords::orderedJson({
        {QStringLiteral("v"), 1},
        {QStringLiteral("command"), QStringLiteral("help agents")},
        {QStringLiteral("at"), AgentWords::isoUtc(at)},
        {QStringLiteral("agent"), rules.agent},
        {QStringLiteral("server"), server},
        {QStringLiteral("where"),
         rules.where.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(rules.where)},
        {QStringLiteral("rules"), QJsonArray::fromStringList(rules.rules)},
        {QStringLiteral("omitted"), QJsonArray::fromStringList(rules.omitted)},
        {QStringLiteral("text"), rules.text},
    });
}

} // namespace omaseed::core::AgentDeny
