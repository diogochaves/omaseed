#pragma once

// AgentGuide — the guide for coding agents (DECISIONS C45 V19, C37 U4). Its library part says
// how to ask the running omaseed (the words, what their JSON holds, the exit codes) and never
// to reach past it. `omaseed --skill` prints it; every primary start writes it into omaseed's
// config folder as AGENTS.md, with a CLAUDE.md that points at it, for an agent started there.
// #225 adds the skins part after the library part.

#include <QString>

namespace omaseed::integration::AgentGuide {

/// The first line of the AGENTS.md omaseed writes. A file without it is someone else's.
QString header();

/// The guide as `--skill` prints it: the library part (`:/omaseed/agents/library.md`).
QString text();

/// What writeInto did with one file.
enum class Wrote { Written, Same, LeftAlone, Failed };

struct WriteResult
{
    Wrote agents = Wrote::Failed;
    Wrote claude = Wrote::Failed;
};

/// Writes `dir`/AGENTS.md (header, then text()) when it is missing or differs, and
/// `dir`/CLAUDE.md ("@AGENTS.md") when it is missing. An AGENTS.md without the header, or a
/// CLAUDE.md that says anything else, is left as it is and the reason logged. A file that
/// already says the same is not touched, so its mtime stays. Creates `dir` when needed.
WriteResult writeInto(const QString &dir);

} // namespace omaseed::integration::AgentGuide
