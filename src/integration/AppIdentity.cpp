#include "integration/AppIdentity.hpp"

namespace omaseed::integration::AppIdentity {

QString applicationId()
{
    return QStringLiteral("io.github.diogochaves.omaseed");
}

QString desktopFileName()
{
    return applicationId();
}

QString dbusServiceName()
{
    return applicationId();
}

QString dbusObjectPath()
{
    QString path = QLatin1Char('/') + applicationId();
    path.replace(QLatin1Char('.'), QLatin1Char('/'));
    return path;
}

QString applicationName()
{
    return QStringLiteral("omaseed");
}

QString organizationDomain()
{
    return QStringLiteral("io.github.diogochaves");
}

} // namespace omaseed::integration::AppIdentity
