#pragma once

// AddRoute — where an add that comes from outside the window opens (docs/DECISIONS.md C35,
// board 54 §3). A pure decision, so its whole table is tested without a window or a clock;
// App::requestAdd feeds it what it knows and acts on the answer.
//
// Adds made inside the window (`a`, "+ add", the hero, a drop, search's add) never come here:
// they open the sheet in the window, as always (C35 W2).

#include <QString>
#include <QtGlobal>

#include <chrono>

namespace omaseed::app {

/// Where an add from outside came from.
enum class AddSource {
    Handoff, ///< a second launch's URIs (a browser's magnet, a file manager's .torrent)
    Startup, ///< the URIs this very process was launched with
    Menu,    ///< the shared menu's "add torrent…" (the tray, the widget's ⋯, the window's mark)
    Bus,     ///< the Status bus's OpenAddSheet (the widget's add, with or without sources)
};

/// Where it opens.
enum class AddRoute {
    Main,   ///< the add sheet in the main window, as before C35
    Window, ///< the add window, where the user is (C35 W1)
};

struct AddRouting
{
    AddRoute route = AddRoute::Window;
    /// The main window comes up for it (first run, and an add this process was launched
    /// with); every other route leaves the main window where it is.
    bool raise = false;
};

/// docs/DECISIONS.md C35 W2: how long after the main window lost focus an add from the menu
/// or the bus still counts as "the window had focus just before". A bar popup or Omarchy's
/// menu may take the keyboard while it is open, so the window's focus *at that moment* says
/// nothing; a hand-off counts only focus now. A guess until it is measured in a box (#218).
inline constexpr std::chrono::milliseconds kAddFocusGrace{10'000};

/// The decision. `mainActive`: the main window has focus now. `msSinceMainActive`: how long
/// ago it last stopped having focus, or a negative number when it never had it (ignored while
/// `mainActive`). `firstRun`: no server yet, and not a demo (C35 W6).
AddRouting routeAdd(AddSource source, bool firstRun, bool mainActive, qint64 msSinceMainActive);

/// "handoff", "startup", "menu", "bus": for the log.
QString addSourceName(AddSource source);

} // namespace omaseed::app
