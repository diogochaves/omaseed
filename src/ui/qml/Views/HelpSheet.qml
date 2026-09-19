pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The `?` sheet (spec v2 §7 Fig. 26): a head — `?` in a bordered square, "Shortcuts", the
// subtitle, and the `esc` chip on the right — over columns of key/description rows
// (`.helpsheet`: key column right-aligned in `brightForeground`, description dim, small).
// The spec's `repeat(auto-fit, minmax(200px,1fr))` is reproduced by counting how many
// `Theme.space(200)` columns fit, so the four groups sit side by side when there is room and
// wrap when there is not.
//
// Every row comes out of `Keymap`, so the sheet cannot drift from what the keys actually do:
// the groups are `Keymap.columns`, the rows `Keymap.helpRows(group)`. Nothing is written
// twice.
//
// This is the content only. The caller draws the scrim and the panel around it (Main does,
// until T4.1 brings a Kit `Sheet`).
Item {
    id: root

    signal closeRequested

    readonly property int _minColumn: Theme.space(200)
    readonly property int _gap: Theme.space(18)

    implicitHeight: layout.implicitHeight
    implicitWidth: Theme.space(700)

    Keys.onEscapePressed: event => {
        root.closeRequested();
        event.accepted = true;
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Question) {
            root.closeRequested();
            event.accepted = true;
        }
    }

    ColumnLayout {
        id: layout

        anchors.fill: parent
        spacing: Theme.space(12)

        // head (spec `.sheet .hd`) -------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space(10)

            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: Theme.space(30)
                Layout.preferredWidth: Theme.space(30)
                border.color: Theme.fill(Theme.borderIdle)
                border.width: 1
                color: "transparent"

                Text {
                    anchors.centerIn: parent
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.subtitle
                    text: Glyphs.help
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space(1)

                Text {
                    Layout.fillWidth: true
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.body
                    font.weight: Font.DemiBold
                    text: qsTr("Shortcuts")
                }

                Text {
                    Layout.fillWidth: true
                    color: Theme.dimForeground
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: qsTr(
                              "single letters act on the table · capitals are the stronger variant · ctrl is app-level")
                }
            }

            Text {
                Layout.alignment: Qt.AlignTop
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: "esc"
            }
        }

        // the groups ---------------------------------------------------------------
        GridLayout {
            Layout.fillWidth: true
            columnSpacing: root._gap
            columns: Math.max(1, Math.min(Keymap.columns.length, Math.floor((width + root._gap) / (
                                                                                root._minColumn
                                                                                + root._gap))))
            rowSpacing: Theme.space(18)

            Repeater {
                model: Keymap.columns

                ColumnLayout {
                    id: group

                    required property string modelData

                    readonly property var rows: Keymap.helpRows(group.modelData)
                    /// The widest key chip of this group sets its key column, so the chips
                    /// line up on the right the way `.helpsheet .k { text-align:right }` does.
                    readonly property string widestKeys: group.rows.reduce((widest, row)
                                                                           => row.helpKeys.length
                                                                              > widest.length
                                                                              ? row.helpKeys :
                                                                                widest, "")

                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: Theme.space(6)

                    TextMetrics {
                        id: keyMetrics

                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: group.widestKeys
                    }

                    Label {
                        Layout.fillWidth: true
                        text: group.modelData
                        tracked: true
                    }

                    Repeater {
                        model: group.rows

                        RowLayout {
                            id: helpRow

                            required property var modelData

                            Layout.fillWidth: true
                            spacing: Theme.space(14)

                            Text {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: Math.ceil(keyMetrics.width)
                                color: Theme.brightForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.small
                                horizontalAlignment: Text.AlignRight
                                text: helpRow.modelData.helpKeys
                            }

                            Text {
                                Layout.fillWidth: true
                                color: Theme.dimForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.small
                                text: helpRow.modelData.help
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }
}
