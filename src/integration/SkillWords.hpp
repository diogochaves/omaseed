#pragma once

// SkillWords — the answers of `omaseed skill link | unlink | status` (DECISIONS C48 AS6–AS8,
// docs/65 §5; C45 V4, V12): one JSON object in the words' envelope with no `omaseed` key (no
// running omaseed answered), and the lines made from the same report.

#include "integration/SkillLinker.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace omaseed::integration::SkillWords {

/// {v, command, at, source, folders[] {agent, path, state, target, …}} and, for link and
/// unlink, changed[] (the paths made, moved or removed).
QByteArray json(const QString &command, const QDateTime &at, const SkillLinker::Report &report);

/// The lines for a terminal, paths under `home` written with "~" (docs/65 §5).
QString lines(const QString &command, const SkillLinker::Report &report, const QString &home);

} // namespace omaseed::integration::SkillWords
