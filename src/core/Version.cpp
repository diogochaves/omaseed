#include "core/Version.hpp"

#include "omaseed/core/BuildConfig.hpp"

#include <QtGlobal>

namespace omaseed::core {

QString version()
{
    return QStringLiteral(OMASEED_VERSION_STRING);
}

QString versionLine()
{
    return QStringLiteral("omaseed %1 (Qt %2)").arg(version(), QString::fromLatin1(qVersion()));
}

} // namespace omaseed::core
