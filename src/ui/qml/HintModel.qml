import QtQuick
import Omaseed

// The footer's teaching line, derived from `Keymap` and the window's state. It owns no
// visuals: `MainView` feeds it the focus area and a handful of facts and hands `hints` and
// `pinned` straight to a Kit `KeyHints`.
//
// The lists are the ones the spec draws: §1 Fig. 1 for the table, §2 Fig. 2-4 for the
// details pane's three list tabs, §7 Fig. 22 for the empty state, Fig. 23 for a lost server
// and Fig. 24 for a filter with no result. While an overlay is up (T4.8) the footer prints
// no hints at all: the window underneath answers no key, and the overlay's own hint row is
// the one place its keys are taught (batch-2 D3), so the two can never repeat or contradict
// each other ("esc close" beside the help sheet's "esc clear, then close"). The facts on the
// left stay. `? help` is pinned to the right edge everywhere
// the window is usable (Fig. 1 and Fig. 22); Fig. 23 and Fig. 24 print no pinned hint.
//
// Every key and every word comes out of `Keymap`, so a key can never be taught here and
// bound somewhere else. The two exceptions carry a comment.
QtObject {
    id: root

    // what MainView feeds in -------------------------------------------------------
    /// Which view holds the focus: "table", "rail", "details" or "search".
    property string focusArea: "table"
    /// GlobalState.connected (or true while there is nothing to connect to).
    property bool connected: true
    /// The table is renaming a row inline.
    property bool renaming: false
    /// App.filtered.text.
    property string filterText: ""
    /// App.torrents.count and App.filtered.count.
    property int torrentCount: 0
    property int filteredCount: 0
    /// Details.currentTab: 0 General, 1 Files, 2 Trackers, 3 Peers, 4 HTTP sources (#10).
    property int detailsTab: 0
    /// A sheet, palette or menu is over the window (MainView's `overlayOpen`). The spec
    /// draws an overlay's hints inside the overlay, and the window underneath is modal, so
    /// the footer must stop teaching table keys that nothing will answer.
    property bool overlayOpen: false
    /// The details pane is folded (docs/17 M10): the table's line adds `i`, the way back.
    property bool detailsFolded: false

    // what the footer reads ---------------------------------------------------------
    /// The state the hints describe. A window-wide state (lost server, empty library,
    /// filter with no result) outranks whatever holds the focus, because that is what the
    /// figures show.
    readonly property string area: {
        if (root.overlayOpen)
            return "overlay";
        if (!root.connected)
            return "disconnected";
        if (root.torrentCount === 0)
            return "empty";
        if (root.filteredCount === 0)
            return "filteredEmpty";
        if (root.focusArea === "search")
            return "search";
        if (root.focusArea === "rail")
            return "rail";
        if (root.focusArea === "details")
            return "details";
        if (root.focusArea === "console")
            return "console";
        return "table";
    }

    readonly property var hints: {
        switch (root.area) {
        case "overlay":
            return [];
        case "disconnected":
            // Fig. 23. `R retry` is the one hint with no Keymap entry: retrying is not a
            // table action, it only exists while the server is gone (App.retryNow()).
            return [
                        {
                            "key": "R",
                            "label": qsTr("retry")
                        },
                        root._hint("global", ",")];
        case "empty":
            return [root._hint("global", "a")]; // Fig. 22
        case "filteredEmpty":
            return [root._hint("global", "esc")]; // Fig. 24
        case "search":
            return [root._hint("search", "enter"), root._hint("search", "esc")];
        case "rail":
            return [root._hint("rail", "j k"), root._hint("rail", "enter"), root._hint("rail",
                                                                                       "0–8"), root._hint(
                        "rail", "z"), root._hint("rail", "l")];
        case "details":
            return root._detailsHints();
        case "console": // #24, docs/22's footer
            return [root._hint("global", "`", qsTr("library")), root._hint("console", "[ ]"),
                    root._hint("console", "j k"), root._hint("console", "esc"), root._hint("global", "/")];
        default:
            return root._tableHints();
        }
    }

    /// The `? help` pair `KeyHints` pushes to the right edge, or null.
    readonly property var pinned: (root.area === "disconnected" || root.area === "filteredEmpty"
                                   || root.area === "overlay") ? null : root._hint("global", "?")

    // internals ----------------------------------------------------------------------
    /// One `{key, label}` pair. `keys` is what the footer prints ("j k", "+ -"); the label
    /// comes from the Keymap entry of the first key, unless the caller overrides it.
    function _hint(area, keys, custom) {
        const label = (custom && custom.length > 0) ? custom : Keymap.labelOf(area, keys.split(
                                                                                  " ")[0]);
        return {
            "key": keys,
            "label": label
        };
    }

    function _tableHints() {
        if (root.renaming) {
            return [root._hint("table", "enter", qsTr("apply")), root._hint("table", "esc", qsTr(
                                                                                "cancel"))];
        }
        // Fig. 1, in the figure's order; then `i` while the details pane is folded (docs/17
        // M16), since nothing else on screen names the key that brings it back.
        const hints = [root._hint("table", "j k"), root._hint("table", "space"), root._hint(
                           "table", "enter"), root._hint("table", "p"), root._hint("table", "d"),
                       root._hint("table", "o"), root._hint("global", "/")];
        return root.detailsFolded ? hints.concat([root._hint("global", "i")]) : hints;
    }

    function _detailsHints() {
        switch (root.detailsTab) {
        case 1: // Fig. 2, Files, with N1's menu, selection and filter, and docs/27's ticks
            return [root._hint("details", "x"), root._hint("details", "space"), root._hint("details",
                                                                                           "ctrl+a"),
                    root._hint("details", "p"), root._hint("details", "enter"), root._hint("details",
                                                                                           "/")];
        case 2: // Fig. 3, Trackers
            return [root._hint("details", "a"), root._hint("details", "d"), root._hint("details",
                                                                                       "R"),
                    root._hint("details", "y")];
        case 3: // Fig. 4, Peers
            return [root._hint("details", "b"), root._hint("details", "a", qsTr("add peer")),
                    root._hint("details", "y")];
        case 4: // #10, HTTP sources
            return [root._hint("details", "a"), root._hint("details", "d"), root._hint("details",
                                                                                       "y")];
        default:
            // docs/12 G5 and G4: General's rows are a list with a cursor, `y` copies a value
            // and `enter` changes an option row.
            return [root._hint("details", "j k"), root._hint("details", "y", qsTr("copy value")),
                    root._hint("details", "enter", qsTr("change"))];
        }
    }
}
