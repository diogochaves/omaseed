#include "core/SkippedOutlook.hpp"

#include "core/Format.hpp"

#include <QCoreApplication>

#include <algorithm>

namespace omaseed::core {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("SkippedOutlook", text);
}

} // namespace

SkippedOutlook SkippedOutlook::of(const SkippedReading &reading, HashVersion version)
{
    SkippedOutlook outlook;
    outlook.version = version;
    if (!reading.seen)
        return outlook;
    for (const SkippedFile &file : reading.files) {
        if (!file.holdsSpace())
            continue;
        // C17 D3, C29: the same test the plan's Pieces verdict makes.
        if (file.holdsPieces && version != HashVersion::V1) {
            outlook.readdOnly += file.allocated;
            ++outlook.readdFiles;
        } else {
            outlook.freeable += file.allocated;
        }
    }
    if (outlook.readdOnly > 0)
        outlook.kind = outlook.freeable > 0 ? Kind::Mixed : Kind::Readd;
    else if (outlook.freeable > 0)
        outlook.kind = Kind::Free;
    return outlook;
}

QString SkippedOutlook::kindName(Kind kind)
{
    switch (kind) {
    case Kind::Free:
        return QStringLiteral("free");
    case Kind::Mixed:
        return QStringLiteral("mixed");
    case Kind::Readd:
        return QStringLiteral("readd");
    case Kind::None:
        break;
    }
    return QStringLiteral("none");
}

QString SkippedOutlook::stillOnDisk(qint64 held)
{
    return held > 0 ? tr("%1 still on disk").arg(Format::size(held)) : QString();
}

QString SkippedOutlook::sizeTail(int skipped, qint64 held, bool seen, qint64 downloaded)
{
    if (seen && held > 0)
        return tr("%1 skipped, %2").arg(std::max(1, skipped)).arg(stillOnDisk(held));
    if (!seen && downloaded > 0)
        return tr("%1 of it in skipped files").arg(Format::size(downloaded));
    return QString();
}

} // namespace omaseed::core
