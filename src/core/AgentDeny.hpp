#pragma once

// AgentDeny — `omaseed help agents` (DECISIONS C50 GT7): ready-made deny rules for the agents
// C48 links its skill for, filled with the server on show's address. The Agents row binds only
// omaseed's own way in; these rules are what an agent's own settings can add: no calls to the
// torrent server's port, no busctl/gdbus on omaseed's bus name, no edits to omaseed's files
// except skins/ and layouts/. omaseed prints them and never writes them.
//
// Each agent's syntax is the one its own documentation gives (the URL and the day it was read
// sit beside its rules in AgentDeny.cpp). A rule an agent's syntax can't write well (a port in
// a domain rule, a substring in a prefix rule, a path its patterns can't spell) is left out,
// and the page says what is left out and why. Pure: no file, no bus.

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace omaseed::core::AgentDeny {

/// What `--for` takes, the default first: claude, codex, pi, gemini.
QStringList agents();

/// What the rules are filled with.
struct Target
{
    QString host;       ///< the server on show's host; "" unknown (placeholders printed)
    QString port;       ///< its port; "" unknown
    QString serverName; ///< "" unknown
    QString busName;    ///< omaseed's bus name (AppIdentity::dbusServiceName)
    QString configDir;  ///< omaseed's config folder, absolute
    QString home;       ///< the person's home, absolute
    QString unknownWhy; ///< "not-running" or another code when the address is unknown
};

/// One agent's rules and the page that hands them over.
struct Rules
{
    QString agent;       ///< claude, codex, pi or gemini
    QString name;        ///< "Claude Code", "Codex", "pi", "Gemini CLI"
    QString where;       ///< the file they go in ("" for pi: it has none)
    QStringList rules;   ///< each rule as written in that file
    QStringList omitted; ///< a sentence for each rule this agent's syntax can't write well
    QString text;        ///< the page as printed
};

/// `agent` is one of agents(); anything else is treated as claude.
Rules rulesFor(const QString &agent, const Target &target);

/// `--json`: {v, command:"help agents", at, agent, server, where, rules, omitted, text};
/// server is "host:port", or null when omaseed didn't say.
QByteArray json(const Rules &rules, const Target &target, const QDateTime &at);

} // namespace omaseed::core::AgentDeny
