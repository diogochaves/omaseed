pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The body of the `?` sheet, laid out as docs/08 §4 layout A (after Strata's shape; nothing
// copied): a filter on top, then the sections of `Keymap` in two balanced columns, each with
// a heading, a one-line description and a row per key that prints the keys beside a sentence
// saying what they do.
//
// Typing filters every section at once (keys, gloss and sentence all match). While a filter is
// on, the first match carries the cursor, `up`/`down` move it and `enter` asks for its action
// (`runRequested`), so the sheet doubles as a command list. `esc` clears the filter, then
// closes; `?` closes when there is nothing typed. With the mouse, a click on a row of that list
// moves the cursor there, and a click on the row already under it (so a double-click) runs it.
// The keyboard stays in the filter.
//
// Every row comes out of `Keymap`, so the sheet cannot drift from what the keys actually do.
// This is the body only: `Kit/Sheet` draws the panel, the head and the scrim.
FocusScope {
    id: root

    signal closeRequested
    /// `enter` on a match: the `action` of its Keymap entry.
    signal runRequested(string action)

    readonly property alias filterField: filterField
    /// docs/31 S1: A's line follows the alternative set, "burst", "turtle" or "alt".
    property string altKind: "alt"
    readonly property string query: filterField.text.trim().toLowerCase()
    /// The sections that still have rows, each `{key, title, about, rows}`. DECISIONS C18 D6:
    /// the mouse's block follows the select keys.
    readonly property var sections: root._sectionKeys.map(column => ({
                                                                         "key": column,
                                                                         "title": column === "mouse" ? Keymap.mouseTitle
                                                                                                     : Keymap.columnTitles[column],
                                                                         "about": column === "mouse" ? Keymap.mouseAbout
                                                                                                     : Keymap.columnAbout[column],
                                                                         "rows": root._rowsOf(column).filter(row => root.matches(row))
                                                                     })).filter(section => section.rows.length > 0)
    readonly property var _sectionKeys: {
        const keys = Keymap.columns.slice();
        keys.splice(keys.indexOf("select") + 1, 0, "mouse");
        return keys;
    }
    /// Every row still shown, in reading order (left column first).
    readonly property var matchedRows: root._columnSections(0).concat(root._columnSections(1)).reduce(
                                           (all, section) => all.concat(section.rows), [])
    readonly property int totalRows: root._sectionKeys.reduce((n, column) => n + root._rowsOf(column).length, 0)
    /// Where `up`/`down` put the cursor, as an index into `matchedRows`; -1 until they are
    /// used, and again after every change of filter.
    property int cursor: -1
    /// The row `enter` runs: the moved cursor, else the best match; none with no filter.
    readonly property int cursorIndex: root.query.length === 0 ? -1 : root.cursor >= 0
                                                                     && root.cursor < root.matchedRows.length
                                                                     ? root.cursor : root._bestMatch()
    readonly property var cursorRow: root.cursorIndex >= 0 ? root.matchedRows[root.cursorIndex] :
                                                            null

    /// The most the list may take before it scrolls: 60 % of the window in the `?` sheet;
    /// Settings › Keybindings gives it the rest of its pane.
    property real maxListHeight: Math.max(Theme.space(200), (root.Window.height || 800) * 0.6)

    readonly property int _gap: Theme.space(28)
    readonly property bool _twoColumns: root.width >= Theme.space(560)
    /// The widest key chip of the whole sheet sets the key column, so every sentence starts on
    /// the same line.
    readonly property string _widestKeys: root._sectionKeys.reduce((widest, column) => root._rowsOf(column).reduce(
                                                                        (w, row) => row.helpKeys.length > w.length ? row.helpKeys : w,
                                                                        widest), "")

    /// A section's rows: the keys of a Keymap column, or the mouse's.
    function _rowsOf(column: string): var {
        return column === "mouse" ? Keymap.mouseRows : Keymap.helpRows(column, root.altKind);
    }

    function matches(row: var): bool {
        if (root.query.length === 0)
            return true;
        return (row.helpKeys + " " + row.help + " " + (row.about || "")).toLowerCase().includes(
                    root.query);
    }

    /// The sections of one column: sections go left until the left column holds half the rows,
    /// so the two columns come out about the same height.
    function _columnSections(column: int): var {
        if (!root._twoColumns)
            return column === 0 ? root.sections : [];
        const total = root.sections.reduce((n, section) => n + section.rows.length + 2, 0);
        let left = 0;
        let split = root.sections.length;
        for (let i = 0; i < root.sections.length; ++i) {
            if (left >= total / 2) {
                split = i;
                break;
            }
            left += root.sections[i].rows.length + 2;
        }
        return column === 0 ? root.sections.slice(0, split) : root.sections.slice(split);
    }

    /// A click on `row` of the command list: the cursor goes there; on the row already under
    /// it, it runs. Without a filter there is no command list, and a click does nothing.
    function clickRow(row: var): void {
        if (root.query.length === 0)
            return;
        const at = root.matchedRows.findIndex(r => r.action === row.action);
        if (at < 0)
            return;
        if (at === root.cursorIndex) {
            root.runRequested(row.action);
            return;
        }
        root.cursor = at;
    }

    function moveCursor(delta: int): void {
        if (root.matchedRows.length === 0)
            return;
        root.cursor = Math.max(0, Math.min(root.matchedRows.length - 1, Math.max(
                                               0, root.cursorIndex) + delta));
    }

    /// Scrolls the list by `delta` px, clamped to its ends. With no filter there is no cursor,
    /// so `up`/`down` and the page keys scroll instead: a keyboard user reaches VIEW's rows too.
    function scrollBy(delta: real): void {
        scroller.contentY = Math.max(0, Math.min(scroller.contentHeight - scroller.height,
                                                 scroller.contentY + delta));
    }

    implicitHeight: layout.implicitHeight

    /// Where the cursor starts for a filter: the row whose short name starts with it, then one
    /// whose name contains it, then the first match in reading order ("col" finds Columns,
    /// not "Sort by any column").
    function _bestMatch(): int {
        let best = -1;
        let bestScore = 3;
        for (let i = 0; i < root.matchedRows.length; ++i) {
            const help = root.matchedRows[i].help.toLowerCase();
            const score = help.startsWith(root.query) ? 0 : help.includes(root.query) ? 1 : 2;
            if (score < bestScore) {
                best = i;
                bestScore = score;
            }
        }
        return best;
    }

    onQueryChanged: root.cursor = -1

    Keys.onEscapePressed: event => {
        root.closeRequested();
        event.accepted = true;
    }
    Keys.onPressed: event => {
        // F1 opened the sheet (Keymap "f1"), so it closes it too. A `?` is typed text for the
        // filter's TextInput, which takes it before this handler: see `filterField`.
        if (event.key === Qt.Key_F1 || (event.key === Qt.Key_Question && root.query.length === 0)) {
            root.closeRequested();
            event.accepted = true;
        } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
            const step = event.key === Qt.Key_Down ? 1 : -1;
            if (root.query.length > 0)
                root.moveCursor(step);
            else
                root.scrollBy(step * Theme.space(40));
            event.accepted = true;
        } else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp) {
            root.scrollBy((event.key === Qt.Key_PageDown ? 1 : -1) * scroller.height * 0.9);
            event.accepted = true;
        }
    }

    TextMetrics {
        id: keyMetrics

        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        font.weight: Font.Medium
        text: root._widestKeys
    }

    ColumnLayout {
        id: layout

        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.space(14)

        Field {
            id: filterField

            Layout.fillWidth: true
            focus: true
            objectName: "helpFilter"
            hint: Glyphs.search
            placeholder: qsTr("filter shortcuts")
            trailing: root.query.length > 0 ? qsTr("%1 of %2").arg(root.matchedRows.length).arg(
                                                  root.totalRows) : ""

            onAccepted: if (root.cursorRow)
                            root.runRequested(root.cursorRow.action)
            // `?` with nothing typed closes, the way it opened: the field has the caret and
            // swallows the printable key, so the check is on what it typed.
            onTextChanged: {
                if (filterField.text === "?") {
                    filterField.text = "";
                    root.closeRequested();
                }
            }
        }

        Flickable {
            id: scroller

            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(0, Math.min(columns.implicitHeight, root.maxListHeight))
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            contentHeight: columns.implicitHeight
            contentWidth: width

            // The list is capped (`maxListHeight`), so on a short window VIEW's rows sit below
            // the fold; the bar says so (batch-2 pre-walk P1). Parented to the Flickable
            // itself, not its content, so it stays put while the rows scroll.
            ScrollBar {
                id: scrollHint

                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.top: parent.top
                flickable: scroller
                objectName: "helpScrollBar"
                parent: scroller
                vertical: true
            }

            RowLayout {
                id: columns

                spacing: root._gap
                width: scroller.width - (scrollHint.needed ? scrollHint.width + Theme.space(8) : 0)

                Repeater {
                    model: root._twoColumns ? 2 : 1

                    ColumnLayout {
                        id: column

                        required property int index

                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: Theme.space(14)

                        Repeater {
                            model: root._columnSections(column.index)

                            ColumnLayout {
                                id: section

                                required property var modelData

                                Layout.fillWidth: true
                                spacing: 0

                                Label {
                                    Layout.bottomMargin: Theme.space(2)
                                    color: Theme.accent
                                    text: section.modelData.title
                                    tracked: true
                                }

                                Label {
                                    Layout.bottomMargin: Theme.space(4)
                                    Layout.fillWidth: true
                                    dim: true
                                    size: "caption"
                                    text: section.modelData.about
                                    visible: text.length > 0 && root.query.length === 0
                                    wrapMode: Text.WordWrap
                                }

                                Repeater {
                                    model: section.modelData.rows

                                    Rectangle {
                                        id: keyRow

                                        required property var modelData

                                        readonly property bool waiting: keyRow.modelData.aboutOne !== undefined
                                                                        && App.serverCount < 2
                                        readonly property bool current: root.cursorRow !== null
                                                                        && root.cursorRow.action
                                                                        === keyRow.modelData.action

                                        // #77: a list item named by what the key does, the
                                        // keys its description, selected under the cursor.
                                        // Its press is the click: while searching it moves
                                        // the cursor there, or runs the row under it.
                                        Accessible.description: keyRow.modelData.helpKeys
                                        Accessible.name: aboutText.text
                                        Accessible.role: Accessible.ListItem
                                        Accessible.selected: keyRow.current
                                        Accessible.onPressAction: if (keyRow.enabled)
                                                                      root.clickRow(keyRow.modelData)
                                        Layout.fillWidth: true
                                        color: keyRow.current ? Theme.fill(Theme.fillHover) :
                                                                "transparent"
                                        implicitHeight: Math.max(keysText.implicitHeight,
                                                                 aboutText.implicitHeight)
                                                        + Theme.space(8)
                                        objectName: "helpRow_" + keyRow.modelData.action

                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            color: Theme.fill(Theme.hairlineAlpha)
                                            height: 1
                                        }

                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            color: Theme.accent
                                            visible: keyRow.current
                                            width: 2
                                        }

                                        Text {
                                            id: keysText

                                            anchors.left: parent.left
                                            anchors.leftMargin: Theme.space(6)
                                            anchors.top: parent.top
                                            anchors.topMargin: Theme.space(4)
                                            color: keyRow.waiting ? Theme.dimForeground : Theme.brightForeground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.font.small
                                            font.weight: Font.Medium
                                            text: keyRow.modelData.helpKeys
                                            width: Math.ceil(keyMetrics.width)
                                        }

                                        Text {
                                            id: aboutText

                                            anchors.left: keysText.right
                                            anchors.leftMargin: Theme.space(12)
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.topMargin: Theme.space(4)
                                            // docs/33 D19: a key that needs two servers is
                                            // listed dim, saying so, while there is one.
                                            color: keyRow.waiting ? Theme.dimForeground : Theme.foreground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.font.small
                                            text: keyRow.waiting ? keyRow.modelData.aboutOne : keyRow.modelData.about
                                            wrapMode: Text.WordWrap
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            enabled: root.query.length > 0
                                            objectName: "helpRowMouse"

                                            onClicked: root.clickRow(keyRow.modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            dim: true
            horizontalAlignment: Text.AlignHCenter
            size: "small"
            text: qsTr("Nothing matches “%1”").arg(filterField.text)
            visible: root.matchedRows.length === 0
        }
    }
}
