pragma ComponentBehavior: Bound

import QtQuick
import Omaseed
import Omaseed.Kit

// `omaseed check <skin>`'s picture (DECISIONS C37 U5, docs/55 §6): one fixed sheet of the
// demo library's invented torrents, never a person's, in the skin being checked. The rows
// display (the cursor on one row, another picked, the errored one in its group), the table, a
// menu open, the delete confirm, a notice, and the four seams in each of their states. Every
// part is still (no animation, nothing to wait for) and has an objectName, so a test can find
// it and the picture is the same on every run. src/ui/Check.cpp shows it in a window
// `Theme.space(1440)` × `Theme.space(960)` and grabs that.
Rectangle {
    id: root

    /// Where the parts sit: a gutter round the sheet, and one between its two columns.
    readonly property int gutter: Theme.space(16)
    readonly property int leftWidth: Math.round((root.width - 3 * root.gutter) * 0.6)

    /// The cursor on the first row, the second picked, as a person leaves them.
    function markRows(): void {
        for (const table of [rowsTable, plainTable]) {
            table.moveCursorTo(1);
            table.toggleCursorSelection();
            table.moveCursorTo(0);
        }
    }

    color: Theme.background
    height: Theme.space(960)
    objectName: "checkSheet"
    width: Theme.space(1440)

    Component.onCompleted: Qt.callLater(root.markRows)

    // ---- the rows display (C24 H3), eight rows tall ------------------------------------
    Item {
        id: rowsPart

        clip: true
        height: Math.round((root.height - 3 * root.gutter) * 0.55)
        objectName: "checkRows"
        width: root.leftWidth
        x: root.gutter
        y: root.gutter

        TorrentTable {
            id: rowsTable

            anchors.fill: parent
            face: rowsFace
            filtered: App.filtered
            holdOrder: true
        }

        LibraryRows {
            id: rowsFace

            anchors.fill: parent
            table: rowsTable
        }
    }

    // ---- the table -------------------------------------------------------------------
    Item {
        clip: true
        height: root.height - rowsPart.height - 3 * root.gutter
        objectName: "checkTable"
        width: root.leftWidth
        x: root.gutter
        y: rowsPart.y + rowsPart.height + root.gutter

        TorrentTable {
            id: plainTable

            anchors.fill: parent
            filtered: App.filtered
            holdOrder: true
            keysHere: false
        }
    }

    // ---- the right column: a menu, a sheet, a notice, the seams ------------------------
    Column {
        id: rightColumn

        spacing: root.gutter
        width: root.width - root.leftWidth - 3 * root.gutter
        x: root.leftWidth + 2 * root.gutter
        y: root.gutter

        Row {
            spacing: root.gutter

            Menu {
                cursorIndex: 1
                heading: qsTr("2 selected")
                inline: true
                menuWidth: Theme.space(220)
                objectName: "checkMenu"
                items: [
                    {
                        "text": qsTr("start"),
                        "key": "p"
                    },
                    {
                        "text": qsTr("recheck"),
                        "key": "c"
                    },
                    {
                        "separator": true
                    },
                    {
                        "text": qsTr("set location"),
                        "key": "m"
                    },
                    {
                        "text": qsTr("rename"),
                        "key": "r",
                        "disabled": true
                    },
                    {
                        "separator": true
                    },
                    {
                        "text": qsTr("delete"),
                        "key": "d",
                        "danger": true
                    }
                ]
            }

            ConfirmDialog {
                cancelText: qsTr("Cancel")
                confirmText: qsTr("Delete")
                cursor: 1
                destructive: true
                inline: true
                message: qsTr("Delete 2 torrents and their files?")
                objectName: "checkConfirm"
                panelWidth: Math.min(Theme.space(370), rightColumn.width - Theme.space(220)
                                     - root.gutter)
                subtitle: qsTr("two invented torrents · 5.29 GB on disk")

                Toggle {
                    checked: true
                    text: qsTr("also delete files from disk")
                }
            }
        }

        Notice {
            animated: false
            body: qsTr("The server didn't answer: omaseed tries again on its own.")
            edge: "both"
            kind: "warning"
            objectName: "checkNotice"
            title: qsTr("home isn't answering")
            width: rightColumn.width
            actions: [
                {
                    "label": qsTr("retry now"),
                    "key": "retry"
                }
            ]
        }

        Grid {
            columnSpacing: root.gutter
            columns: 2
            objectName: "checkSeams"
            rowSpacing: Theme.space(10)

            // Plate: a panel, and a key.
            Label {
                dim: true
                text: qsTr("plate")
                tracked: true
            }
            Row {
                objectName: "checkPlate"
                spacing: root.gutter

                Repeater {
                    model: [false, true]

                    Plate {
                        required property bool modelData

                        base: Theme.background
                        fill: Theme.on.idle.fill
                        height: Theme.space(48)
                        key: modelData
                        width: Theme.space(120)
                    }
                }
            }

            // SelectionMark: the cursor's, and a pick's, on the grounds they mark.
            Label {
                dim: true
                text: qsTr("selection")
                tracked: true
            }
            Row {
                objectName: "checkSelectionMark"
                spacing: root.gutter

                Repeater {
                    model: [Theme.on.cursor, Theme.on.selected]

                    Rectangle {
                        id: marked

                        required property int index
                        required property ThemeInk modelData

                        color: marked.modelData.fill
                        height: Theme.space(32)
                        width: Theme.space(120)

                        SelectionMark {
                            anchors.fill: parent
                            color: Theme.accent
                            ground: marked.modelData
                            rule: marked.index === 1
                            ruleWidth: Theme.libraryRowSelectedEdge
                        }
                    }
                }
            }

            // Meter: none, 40 %, done, and the pieces grid.
            Label {
                dim: true
                text: qsTr("meter")
                tracked: true
            }
            Row {
                objectName: "checkMeter"
                spacing: root.gutter

                Repeater {
                    model: [0, 0.4, 1]

                    Meter {
                        required property real modelData

                        anchors.verticalCenter: parent.verticalCenter
                        height: Theme.space(4)
                        value: modelData
                        width: Theme.space(80)
                    }
                }

                Meter {
                    cellHeight: Theme.space(6)
                    cells: Array.from({
                                          "length": 48
                                      }, (_, i) => i < 20 ? 2 : i < 26 ? 1 : 0)
                    columns: 16
                    height: 3 * Theme.space(6) + 2 * Theme.meterCellGap
                    rows: 3
                    width: Theme.space(120)
                }
            }

            // Lamp: ok, down, refused, unknown.
            Label {
                dim: true
                text: qsTr("lamp")
                tracked: true
            }
            Row {
                objectName: "checkLamp"
                spacing: root.gutter

                Repeater {
                    model: [Theme.green, Theme.red, Theme.yellow, Theme.dimForeground]

                    Lamp {
                        required property color modelData

                        color: modelData
                    }
                }
            }
        }
    }
}
