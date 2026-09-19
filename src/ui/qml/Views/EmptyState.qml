pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The teaching empty state (spec v2 §7 Fig. 22): a centred column with the headline, the
// `Press a, drop a file here, or click a magnet link anywhere.` note (its `a` set as a key
// chip, the same look `KeyHints` gives a key: `foreground`, tracked, `font.caption`), and a
// left-aligned three-row key/description list (`.helpsheet`: key column right-aligned dim
// bright, description column dim, `Theme.space(6)`/`Theme.space(14)` gaps — reused here as a
// vertical `Theme.space(10)` gap between rows per the task brief). Every row is clickable
// (hover fill, like a `RailRow`) and emits its own signal; the caller (Main.qml) owns what
// happens next — opening the add dialog, settings, or the `?` help sheet.
Item {
    id: root

    signal addRequested
    signal helpRequested
    signal settingsRequested

    implicitHeight: Theme.space(280)
    implicitWidth: Theme.space(400)

    readonly property var _rows: [
        {
            "key": "a",
            "label": qsTr("add a torrent"),
            "signal": "add"
        },
        {
            "key": ",",
            "label": qsTr("settings and servers"),
            "signal": "settings"
        },
        {
            "key": "?",
            "label": qsTr("every shortcut"),
            "signal": "help"
        }
    ]

    /// What a click on `_rows[index]` does — exposed so `tst_States.qml` can exercise the
    /// row's outcome directly (`mouseClick()` never delivers under the offscreen
    /// qmltestrunner, the same limitation T3.2's Rail report found for RailRow).
    function triggerRow(index) {
        const kind = root._rows[index].signal;
        if (kind === "add")
            root.addRequested();
        else if (kind === "settings")
            root.settingsRequested();
        else
            root.helpRequested();
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.space(10)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.subtitle
            font.weight: Font.DemiBold
            text: qsTr("No torrents yet")
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space(4)

            Text {
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: qsTr("Press")
            }

            Text {
                color: Theme.foreground
                font.capitalization: Font.AllUppercase
                font.family: Theme.fontFamily
                font.letterSpacing: Math.max(1, Math.round(Theme.font.caption * 0.08))
                font.pixelSize: Theme.font.caption
                text: "a"
            }

            Text {
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: qsTr(", drop a file here, or click a magnet link anywhere.")
            }
        }

        Column {
            id: rowsColumn

            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space(10)
            topPadding: Theme.space(6)

            Repeater {
                model: root._rows

                Rectangle {
                    id: row

                    required property int index
                    required property var modelData

                    readonly property bool hot: mouseArea.containsMouse

                    color: row.hot ? Theme.fill(Theme.fillHover) : "transparent"
                    height: keyValueRow.implicitHeight + Theme.space(6)
                    width: keyValueRow.implicitWidth + Theme.space(16)

                    Row {
                        id: keyValueRow

                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.space(14)
                        x: Theme.space(8)

                        Text {
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.small
                            horizontalAlignment: Text.AlignRight
                            text: row.modelData.key
                            width: Theme.space(20)
                        }

                        Text {
                            color: Theme.dimForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.small
                            text: row.modelData.label
                        }
                    }

                    MouseArea {
                        id: mouseArea

                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true

                        onClicked: root.triggerRow(row.index)
                    }
                }
            }
        }
    }
}
