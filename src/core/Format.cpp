#include "core/Format.hpp"

#include <QLocale>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>

namespace omaseed::core::Format {

namespace {

constexpr qint64 kInfiniteEta = 8640000; ///< qBittorrent's "no estimate" sentinel.
constexpr double kUnit = 1024.0;

const QString &dash()
{
    static const QString value = QStringLiteral("—");
    return value;
}

const QString &infinity()
{
    static const QString value = QStringLiteral("∞");
    return value;
}

/// Three significant digits for MB and up, whole numbers for B and KB, which is how every
/// figure of the spec prints a byte count.
QString scaled(qint64 bytes, const QString &suffix)
{
    if (bytes < 0)
        return dash();
    if (bytes == 0)
        return QStringLiteral("0");

    static const std::array<const char *, 6> units{"B", "KB", "MB", "GB", "TB", "PB"};

    double value = double(bytes);
    size_t unit = 0;
    while (value >= kUnit && unit + 1 < units.size()) {
        value /= kUnit;
        ++unit;
    }

    const int decimals = unit <= 1 ? 0 : (value < 10.0 ? 2 : (value < 100.0 ? 1 : 0));
    // Rounding can push 1023.7 KB to "1024 KB"; carry it into the next unit instead.
    if (unit + 1 < units.size() &&
        std::round(value * std::pow(10, decimals)) / std::pow(10, decimals) >= kUnit) {
        value /= kUnit;
        ++unit;
        return QStringLiteral("%1 %2%3").arg(
            QString::number(value, 'f', value < 10.0 ? 2 : (value < 100.0 ? 1 : 0)),
            QString::fromLatin1(units.at(unit)),
            suffix);
    }

    return QStringLiteral("%1 %2%3").arg(
        QString::number(value, 'f', decimals), QString::fromLatin1(units.at(unit)), suffix);
}

} // namespace

QString speed(qint64 bytesPerSecond)
{
    if (bytesPerSecond <= 0)
        return QStringLiteral("0");
    return scaled(bytesPerSecond, QStringLiteral("/s"));
}

QString limit(qint64 bytesPerSecond)
{
    return bytesPerSecond <= 0 ? QStringLiteral("∞") : speed(bytesPerSecond);
}

QString size(qint64 bytes)
{
    return scaled(bytes, QString());
}

QString duration(qint64 seconds)
{
    if (seconds < 0)
        return dash();
    if (seconds < 60)
        return QStringLiteral("%1 s").arg(seconds);
    if (seconds < 3600) {
        const qint64 minutes = seconds / 60;
        const qint64 rest = seconds % 60;
        return rest == 0 ? QStringLiteral("%1 min").arg(minutes)
                         : QStringLiteral("%1 min %2 s").arg(minutes).arg(rest);
    }
    if (seconds < 86400) {
        const qint64 hours = seconds / 3600;
        const qint64 minutes = (seconds % 3600) / 60;
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    }
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    return QStringLiteral("%1d %2h").arg(days).arg(hours);
}

QString eta(qint64 seconds)
{
    if (seconds >= kInfiniteEta)
        return infinity();
    if (seconds < 0)
        return dash();
    return duration(seconds);
}

QString etaShort(qint64 seconds)
{
    if (seconds >= kInfiniteEta)
        return infinity();
    if (seconds < 0)
        return dash();
    if (seconds < 60)
        return QStringLiteral("%1s").arg(seconds);
    if (seconds < 3600) {
        const qint64 minutes = seconds / 60;
        const qint64 rest = seconds % 60;
        return rest == 0
                   ? QStringLiteral("%1m").arg(minutes)
                   : QStringLiteral("%1m %2s").arg(minutes).arg(rest, 2, 10, QLatin1Char('0'));
    }
    if (seconds < 86400) {
        const qint64 hours = seconds / 3600;
        const qint64 minutes = (seconds % 3600) / 60;
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    }
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    return QStringLiteral("%1d %2h").arg(days).arg(hours);
}

QString ratio(double value)
{
    if (value < 0.0 || std::isnan(value))
        return QStringLiteral("0.00");
    if (value >= 9999.0)
        return infinity();
    return QString::number(value, 'f', 2);
}

QString percent(double fraction)
{
    return QStringLiteral("%1 %").arg(percentCompact(fraction).chopped(1));
}

QString percentCompact(double fraction)
{
    double value = fraction * 100.0;
    if (value <= 0.0 || std::isnan(value))
        return QStringLiteral("0%");
    if (value >= 100.0)
        return QStringLiteral("100%");
    // 99.9995 % renders as "100.0", which reads like a stray decimal on a finished item;
    // the spec's own figures print "100 %" there (spec §2 Fig. 2's folder rows).
    const QString rendered = QString::number(value, 'f', 1);
    if (rendered == QLatin1String("100.0"))
        return QStringLiteral("100%");
    return rendered + QLatin1Char('%');
}

QString relativeTime(const QDateTime &when)
{
    if (!when.isValid())
        return QString();

    const QDate today = QDate::currentDate();
    const QDate day = when.date();
    const QString clock = when.toString(QStringLiteral("HH:mm"));

    if (day == today)
        return QStringLiteral("today %1").arg(clock);
    if (day == today.addDays(-1))
        return QStringLiteral("yesterday %1").arg(clock);
    if (day.year() == today.year())
        return QLocale::c().toString(when, QStringLiteral("d MMM HH:mm"));
    return QLocale::c().toString(when, QStringLiteral("d MMM yyyy HH:mm"));
}

QString added(const QDateTime &when)
{
    const QString relative = relativeTime(when);
    return relative.isEmpty() ? QString() : QStringLiteral("added %1").arg(relative);
}

QString seedsPeers(int seeds, int peers)
{
    return QStringLiteral("%1 / %2").arg(seeds).arg(peers);
}

QString speedShort(qint64 bytesPerSecond)
{
    if (bytesPerSecond <= 0)
        return QStringLiteral("0");

    static const std::array<const char *, 5> units{"", "K", "M", "G", "T"};

    double value = double(bytesPerSecond);
    size_t unit = 0;
    while (value >= kUnit && unit + 1 < units.size()) {
        value /= kUnit;
        ++unit;
    }
    if (unit == 0)
        return QString::number(qint64(value));

    const int decimals = unit == 1 ? 0 : (value < 10.0 ? 2 : (value < 100.0 ? 1 : 0));
    return QStringLiteral("%1 %2").arg(QString::number(value, 'f', decimals),
                                       QString::fromLatin1(units.at(unit)));
}

QString count(qint64 value)
{
    // The spec's figures group with a plain space ("1 264", "1 204 total"), not with the
    // locale's separator: the window is monospaced and must read the same in every locale.
    const bool negative = value < 0;
    QString digits = QString::number(negative ? -value : value);
    for (int at = digits.size() - 3; at > 0; at -= 3)
        digits.insert(at, QLatin1Char(' '));
    return negative ? QLatin1Char('-') + digits : digits;
}

QString pieceLayout(int pieceCount, qint64 pieceSize)
{
    if (pieceCount <= 0 || pieceSize <= 0)
        return QString();

    static const std::array<const char *, 5> units{"B", "KiB", "MiB", "GiB", "TiB"};
    double value = double(pieceSize);
    size_t unit = 0;
    while (value >= kUnit && unit + 1 < units.size()) {
        value /= kUnit;
        ++unit;
    }
    const int decimals = value < 10.0 && value != std::floor(value) ? 1 : 0;
    const QString each = QStringLiteral("%1 %2").arg(QString::number(value, 'f', decimals),
                                                     QString::fromLatin1(units.at(unit)));

    return QStringLiteral("%1 × %2").arg(count(pieceCount), each);
}

QString piecesHave(int have, int pieceCount)
{
    if (pieceCount <= 0)
        return QString();
    return QStringLiteral("%1 of %2")
        .arg(count(std::clamp(have, 0, pieceCount)), count(pieceCount));
}

QString connectedTotal(int connected, int total)
{
    return QStringLiteral("%1 connected · %2 total")
        .arg(count(std::max(0, connected)), count(std::max(0, total)));
}

QString shareLimit(double ratioLimit, qint64 seedingTimeLimit)
{
    QStringList parts;
    if (ratioLimit <= -2.0 || seedingTimeLimit <= -2)
        parts.append(QStringLiteral("global"));
    if (ratioLimit >= 0.0)
        parts.append(QStringLiteral("ratio %1").arg(QString::number(ratioLimit, 'f', 1)));
    if (seedingTimeLimit >= 0)
        parts.append(QStringLiteral("seeding %1").arg(duration(seedingTimeLimit * 60)));
    if (parts.isEmpty())
        parts.append(QStringLiteral("unlimited"));
    return parts.join(QStringLiteral(" · "));
}

QString connections(int connected, int limit)
{
    return QStringLiteral("%1 of %2")
        .arg(count(std::max(0, connected)), limit > 0 ? count(limit) : infinity());
}

QString averageSpeeds(qint64 down, qint64 up)
{
    if (down < 0 && up < 0)
        return QString();
    const auto side = [](qint64 value) { return value < 0 ? dash() : speed(value); };
    return QStringLiteral("↓ %1 · ↑ %2").arg(side(down), side(up));
}

QString reannounceIn(qint64 seconds)
{
    if (seconds <= 0)
        return QString();
    return QStringLiteral("in %1").arg(duration(seconds));
}

QString shortMagnet(const QString &magnetUri)
{
    if (magnetUri.isEmpty())
        return QString();
    // Only the first `xt` survives; the name, trackers and web seeds are the long part.
    QString head = magnetUri.section(QLatin1Char('&'), 0, 0);
    static const QString btih = QStringLiteral("urn:btih:");
    static const QString btmh = QStringLiteral("urn:btmh:");
    qsizetype at = head.indexOf(btih);
    qsizetype prefix = btih.size();
    if (at < 0) {
        at = head.indexOf(btmh);
        prefix = btmh.size();
    }
    constexpr qsizetype kShown = 8;
    if (at >= 0 && head.size() > at + prefix + kShown)
        return head.left(at + prefix + kShown) + QStringLiteral("…");
    if (at < 0 && head.size() > 40)
        return head.left(40) + QStringLiteral("…");
    return head;
}

} // namespace omaseed::core::Format
