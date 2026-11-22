#pragma once

// AgentPlan — an agent's plan for `omaseed do --plan FILE|-` (DECISIONS C50 GT4, GT5; board 67
// §4): the JSON the CLI read and sent as text (the app never opens a path an agent names, PLAN
// Q13), read and checked for its shape here, before the app checks each line as a `do` of its
// own (the gate, GT1's guard, GT2's find, the move's checks).
//
//     { "v": 1, "server": "attic", "lines": [
//       { "action": "recheck", "torrents": ["3f9a1c0e", "a20c55d1"] },
//       { "action": "locate", "torrents": ["71ad02bc"], "to": "/data/shelf-b/recordings" },
//       { "action": "move-back", "move": "0a1b2c3d" },
//       { "action": "remove", "torrents": ["9c40e7a2"] } ] }
//
// Torrents are hashes or 8 or more of their first characters, as `do` takes them (C45 V15),
// never names; `to` belongs to locate and move and they need it; `move` (a landed move's id) to
// move-back; pause and resume take no torrents. A word `do` never runs (C45 V14: delete,
// rename…) is kept as a line the app answers `never`, so the plan is refused, not malformed.
// Anything else is usage (exit 2). Pure Qt Core.

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace omaseed::core::AgentPlan {

struct Line
{
    QString action;
    QStringList torrents; ///< as given, lower-cased
    QString to;           ///< locate's and move's folder
    QString move;         ///< move-back's move id
};

struct Plan
{
    QString server; ///< "" when the plan names none
    QList<Line> lines;
    QString usage; ///< "" when the plan reads; else the usage words (exit 2)
};

/// The plan's text as the CLI sent it.
Plan parse(const QString &text);

/// One line as Do's options take it (C45 V3): {action, torrents, to, move, server, dryRun}.
QJsonObject options(const Line &line, const QString &server, bool dryRun);

/// The ask's groups (GT4 "grouped by action and folder"): lines that ask group by action and
/// folder, each torrent a box of its own; lines the setting already allows group by action
/// alone, their folders said in the group's line. In the order each group first appears.
struct Group
{
    QString action;
    QString to; ///< the asking group's folder; "" for an allowed group
    bool asks = false;
    QList<int> lines; ///< indexes into the plan's lines
};
QList<Group> groups(const QList<Line> &lines, const QList<bool> &asks);

/// "Recheck", "Move back": the action as a group's head starts it.
QString actionTitle(const QString &action);

/// The plan's last line (board 67 §6): "Done: 34 of 35 · by an agent".
QString outcome(int done, int of);

} // namespace omaseed::core::AgentPlan
