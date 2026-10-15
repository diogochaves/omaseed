#include "core/SpeedSets.hpp"

namespace omaseed::core::SpeedSets {

int compare(qint64 normal, qint64 alt)
{
    const bool normalOpen = normal <= 0;
    const bool altOpen = alt <= 0;
    if (normalOpen && altOpen)
        return 0;
    if (altOpen)
        return 1;
    if (normalOpen)
        return -1;
    return alt > normal ? 1 : alt < normal ? -1 : 0;
}

Kind kind(qint64 dlLimit, qint64 upLimit, qint64 altDlLimit, qint64 altUpLimit)
{
    const int down = compare(dlLimit, altDlLimit);
    const int up = compare(upLimit, altUpLimit);
    const bool faster = down > 0 || up > 0;
    const bool slower = down < 0 || up < 0;
    if (faster && slower)
        return Kind::Mixed;
    if (faster)
        return Kind::Burst;
    if (slower)
        return Kind::Turtle;
    return Kind::Same;
}

QString word(Kind kind)
{
    switch (kind) {
    case Kind::Burst:
        return QStringLiteral("burst");
    case Kind::Turtle:
        return QStringLiteral("turtle");
    case Kind::Unknown:
    case Kind::Mixed:
    case Kind::Same:
        break;
    }
    return QStringLiteral("alt-speed");
}

QString key(Kind kind)
{
    switch (kind) {
    case Kind::Burst:
        return QStringLiteral("burst");
    case Kind::Turtle:
        return QStringLiteral("turtle");
    case Kind::Unknown:
    case Kind::Mixed:
    case Kind::Same:
        break;
    }
    return QStringLiteral("alt");
}

} // namespace omaseed::core::SpeedSets
