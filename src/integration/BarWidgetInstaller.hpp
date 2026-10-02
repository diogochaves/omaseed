#pragma once

// BarWidgetInstaller — `omaseed --install-bar-widget` (#17, docs/07 §6): copies the bar widget
// the package ships (`/usr/share/omaseed/plugin/`) into the one place the Omarchy shell looks
// for third-party plugins, `~/.config/omarchy/plugins/chaves.omaseed/`. The shell scans no
// system directory, so a package cannot install the widget where it would be found.
//
// A real copy, never a symlink: the shell's plugin registry refuses symlinks inside a plugin
// folder. An older copy is replaced whole. A copy that `omarchy plugin add` made (a git
// checkout) is left alone: `omarchy plugin update` owns it.

#include <QString>

namespace omaseed::integration::BarWidgetInstaller {

/// The plugin's id in the shell, and its folder name: "chaves.omaseed" (Q8).
QString pluginId();

/// The plugin folder to copy, the first of these that holds a manifest.json:
///   1. $OMASEED_PLUGIN_DIR;
///   2. omaseed/plugin in each XDG data directory ($XDG_DATA_HOME, then $XDG_DATA_DIRS);
///   3. ../share/omaseed/plugin next to the executable (a prefix outside the data dirs);
///   4. `devSourceDir`, the source tree's plugin/ of a development build.
/// Empty when none does.
QString findSource(const QString &devSourceDir = QString());

/// "$XDG_CONFIG_HOME/omarchy/plugins/chaves.omaseed" (~/.config when it is unset).
QString targetDir();

/// The line that turns the widget on once it is copied.
QString enableCommand();

struct Result
{
    bool ok = false;
    /// What to tell the user, one or more lines, ending with the enable line on success.
    QString message;
};

/// Copies every regular file under `source` into `target` (files 0644, folders 0755), through
/// a sibling folder that replaces `target` only once the copy is complete. Symlinks in
/// `source` are copied as the files they point at. Refuses a `target` that is a git checkout.
Result install(const QString &source, const QString &target);

} // namespace omaseed::integration::BarWidgetInstaller
