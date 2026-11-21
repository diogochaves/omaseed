#pragma once

// AgentGuide — the guide for coding agents, generated from omaseed's skill (DECISIONS C48,
// amending C37 U4 and C45 V19). The skill is data/agents/skill/: SKILL.md, the map (the reads,
// the never list, the skin loop), and the pages in ref/ (library, skins, files), compiled in
// here and installed as is. `omaseed --skill` prints the map with each `ref: X.md` written as
// `omaseed skill X`; every primary start writes that same map into omaseed's config folder as
// AGENTS.md, with a CLAUDE.md that points at it and skin.schema.json, which holds every token.

#include <QMap>
#include <QString>
#include <QStringList>

namespace omaseed::integration::AgentGuide {

/// The first line of the AGENTS.md omaseed writes. A file without it is someone else's.
QString header();

/// SKILL.md's frontmatter fields (name, description, allowed-tools), unquoted.
QMap<QString, QString> frontmatter();
/// SKILL.md's body: everything after the frontmatter, leading blank lines cut.
QString map();
/// The pages in the skill's ref/ folder, by name (library, skins, files; C48). A new page is a
/// file there and nothing else (C49's layouts).
QStringList pages();
/// The page `name` (ref/`name`.md), or empty for a name that isn't one of pages().
QString page(const QString &name);
/// map() as printed and written to AGENTS.md: each `ref: X.md` as `omaseed skill X`, and the
/// line about ref/ said for a terminal (docs/65 §4). No `ref:` and no "beside this file" left.
QString printedMap();
/// The last line of a printed map: "Pages: omaseed skill library, skins, files."
QString pagesLine();

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

/// Writes `dir`/AGENTS.md (header, then printedMap()) when it is missing or differs, and
/// `dir`/CLAUDE.md ("@AGENTS.md") when it is missing, and `dir`/skin.schema.json when it is
/// missing or differs. An AGENTS.md without the header, a schema that doesn't start with
/// header() as its `$comment`, or a CLAUDE.md that says anything else, is left as it is and
/// the reason logged. A file that already says the same is not touched, so its mtime stays.
/// Creates `dir` when needed; every write is atomic.
WriteResult writeInto(const QString &dir);

} // namespace omaseed::integration::AgentGuide
