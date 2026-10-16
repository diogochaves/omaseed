pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// docs/26 S1–S3/L1: two (or more) choice rows under the shared-data line, one of them picked.
// `j`/`k` (or the arrows) move the pick while the rows hold the keyboard, a click picks a row,
// and `tab` reaches the rows from the dialog's own cycle. The dialog around it says what the
// pick does in its button.
//
//     SharedChoice { rows: [{title: "keep the files", detail: "…"}, {title: "…", detail: "…"}] }
FocusScope {
    id: root

    /// `[{title, detail}]`, top to bottom.
    property var rows: []
    /// The picked row.
    property int choice: 0

    /// Move the pick by `delta` rows, clamped.
    function move(delta: int): void {
    root.choice = Math.max(0, Math.min(root.rows.length - 1, root.choice + delta));
}

    Layout.fillWidth: true
    activeFocusOnTab: true
    implicitHeight: column.implicitHeight

    Keys.onPressed: event => {
        if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
            root.move(1);
            event.accepted = true;
        } else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
            root.move(-1);
            event.accepted = true;
        }
    }

    ColumnLayout {
        id: column

        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.space(2)

        Repeater {
            model: root.rows

            delegate: Rectangle {
                id: row

                required property int index
                required property var modelData

                readonly property bool picked: row.index === root.choice

                /// Pick this row: a click, or a screen reader's press or toggle.
                function pick(): void {
                    if (row.enabled)
                        root.choice = row.index;
                }

                // #77: a radio button, named by its title, the detail as its description.
                Accessible.checkable: true
                Accessible.checked: row.picked
                Accessible.description: row.modelData.detail ?? ""
                Accessible.name: row.modelData.title
                Accessible.role: Accessible.RadioButton
                Accessible.onPressAction: row.pick()
                Accessible.onToggleAction: row.pick()
                Layout.fillWidth: true
                objectName: "sharedChoice_" + row.index
                border.color: Qt.alpha(Theme.foreground, row.picked ? Theme.borderHover : 0)
                border.width: 1
                color: row.picked ? Qt.alpha(Theme.foreground, Theme.fillHover) : "transparent"
                implicitHeight: rowLayout.implicitHeight + Theme.space(10)
                radius: Theme.cornerRadius

                RowLayout {
                    id: rowLayout

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.space(8)
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space(8)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space(8)

                    Label {
                        Layout.alignment: Qt.AlignTop
                        color: row.picked ? Theme.accent : Theme.dimForeground
                        size: "small"
                        text: row.picked ? "●" : "○"
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            Layout.fillWidth: true
                            bright: row.picked
                            size: "small"
                            text: row.modelData.title
                            wrapMode: Text.WordWrap
                        }

                        Label {
                            Layout.fillWidth: true
                            dim: true
                            size: "caption"
                            text: row.modelData.detail ?? ""
                            visible: text.length > 0
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor

                    onClicked: row.pick()
                }
            }
        }
    }

    FocusRing {}
}
