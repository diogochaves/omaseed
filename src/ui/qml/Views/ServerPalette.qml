pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// docs/33 S1 (D2–D5, DECISIONS C12): the server palette. `ctrl+s`, a click on the hero's
// server line, "switch server…" in `:`, "… N more" in the rail and the add sheet's chip all
// open it. One row per server in Settings' order, the one the app is on first ("on it now",
// its live figures), the others with what omaseed saw last time it was on them (D5: the
// count, or why the last try failed, and when; no request goes to them). The cursor starts on
// the next one. Typing filters by name or address; `enter` switches (App.switchServer, the
// same switch everywhere); `,` opens Settings › Servers on the cursor's row.
//
// The rows are App.serverRows, built from the same ServerListModel as Settings' ServerSelect
// (docs/34, DECISIONS C11).
Palette {
    id: root

    /// `,`: Settings › Servers on server `id`.
    signal manageRequested(string id)
    /// `enter` switched (or stayed) to `id`.
    signal switched(string id)

    readonly property var allRows: App.serverRows
    readonly property var _rows: {
        const q = root.query.trim();
        if (q.length === 0)
            return root.allRows;
        return root.allRows.filter(row => Fuzzy.match(row.title + " " + row.address, q));
    }

    /// Opens with nothing typed and the cursor on the next server (the row after the one the
    /// app is on).
    function openOnNext(): void {
        root.query = "";
        root.open();
        root.cursorIndex = root._rows.length > 1 ? 1 : 0;
    }

    function _manage(): void {
        const item = root.currentItem;
        root.close();
        root.manageRequested(item ? item.key : "");
    }

    counter: qsTr("%n servers", "", root.allRows.length)
    emptyText: root._rows.length === 0 ? qsTr("No server matches “%1”.").arg(root.query.trim()) : ""
    glyph: Glyphs.server
    hints: [
        {
            "key": "↑↓",
            "label": qsTr("move")
        },
        {
            "key": "enter",
            "label": qsTr("switch")
        },
        {
            "key": ",",
            "label": qsTr("manage servers"),
            "clickable": true
        },
        {
            "key": "esc",
            "label": root.query.length > 0 ? qsTr("clear") : qsTr("close")
        }
    ]
    items: root._rows
    keyHandler: event => {
        if (event.key === Qt.Key_Comma && (event.modifiers & (Qt.ControlModifier | Qt.AltModifier)) === 0) {
            root._manage();
            event.accepted = true;
        }
    }
    objectName: "serverPalette"
    paletteWidth: Theme.space(520)
    title: qsTr("switch server")

    onActivated: (index, item) => {
        root.close();
        App.switchServer(item.key);
        root.switched(item.key);
    }
    onHintClicked: (index, hint) => {
        if (hint.key === ",")
            root._manage();
    }
    onRejected: root.close()
}
