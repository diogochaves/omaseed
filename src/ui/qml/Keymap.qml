pragma Singleton
import QtQuick

// The one table of keys (spec v2 §7 Fig. 26, "the same map as v1"). Everything that binds a
// key in omaseed reads it from here: `MainView` routes presses through it, `HintModel`
// derives the footer's hints from it, and `HelpSheet` draws the `?` sheet from it. Nothing
// else may invent a key.
//
// Every entry is plain data:
//   area      where the key applies: "global" (anywhere), "table", "rail", "details",
//             "search". A key event reaches the focused view first (Qt propagates an
//             unaccepted key up the parent chain), so an area entry always wins over the
//             "global" entry for the same key — that is how `a` adds a tracker inside the
//             details pane and a torrent everywhere else.
//   key       the key as the spec writes it: "j", "G", "ctrl+q", "esc", "enter", "space",
//             "0–9", "1–4" (en dashes, like the figure).
//   action    the verb `MainView` switches on.
//   label     the short word the footer hints print after the key.
//   column    for the help sheet: "navigate", "select", "act", "app" — set only on the
//             entry that leads a sheet row; "" on the rest.
//   rank      the row's place inside its column, so the sheet reproduces Fig. 26's order.
//   helpKeys  the row's key chip ("p P", "C t m L r"); "" when the entry leads no row.
//   help      the row's description ("stop/start · force"); "" when it leads no row.
QtObject {
    id: root

    readonly property var columns: ["navigate", "select", "act", "app"]

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
            "help": qsTr("filter")
        },
        {
            "area": "global",
            "key": "i",
            "action": "toggleDetails",
            "label": qsTr("details"),
            "column": "navigate",
            "rank": 6,
            "helpKeys": "i",
            "help": qsTr("details")
        },
        {
            "area": "global",
            "key": "ctrl+k",
            "action": "jump",
            "label": qsTr("jump"),
            "column": "navigate",
            "rank": 8,
            "helpKeys": "ctrl+k",
            "help": qsTr("jump")
        },
        {
            "area": "global",
            "key": "esc",
            "action": "escape",
            "label": qsTr("clear"),
            "column": "select",
            "rank": 4,
            "helpKeys": "esc",
            "help": qsTr("clear, then filter, then close")
        },
        {
            "area": "global",
            "key": "a",
            "action": "add",
            "label": qsTr("add"),
            "column": "act",
            "rank": 1,
            "helpKeys": "a",
            "help": qsTr("add")
        },
        {
            "area": "global",
            "key": "s",
            "action": "sort",
            "label": qsTr("sort"),
            "column": "app",
            "rank": 1,
            "helpKeys": "s",
            "help": qsTr("sort")
        },
        {
            "area": "global",
            "key": "A",
            "action": "altSpeed",
            "label": qsTr("alt-speed"),
            "column": "app",
            "rank": 2,
            "helpKeys": "A",
            "help": qsTr("alt-speed")
        },
        {
            "area": "global",
            "key": ",",
            "action": "settings",
            "label": qsTr("settings"),
            "column": "app",
            "rank": 3,
            "helpKeys": ",",
            "help": qsTr("settings")
        },
        {
            "area": "global",
            "key": "ctrl+,",
            "action": "columns",
            "label": qsTr("columns"),
            "column": "app",
            "rank": 4,
            "helpKeys": "ctrl+,",
            "help": qsTr("columns")
        },
        {
            "area": "global",
            "key": "q",
            "action": "hide",
            "label": qsTr("hide"),
            "column": "app",
            "rank": 5,
            "helpKeys": "q",
            "help": qsTr("hide to tray")
        },
        {
            "area": "global",
            "key": "ctrl+q",
            "action": "quit",
            "label": qsTr("quit"),
            "column": "app",
            "rank": 6,
            "helpKeys": "ctrl+q",
            "help": qsTr("quit")
        },
        // Not bound by omaseed: the figure teaches the Hyprland bind that raises it.
        {
            "area": "global",
            "key": "super+t",
            "action": "hyprlandBind",
            "label": qsTr("omaseed"),
            "column": "app",
            "rank": 7,
            "helpKeys": "super+t",
            "help": qsTr("suggested Hyprland bind")
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
            "help": qsTr("move")
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
            "help": qsTr("first · last")
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
            "help": qsTr("rail · table · details")
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
            "help": qsTr("toggle")
        },
        {
            "area": "table",
            "key": "v",
            "action": "selectRange",
            "label": qsTr("range"),
            "column": "select",
            "rank": 2,
            "helpKeys": "v",
            "help": qsTr("range")
        },
        {
            "area": "table",
            "key": "ctrl+a",
            "action": "selectAll",
            "label": qsTr("all"),
            "column": "select",
            "rank": 3,
            "helpKeys": "ctrl+a",
            "help": qsTr("all visible")
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
            "help": qsTr("actions")
        },
        {
            "area": "table",
            "key": "p",
            "action": "stopStart",
            "label": qsTr("stop/start"),
            "column": "act",
            "rank": 3,
            "helpKeys": "p P",
            "help": qsTr("stop/start · force")
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
            "help": qsTr("delete · with files")
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
            "help": qsTr("recheck · reannounce")
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
            "help": qsTr("category · tags · location · limits · rename")
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
            "help": qsTr("queue")
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
            "help": qsTr("open folder")
        },
        {
            "area": "table",
            "key": "y",
            "action": "copyMagnet",
            "label": qsTr("copy magnet"),
            "column": "act",
            "rank": 9,
            "helpKeys": "y Y",
            "help": qsTr("copy magnet · hash")
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
        {
            "area": "rail",
            "key": "0–9",
            "action": "statusFilter",
            "label": qsTr("status"),
            "column": "navigate",
            "rank": 5,
            "helpKeys": "0–9",
            "help": qsTr("status filter")
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
            "column": "navigate",
            "rank": 7,
            "helpKeys": "1–4",
            "help": qsTr("details tab")
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

    readonly property var areas: ["global", "table", "rail", "details", "search"]

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
