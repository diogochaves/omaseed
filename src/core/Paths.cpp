#include "core/Paths.hpp"

#include <QDir>

namespace omaseed::core::paths {

QString configHome()
{
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!xdg.isEmpty())
        return xdg;
    return QDir::homePath() + QStringLiteral("/.config");
}

QString configDir()
{
    return configHome() + QStringLiteral("/omaseed");
}

QString secretsDir()
{
    return configDir() + QStringLiteral("/secrets");
}

} // namespace omaseed::core::paths
