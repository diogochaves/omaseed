#pragma once

// AgentGuide — the guide for coding agents (DECISIONS C45 V19, C37 U4). Its library part says
// how to ask the running omaseed (the words, what their JSON holds, the exit codes) and never
// to reach past it. `omaseed --skill` prints it; every primary start writes it into omaseed's
// config folder as AGENTS.md, with a CLAUDE.md that points at it, for an agent started there.
// Its skins part (#225, C37 U4) says how to make a skin, and skin.schema.json beside it holds
// every token, so a new token needs no prose.

#include <QString>

namespace omaseed::integration::AgentGuide {

/// The first line of the AGENTS.md omaseed writes. A file without it is someone else's.
QString header();

/// The guide as `--skill` prints it: the library part, a blank line, the skins part.
QString text();
/// The library part (`:/omaseed/agents/library.md`).
QString libraryPart();
/// The skins part (`:/omaseed/agents/skins.md`).
QString skinsPart();
/// skin.schema.json as written: `$comment` (header()) first, then SkinReader::schema().
QByteArray schemaBytes();

/// What writeInto did with one file.
enum class Wrote { Written, Same, LeftAlone, Failed };

struct WriteResult
{
    Wrote agents = Wrote::Failed;
    Wrote claude = Wrote::Failed;
    Wrote schema = Wrote::Failed;
};

/// Writes `dir`/AGENTS.md (header, then text()) when it is missing or differs, and
/// `dir`/CLAUDE.md ("@AGENTS.md") when it is missing, and `dir`/skin.schema.json when it is
/// missing or differs. An AGENTS.md without the header, a schema that doesn't start with
/// header() as its `$comment`, or a CLAUDE.md that says anything else, is left as it is and
/// the reason logged. A file that already says the same is not touched, so its mtime stays.
/// Creates `dir` when needed; every write is atomic.
WriteResult writeInto(const QString &dir);

} // namespace omaseed::integration::AgentGuide
