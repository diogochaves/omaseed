#pragma once

#include <QString>

namespace omaseed::integration {

/// The one place that knows what this application is called on the desktop bus, in the
/// .desktop file and in the icon theme. Everything else asks here.
namespace AppIdentity {

/// Reverse-DNS application id, used for D-Bus activation and the .desktop basename.
QString applicationId();
/// The value for QGuiApplication::setDesktopFileName().
QString desktopFileName();
/// The bus name this application owns for single-instance and URI hand-off.
QString dbusServiceName();
/// The org.freedesktop.Application object path derived from the application id.
QString dbusObjectPath();
/// Short name used for the binary, the config directory and QCoreApplication.
QString applicationName();
/// Organisation domain for QSettings and QCoreApplication.
QString organizationDomain();

} // namespace AppIdentity

} // namespace omaseed::integration
