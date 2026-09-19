pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The anchored actions list of spec v2 §3 Fig. 11 (`.menu`, `.actions`): verb at the left,
// key at the right, separators between the groups. Opened at the cursor row on `enter` or by
// a right-click, so the caller sets `anchorX`/`anchorY` in this item's own coordinates; the
// panel clamps itself inside the item rather than hanging off the window edge.
//
// `items` is `[{text, glyph, key, action, disabled, danger}]`, or `{separator: true}` for a
// rule. `activated` carries `action` when the row has one and the text otherwise.
//
// Keys: `j`/`k` and up/down move over the rows (separators and disabled rows are skipped),
// `enter` runs the row under the cursor, a row's own key letter runs it straight away
// (Fig. 11: `c` rechecks without moving the cursor first), `esc` closes.
Item {
    id: root

    /// `[{text, glyph, key, action, disabled, danger}]` / `{separator: true}`.
    property var items: []
    /// Where the panel's top-left corner wants to be, in this item's coordinates.
    property int anchorX: 0
    property int anchorY: 0
    /// The spec's `.menu` is 280 wide at most.
    property int menuWidth: Theme.space(280)
    property int cursorIndex: 0
    /// Draw the panel alone, at 0,0, with no scrim (the gallery).
    property bool inline: false
    /// Whether the scrim behind the panel paints the spec's 70 % ground (Fig. 11) or stays
    /// invisible while still eating the clicks underneath.
    property bool dimmed: true

    readonly property bool opened: root._opened
    readonly property alias panel: panel

    /// The row's `action`, or its `text` when it has none.
    signal activated(string action)
    /// `esc`, or a click outside the panel.
    signal rejected

    property bool _opened: false
    property Item _previousFocus: null

    readonly property int _edge: Theme.space(8)

    function open(): void {
    if (root._opened)
    return;
    root._previousFocus = root.Window.activeFocusItem;
    root.cursorIndex = root._firstEnabled();
    root._opened = true;
    root.forceActiveFocus();
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

    /// Move over the rows, skipping separators and disabled rows; it does not wrap.
    function moveCursor(delta: int): void {
    let at = root.cursorIndex + delta;
    while (at >= 0 && at < root.items.length) {
        if (root.selectable(at)) {
            root.cursorIndex = at;
            return;
        }
            at += delta;
        }
        }

            /// True when row `index` is a real, enabled row.
            function selectable(index: int): bool {
                if (index < 0 || index >= root.items.length)
                return false;
                const item = root.items[index];
                return item.separator !== true && item.disabled !== true;
            }

                /// Run row `index`, if it can be run.
                function activate(index: int): void {
                if (!root.selectable(index))
                return;
                const item = root.items[index];
                root.close();
                root.activated(item.action !== undefined ? item.action : item.text);
            }

            /// The row whose own key chip is exactly `text`, or -1.
            function indexOfKey(text: string): int {
                for (let i = 0; i < root.items.length; ++i) {
                    if (root.selectable(i) && root.items[i].key === text)
                        return i;
                }
                return -1;
            }

            function _firstEnabled(): int {
                for (let i = 0; i < root.items.length; ++i) {
                    if (root.selectable(i))
                        return i;
                }
                return -1;
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
                    root.moveCursor(1);
                break;
                case Qt.Key_Up:
                    case Qt.Key_K:
                    root.moveCursor(-1);
                break;
                case Qt.Key_Return:
                    case Qt.Key_Enter:
                    root.activate(root.cursorIndex);
                break;
                default:
                {
                const at = root.indexOfKey(event.text);
                if (at < 0)
                return;
                root.activate(at);
            }
                break;
            }
                event.accepted = true;
            }

                implicitHeight: panel.implicitHeight
                implicitWidth: root.menuWidth
                visible: root.inline || root._opened

                Keys.onPressed: event => root._onKey(event)

                Scrim {
                    id: scrim

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

                    border.color: Theme.accent
                    border.width: 1
                    color: Theme.background
                    implicitHeight: rows.implicitHeight + 2 * panel.padding
                    radius: Theme.cornerRadius
                    width: Math.min(root.menuWidth, root.inline ? root.menuWidth : Math.max(0,
                                                                                            root.width
                                                                                            - 2 * root._edge))
                    x: root.inline ? 0 : Math.max(root._edge, Math.min(root.anchorX, root.width
                                                                       - width - root._edge))
                    y: root.inline ? 0 : Math.max(root._edge, Math.min(root.anchorY, root.height
                                                                       - height - root._edge))

                    ColumnLayout {
                        id: rows

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: panel.padding
                        spacing: 0

                        Repeater {
                            model: root.items

                            Item {
                                id: menuRow

                                required property int index
                                required property var modelData

                                readonly property bool current: menuRow.index === root.cursorIndex
                                readonly property bool disabled: menuRow.modelData.disabled === true
                                readonly property bool separator: menuRow.modelData.separator
                                                                  === true

                                Layout.fillWidth: true
                                Layout.preferredHeight: menuRow.separator ? 2 * Theme.space(4) + 1 :
                                                                            Theme.railRowHeight

                                Rectangle {
                                    anchors.fill: parent
                                    color: Theme.fill(Theme.fillHover)
                                    visible: menuRow.current && !menuRow.separator
                                }

                                Hairline {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.space(14)
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.space(14)
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: menuRow.separator
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.space(14)
                                    anchors.rightMargin: Theme.space(14)
                                    spacing: Theme.space(8)
                                    visible: !menuRow.separator

                                    Label {
                                        Layout.preferredWidth: Theme.space(14)
                                        dim: true
                                        horizontalAlignment: Text.AlignHCenter
                                        text: menuRow.modelData.glyph !== undefined
                                              ? menuRow.modelData.glyph : ""
                                        visible: text.length > 0
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        color: menuRow.disabled ? Theme.dimForeground :
                                                                  menuRow.modelData.danger === true
                                                                  ? Theme.red : menuRow.current
                                                                    ? Theme.accent :
                                                                      Theme.foreground
                                        elide: Text.ElideRight
                                        text: menuRow.modelData.text !== undefined
                                              ? menuRow.modelData.text : ""
                                    }

                                    Label {
                                        dim: true
                                        size: "caption"
                                        text: menuRow.modelData.key !== undefined
                                              ? menuRow.modelData.key : ""
                                        visible: text.length > 0
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !menuRow.separator && !menuRow.disabled
                                    hoverEnabled: true

                                    onClicked: root.activate(menuRow.index)
                                    onEntered: root.cursorIndex = menuRow.index
                                }
                            }
                        }
                    }
                }
            }
