pragma Singleton
import QtQuick

// The one table of keys (spec v2 §7 Fig. 26, "the same map as v1"). Everything that binds a
// key in omaseed reads it from here: `MainView` routes presses through it, `HintModel`
// derives the footer's hints from it, and `HelpSheet` draws the `?` sheet from it. Nothing
// else may invent a window key. A menu's own row letters (Fig. 11's actions menu, the Files
// tab's menus) are part of that menu's rows and live with it.
//
// Every entry is plain data:
//   area      where the key applies: "global" (anywhere), "table", "rail", "details",
//             "columns" (the columns palette), "search", "addFiles" (the add sheet's
//             file tree), "addDownloads" (its "in Downloads" list). A key event reaches the focused view first (Qt propagates an
//             unaccepted key up the parent chain), so an area entry always wins over the
//             "global" entry for the same key — that is how `a` adds a tracker inside the
//             details pane and a torrent everywhere else.
//   key       the key as the spec writes it: "j", "G", "ctrl+q", "esc", "enter", "space",
//             "0–8", "1–4" (en dashes, like the figure).
//   action    the verb `MainView` switches on.
//   label     the short word the footer hints print after the key.
//   column    the help-sheet section: "navigate", "select", "act", "view", "app" — set
//             only on the entry that leads a sheet row; "" on the rest.
//   rank      the row's place inside its section.
//   helpKeys  the row's key chip ("p P", "C t m L r"); "" when the entry leads no row.
//   help      the row's short gloss ("stop/start · force"); "" when it leads no row.
//   about     on a row's entry only: the sentence the sheet prints beside the keys
//             (docs/08 §4, layout A, after Strata's shape).
//
// The command palette (`:`, docs/30, ruled in docs/DECISIONS.md C1) lists the entries that
// are commands, from `commandRows()`. Those entries carry more fields:
//   title     what `enter` does, as the palette's row prints it ("force start"); the
//             footer's `label` is too short out of context (D6). An entry with no title (or
//             `titles`) is a movement or a gesture, never a palette row.
//   titleOn   the title while `state` is on ("hide details" rather than "show details").
//   titles    for the details pane's shared keys: a title per tab ("files", "trackers",
//             "peers", "webseeds"); the entry is a command in those tabs only. `titlesOn`
//             likewise.
//   state     the context's `states` key the row reads: on/off picks the title; a word
//             ("right") is added to the second line as "now right".
//   needs     what the row needs to run; `whyNot()` is the rule (docs/30 §5): "" always,
//             "torrents" a selection or a cursor row, "cursor" a cursor row, "stopped" /
//             "running" one target to start / stop, "selection" one torrent selected,
//             "details" the pane on show, and in a details tab "marked" (marked rows or the
//             cursor's), "row" (the cursor's row), "rows" (any row), "file" (a file, not a
//             folder, under the cursor).
//   via       a menu-only action has no key ("key": ""): the path to it ("enter s",
//             "ctrl+."), printed in the key's place (D3). Key routing, the footer and the `?`
//             sheet never see an entry with no key.
//   sharesKey start and stop have no key of their own: `p` runs one or the other (D5), and
//             the palette prints it on the one it would run now.
QtObject {
    id: root

    // In this order the help sheet splits them into two columns of about equal height:
    // move, select, view | act on torrents, app.
    readonly property var columns: ["navigate", "select", "view", "act", "app"]
    /// Each help-sheet section's heading and one-line description (docs/08 §4, layout A).
    readonly property var columnTitles: ({
                                             "navigate": qsTr("move"),
                                             "select": qsTr("select"),
                                             "act": qsTr("act on torrents"),
                                             "view": qsTr("view"),
                                             "app": qsTr("app")
                                         })
    readonly property var columnAbout: ({
                                            "navigate": qsTr("Get around the window without the mouse."),
                                            "select": qsTr("Most actions apply to the selection, or to the cursor row when nothing is selected."),
                                            "act": qsTr("On the selection, or the cursor row. Open folder and rename take the cursor row."),
                                            "view": qsTr("What the window shows."),
                                            "app": ""
                                        })

    readonly property var entries: [
        // global: routed by MainView wherever the focus is -------------------------
        // #24 (docs/22): the window's second face.
        {
            "area": "global",
            "key": "`",
            "action": "toggleConsole",
            "title": qsTr("seed console"),
            "titleOn": qsTr("back to the library"),
            "state": "consoleShown",
            "needs": "",
            "label": qsTr("seed console"),
            "column": "view",
            "rank": 0,
            "helpKeys": "`",
            "help": qsTr("seed console"),
            "about": qsTr("Flip between the library and the seed console; with a selection, the console counts only it")
        },
        // console: MainView routes these while the seed console is on show (#24)
        {
            "area": "console",
            "key": "[",
            "action": "consoleShorter",
            "title": qsTr("shorter range"),
            "needs": "",
            "label": qsTr("range"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": "",
            "about": ""
        },
        {
            "area": "console",
            "key": "]",
            "action": "consoleLonger",
            "title": qsTr("longer range"),
            "needs": "",
            "label": qsTr("range"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": "",
            "about": ""
        },
        {
            "area": "console",
            "key": "j",
            "action": "consoleScroll",
            "label": qsTr("scroll"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": "",
            "about": ""
        },
        {
            "area": "console",
            "key": "esc",
            "action": "consoleEscape",
            "title": qsTr("back to the library"),
            "titleOn": qsTr("clear the scope"),
            "state": "consoleScoped",
            "needs": "",
            "label": qsTr("clear scope"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": "",
            "about": ""
        },
        {
            "area": "global",
            "key": "/",
            "action": "filter",
            "title": qsTr("filter the table"),
            "needs": "",
            "label": qsTr("filter"),
            "column": "navigate",
            "rank": 4,
            "helpKeys": "/",
            "help": qsTr("filter"),
            "about": qsTr("Filter the table by name, category, tag or hash")
        },
        {
            "area": "global",
            "key": "i",
            "action": "toggleDetails",
            "title": qsTr("show details"),
            "titleOn": qsTr("hide details"),
            "state": "detailsShown",
            "needs": "",
            "label": qsTr("details"),
            "column": "view",
            "rank": 1,
            "helpKeys": "i",
            "help": qsTr("details"),
            "about": qsTr("Show or hide the details panel")
        },
        // docs/10 §5 (B1.7): where Details sits, and how big it is.
        {
            "area": "global",
            "key": "I",
            "action": "cycleDetailsPlacement",
            "title": qsTr("place details: right · bottom · auto"),
            "state": "placement",
            "needs": "",
            "label": qsTr("place"),
            "column": "view",
            "rank": 6,
            "helpKeys": "I",
            "help": qsTr("details right · bottom · auto"),
            "about": qsTr("Move the details panel: right, bottom, or by window width")
        },
        {
            "area": "global",
            "key": "<",
            "action": "detailsSmaller",
            "title": qsTr("details smaller"),
            "needs": "details",
            "label": qsTr("smaller"),
            "column": "view",
            "rank": 7,
            "helpKeys": "< >",
            "help": qsTr("details smaller · larger"),
            "about": qsTr("Resize the details panel; its divider also drags")
        },
        {
            "area": "global",
            "key": ">",
            "action": "detailsLarger",
            "title": qsTr("details larger"),
            "needs": "details",
            "label": qsTr("larger"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "global",
            "key": "ctrl+k",
            "action": "jump",
            "title": qsTr("jump to a torrent"),
            "needs": "",
            "label": qsTr("jump"),
            "column": "navigate",
            "rank": 6,
            "helpKeys": "ctrl+k",
            "help": qsTr("jump"),
            "about": qsTr("Jump to any torrent, category or tag by name")
        },
        {
            "area": "global",
            "key": "esc",
            "action": "escape",
            "label": qsTr("clear"),
            "column": "select",
            "rank": 4,
            "helpKeys": "esc",
            "help": qsTr("clear, then filter, then close"),
            "about": qsTr("Clear the selection, then the filter, then close")
        },
        {
            "area": "global",
            "key": "a",
            "action": "add",
            "title": qsTr("add a torrent"),
            "needs": "",
            "label": qsTr("add"),
            "column": "act",
            "rank": 1,
            "helpKeys": "a",
            "help": qsTr("add"),
            "about": qsTr("Add a torrent: magnet, file or URL")
        },
        {
            "area": "global",
            "key": "s",
            "action": "sort",
            "title": qsTr("sort"),
            "needs": "",
            "label": qsTr("sort"),
            "column": "view",
            "rank": 3,
            "helpKeys": "s",
            "help": qsTr("sort"),
            "about": qsTr("Sort by name, size, progress, speed, ratio, eta or date added; a click on a header sorts by any column")
        },
        {
            "area": "global",
            "key": "A",
            "action": "altSpeed",
            "title": qsTr("alt-speed on"),
            "titleOn": qsTr("alt-speed off"),
            "state": "altSpeed",
            "needs": "",
            "label": qsTr("alt-speed"),
            "column": "app",
            "rank": 1,
            "helpKeys": "A",
            "help": qsTr("alt-speed"),
            "about": qsTr("Toggle the alternative speed limits")
        },
        {
            "area": "global",
            "key": ",",
            "action": "settings",
            "title": qsTr("settings"),
            "needs": "",
            "label": qsTr("settings"),
            "column": "app",
            "rank": 2,
            "helpKeys": ", ctrl+,",
            "help": qsTr("settings"),
            "about": qsTr("Settings")
        },
        // `ctrl+,` is where the rest of the desktop keeps settings (GTK, macOS, VS Code,
        // Strata), so it opens them here too; bare `,` stays the keyboard-first key. Fig. 26
        // gave `ctrl+,` to Columns, which moved to `|` (docs/08 §4, REVIEW §9).
        {
            "area": "global",
            "key": "ctrl+,",
            "action": "settings",
            "label": qsTr("settings"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        // docs/16 §2: Settings, straight on the qBittorrent group.
        {
            "area": "global",
            "key": "ctrl+shift+,",
            "action": "qbtSettings",
            "title": qsTr("qBittorrent settings"),
            "needs": "",
            "label": qsTr("qBittorrent settings"),
            "column": "app",
            "rank": 2,
            "helpKeys": "ctrl+shift+,",
            "help": qsTr("qBittorrent settings"),
            "about": qsTr("Settings, open on the server's own: speed, discovery, port, seeding, queue…")
        },
        // docs/08 §5: hide or show the rail (the sidebar key of most editors).
        {
            "area": "global",
            "key": "ctrl+b",
            "action": "toggleRail",
            "title": qsTr("show the rail"),
            "titleOn": qsTr("hide the rail"),
            "state": "railShown",
            "needs": "",
            "label": qsTr("rail"),
            "column": "view",
            "rank": 5,
            "helpKeys": "ctrl+b",
            "help": qsTr("rail"),
            "about": qsTr("Hide or show the rail")
        },
        // The hero's ⋯ menu (docs/08 §3).
        {
            "area": "global",
            "key": "ctrl+.",
            "action": "more",
            "label": qsTr("more"),
            "column": "app",
            "rank": 3,
            "helpKeys": "ctrl+.",
            "help": qsTr("more actions"),
            "about": qsTr("The ⋯ menu: speed limits, columns, select all")
        },
        {
            "area": "global",
            "key": "|",
            "action": "columns",
            "title": qsTr("columns"),
            "needs": "",
            "label": qsTr("columns"),
            "column": "view",
            "rank": 4,
            "helpKeys": "|",
            "help": qsTr("columns"),
            "about": qsTr("Pick and order the table's columns")
        },
        {
            "area": "global",
            "key": "q",
            "action": "hide",
            "title": qsTr("hide the window"),
            "needs": "",
            "label": qsTr("hide"),
            "column": "app",
            "rank": 4,
            "helpKeys": "q",
            "help": qsTr("hide to tray"),
            "about": qsTr("Hide to the tray")
        },
        {
            "area": "global",
            "key": "ctrl+q",
            "action": "quit",
            "title": qsTr("quit"),
            "needs": "",
            "label": qsTr("quit"),
            "column": "app",
            "rank": 5,
            "helpKeys": "ctrl+q",
            "help": qsTr("quit"),
            "about": qsTr("Quit")
        },
        // Not bound by omaseed: the figure teaches the Hyprland bind that raises it.
        {
            "area": "global",
            "key": "super+t",
            "action": "hyprlandBind",
            "label": qsTr("omaseed"),
            "column": "app",
            "rank": 6,
            "helpKeys": "super+t",
            "help": qsTr("suggested Hyprland bind"),
            "about": qsTr("Bring omaseed forward: a bind for your own Hyprland config")
        },
        // The sheet's own key. Fig. 26 does not list it inside the sheet (it is the footer's
        // pinned hint), so it leads no row.
        {
            "area": "global",
            "key": "?",
            "action": "help",
            "title": qsTr("shortcuts"),
            "needs": "",
            "label": qsTr("help"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        // F1 opens the same sheet. `?` is the keyboard-first convention this app is built
        // on and stays the key the footer teaches; F1 is what the desktop at large uses
        // (GTK/GNOME, and Strata next door), and it costs one entry to stop the question
        // being asked. Omarchy itself has no in-app help key to follow: its own binding,
        // SUPER+K, opens a global Hyprland cheat sheet, not an application's.
        {
            "area": "global",
            "key": "f1",
            "action": "help",
            "label": qsTr("help"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        // docs/30 D1 a (DECISIONS C1): the command palette. `:` is vim's command line, one
        // symbol key like `/`, `?` and `` ` ``; `ctrl+shift+p` is what editor users try first,
        // and works from a field too. It is no row of itself.
        {
            "area": "global",
            "key": ":",
            "action": "commands",
            "label": qsTr("commands"),
            "column": "app",
            "rank": 0.5,
            "helpKeys": ": ctrl+shift+p",
            "help": qsTr("commands"),
            "about": qsTr("Find any command by name and run it; each row teaches its key")
        },
        {
            "area": "global",
            "key": "ctrl+shift+p",
            "action": "commands",
            "label": qsTr("commands"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        // docs/30 D3: the ⋯ menu's two limit sheets have no key; the palette lists them with
        // the path to them.
        {
            "area": "global",
            "key": "",
            "via": "ctrl+.",
            "action": "globalLimits",
            "title": qsTr("speed limits…"),
            "needs": "",
            "label": qsTr("speed limits"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "global",
            "key": "",
            "via": "ctrl+.",
            "action": "altLimits",
            "title": qsTr("alt-speed limits…"),
            "needs": "",
            "label": qsTr("alt-speed limits"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },

        // table ---------------------------------------------------------------------
        {
            "area": "table",
            "key": "j",
            "action": "moveDown",
            "label": qsTr("move"),
            "column": "navigate",
            "rank": 1,
            "helpKeys": "j k",
            "help": qsTr("move"),
            "about": qsTr("Move the cursor down and up")
        },
        {
            "area": "table",
            "key": "k",
            "action": "moveUp",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "g",
            "action": "first",
            "label": qsTr("first"),
            "column": "navigate",
            "rank": 2,
            "helpKeys": "g G",
            "help": qsTr("first · last"),
            "about": qsTr("Jump to the first or the last torrent")
        },
        {
            "area": "table",
            "key": "G",
            "action": "last",
            "label": qsTr("last"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "h",
            "action": "focusRail",
            "label": qsTr("rail"),
            "column": "navigate",
            "rank": 3,
            "helpKeys": "h l",
            "help": qsTr("rail · table · details"),
            "about": qsTr("Move focus between the rail, the table and the details")
        },
        // docs/10 §4: when the chosen columns are wider than the table, it scrolls sideways.
        {
            "area": "table",
            "key": "ctrl+h",
            "action": "scrollLeft",
            "label": qsTr("scroll"),
            "column": "navigate",
            "rank": 7,
            "helpKeys": "ctrl+h ctrl+l",
            "help": qsTr("scroll sideways"),
            "about": qsTr("Scroll the table left and right when its columns don't fit")
        },
        {
            "area": "table",
            "key": "ctrl+l",
            "action": "scrollRight",
            "label": qsTr("scroll"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "l",
            "action": "focusDetails",
            "label": qsTr("details"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "space",
            "action": "toggleSelection",
            "label": qsTr("select"),
            "column": "select",
            "rank": 1,
            "helpKeys": "space",
            "help": qsTr("toggle"),
            "about": qsTr("Add or remove the cursor row")
        },
        {
            "area": "table",
            "key": "v",
            "action": "selectRange",
            "label": qsTr("range"),
            "column": "select",
            "rank": 2,
            "helpKeys": "v",
            "help": qsTr("range"),
            "about": qsTr("Select a range from the last row you marked")
        },
        {
            "area": "table",
            "key": "ctrl+a",
            "action": "selectAll",
            "title": qsTr("select all visible"),
            "needs": "",
            "label": qsTr("all"),
            "column": "select",
            "rank": 3,
            "helpKeys": "ctrl+a",
            "help": qsTr("all visible"),
            "about": qsTr("Select every torrent the filter shows; in the Files tab, every file it shows; in Trackers, Peers and HTTP sources, every row")
        },
        {
            "area": "table",
            "key": "esc",
            "action": "clearSelection",
            "title": qsTr("clear the selection"),
            "needs": "selection",
            "label": qsTr("clear"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "enter",
            "action": "actions",
            "label": qsTr("actions"),
            "column": "act",
            "rank": 2,
            "helpKeys": "enter",
            "help": qsTr("actions"),
            "about": qsTr("Open the actions menu for the selection")
        },
        {
            "area": "table",
            "key": "p",
            "action": "stopStart",
            "label": qsTr("stop/start"),
            "column": "act",
            "rank": 3,
            "helpKeys": "p P",
            "help": qsTr("stop/start · force"),
            "about": qsTr("Stop or start · force start, skipping the queue")
        },
        // docs/30 D5: start and stop as two commands on the whole selection, as the selection
        // bar's two buttons; `p` runs one of them, the one the first target calls for.
        {
            "area": "table",
            "key": "",
            "sharesKey": "p",
            "action": "start",
            "title": qsTr("start"),
            "needs": "stopped",
            "label": qsTr("start"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "",
            "sharesKey": "p",
            "action": "stop",
            "title": qsTr("stop"),
            "needs": "running",
            "label": qsTr("stop"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        // docs/30 D3: the actions menu's option rows (docs/12 G4), whose letters are the menu's
        // own: no window key, the path to them instead. On or off follows the cursor torrent.
        {
            "area": "table",
            "key": "",
            "via": "enter s",
            "action": "sequential",
            "title": qsTr("sequential download on"),
            "titleOn": qsTr("sequential download off"),
            "state": "seqDl",
            "needs": "torrents",
            "label": qsTr("sequential"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "",
            "via": "enter f",
            "action": "firstLast",
            "title": qsTr("first / last pieces first on"),
            "titleOn": qsTr("first / last pieces first off"),
            "state": "firstLast",
            "needs": "torrents",
            "label": qsTr("first / last"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "",
            "via": "enter S",
            "action": "superSeeding",
            "title": qsTr("super seeding on"),
            "titleOn": qsTr("super seeding off"),
            "state": "superSeeding",
            "needs": "torrents",
            "label": qsTr("super seeding"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "",
            "via": "enter a",
            "action": "autoManagement",
            "title": qsTr("automatic management on"),
            "titleOn": qsTr("automatic management off"),
            "state": "autoTmm",
            "needs": "torrents",
            "label": qsTr("automatic management"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "P",
            "action": "forceStart",
            "title": qsTr("force start"),
            "needs": "torrents",
            "label": qsTr("force"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "d",
            "action": "delete",
            "title": qsTr("delete"),
            "needs": "torrents",
            "label": qsTr("delete"),
            "column": "act",
            "rank": 4,
            "helpKeys": "d D",
            "help": qsTr("delete · with files"),
            "about": qsTr("Delete · delete with its files")
        },
        {
            "area": "table",
            "key": "D",
            "action": "deleteWithFiles",
            "title": qsTr("delete with files"),
            "needs": "torrents",
            "label": qsTr("delete with files"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "c",
            "action": "recheck",
            "title": qsTr("recheck"),
            "needs": "torrents",
            "label": qsTr("recheck"),
            "column": "act",
            "rank": 5,
            "helpKeys": "c R",
            "help": qsTr("recheck · reannounce"),
            "about": qsTr("Recheck the data · reannounce to every tracker")
        },
        {
            "area": "table",
            "key": "R",
            "action": "reannounce",
            "title": qsTr("reannounce"),
            "needs": "torrents",
            "label": qsTr("reannounce"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "C",
            "action": "category",
            "title": qsTr("set category"),
            "needs": "torrents",
            "label": qsTr("category"),
            "column": "act",
            "rank": 6,
            "helpKeys": "C t m L r",
            "help": qsTr("category · tags · location · limits · rename"),
            "about": qsTr("Set category · tags · location · limits; rename the cursor row")
        },
        {
            "area": "table",
            "key": "t",
            "action": "tags",
            "title": qsTr("set tags"),
            "needs": "torrents",
            "label": qsTr("tags"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "m",
            "action": "location",
            "title": qsTr("set location"),
            "needs": "torrents",
            "label": qsTr("location"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "L",
            "action": "limits",
            "title": qsTr("limits"),
            "needs": "torrents",
            "label": qsTr("limits"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "r",
            "action": "rename",
            "title": qsTr("rename"),
            "needs": "cursor",
            "label": qsTr("rename"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "[",
            "action": "queueUp",
            "title": qsTr("queue up"),
            "needs": "torrents",
            "label": qsTr("queue up"),
            "column": "act",
            "rank": 7,
            "helpKeys": "[ ]",
            "help": qsTr("queue"),
            "about": qsTr("Move up or down in the queue")
        },
        {
            "area": "table",
            "key": "]",
            "action": "queueDown",
            "title": qsTr("queue down"),
            "needs": "torrents",
            "label": qsTr("queue down"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "o",
            "action": "openFolder",
            "title": qsTr("open folder"),
            "needs": "cursor",
            "label": qsTr("folder"),
            "column": "act",
            "rank": 8,
            "helpKeys": "o",
            "help": qsTr("open folder"),
            "about": qsTr("Open the cursor row's folder in the file manager")
        },
        // docs/32 D3 (#55): the failure block's `x`, while the pane shows it on General. A
        // gesture on that block rather than a command: no title, no palette row.
        {
            "area": "table",
            "key": "x",
            "action": "dismissFailure",
            "label": qsTr("dismiss"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "table",
            "key": "y",
            "action": "copyMagnet",
            "title": qsTr("copy magnet"),
            "needs": "torrents",
            "label": qsTr("copy magnet"),
            "column": "act",
            "rank": 9,
            "helpKeys": "y Y n",
            "help": qsTr("copy magnet · hash · name"),
            "about": qsTr("Copy the magnet · hash · name, one line per selected torrent")
        },
        {
            "area": "table",
            "key": "Y",
            "action": "copyHash",
            "title": qsTr("copy hash"),
            "needs": "torrents",
            "label": qsTr("copy hash"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        // The third of the copy group. qui offers these as a "Copy…" submenu; `Kit/Menu` is
        // deliberately one flat list with a key per row, so they are three rows instead —
        // which is also the shape a keyboard reaches fastest.
        {
            "area": "table",
            "key": "n",
            "action": "copyName",
            "title": qsTr("copy name"),
            "needs": "torrents",
            "label": qsTr("copy name"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },

        // rail ----------------------------------------------------------------------
        {
            "area": "rail",
            "key": "j",
            "action": "moveDown",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "rail",
            "key": "k",
            "action": "moveUp",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "rail",
            "key": "g",
            "action": "first",
            "label": qsTr("first"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "rail",
            "key": "G",
            "action": "last",
            "label": qsTr("last"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "rail",
            "key": "l",
            "action": "focusTable",
            "label": qsTr("table"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "rail",
            "key": "enter",
            "action": "selectFilter",
            "label": qsTr("select"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "rail",
            "key": "space",
            "action": "selectFilter",
            "label": qsTr("select"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        // docs/08 §5: fold or unfold the group the cursor is in.
        {
            "area": "rail",
            "key": "z",
            "action": "fold",
            "label": qsTr("fold"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "global", // handled by the window from the table, rail and details alike
            // Fig. 26 prints 0–9, but the rail has nine status rows, 0 (all) to 8 (errored),
            // so `9` does nothing; the sheet prints the keys that work (batch-2 S1, REVIEW §9).
            "key": "0–8",
            "action": "statusFilter",
            "label": qsTr("status"),
            "column": "navigate",
            "rank": 5,
            "helpKeys": "0–8",
            "help": qsTr("status filter"),
            "about": qsTr("Filter by status: 0 all, 1 downloading, 2 seeding…")
        },

        // details (spec §2 Fig. 2-4 add the file/tracker/peer keys) -------------------
        {
            "area": "details",
            "key": "j",
            "action": "moveDown",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "k",
            "action": "moveUp",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "h",
            "action": "fold",
            "label": qsTr("fold"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "l",
            "action": "unfold",
            "label": qsTr("unfold"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "1–4",
            "action": "detailsTab",
            "label": qsTr("tab"),
            "column": "view",
            "rank": 2,
            "helpKeys": "1–4",
            "help": qsTr("details tab"),
            "about": qsTr("In the details: general, files, trackers, peers tab, and 5 for HTTP sources when the torrent has any; 0 and 5–8 pick a status otherwise")
        },
        {
            "area": "details",
            "key": "+",
            "action": "raisePriority",
            "titles": {"files": qsTr("raise priority")},
            "needs": "marked",
            "label": qsTr("priority"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "-",
            "action": "lowerPriority",
            "titles": {"files": qsTr("lower priority")},
            "needs": "marked",
            "label": qsTr("priority"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "x",
            "action": "skipFile",
            "titles": {"files": qsTr("skip or keep")},
            "needs": "marked",
            "label": qsTr("skip / keep"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "o",
            "action": "revealFile",
            "titles": {"files": qsTr("show in folder")},
            "needs": "row",
            "label": qsTr("folder"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        // docs/12 G5/G4: the sheet row for the details pane's `y`, which General's rows share,
        // and General's `enter` on an option row.
        {
            "area": "details",
            "key": "y",
            "action": "copy",
            "titles": {"trackers": qsTr("copy tracker URLs"), "peers": qsTr("copy peer addresses"), "webseeds": qsTr("copy URLs")},
            "needs": "marked",
            "label": qsTr("copy"),
            "column": "act",
            "rank": 12,
            "helpKeys": "y enter",
            "help": qsTr("copy value · change option"),
            "about": qsTr("In the details: copy the row under the cursor; in General, its whole value (the full hash or magnet). enter turns a General option on or off; a click does either")
        },
        {
            "area": "details",
            "key": "a",
            "action": "addRow",
            "titles": {"trackers": qsTr("add trackers"), "peers": qsTr("add peers"), "webseeds": qsTr("add HTTP sources")},
            "needs": "",
            "label": qsTr("add"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "d",
            "action": "removeRow",
            "titles": {"trackers": qsTr("remove trackers"), "webseeds": qsTr("remove HTTP sources")},
            "needs": "marked",
            "label": qsTr("remove"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "R",
            "action": "trackersReannounce",
            "titles": {"trackers": qsTr("reannounce to every tracker")},
            "needs": "",
            "label": qsTr("reannounce"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "b",
            "action": "ban",
            "titles": {"peers": qsTr("ban peers")},
            "needs": "marked",
            "label": qsTr("ban"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },

        // the columns palette (`|`, N2): pick the columns and put them in order. `j`/`k`
        // are typed text in a palette, so the moves take `alt`. ------------------------
        {
            "area": "columns",
            "key": "space",
            "action": "toggleColumn",
            "label": qsTr("toggle"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "columns",
            "key": "alt+↑",
            "action": "moveColumnUp",
            "label": qsTr("move"),
            "column": "view",
            "rank": 4.5,
            "helpKeys": "alt+↑ alt+↓",
            "help": qsTr("column order"),
            "about": qsTr("In the columns palette: move a column up or down, or drag it; ctrl+r puts back the default order")
        },
        {
            "area": "columns",
            "key": "alt+↓",
            "action": "moveColumnDown",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "columns",
            "key": "alt+k",
            "action": "moveColumnUp",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "columns",
            "key": "alt+j",
            "action": "moveColumnDown",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "columns",
            "key": "ctrl+r",
            "action": "resetColumnOrder",
            "label": qsTr("default order"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },

        // the add sheet's file tree (docs/11 D1): the details Files tab's keys, plus space
        // to check or uncheck a file or a whole folder. The sheet's own hint line reads these.
        {
            "area": "addFiles",
            "key": "j",
            "action": "moveDown",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "addFiles",
            "key": "k",
            "action": "moveUp",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "addFiles",
            "key": "space",
            "action": "toggleFile",
            "label": qsTr("tick"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        // docs/27 D2: every file ticked, then none (the header row's tick does the same).
        {
            "area": "addFiles",
            "key": "ctrl+a",
            "action": "toggleAllFiles",
            "label": qsTr("all / none"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "addFiles",
            "key": "+",
            "action": "raisePriority",
            "label": qsTr("priority"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "addFiles",
            "key": "-",
            "action": "lowerPriority",
            "label": qsTr("priority"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "addFiles",
            "key": "x",
            "action": "skipFile",
            "label": qsTr("skip"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "addFiles",
            "key": "h",
            "action": "fold",
            "label": qsTr("fold"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "addFiles",
            "key": "l",
            "action": "unfold",
            "label": qsTr("unfold"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        // Manual box M18: `r` (or F2) renames the row before the add.
        {
            "area": "addFiles",
            "key": "r",
            "action": "rename",
            "label": qsTr("rename"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },

        // The add sheet's batch (#40, docs/27 B1): its list of torrents. `tab` stays the sheet's.
        {
            "area": "addBatch",
            "key": "j",
            "action": "moveDown",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "addBatch",
            "key": "d",
            "action": "remove",
            "label": qsTr("remove"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },

        // The add sheet's "in Downloads" list (manual box M7). `tab` stays the sheet's.
        {
            "area": "addDownloads",
            "key": "j",
            "action": "moveDown",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "addDownloads",
            "key": "k",
            "action": "moveUp",
            "label": qsTr("move"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "addDownloads",
            "key": "enter",
            "action": "use",
            "label": qsTr("use this file"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "addDownloads",
            "key": "space",
            "action": "tick",
            "label": qsTr("tick"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },

        // The Files tab (docs/DECISIONS.md N1): the same verbs as the table where there is one
        // (space, v, enter, r, y, n, g G, s, /), and qBittorrent's content actions.
        {
            "area": "details",
            "key": "g",
            "action": "first",
            "label": qsTr("first"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "G",
            "action": "last",
            "label": qsTr("last"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "space",
            "action": "toggleSelection",
            "label": qsTr("mark"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "v",
            "action": "selectRange",
            "label": qsTr("range"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "ctrl+a",
            "action": "selectAllFiles",
            "titles": {"files": qsTr("mark all")},
            "needs": "rows",
            "label": qsTr("mark all"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "esc",
            "action": "clearSelection",
            "label": qsTr("clear"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "enter",
            "action": "fileMenu",
            "label": qsTr("menu"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "p",
            "action": "priorityMenu",
            "titles": {"files": qsTr("file priority…")},
            "needs": "marked",
            "label": qsTr("priority"),
            "column": "act",
            "rank": 10,
            "helpKeys": "p + - x",
            "help": qsTr("file priority"),
            "about": qsTr("In the Files tab: the priority menu, raise, lower, skip; on the selected files or the cursor row")
        },
        {
            "area": "details",
            "key": "O",
            "action": "openFile",
            "titles": {"files": qsTr("open file")},
            "needs": "file",
            "label": qsTr("open"),
            "column": "act",
            "rank": 11,
            "helpKeys": "O o r",
            "help": qsTr("open · folder · rename"),
            "about": qsTr("In the Files tab: open a file, show it in its folder, rename a file or folder (with several files selected, all of them at once)")
        },
        {
            "area": "details",
            "key": "r",
            "action": "renameFile",
            "titles": {"files": qsTr("rename files")},
            "needs": "marked",
            "label": qsTr("rename"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "Y",
            "action": "copyLocalPath",
            "titles": {"files": qsTr("copy local path")},
            "needs": "row",
            "label": qsTr("copy local path"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "n",
            "action": "copyFileName",
            "titles": {"files": qsTr("copy name")},
            "needs": "row",
            "label": qsTr("copy name"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "s",
            "action": "sortFiles",
            "titles": {"files": qsTr("sort files")},
            "needs": "",
            "label": qsTr("sort"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "/",
            "action": "filterFiles",
            "titles": {"files": qsTr("filter files")},
            "needs": "",
            "label": qsTr("filter"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "H",
            "action": "collapseAll",
            "titles": {"files": qsTr("collapse all")},
            "needs": "rows",
            "label": qsTr("collapse all"),
            "column": "view",
            "rank": 10,
            "helpKeys": "H L",
            "help": qsTr("fold every folder"),
            "about": qsTr("In the Files tab: collapse or open every folder; s sorts the files, / filters them")
        },
        {
            "area": "details",
            "key": "L",
            "action": "expandAll",
            "titles": {"files": qsTr("expand all")},
            "needs": "rows",
            "label": qsTr("expand all"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "X",
            "action": "hideSkipped",
            "titles": {"files": qsTr("hide skipped files")},
            "titlesOn": {"files": qsTr("show skipped files")},
            "state": "hideSkipped",
            "needs": "",
            "label": qsTr("hide skipped"),
            "column": "view",
            "rank": 11,
            "helpKeys": "X",
            "help": qsTr("hide skipped files"),
            "about": qsTr("In the Files tab: hide the files set to skip, or show them again")
        },

        // search field ---------------------------------------------------------------
        {
            "area": "search",
            "key": "enter",
            "action": "apply",
            "label": qsTr("apply"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "search",
            "key": "esc",
            "action": "clear",
            "label": qsTr("clear"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        }
    ]

    readonly property var areas: ["global", "table", "rail", "details", "columns", "search", "addFiles", "addBatch",
        "addDownloads", "console"]

    /// Every entry of one area, in declaration order.
    function forArea(area) {
        return root.entries.filter(entry => entry.area === area);
    }

    /// The entry for `key` in `area`, or null. Keys are unique inside an area. An entry with
    /// no key (a menu-only action, docs/30 D3) is never found by its key.
    function find(area, key) {
        if (key.length === 0)
            return null;
        const hit = root.entries.find(entry => entry.area === area && entry.key === key);
        return hit === undefined ? null : hit;
    }

    // ---- the command palette (docs/30, DECISIONS C1) -------------------------------------
    // Plain data in, plain data out, so the omaseed site's demo can keep a copy of the table
    // and of `commandRows()` and run its own switch on the same action ids.

    /// The palette's rows in the `?` sheet's order (docs/30 §5): torrents, selection,
    /// library, view, speed, app, 43 in all. A group with an `area` is the focused area's own
    /// (D2): listed only when the palette was opened from that tab or face, and first.
    readonly property var commandGroups: [
        {
            "group": "torrents",
            "word": qsTr("torrents"),
            "actions": ["start", "stop", "forceStart", "recheck", "reannounce", "delete",
                "deleteWithFiles", "category", "tags", "location", "limits", "rename", "openFolder",
                "queueUp", "queueDown", "copyMagnet", "copyHash", "copyName", "sequential", "firstLast",
                "superSeeding", "autoManagement"]
        },
        {
            "group": "selection",
            "word": qsTr("selection"),
            "actions": ["selectAll", "clearSelection"]
        },
        {
            "group": "library",
            "word": qsTr("library"),
            "actions": ["add", "filter", "jump", "sort"]
        },
        {
            "group": "view",
            "word": qsTr("view"),
            "actions": ["columns", "toggleDetails", "cycleDetailsPlacement", "toggleRail",
                "toggleConsole", "detailsSmaller", "detailsLarger"]
        },
        {
            "group": "speed",
            "word": qsTr("speed"),
            "actions": ["altSpeed", "globalLimits", "altLimits"]
        },
        {
            "group": "app",
            "word": qsTr("app"),
            "actions": ["settings", "qbtSettings", "help", "hide", "quit"]
        },
        {
            "group": "files",
            "area": "files",
            "word": qsTr("files"),
            "actions": ["priorityMenu", "raisePriority", "lowerPriority", "skipFile", "openFile",
                "revealFile", "renameFile", "copyLocalPath", "copyFileName", "sortFiles", "filterFiles",
                "selectAllFiles", "collapseAll", "expandAll", "hideSkipped"]
        },
        {
            "group": "trackers",
            "area": "trackers",
            "word": qsTr("trackers"),
            "actions": ["addRow", "removeRow", "trackersReannounce", "copy"]
        },
        {
            "group": "peers",
            "area": "peers",
            "word": qsTr("peers"),
            "actions": ["addRow", "ban", "copy"]
        },
        {
            "group": "webseeds",
            "area": "webseeds",
            "word": qsTr("HTTP sources"),
            "actions": ["addRow", "removeRow", "copy"]
        },
        {
            "group": "console",
            "area": "console",
            "word": qsTr("console"),
            "actions": ["consoleShorter", "consoleLonger", "consoleEscape"]
        }
    ]

    /// Fig. 23's read-only guard: all that runs while the server is lost (docs/30 P6), and
    /// the palette itself, which shows the rest dim.
    readonly property var offlineActions: ["settings", "help", "hide", "quit", "commands"]

    /// The entry that is command `action` of `group`, or null: a details tab's from the pane's
    /// shared keys (`titles`), the console's from its face, the rest from the window and table.
    function commandEntry(group: string, action: string): var {
        const tab = ["files", "trackers", "peers", "webseeds"].indexOf(group) >= 0;
        const hit = root.entries.find(entry => {
            if (entry.action !== action)
                return false;
            if (tab)
                return entry.area === "details" && entry.titles !== undefined
                        && entry.titles[group] !== undefined;
            if (group === "console")
                return entry.area === "console" && entry.title !== undefined;
            return (entry.area === "global" || entry.area === "table") && entry.title !== undefined;
        });
        return hit === undefined ? null : hit;
    }

    /// The `needs` rule (docs/30 §5): why `entry` can't run in `context`, or "" when it can.
    /// It follows what the actions menu and `torrentAction()` do: act on the selection, or on
    /// the cursor row when nothing is selected. `context` is `commandRows()`'s.
    function whyNot(entry: var, context: var): string {
        const ctx = context || {};
        const states = ctx.states || {};
        if (ctx.serverLost === true && root.offlineActions.indexOf(entry.action) < 0)
            return qsTr("read-only until the server is back");
        const selected = ctx.selected || 0;
        const cursor = ctx.cursor || "";
        const targets = selected > 0 ? selected : cursor.length > 0 ? 1 : 0;
        const already = word => targets === 1 ? qsTr("already %1").arg(word) : targets === 2
                                                ? qsTr("both already %1").arg(word)
                                                : qsTr("all %1 already %2").arg(targets).arg(word);
        switch (entry.needs) {
        case "torrents":
        case "stopped":
        case "running":
            if (targets === 0)
                return qsTr("nothing selected, no torrent under the cursor");
            if (entry.needs === "stopped" && (ctx.stopped || 0) === 0)
                return already(qsTr("running"));
            if (entry.needs === "running" && (ctx.running || 0) === 0)
                return already(qsTr("stopped"));
            return "";
        case "cursor":
            return cursor.length > 0 ? "" : qsTr("no torrent under the cursor");
        case "selection":
            return selected > 0 ? "" : qsTr("nothing selected");
        case "details":
            return states.detailsShown === true ? "" : qsTr("details hidden · i shows them");
        case "marked":
        case "row":
        case "rows":
        case "file":
            if ((ctx.row || "").length === 0)
                return qsTr("nothing under the cursor");
            if (entry.needs === "file" && ctx.rowIsFolder === true)
                return qsTr("a folder · o shows it");
            return "";
        default:
            return "";
        }
    }

    /// What a row acts on, for its second line: "2 selected", the cursor torrent's name,
    /// "3 marked" or the cursor's row; "" for a command that takes nothing.
    function _commandTarget(needs: string, ctx: var): string {
        switch (needs) {
        case "torrents":
        case "stopped":
        case "running":
            return (ctx.selected || 0) > 0 ? qsTr("%n selected", "", ctx.selected) : (ctx.cursor || "");
        case "cursor":
            return ctx.cursor || "";
        case "selection":
            return qsTr("%n selected", "", ctx.selected || 0);
        case "marked":
            return (ctx.marked || 0) > 0 ? qsTr("%n marked", "", ctx.marked) : (ctx.row || "");
        case "row":
        case "file":
            return ctx.row || "";
        default:
            return "";
        }
    }

    /// One palette row for `entry` of `group`: `{action, key, title, meta, via, detail,
    /// disabled, group, word, recent}`.
    function _commandRow(entry: var, group: var, ctx: var): var {
        const states = ctx.states || {};
        const state = entry.state !== undefined ? states[entry.state] : undefined;
        const title = entry.titles !== undefined ? entry.titles[group.group] : entry.title;
        const titleOn = entry.titlesOn !== undefined ? entry.titlesOn[group.group] : entry.titleOn;
        const reason = root.whyNot(entry, ctx);
        let meta = entry.key;
        // D5: `p` shows on the one of start and stop that it would run now.
        if (entry.sharesKey !== undefined)
            meta = (entry.needs === "stopped") === (ctx.firstStopped === true) ? entry.sharesKey : "";
        const parts = [group.word];
        if (reason.length > 0) {
            parts.push(reason);
        } else {
            parts.push(root._commandTarget(entry.needs, ctx));
            if (typeof state === "string" && state.length > 0)
                parts.push(qsTr("now %1").arg(state));
            else if (typeof state === "boolean" && entry.needs === "torrents") // an option row
                parts.push((ctx.selected || 0) > 0 && (ctx.cursor || "").length > 0
                           ? qsTr("now %1 on %2").arg(state ? qsTr("on") : qsTr("off")).arg(ctx.cursor)
                           : qsTr("now %1").arg(state ? qsTr("on") : qsTr("off")));
        }
        return {
            "action": entry.action,
            "key": entry.action,
            "title": state === true && titleOn !== undefined ? titleOn : title,
            "meta": meta,
            "via": entry.via !== undefined ? entry.via : "",
            "detail": parts.filter(part => part.length > 0).join(" · "),
            "disabled": reason.length > 0,
            "group": group.group,
            "word": group.word,
            "recent": (ctx.recent || []).indexOf(entry.action)
        };
    }

    /// The command palette's rows for `context` (docs/30 §5), with nothing typed, in order:
    /// the focused area's own commands when it is not the table (P5), the last ones run
    /// (D7), the rest in the `?` sheet's order, then every row that can't run (D4).
    ///
    /// `context` is plain data: `{area: "table" | "rail" | "general" | "files" | "trackers" |
    /// "peers" | "webseeds" | "console", selected: 2, cursor: "<the cursor torrent's name>",
    /// stopped: 0, running: 2 (of the targets), firstStopped: false (`p` starts),
    /// marked: 3, row: "<the focused tab's cursor row>", rowIsFolder: false,
    /// serverLost: false, recent: ["stop", …], states: {detailsShown, railShown,
    /// consoleShown, consoleScoped, altSpeed, placement, hideSkipped, seqDl, firstLast,
    /// superSeeding, autoTmm}}`.
    function commandRows(context: var): var {
        const ctx = context || {};
        const own = [];
        const recent = [];
        const rest = [];
        const off = [];
        for (const group of root.commandGroups) {
            if (group.area !== undefined && group.area !== ctx.area)
                continue;
            for (const action of group.actions) {
                const entry = root.commandEntry(group.group, action);
                if (entry === null)
                    continue;
                const row = root._commandRow(entry, group, ctx);
                if (row.disabled)
                    off.push(row);
                else if (group.area !== undefined)
                    own.push(row);
                else if (row.recent >= 0)
                    recent.push(row);
                else
                    rest.push(row);
            }
        }
        recent.sort((a, b) => a.recent - b.recent);
        return own.concat(recent, rest, off);
    }

    /// `label` of (area, key), falling back to the global entry and then to the key itself,
    /// so `HintModel` never has to repeat a word the map already holds.
    function labelOf(area, key) {
        const own = root.find(area, key);
        if (own)
            return own.label;
        const global = root.find("global", key);
        return global ? global.label : key;
    }

    /// The rows of one help-sheet column, in Fig. 26's order.
    function helpRows(column) {
        return root.entries.filter(entry => entry.column === column).sort((a, b) => a.rank
                                                                                    - b.rank);
    }
}
