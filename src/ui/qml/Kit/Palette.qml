pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The one centred palette of spec v2 §4 (`.palette`, Fig. 12, 13, 14): a query row, rows with
// a title, a right-aligned dim meta and a dim second line, an outlined cursor, and the hint
// line inside the panel. Jump, category, tags, sort and columns are all this component with a
// different list behind it.
//
// Filtering is the caller's job — the palette draws exactly the `items` it is given and
// reports every keystroke through `query`/`queryChanged`, so a picker can rank, group or
// create-on-miss however it likes. `Fuzzy.match(text, query)` is the shared default test.
//
// Keys: `up`/`down` and `ctrl+p`/`ctrl+n` move (never `j`/`k` — in a palette those are typed
// text), `enter` activates, `tab` asks for the secondary action (Fig. 12's "tab actions"),
// `space` toggles when `multi` is set, `esc` closes. `inline: true` drops the scrim and the
// centring, for the gallery.
Item {
    id: root

    /// The dim glyph at the left of the query row (`.q .g`).
    property string glyph: ""
    /// What the row shows instead of the query while nothing is typed — Fig. 13's
    /// "category for 2 torrents". Empty means the row is only ever the query.
    property string title: ""
    /// The dim caption at the right of the query row (`.q .c`), e.g. "2 of 142".
    property string counter: ""
    property alias query: input.text
    /// `[{title, meta, detail, checked, key}]`.
    property var items: []
    /// Show the check column and let `space` toggle rows (the tags picker).
    property bool multi: false
    property var hints: []
    property string footerRight: ""
    property bool inline: false
    /// The panel's maximum width; the spec's `.palette` is 480.
    property int paletteWidth: Theme.space(480)
    /// The row the keyboard is on.
    property int cursorIndex: 0

    readonly property bool opened: root._opened
    readonly property alias panel: panel
    readonly property alias listView: list
    /// The row under the cursor, or null.
    readonly property var currentItem: root.cursorIndex >= 0 && root.cursorIndex < root.items.length
                                       ? root.items[root.cursorIndex] : null

    /// `enter` on a row.
    signal activated(int index, var item)
    /// `tab` on a row (Fig. 12's "tab actions").
    signal secondary(int index, var item)
    /// `space` on a row while `multi` is set. The caller owns the checked state.
    signal toggled(int index, var item)
    /// `esc`, or a click on the scrim.
    signal rejected

    property bool _opened: false
    property Item _previousFocus: null

    readonly property int _margin: Theme.space(16)
    /// The rule between rows: the spec's `.palette .it` uses the foreground at 5 %, lighter
    /// than the 12 % hairline everywhere else.
    readonly property color _rowRule: Theme.fill(0.05)

    function open(): void {
    if (root._opened)
    return;
    root._previousFocus = root.Window.activeFocusItem;
    root._opened = true;
    root.focusQuery();
}

    function close(): void {
                          if (!root._opened)
                          return;
                          root._opened = false;
                          const previous = root._previousFocus;
                          root._previousFocus = null;
                          if (previous)
                          previous.forceActiveFocus();
                      }

    /// Open with the cursor on the row whose `key` is `key` (the current value) instead of
    /// on row 0 — spec §4 Fig. 14 draws the cursor on "download speed · ▾ current". Falls
    /// back to row 0 when no row carries that key.
    function openOn(key: var): void {
        root.open();
        root.cursorToKey(key);
    }

    /// Move the cursor onto the row carrying `key`. A no-op when there is none.
    function cursorToKey(key: var): void {
        for (let i = 0; i < root.items.length; ++i) {
            if (root.items[i].key === key) {
                root.cursorIndex = i;
                list.positionViewAtIndex(i, ListView.Contain);
                return;
            }
        }
    }

    /// Put the caret in the query row.
    function focusQuery(): void {
    input.forceActiveFocus();
}

    /// Move the cursor by `delta` rows, clamped at both ends, keeping it in view.
    function moveCursor(delta: int): void {
    if (root.items.length === 0)
    return;
    root.cursorIndex = Math.max(0, Math.min(root.items.length - 1, root.cursorIndex + delta));
    list.positionViewAtIndex(root.cursorIndex, ListView.Contain);
}

    /// `enter`: hand the cursor's row to the caller. The caller closes the palette.
    function activate(): void {
                             if (root.currentItem)
                             root.activated(root.cursorIndex, root.currentItem);
                         }

    /// The shared matcher, so a caller filtering for this palette need not import anything.
    function match(text: string, query: string): bool {
        return Fuzzy.match(text, query);
    }

    function _onKey(event: var): void {
    const ctrl = (event.modifiers & Qt.ControlModifier) !== 0;
    if (ctrl && (event.key === Qt.Key_N || event.key === Qt.Key_P)) {
        root.moveCursor(event.key === Qt.Key_N ? 1 : -1);
        event.accepted = true;
        return;
    }
        if (ctrl)
        return;
        switch (event.key) {
            case Qt.Key_Escape:
                root.close();
            root.rejected();
            break;
            case Qt.Key_Down:
                root.moveCursor(1);
            break;
            case Qt.Key_Up:
                root.moveCursor(-1);
            break;
            case Qt.Key_Return:
                case Qt.Key_Enter:
                root.activate();
            break;
            case Qt.Key_Tab:
                case Qt.Key_Backtab:
                if (root.currentItem)
            root.secondary(root.cursorIndex, root.currentItem);
            break;
            case Qt.Key_Space:
                if (!root.multi)
                    return;
            if (root.currentItem)
            root.toggled(root.cursorIndex, root.currentItem);
            break;
            default:
                return;
        }
        event.accepted = true;
    }

        implicitHeight: panel.implicitHeight
        implicitWidth: root.paletteWidth
        visible: root.inline || root._opened

        Keys.onPressed: event => root._onKey(event)
        onItemsChanged: root.cursorIndex = Math.max(0, Math.min(root.cursorIndex, root.items.length
                                                                - 1))

        onQueryChanged: root.cursorIndex = 0

        Scrim {
            anchors.fill: parent
            visible: !root.inline

            onDismissed: {
                root.close();
                root.rejected();
            }
        }

        Rectangle {
            id: panel

            border.color: Theme.accent
            border.width: 1
            color: Theme.background
            implicitHeight: layout.implicitHeight
            radius: Theme.cornerRadius
            width: Math.min(root.paletteWidth, root.inline ? root.paletteWidth : Math.max(0,
                                                                                          root.width
                                                                                          - 2 * root._margin))
            x: root.inline ? 0 : Math.round((root.width - width) / 2)
            y: root.inline ? 0 : Math.round((root.height - height) / 2)

            ColumnLayout {
                id: layout

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: 0

                // the query row (`.palette .q`) -----------------------------------------
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.controlHeight + Theme.space(8)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space(14)
                        anchors.rightMargin: Theme.space(14)
                        spacing: Theme.space(10)

                        Glyph {
                            color: Theme.dimForeground
                            text: root.glyph
                            visible: root.glyph.length > 0
                        }

                        Item {
                            Layout.fillHeight: true
                            Layout.fillWidth: true

                            TextInput {
                                id: input

                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                clip: true
                                color: Theme.brightForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.body
                                selectByMouse: true
                                selectionColor: Theme.selection

                                cursorDelegate: Rectangle {
                                    color: Theme.accent
                                    width: 1
                                }

                                Keys.onPressed: event => root._onKey(event)
                            }

                            Label {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                dim: true
                                elide: Text.ElideRight
                                text: root.title
                                visible: root.title.length > 0 && input.text.length === 0
                            }
                        }

                        Label {
                            dim: true
                            size: "caption"
                            text: root.counter
                            visible: root.counter.length > 0
                        }
                    }

                    Hairline {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                    }
                }

                // the rows (`.palette .it`) ---------------------------------------------
                ListView {
                    id: list

                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, Theme.space(360))
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    model: root.items

                    delegate: Rectangle {
                        id: row

                        required property int index
                        required property var modelData

                        readonly property bool current: row.index === root.cursorIndex

                        color: row.current ? Theme.fill(Theme.fillHover) : "transparent"
                        height: rowColumn.implicitHeight + 2 * Theme.space(6)
                        width: ListView.view.width

                        Rectangle {
                            anchors.fill: parent
                            border.color: Theme.fill(Theme.borderHover)
                            border.width: 1
                            color: "transparent"
                            visible: row.current
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            color: root._rowRule
                            height: 1
                        }

                        RowLayout {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.space(14)
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space(14)
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.space(8)

                            Label {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: Theme.space(14)
                                bright: row.modelData.checked === true
                                dim: row.modelData.checked !== true
                                horizontalAlignment: Text.AlignHCenter
                                text: row.modelData.checked === true ? Glyphs.check : Glyphs.middot
                                visible: root.multi
                            }

                            ColumnLayout {
                                id: rowColumn

                                Layout.fillWidth: true
                                spacing: Theme.space(1)

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.space(8)

                                    Label {
                                        Layout.fillWidth: true
                                        bright: true
                                        elide: Text.ElideRight
                                        font.weight: Font.Medium
                                        text: row.modelData.title !== undefined
                                              ? row.modelData.title : ""
                                    }

                                    Label {
                                        dim: true
                                        size: "small"
                                        text: row.modelData.meta !== undefined ? row.modelData.meta :
                                                                                 ""
                                        visible: text.length > 0
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    dim: true
                                    elide: Text.ElideRight
                                    size: "small"
                                    text: row.modelData.detail !== undefined ? row.modelData.detail :
                                                                               ""
                                    visible: text.length > 0
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: false

                            onClicked: {
                                root.cursorIndex = row.index;
                                root.activate();
                            }
                        }
                    }
                }

                // the hint line (`.palette .ft`) ----------------------------------------
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: footerHints.implicitHeight + 2 * Theme.space(6)
                    visible: root.hints.length > 0 || root.footerRight.length > 0

                    KeyHints {
                        id: footerHints

                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(14)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space(14)
                        anchors.verticalCenter: parent.verticalCenter
                        model: root.hints
                        pinned: root.footerRight.length > 0 ? {
                                                                  "key": "",
                                                                  "label": root.footerRight
                                                              } : null
                        size: "caption"
                    }
                }
            }
        }
    }
