#pragma once

// The words MoveController's two halves share (MoveController.cpp: the job;
// MoveControllerSheet.cpp: the sheet). Internal to them.

#include "ui/app/MoveController.hpp"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>

namespace omaseed::app::movewords {

/// "about 50 min left", "about 2 h 10 min left", "" when it can't tell.
inline QString timeLeft(qint64 seconds)
{
    if (seconds < 0)
        return {};
    const qint64 minutes = std::max<qint64>(1, (seconds + 59) / 60);
    if (minutes < 90)
        return MoveController::tr("about %1 min left").arg(minutes);
    return MoveController::tr("about %1 h %2 min left").arg(minutes / 60).arg(minutes % 60);
}

/// "1 h 52 min", "14 min", "40 s".
inline QString span(qint64 seconds)
{
    if (seconds < 60)
        return MoveController::tr("%1 s").arg(std::max<qint64>(0, seconds));
    const qint64 minutes = seconds / 60;
    if (minutes < 60)
        return MoveController::tr("%1 min").arg(minutes);
    return MoveController::tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

/// `one` or `many` with `n` for its %1 (the codebase's plural idiom: English, no .qm).
inline QString counted(int n, const QString &one, const QString &many)
{
    return (n == 1 ? one : many).arg(n);
}

inline QVariantMap action(const QString &id, const QString &text)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("text"), text}};
}

inline QVariantMap check(const QString &kind,
                         const QString &text,
                         const QString &note = QString(),
                         const QStringList &hashes = {})
{
    // Each line is green (fine), yellow (fine, but know it), red (something is left out) or
    // dim (a fact): S1.
    static const QHash<QString, QString> glyphs{{QStringLiteral("ok"), QStringLiteral("✓")},
                                                {QStringLiteral("warn"), QStringLiteral("!")},
                                                {QStringLiteral("bad"), QStringLiteral("✕")},
                                                {QStringLiteral("unknown"), QStringLiteral("?")},
                                                {QStringLiteral("fact"), QStringLiteral("·")},
                                                {QStringLiteral("busy"), QStringLiteral("…")},
                                                {QStringLiteral("loose"), QStringLiteral("▱")}};
    return {{QStringLiteral("kind"), kind},
            {QStringLiteral("glyph"), glyphs.value(kind)},
            {QStringLiteral("text"), text},
            {QStringLiteral("note"), note},
            {QStringLiteral("hashes"), hashes}};
}

} // namespace omaseed::app::movewords
