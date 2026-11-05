#include "core/AltPickWords.hpp"

#include "core/Format.hpp"

#include <QCoreApplication>
#include <QStringList>

namespace omaseed::core::AltPickWords {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AltPickWords", text);
}

QString limits(qint64 dl, qint64 up)
{
    if (dl < 0 || up < 0)
        return {};
    QStringList parts;
    if (dl > 0)
        parts << tr("↓ %1").arg(Format::speed(dl));
    if (up > 0)
        parts << tr("↑ %1").arg(Format::speed(up));
    return parts.isEmpty() ? tr("No limits") : parts.join(QLatin1Char(' '));
}

} // namespace

Notice notice(const QString &kind, qint64 dl, qint64 up, int minutes)
{
    if (kind == QLatin1String("burst")) {
        const QString summary =
            minutes > 0 ? tr("Burst for %1 min").arg(minutes) : tr("Burst until stopped");
        return {summary, limits(dl, up)};
    }
    if (kind == QLatin1String("turtle")) {
        QString body = limits(dl, up);
        if (dl == kQbtDefaultAltLimit && up == kQbtDefaultAltLimit)
            body += QLatin1Char('\n') +
                    tr("These are qBittorrent's defaults, never changed. Set your own in "
                       "Settings › qBittorrent › Speed.");
        return {tr("Turtle"), body};
    }
    return {tr("Normal speeds"), QString()};
}

} // namespace omaseed::core::AltPickWords
