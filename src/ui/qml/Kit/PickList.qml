pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// A list of things to pick under a field (board 43 P1, DECISIONS C16 L4): rows in sections,
// one of them the cursor, and a line of keys at its foot. It takes no keyboard of its own: the
// field above it keeps typing, and hands it ↑↓ (`move`), tab and enter (`currentRow`).
//
//     PickList { rows: [{section: "recent", text: "/mnt/fast/films", detail: ""}, …] }
//
// A section's caption shows above its first row. A click picks a row (`picked`). Only Theme
// tokens: the 4 % fill of a sheet's blocks, the hover fill under the cursor row, a 12 %
// hairline round it.
Rectangle {
    id: root

    /// `[{section, text, detail}]`, top to bottom; rows of one section together.
    property var rows: []
    /// The cursor row, -1 for none.
    property int current: -1
    /// The keys at the foot, as KeyHints' model takes them.
    property var hints: []
    /// The line under the rows when the list was cut short ("12 more: type to narrow").
    property string more: ""

    readonly property var currentRow: root.current >= 0 && root.current < root.rows.length
                                      ? root.rows[root.current] : null

    /// A click on a row.
    signal picked(int index)

    /// Moves the cursor by `delta` rows, clamped; from none, down starts at the top.
    function move(delta: int): void {
        if (root.rows.length === 0) {
            root.current = -1;
            return;
        }
        if (root.current < 0) {
            root.current = delta > 0 ? 0 : root.rows.length - 1;
            return;
        }
        root.current = Math.max(0, Math.min(root.rows.length - 1, root.current + delta));
    }

    border.color: Theme.fill(Theme.hairlineAlpha)
    border.width: 1
    color: Theme.fill(Theme.fillIdle)
    implicitHeight: column.implicitHeight + 2 * Theme.space(4)
    objectName: "pickList"
    radius: Theme.cornerRadius

    onRowsChanged: {
        if (root.current >= root.rows.length)
            root.current = root.rows.length - 1;
    }

    ColumnLayout {
        id: column

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: Theme.space(4)
        spacing: 0

        Repeater {
            model: root.rows

            ColumnLayout {
                id: entry

                required property int index
                required property var modelData

                readonly property bool firstOfSection: entry.index === 0
                                                       || root.rows[entry.index - 1].section
                                                       !== entry.modelData.section

                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.leftMargin: Theme.space(10)
                    Layout.topMargin: entry.index === 0 ? 0 : Theme.space(4)
                    objectName: "pickListSection"
                    text: entry.modelData.section ?? ""
                    tracked: true
                    visible: entry.firstOfSection && text.length > 0
                }

                Rectangle {
                    id: rowBox

                    readonly property bool isCurrent: entry.index === root.current

                    Accessible.name: entry.modelData.text
                    Accessible.role: Accessible.ListItem
                    Accessible.selected: rowBox.isCurrent
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.railRowHeight
                    color: rowBox.isCurrent || rowMouse.containsMouse ? Theme.fill(Theme.fillHover)
                                                                      : "transparent"
                    objectName: "pickListRow"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space(10)
                        anchors.rightMargin: Theme.space(10)
                        spacing: Theme.space(8)

                        Label {
                            Layout.fillWidth: true
                            bright: rowBox.isCurrent
                            elide: Text.ElideLeft
                            size: "small"
                            text: entry.modelData.text
                        }

                        Label {
                            dim: true
                            size: "small"
                            text: entry.modelData.detail ?? ""
                            visible: text.length > 0
                        }
                    }

                    MouseArea {
                        id: rowMouse

                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true

                        onClicked: {
                            root.current = entry.index;
                            root.picked(entry.index);
                        }
                    }
                }
            }
        }

        Label {
            Layout.leftMargin: Theme.space(10)
            Layout.topMargin: Theme.space(2)
            dim: true
            objectName: "pickListMore"
            size: "small"
            text: root.more
            visible: text.length > 0
        }

        Hairline {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space(4)
            visible: root.hints.length > 0
        }

        KeyHints {
            Layout.leftMargin: Theme.space(10)
            Layout.topMargin: Theme.space(2)
            model: root.hints
            visible: root.hints.length > 0
        }
    }
}
