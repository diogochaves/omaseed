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
/// Object path of the theme control object (the theme-set hook pokes it).
QString dbusThemeObjectPath();
/// Interface name of the theme control object.
QString dbusThemeInterface();
/// Short name used for the binary, the config directory and QCoreApplication.
QString applicationName();
/// The name people see: launcher entries, notifications. Capitalised as Omarchy's apps are;
/// the in-app wordmark stays lowercase (v2).
QString displayName();
/// Organisation domain for QSettings and QCoreApplication.
QString organizationDomain();

} // namespace AppIdentity

} // namespace omaseed::integration
