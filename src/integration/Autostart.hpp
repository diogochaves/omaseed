#pragma once

// Autostart — "Start at login" (charter §2: an XDG autostart entry, opt-in).
//
// The setting is the file itself: `$XDG_CONFIG_HOME/autostart/<application id>.desktop`. No copy
// of it is kept anywhere else, so what Settings shows is what the session will do at the next
// login, even after the user removed or edited the file by hand. Omarchy runs the session under
// uwsm, whose xdg-desktop-autostart.target starts every entry in that folder.
//
// Three modes:
//   "off"     no entry (or one with Hidden=true, which the spec reads as removed)
//   "tray"    Exec=… --hidden: omaseed starts with no window, in the tray or the bar widget
//   "window"  Exec=… with no flag: omaseed opens its window as from the launcher
//
// The entry is written without DBusActivatable, so the autostart generator runs Exec as it is
// rather than activating the app over D-Bus (which would lose --hidden).

#include <QString>

namespace omaseed::integration::Autostart {

/// `--hidden`: start without showing the window.
QString hiddenFlag();

/// `$XDG_CONFIG_HOME/autostart` (`~/.config/autostart` when unset).
QString directory();
/// The entry's full path in directory().
QString entryPath();

/// "off", "tray" or "window", read from the entry on disk at `path`.
QString mode(const QString &path = entryPath());

/// Writes the entry for `mode` ("tray" or "window") at `path`, or removes it for "off" (anything
/// else reads as "off"). `program` is what Exec runs: see execProgram(). False when the file
/// could not be written or removed.
bool setMode(const QString &mode, const QString &program, const QString &path = entryPath());

/// What Exec should run for the binary at `binaryPath`: plain `omaseed` for an installed binary
/// (under /usr), so a package upgrade keeps working; the absolute path for anything else (a dev
/// build), quoted per the Desktop Entry spec when it needs it.
QString execProgram(const QString &binaryPath);

/// The whole entry, as setMode() writes it.
QByteArray entryContents(const QString &mode, const QString &program);

} // namespace omaseed::integration::Autostart
