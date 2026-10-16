pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The hero's filters on its last step (docs/15 Q1 A): the kit Menu's panel with the name
// filter on top, one row per active filter (glyph, text, ✕), a rule and "clear all filters".
// The caller hangs it under the hero's `/` button and owns the state: it passes the filters
// in (`filters`, the hero's `[{key, glyph, text}]`) and the name filter (`text`), and reacts
// to the signals.
//
// Keys: the caret starts in the name filter, where typing goes. `down`/`tab` leave it for the
// rows; there `j`/`k` and the arrows move, `enter` or `x` clears the row under the cursor,
// and any other letter goes back to the field and types. `enter` in the field keeps the
// filter and closes; `esc` closes from anywhere.
//
// Taller than the item it clamps itself in, the panel is cut to it and its content scrolls,
// as the kit Menu's rows do: the wheel moves it and the cursor keeps itself in view.
Item {
    id: root

    /// `[{key, glyph, text}]`, one row each.
    property var filters: []
    /// The name filter. Read when the menu opens; edits come back through `textEdited`.
    property string text: ""
    /// What the field's trailing count prints ("12 of 142").
    property string countText: ""
    /// Where the panel's top-left corner wants to be, in this item's coordinates.
    property int anchorX: 0
    property int anchorY: 0
    property int menuWidth: Theme.space(280)
    /// -1 is the field; 0…n-1 the filters; n "clear all filters".
    property int cursorIndex: -1
    /// Draw the panel alone, at 0,0, with no scrim (the gallery).
    property bool inline: false
    property bool dimmed: true
    /// Keys the menu has no use for stop here (KIT-6); only `ctrl+q` goes on.
    property bool modal: true

    readonly property bool opened: root._opened
    readonly property alias panel: panel
    readonly property alias field: field
    /// The Flickable the panel's content scrolls in, for the tests.
    readonly property alias scroller: scroller

    signal textEdited(string text)
    /// The ✕ of one filter, by its `key`.
    signal filterCleared(string key)
    /// "clear all filters": the rows and the name filter.
    signal allCleared
    /// `enter` in the field: the filter stays, the menu has closed.
    signal accepted
    /// `esc`, or a click outside the panel.
    signal rejected

    property bool _opened: false
    property Item _previousFocus: null
    readonly property int _edge: Theme.space(8)
    readonly property int _clearAllIndex: root.filters.length

    function open(): void {
    if (root._opened)
    return;
    root._previousFocus = root.Window.activeFocusItem;
    field.text = root.text;
    root.cursorIndex = -1;
    root._opened = true;
    scroller.contentY = 0;
    field.focusInput();
}

    /// Open the menu with its top-left corner at `x`, `y` (clamped inside this item).
    function openAt(x: int, y: int): void {
    root.anchorX = x;
    root.anchorY = y;
    root.open();
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

    /// Move the cursor between the field (-1) and the rows; it does not wrap.
    function moveCursor(delta: int): void {
    root.cursorIndex = Math.max(-1, Math.min(root._clearAllIndex, root.cursorIndex + delta));
    if (root.cursorIndex < 0)
    field.focusInput();
    else
    root.forceActiveFocus();
    root._showCursor();
}

    /// Scroll the content so the cursor's row (or the field) is in view. Only the keyboard
    /// asks: what the pointer is on is in view already.
    function _showCursor(): void {
        const at = root.cursorIndex;
        const row = at < 0 ? field : at === root._clearAllIndex ? clearAllRow
                                                                 : filterRepeater.itemAt(at);
        if (row === null)
            return;
        if (row.y < scroller.contentY)
            scroller.contentY = row.y;
        else if (row.y + row.height > scroller.contentY + scroller.height)
            scroller.contentY = row.y + row.height - scroller.height;
    }

    /// Clear the row `index`: one filter, or all of them.
    function activate(index: int): void {
    if (index === root._clearAllIndex) {
        root.close();
        root.allCleared();
        return;
    }
        if (index < 0 || index >= root.filters.length)
        return;
        root.filterCleared(root.filters[index].key);
    }

        function _passesThrough(event: var): bool {
            return (event.modifiers & Qt.ControlModifier) !== 0 && event.key === Qt.Key_Q;
        }

            function _onKey(event: var): void {
            if ((event.modifiers & Qt.ControlModifier) !== 0)
            return;
            switch (event.key) {
                case Qt.Key_Escape:
                    root.close();
                root.rejected();
                break;
                case Qt.Key_Down:
                    case Qt.Key_J:
                    case Qt.Key_Tab:
                    root.moveCursor(1);
                break;
                case Qt.Key_Up:
                    case Qt.Key_K:
                    case Qt.Key_Backtab:
                    root.moveCursor(-1);
                break;
                case Qt.Key_Return:
                    case Qt.Key_Enter:
                    case Qt.Key_X:
                    root.activate(root.cursorIndex);
                break;
                default:
                // Anything printable goes to the name filter.
                if (event.text.length === 0 || event.text.charCodeAt(0) < 32)
                return;
                root.cursorIndex = -1;
                field.text += event.text;
                field.focusInput();
                break;
            }
                event.accepted = true;
            }

                // A filter cleared from here leaves the list: keep the cursor on a real row, and close
                // once no filter is left for the menu to hold.
                onFiltersChanged: {
                    if (!root._opened)
                        return;
                    if (root.filters.length === 0) {
                        root.close();
                        return;
                    }
                    // `filters.length`, not `_clearAllIndex`: that binding may not have caught up yet.
                    if (root.cursorIndex >= root.filters.length)
                        root.cursorIndex = Math.max(0, root.filters.length - 1);
                }

                implicitHeight: panel.implicitHeight
                implicitWidth: root.menuWidth
                visible: root.inline || root._opened

                Keys.onPressed: event => {
                    root._onKey(event);
                    if (root.modal && root._opened && !event.accepted)
                        event.accepted = !root._passesThrough(event);
                }

                Scrim {
                    anchors.fill: parent
                    opacity: root.dimmed ? 1 : 0
                    visible: !root.inline

                    onDismissed: {
                        root.close();
                        root.rejected();
                    }
                }

                Rectangle {
                    id: panel

                    readonly property int padding: Theme.space(6)

                    Accessible.role: Accessible.PopupMenu
                    border.color: Theme.accent
                    border.width: 1
                    color: Theme.background
                    implicitHeight: column.implicitHeight + 2 * panel.padding
                    // Never taller than the item it clamps itself in: the content scrolls.
                    height: root.inline ? panel.implicitHeight
                                        : Math.min(panel.implicitHeight, Math.max(0, root.height
                                                                                  - 2 * root._edge))
                    radius: Theme.cornerRadius
                    width: Math.min(root.menuWidth, root.inline ? root.menuWidth : Math.max(0,
                                                                                            root.width
                                                                                            - 2 * root._edge))
                    x: root.inline ? 0 : Math.max(root._edge, Math.min(root.anchorX, root.width
                                                                       - width - root._edge))
                    y: root.inline ? 0 : Math.max(root._edge, Math.min(root.anchorY, root.height
                                                                       - height - root._edge))

                    Flickable {
                        id: scroller

                        anchors.fill: parent
                        anchors.bottomMargin: panel.padding
                        anchors.topMargin: panel.padding
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true
                        contentHeight: column.implicitHeight
                        contentWidth: width
                        interactive: contentHeight > height
                        objectName: "filterMenuScroller"

                        ColumnLayout {
                            id: column

                            spacing: 0
                            width: scroller.width

                            Field {
                                id: field

                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.space(8)
                                Layout.rightMargin: Theme.space(8)
                                // `esc` closes the menu rather than wiping the filter under the
                                // caret.
                                escClears: false
                                hasCursor: root.cursorIndex < 0
                                hint: Glyphs.search
                                objectName: "filterMenuField"
                                placeholder: qsTr("filter by name")
                                trailing: root.countText

                                onAccepted: {
                                    root.close();
                                    root.accepted();
                                }
                                onTextChanged: {
                                    if (root._opened)
                                        root.textEdited(field.text);
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 2 * Theme.space(4) + 1

                                Hairline {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.space(14)
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.space(14)
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Repeater {
                                id: filterRepeater

                                model: root.filters

                                Item {
                                    id: filterRow

                                    required property int index
                                    required property var modelData

                                    readonly property bool current: filterRow.index
                                                                    === root.cursorIndex

                                    Accessible.name: filterRow.modelData.text
                                    Accessible.role: Accessible.MenuItem
                                    // A screen reader's press is the click (#75); disabled,
                                    // it does nothing, as the pointer.
                                    Accessible.onPressAction: if (filterRow.enabled)
                                                                  root.activate(filterRow.index)
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Theme.railRowHeight
                                    objectName: "filterMenuRow_" + filterRow.modelData.key

                                    Rectangle {
                                        anchors.fill: parent
                                        color: Theme.fill(Theme.fillHover)
                                        visible: filterRow.current
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: Theme.space(14)
                                        anchors.rightMargin: Theme.space(14)
                                        spacing: Theme.space(8)

                                        Glyph {
                                            Layout.preferredHeight: Theme.font.body
                                            Layout.preferredWidth: Theme.space(14)
                                            color: Theme.dimForeground
                                            text: filterRow.modelData.glyph ?? ""
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            color: filterRow.current ? Theme.accent
                                                                     : Theme.foreground
                                            elide: Text.ElideRight
                                            text: filterRow.modelData.text ?? ""
                                        }

                                        Label {
                                            color: filterRow.current ? Theme.accent :
                                                                       Theme.dimForeground
                                            text: Glyphs.close
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        hoverEnabled: true

                                        onClicked: root.activate(filterRow.index)
                                        onEntered: root.cursorIndex = filterRow.index
                                    }
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 2 * Theme.space(4) + 1

                                Hairline {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.space(14)
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.space(14)
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Item {
                                id: clearAllRow

                                readonly property bool current: root.cursorIndex
                                                                === root._clearAllIndex

                                Accessible.name: clearAllText.text
                                Accessible.role: Accessible.MenuItem
                                // As a filter row's (#75).
                                Accessible.onPressAction: if (clearAllRow.enabled)
                                                              root.activate(root._clearAllIndex)
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.railRowHeight
                                objectName: "filterMenuClearAll"

                                Rectangle {
                                    anchors.fill: parent
                                    color: Theme.fill(Theme.fillHover)
                                    visible: clearAllRow.current
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.space(14)
                                    anchors.rightMargin: Theme.space(14)
                                    spacing: Theme.space(8)

                                    Glyph {
                                        Layout.preferredHeight: Theme.font.body
                                        Layout.preferredWidth: Theme.space(14)
                                        color: Theme.dimForeground
                                        text: Glyphs.close
                                    }

                                    Label {
                                        id: clearAllText

                                        Layout.fillWidth: true
                                        color: clearAllRow.current ? Theme.accent : Theme.foreground
                                        elide: Text.ElideRight
                                        text: qsTr("clear all filters")
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    hoverEnabled: true

                                    onClicked: root.activate(root._clearAllIndex)
                                    onEntered: root.cursorIndex = root._clearAllIndex
                                }
                            }
                        }
                    }

                    WheelScroll {
                        anchors.fill: scroller
                        flickable: scroller
                        rowHeight: Theme.railRowHeight
                    }

                    ScrollBar {
                        flickable: scroller
                        height: scroller.height
                        objectName: "filterMenuScrollBar"
                        vertical: true
                        width: Theme.space(4)
                        x: panel.width - width - panel.border.width - Theme.space(2)
                        y: scroller.y
                    }

                    // The panel's own ground (its padding, the gaps between rows) takes the
                    // clicks nothing on it wanted. Without it they fell through to the Scrim
                    // underneath, and a click inside the menu closed it.
                    MouseArea {
                        // Last, at z -1: under the rows, and `children[0]` is still the layout.
                        acceptedButtons: Qt.AllButtons
                        anchors.fill: parent
                        z: -1

                        onWheel: wheel => wheel.accepted = true
                    }
                }
            }
