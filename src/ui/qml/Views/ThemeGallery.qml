import QtQuick
import Omaseed.Kit

// Every token the Theme singleton exposes, on one scrollable page (spec v2 §8). This is the
// proof that the theme follows Omarchy: run `omaseed --demo gallery`, switch the theme with
// `omarchy-theme-set`, and every swatch, size and alpha below repaints without a restart.
Window {
    id: root

    // Colour roles, in the order the spec lists them. The bindings read Theme directly, so
    // the whole list is rebuilt when the theme changes.
    readonly property var colourRoles: [
        {
            "name": "background",
            "value": Theme.background
        },
        {
            "name": "darkBackground",
            "value": Theme.darkBackground
        },
        {
            "name": "darkerBackground",
            "value": Theme.darkerBackground
        },
        {
            "name": "lighterBackground",
            "value": Theme.lighterBackground
        },
        {
            "name": "foreground",
            "value": Theme.foreground
        },
        {
            "name": "dimForeground",
            "value": Theme.dimForeground
        },
        {
            "name": "brightForeground",
            "value": Theme.brightForeground
        },
        {
            "name": "accent",
            "value": Theme.accent
        },
        {
            "name": "muted",
            "value": Theme.muted
        },
        {
            "name": "selection",
            "value": Theme.selection
        },
        {
            "name": "red",
            "value": Theme.red
        },
        {
            "name": "yellow",
            "value": Theme.yellow
        },
        {
            "name": "orange",
            "value": Theme.orange
        },
        {
            "name": "green",
            "value": Theme.green
        },
        {
            "name": "cyan",
            "value": Theme.cyan
        },
        {
            "name": "blue",
            "value": Theme.blue
        },
        {
            "name": "magenta",
            "value": Theme.magenta
        },
        {
            "name": "brown",
            "value": Theme.brown
        }
    ]
    readonly property var stateRoles: [
        {
            "name": "stateDownloading",
            "value": Theme.stateDownloading
        },
        {
            "name": "stateSeeding",
            "value": Theme.stateSeeding
        },
        {
            "name": "stateStopped",
            "value": Theme.stateStopped
        },
        {
            "name": "stateError",
            "value": Theme.stateError
        },
        {
            "name": "stateChecking",
            "value": Theme.stateChecking
        },
        {
            "name": "stateQueued",
            "value": Theme.stateQueued
        },
        {
            "name": "stateStalled",
            "value": Theme.stateStalled
        }
    ]
    readonly property var typeScale: [
        {
            "name": "caption ×0.833",
            "size": Theme.font.caption
        },
        {
            "name": "small ×0.917",
            "size": Theme.font.small
        },
        {
            "name": "body ×1",
            "size": Theme.font.body
        },
        {
            "name": "subtitle ×1.083",
            "size": Theme.font.subtitle
        },
        {
            "name": "title ×1.167",
            "size": Theme.font.title
        },
        {
            "name": "heading ×1.333",
            "size": Theme.font.heading
        },
        {
            "name": "display ×2",
            "size": Theme.font.display
        },
        {
            "name": "iconSmall",
            "size": Theme.font.iconSmall
        },
        {
            "name": "icon",
            "size": Theme.font.icon
        }
    ]
    readonly property var surfaceStates: [
        {
            "name": "idle",
            "fill": Theme.fillIdle,
            "border": Theme.borderIdle
        },
        {
            "name": "hover / cursor",
            "fill": Theme.fillHover,
            "border": Theme.borderHover
        },
        {
            "name": "selected",
            "fill": Theme.fillSelected,
            "border": 0
        },
        {
            "name": "pressed",
            "fill": Theme.fillPressed,
            "border": 0
        },
        {
            "name": "hairline",
            "fill": Theme.hairlineAlpha,
            "border": 0
        }
    ]
    readonly property var spacingSteps: [2, 4, 6, 8, 12, 16, 24, 32]

    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.windowAlpha)
    height: 820
    title: qsTr("omaseed — theme gallery")
    visible: true
    width: 1180

    // The 1 px window edge, as spec v2 §1: accent while active, muted otherwise.
    Rectangle {
        anchors.fill: parent
        border.color: root.active ? Theme.accent : Theme.muted
        border.width: 1
        color: "transparent"
        radius: Theme.cornerRadius
        z: 1
    }

    Flickable {
        id: page

        anchors.fill: parent
        anchors.margins: Theme.space(18)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        contentHeight: body.implicitHeight
        contentWidth: width

        Column {
            id: body

            spacing: Theme.space(22)
            width: page.width

            // ---- head ----------------------------------------------------------------
            Column {
                spacing: Theme.space(4)
                width: parent.width

                Text {
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.heading
                    font.weight: Font.DemiBold
                    text: Theme.themeName
                }

                Text {
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: qsTr("%1 · base %2 px · radius %3 px · window alpha %4 · %5").arg(
                              Theme.mode).arg(Theme.font.baseSize).arg(Theme.cornerRadius).arg(
                              Theme.windowAlpha.toFixed(2)).arg(Theme.fontFamily)
                }
            }

            // ---- colour roles --------------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("colour roles")
                }

                Flow {
                    spacing: Theme.space(10)
                    width: parent.width

                    Repeater {
                        model: root.colourRoles

                        Swatch {
                            required property var modelData

                            role: modelData.name
                            value: modelData.value
                        }
                    }
                }
            }

            // ---- state roles ---------------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("state roles")
                }

                Flow {
                    spacing: Theme.space(10)
                    width: parent.width

                    Repeater {
                        model: root.stateRoles

                        Swatch {
                            required property var modelData

                            role: modelData.name
                            value: modelData.value
                        }
                    }
                }
            }

            // ---- type scale ----------------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("type scale")
                }

                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    Repeater {
                        model: root.typeScale

                        Row {
                            id: typeRow

                            required property var modelData

                            spacing: Theme.space(14)

                            Text {
                                color: Theme.dimForeground
                                font.capitalization: Font.AllUppercase
                                font.family: Theme.fontFamily
                                font.letterSpacing: Theme.font.caption * 0.1
                                font.pixelSize: Theme.font.caption
                                horizontalAlignment: Text.AlignRight
                                text: typeRow.modelData.name
                                width: Theme.space(130)
                                y: Theme.space(4)
                            }

                            Text {
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: typeRow.modelData.size
                                text: qsTr("%1 px · seeding · ↓ 4.21 MB/s").arg(
                                          typeRow.modelData.size)
                            }
                        }
                    }
                }
            }

            // ---- fills and borders ---------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("fills and borders")
                }

                Row {
                    spacing: Theme.space(10)
                    width: parent.width

                    Repeater {
                        model: root.surfaceStates

                        Rectangle {
                            id: stateBox

                            required property var modelData

                            border.color: Theme.fill(stateBox.modelData.border)
                            border.width: stateBox.modelData.border > 0 ? 1 : 0
                            color: Theme.fill(stateBox.modelData.fill)
                            height: Theme.controlHeight
                            radius: Theme.cornerRadius
                            width: Theme.space(180)

                            Text {
                                anchors.centerIn: parent
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.small
                                text: qsTr("%1 · %2 %").arg(stateBox.modelData.name).arg(Math.round(
                                                                                             stateBox.modelData.fill
                                                                                             * 100))
                            }
                        }
                    }
                }
            }

            // ---- rows ----------------------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("row heights")
                }

                Row {
                    spacing: Theme.space(10)

                    GalleryBox {
                        boxHeight: Theme.rowHeight
                        caption: qsTr("rowHeight %1").arg(Theme.rowHeight)
                    }

                    GalleryBox {
                        boxHeight: Theme.controlHeight
                        caption: qsTr("controlHeight %1").arg(Theme.controlHeight)
                    }

                    GalleryBox {
                        boxHeight: Theme.railRowHeight
                        caption: qsTr("railRowHeight %1").arg(Theme.railRowHeight)
                    }
                }
            }

            // ---- spacing -------------------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("space()")
                }

                Row {
                    spacing: Theme.space(10)

                    Repeater {
                        model: root.spacingSteps

                        Column {
                            id: stepColumn

                            required property int modelData

                            spacing: Theme.space(4)

                            Rectangle {
                                color: Theme.accent
                                height: Theme.space(24)
                                width: Math.max(1, Theme.space(stepColumn.modelData))
                            }

                            Text {
                                color: Theme.dimForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.caption
                                text: qsTr("%1→%2").arg(stepColumn.modelData).arg(Theme.space(
                                                                                      stepColumn.modelData))
                            }
                        }
                    }
                }
            }
        }
    }

    // A tracked caption label, the spec's section heading.
    component GalleryLabel: Item {
        id: label

        property alias text: labelText.text

        implicitHeight: labelText.implicitHeight + Theme.space(8)
        implicitWidth: labelText.implicitWidth

        Text {
            id: labelText

            color: Theme.dimForeground
            font.capitalization: Font.AllUppercase
            font.family: Theme.fontFamily
            font.letterSpacing: Theme.font.caption * 0.1
            font.pixelSize: Theme.font.caption
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            color: Theme.fill(Theme.hairlineAlpha)
            height: 1
        }
    }

    // One colour role: the colour itself, its name and its hex.
    component Swatch: Column {
        id: swatch

        required property string role
        required property color value

        spacing: Theme.space(4)

        Rectangle {
            border.color: Theme.fill(Theme.hairlineAlpha)
            border.width: 1
            color: swatch.value
            height: Theme.space(46)
            radius: Theme.cornerRadius
            width: Theme.space(130)
        }

        Text {
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: swatch.role
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: swatch.value.toString()
        }
    }

    // A box whose height is one of the height tokens, with the number under it.
    component GalleryBox: Column {
        id: box

        required property int boxHeight
        required property string caption

        spacing: Theme.space(4)

        Rectangle {
            border.color: Theme.fill(Theme.borderIdle)
            border.width: 1
            color: Theme.fill(Theme.fillIdle)
            height: box.boxHeight
            radius: Theme.cornerRadius
            width: Theme.space(200)
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: box.caption
        }
    }
}
