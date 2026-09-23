pragma Singleton
import QtQuick

// The one table of keys (spec v2 §7 Fig. 26, "the same map as v1"). Everything that binds a
// key in omaseed reads it from here: `MainView` routes presses through it, `HintModel`
// derives the footer's hints from it, and `HelpSheet` draws the `?` sheet from it. Nothing
// else may invent a key.
//
// Every entry is plain data:
//   area      where the key applies: "global" (anywhere), "table", "rail", "details",
//             "search", "addFiles" (the add sheet's file tree). A key event reaches the
//             focused view first (Qt propagates an
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
                                            "act": qsTr("On the selection, or the cursor row."),
                                            "view": qsTr("What the window shows."),
                                            "app": ""
                                        })

    readonly property var entries: [
        // global: routed by MainView wherever the focus is -------------------------
        {
            "area": "global",
            "key": "/",
            "action": "filter",
            "label": qsTr("filter"),
            "column": "navigate",
            "rank": 4,
            "helpKeys": "/",
            "help": qsTr("filter"),
            "about": qsTr("Filter the table by name")
        },
        {
            "area": "global",
            "key": "i",
            "action": "toggleDetails",
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
            "label": qsTr("sort"),
            "column": "view",
            "rank": 3,
            "helpKeys": "s",
            "help": qsTr("sort"),
            "about": qsTr("Sort by any column")
        },
        {
            "area": "global",
            "key": "A",
            "action": "altSpeed",
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
        // docs/08 §5: hide or show the rail (the sidebar key of most editors).
        {
            "area": "global",
            "key": "ctrl+b",
            "action": "toggleRail",
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
            "label": qsTr("all"),
            "column": "select",
            "rank": 3,
            "helpKeys": "ctrl+a",
            "help": qsTr("all visible"),
            "about": qsTr("Select every torrent the filter shows")
        },
        {
            "area": "table",
            "key": "esc",
            "action": "clearSelection",
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
        {
            "area": "table",
            "key": "P",
            "action": "forceStart",
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
            "label": qsTr("category"),
            "column": "act",
            "rank": 6,
            "helpKeys": "C t m L r",
            "help": qsTr("category · tags · location · limits · rename"),
            "about": qsTr("Set category · tags · location · limits · rename")
        },
        {
            "area": "table",
            "key": "t",
            "action": "tags",
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
            "label": qsTr("folder"),
            "column": "act",
            "rank": 8,
            "helpKeys": "o",
            "help": qsTr("open folder"),
            "about": qsTr("Open the folder in the file manager")
        },
        {
            "area": "table",
            "key": "y",
            "action": "copyMagnet",
            "label": qsTr("copy magnet"),
            "column": "act",
            "rank": 9,
            "helpKeys": "y Y n",
            "help": qsTr("copy magnet · hash · name"),
            "about": qsTr("Copy magnet · hash · name")
        },
        {
            "area": "table",
            "key": "Y",
            "action": "copyHash",
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
            "area": "rail",
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
            "about": qsTr("In the details: general, files, trackers, peers tab")
        },
        {
            "area": "details",
            "key": "+",
            "action": "raisePriority",
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
            "label": qsTr("skip"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "o",
            "action": "openFile",
            "label": qsTr("open"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "y",
            "action": "copy",
            "label": qsTr("copy"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "a",
            "action": "addRow",
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
            "label": qsTr("remove"),
            "column": "",
            "rank": 0,
            "helpKeys": "",
            "help": ""
        },
        {
            "area": "details",
            "key": "R",
            "action": "reannounce",
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
            "label": qsTr("ban"),
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
            "label": qsTr("check"),
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

    readonly property var areas: ["global", "table", "rail", "details", "search", "addFiles"]

    /// Every entry of one area, in declaration order.
    function forArea(area) {
        return root.entries.filter(entry => entry.area === area);
    }

    /// The entry for `key` in `area`, or null. Keys are unique inside an area.
    function find(area, key) {
        const hit = root.entries.find(entry => entry.area === area && entry.key === key);
        return hit === undefined ? null : hit;
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
