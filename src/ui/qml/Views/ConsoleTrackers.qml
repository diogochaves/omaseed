import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The seed console's trackers table (docs/22 §4, qui's tracker breakdown plus history and
// rarity): one row per tracker host in the scope, biggest given first. Columns: tracker
// (with a health dot), torrents, on offer, given (with a bar), buffer, seeded ×, rare,
// 30 days (a sparkline from the ledger), status. A click scopes the console to the host.
Column {
    id: root

    property var rows: []

    signal activated(string host)

    readonly property var columns: [
        { key: "host", title: qsTr("tracker"), width: 0 },
        { key: "torrents", title: qsTr("torrents"), width: 72 },
        { key: "offer", title: qsTr("on offer"), width: 86 },
        { key: "given", title: qsTr("given"), width: 132 },
        { key: "buffer", title: qsTr("buffer"), width: 92 },
        { key: "seeded", title: qsTr("seeded ×"), width: 80 },
        { key: "rare", title: qsTr("rare"), width: 52 },
        { key: "spark", title: qsTr("30 days"), width: 110 },
        { key: "status", title: qsTr("status"), width: 104 }
    ]

    function statusColor(role: string): color {
        return role === "error" ? Theme.stateError : role === "warn" ? Theme.yellow : Theme.dimForeground;
    }

    RowLayout {
        spacing: 0
        width: parent.width

        Repeater {
            model: root.columns

            delegate: Label {
                required property var modelData
                required property int index

                Layout.fillWidth: modelData.width === 0
                Layout.leftMargin: index === 0 ? Theme.space(8) : 0
                Layout.preferredWidth: modelData.width === 0 ? -1 : Theme.space(modelData.width)
                horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                rightPadding: index === root.columns.length - 1 ? Theme.space(8) : Theme.space(10)
                text: modelData.title
                tracked: true
            }
        }
    }

    Hairline {
        width: parent.width
    }

    // Counted, not handed the rows: the givens change on nearly every refresh while you seed,
    // and a Repeater makes its delegates again for every new array (#50). Each row reads its
    // entry, so new numbers only update its texts.
    Repeater {
        model: root.rows.length

        delegate: Item {
            id: row

            required property int index
            readonly property var modelData: root.rows[row.index] ?? ({})

            // #77: a list item named by the tracker, its status the description; its press is
            // the click, and does nothing while disabled.
            Accessible.description: row.modelData.status ?? ""
            Accessible.name: row.modelData.host ?? ""
            Accessible.role: Accessible.ListItem
            Accessible.onPressAction: if (row.enabled)
                                          root.activated(row.modelData.host)
            height: Theme.space(30)
            objectName: "consoleTracker_" + row.index
            width: root.width

            Rectangle {
                anchors.fill: parent
                color: hover.hovered ? Theme.fill(Theme.fillHover) : "transparent"
            }

            Rectangle {
                anchors.bottom: parent.bottom
                color: Theme.fill(Theme.fillIdle)
                height: 1
                width: parent.width
            }

            RowLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space(8)
                    spacing: Theme.space(8)

                    Rectangle {
                        color: row.modelData.statusRole === "error" ? Theme.stateError
                             : row.modelData.statusRole === "warn" ? Theme.yellow : Theme.stateSeeding
                        height: Theme.space(6)
                        radius: height / 2
                        width: height
                    }

                    Text {
                        id: hostText

                        Layout.fillWidth: true
                        color: Theme.foreground
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        objectName: "consoleTrackerHost"
                        text: row.modelData.host

                        HoverHandler {
                            id: hostHover
                        }

                        // The kit's one tooltip, as KeyValue's: the full text
                        // while it is cut off and the pointer is on it.
                        Tooltip {
                            id: hostTip

                            readonly property bool wanted: hostText.truncated && hostHover.hovered

                            objectName: "consoleTrackerTip"
                            parent: hostText.Window.window ? hostText.Window.window.contentItem : hostText
                            z: 1000

                            onWantedChanged: hostTip.wanted ? hostTip.showFor(hostText, hostText.text, false)
                                                           : hostTip.hide()
                        }
                    }
                }

                Text {
                    Layout.preferredWidth: Theme.space(72)
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    rightPadding: Theme.space(10)
                    text: row.modelData.torrents
                }

                Text {
                    Layout.preferredWidth: Theme.space(86)
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    rightPadding: Theme.space(10)
                    text: row.modelData.offerText
                }

                // Right-aligned by its own layout direction: a spacer item here would make
                // the whole column fill, as RowLayout passes fillWidth up from a child.
                RowLayout {
                    Layout.fillWidth: false
                    Layout.maximumWidth: Theme.space(132)
                    Layout.minimumWidth: Theme.space(132)
                    Layout.preferredWidth: Theme.space(132)
                    layoutDirection: Qt.RightToLeft
                    spacing: Theme.space(6)

                    Text {
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        rightPadding: Theme.space(10)
                        text: row.modelData.givenText
                    }

                    Rectangle {
                        Layout.preferredHeight: Theme.space(3)
                        Layout.preferredWidth: Math.max(1, Theme.space(40) * row.modelData.fraction)
                        color: Theme.stateSeeding
                        opacity: 0.6
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }

                Text {
                    Layout.preferredWidth: Theme.space(92)
                    color: row.modelData.bufferNegative ? Theme.stateError : Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    rightPadding: Theme.space(10)
                    text: row.modelData.bufferText
                }

                Text {
                    Layout.preferredWidth: Theme.space(80)
                    color: row.modelData.seededGood ? Theme.stateSeeding : Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    rightPadding: Theme.space(10)
                    text: row.modelData.seededText
                }

                Text {
                    Layout.preferredWidth: Theme.space(52)
                    color: row.modelData.rare > 0 ? Theme.yellow : Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    rightPadding: Theme.space(10)
                    text: row.modelData.rare > 0 ? row.modelData.rare : "—"
                }

                Item {
                    Layout.preferredHeight: Theme.space(18)
                    Layout.preferredWidth: Theme.space(110)

                    Sparkline {
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space(10)
                        fillAlpha: 0.12
                        lineAlpha: 1
                        values: row.modelData.spark ?? []
                        visible: (row.modelData.spark ?? []).length > 1
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space(10)
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.caption
                        text: qsTr("from today")
                        visible: (row.modelData.spark ?? []).length <= 1
                    }
                }

                Text {
                    Layout.preferredWidth: Theme.space(104)
                    color: root.statusColor(row.modelData.statusRole)
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    rightPadding: Theme.space(8)
                    text: row.modelData.status
                }
            }

            HoverHandler {
                id: hover

                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: root.activated(row.modelData.host)
            }
        }
    }
}
