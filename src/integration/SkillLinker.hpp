#pragma once

// SkillLinker — omaseed's skill linked where coding agents look for skills (DECISIONS C48
// AS6–AS8, AS10; docs/65 §5). `omaseed skill link` puts a link named `omaseed` in each agent's
// skill folder, pointing at the installed skill (/usr/share/omaseed/agents/skill) or a build's
// (data/agents/skill); `unlink` takes out only those links; `status` says what each folder holds.
//
// The folders are Omarchy's own list for its skills (omarchy-provision-user): the shared
// ~/.agents/skills and Claude Code's always (made when missing), Codex, pi, Gemini and Hermes
// (and each Hermes profile) only when that agent is installed. Under $HOME, read on every call
// (QDir::homePath()), so a test or a box sets HOME and nothing else is touched.
//
// What is never written (C48): a folder, a file or someone else's link named `omaseed` (it is
// the person's, left and named); a folder this made (unlink keeps it); the skill itself (the
// installed one is root's). Nothing here runs at start or from the package (AS8 a): only the
// word and the Settings row call it. Pure file work, no Qt GUI.

#include <QList>
#include <QString>
#include <QStringList>

namespace omaseed::integration::SkillLinker {

/// The skill folder to link to, the first of these that holds a SKILL.md:
///   1. $OMASEED_SKILL_DIR;
///   2. omaseed/agents/skill in each XDG data directory ($XDG_DATA_HOME, then $XDG_DATA_DIRS);
///   3. ../share/omaseed/agents/skill next to the executable (a prefix outside the data dirs);
///   4. `devSourceDir`, the source tree's data/agents/skill of a development build.
/// Empty when none does.
QString findSource(const QString &devSourceDir = QString());

/// One agent's skill folder, from Omarchy's list.
struct Folder
{
    QString agent;  ///< "the shared ~/.agents", "Claude Code", … "Hermes profile NAME"
    QString path;   ///< the skills folder, absolute ("$HOME/.claude/skills")
    bool always;    ///< linked even when missing (~/.agents, ~/.claude)
    bool installed; ///< always, or the agent's own folder is there
    QString link() const { return path + QStringLiteral("/omaseed"); }
};
/// Every folder of the list, installed or not, in its order.
QList<Folder> folders();

/// True when the entry at `linkPath` is a link omaseed made: its link text (not what it points
/// at, so a dead link after a package removal is still ours) ends in /omaseed/agents/skill or
/// /data/agents/skill.
bool isOurs(const QString &linkPath);

/// What one folder holds, or what link did there.
enum class State {
    Linked,  ///< ours, and what it points at is there
    Dead,    ///< ours, and what it points at is gone
    Yours,   ///< a folder, a file or a link elsewhere: the person's
    Absent,  ///< nothing named omaseed there
    Skipped, ///< the agent isn't installed
    Failed,  ///< link could not make the link (why says why)
};
QString stateName(State state);

struct FolderState
{
    QString agent;
    QString path; ///< the `omaseed` entry in the folder
    State state = State::Absent;
    QString target;    ///< the link text, empty when it isn't a link
    bool copy = false; ///< Yours, and a folder holding a SKILL.md named omaseed (a copy)
    QString why;       ///< Yours or Failed: in words
};

struct Report
{
    QString source; ///< the skill found, or empty
    QList<FolderState> folders;
    QStringList changed; ///< link and unlink: the links made, moved or removed
    /// Empty when the word is done, else "not-written" and `message`.
    QString code;
    QString message;
};

/// Each folder's state; `source` is said, not used.
Report status(const QString &source);
/// True when at least one folder holds a live link of ours.
bool anyLinked();
/// Links `source` into every installed folder: where nothing is, and over a link of ours
/// (pointed at `source` now: a build's moves to the installed one and back). Anything else is
/// left. Then writes omaseed's guide files into its config folder (AgentGuide::writeInto, AS6),
/// so the schema the skill names is there before omaseed's first start. Fails "not-written"
/// when `source` is empty or no folder holds the link afterwards.
Report link(const QString &source);
/// Removes the links of ours from every folder of the list, installed or not. Never a folder.
Report unlink(const QString &source);

/// The guide as `omaseed skill` and `--skill` print it (C48, docs/65 §4): the printed map, the
/// pages line and, only while no folder is linked, the line that says how to link it.
/// AGENTS.md never carries that line (it would be rewritten on every link and unlink).
QString printedGuide();
/// "Coding agents load this by themselves once it is linked: omaseed skill link."
QString notLinkedLine();

} // namespace omaseed::integration::SkillLinker
