#pragma once

// BarWidgetInstaller — where omaseed's bar widget comes from and how it reaches the shell
// (#17). The package ships the widget at `/usr/share/omaseed/plugin/`, but the Omarchy shell
// scans no system directory, only `~/.config/omarchy/plugins/`. So the app links
// `~/.config/omarchy/plugins/chaves.omaseed` to the packaged folder, the way `omabox setup`
// links its own widget.
//
// A link and not a copy (ruled 2026-10-02): every package upgrade updates the widget with the
// app, so the two never disagree about the status file (Q12: no compatibility). Omarchy's own
// update restarts the shell after the packages, which is when the shell reads the new widget.
// The shell follows a linked plugin folder (its scan globs `plugins/*/`, and `omarchy plugin
// remove` unlinks one); only links *inside* a plugin folder are refused, and the packaged
// folder has none.
//
// Pure file work, no processes: `BarWidget` runs the shell's commands around it.

#include <QString>

namespace omaseed::integration::BarWidgetInstaller {

/// The plugin's id in the shell, and its folder name: "chaves.omaseed" (Q8).
QString pluginId();

/// The plugin folder to link to, the first of these that holds a manifest.json:
///   1. $OMASEED_PLUGIN_DIR;
///   2. omaseed/plugin in each XDG data directory ($XDG_DATA_HOME, then $XDG_DATA_DIRS);
///   3. ../share/omaseed/plugin next to the executable (a prefix outside the data dirs);
///   4. `devSourceDir`, the source tree's plugin/ of a development build.
/// Empty when none does.
QString findSource(const QString &devSourceDir = QString());

/// "$XDG_CONFIG_HOME/omarchy/plugins/chaves.omaseed" (~/.config when it is unset).
QString targetDir();

/// "$XDG_CONFIG_HOME/omarchy/shell.json", where the shell keeps what is in the bar.
QString shellConfigPath();

/// The line that turns the widget on once it is linked.
QString enableCommand();

/// What is at `target` now.
enum class Presence {
    Absent,   ///< nothing
    Linked,   ///< a link (ours, or one a user made); `linkTarget()` says where
    Folder,   ///< a plain folder: a copy someone put there
    Checkout, ///< a folder with .git: `omarchy plugin add` made it, `omarchy plugin update` owns it
};
Presence presence(const QString &target);
/// Where the link at `target` points; empty when it is not a link.
QString linkTarget(const QString &target);

/// True when the shell config at `shellJson` loads the widget: where the shell itself looks
/// for a plugin's entry (PluginRegistry.findEntryLocation), the bar's `id`, an entry in any
/// of `bar.layout`'s sections (4.5 adds centerBegin, centerEnd and tray), or a line of
/// `plugins[]`. A container widget that hosts it (the
/// console's rail) lists it in its own settings *and* gives it a `plugins[]` line; `omarchy
/// plugin disable` drops only that line, so the container's list alone does not count.
bool isInBar(const QString &shellJson);

struct Result
{
    bool ok = false;
    /// The plugins folder changed. The shell reloads every plugin when it does, so a caller
    /// only asks it to rescan then.
    bool changed = false;
    /// What to tell the user, one or more lines.
    QString message;
};

/// Points `target` at `source`. Replaces a link or a plain folder (an older copy) whole, through
/// a hidden sibling name that is renamed into place, so the shell never sees half a plugin.
/// Refuses a git checkout, and a `source` without a manifest.
Result link(const QString &source, const QString &target);

/// Removes the link at `target`. Refuses anything that is not a link: a folder someone put
/// there is theirs (`omarchy plugin remove` takes it away).
Result unlink(const QString &target);

} // namespace omaseed::integration::BarWidgetInstaller
