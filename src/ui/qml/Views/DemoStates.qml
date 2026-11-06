pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit
import Omaseed.Views

// `--demo states` host: the demo library is already loaded into `App` by main.cpp (T3.6 does
// not use it — every quadrant is a static reproduction of its spec figure, not driven by
// live App data, since the whole point is showing the *other* states a live app can be in).
// Four quadrants side by side (spec v2 §7 Fig. 22-25) so a screenshot can be compared
// straight against the figures. `OMASEED_DEMO_STATE=empty|disconnected|filtered|loading`
// shows one quadrant's state full-window instead, for the phase-exit screenshot procedure.
Window {
    id: root

    readonly property string _only: DemoEnv.value("OMASEED_DEMO_STATE")

    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.windowAlpha)
    height: Theme.space(800)
    title: qsTr("omaseed — states demo")
    visible: true
    width: Theme.space(1360)

    // Opens at that size and keeps it, as Main.qml does: Theme.space() follows the base size
    // live (#156), and a demo window left bound resized itself on a change (#184).
    Component.onCompleted: {
        root.width = root.width;
        root.height = root.height;
    }

    // ---- shared bits reused across the four figures ---------------------------------
    component HeroLine: RowLayout {
        id: heroLine

        required property color dotColor
        required property color markColor
        required property string serverLine

        spacing: Theme.space(8)

        Glyph {
            Layout.preferredHeight: Theme.space(18)
            Layout.preferredWidth: Theme.space(18)
            color: heroLine.markColor
            pixelSize: Theme.space(18)
            text: Glyphs.mark
        }

        Text {
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.body
            font.weight: Font.DemiBold
            text: qsTr("omaseed")
        }

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: Theme.space(6)
            Layout.preferredWidth: Theme.space(6)
            color: heroLine.dotColor
            radius: width / 2
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: heroLine.serverLine
        }
    }

    component FigureFoot: RowLayout {
        id: foot

        required property string factsText
        property var hints: []
        property var pinnedHint: null

        spacing: Theme.space(16)

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: foot.factsText
        }

        Item {
            Layout.fillWidth: true
        }

        KeyHints {
            Layout.preferredWidth: Theme.space(220)
            model: foot.hints
            pinned: foot.pinnedHint
            visible: foot.hints.length > 0
        }
    }

    component Figure: Rectangle {
        id: figure

        default property alias body: bodySlot.children
        required property string caption

        border.color: Theme.muted
        border.width: Theme.hairWidth
        color: Theme.on.idle.fill

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                id: bodySlot

                Layout.fillHeight: true
                Layout.fillWidth: true
            }

            Text {
                Layout.alignment: Qt.AlignLeft
                Layout.leftMargin: Theme.space(4)
                // docs/DECISIONS.md C30 F1: the caption is on the figure's idle fill.
                color: Theme.on.idle.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.caption
                text: figure.caption
            }
        }
    }

    // ---- the four quadrants -----------------------------------------------------------
    GridLayout {
        anchors.fill: parent
        anchors.margins: Theme.space(20)
        columnSpacing: Theme.space(20)
        columns: root._only.length > 0 ? 1 : 2
        rowSpacing: Theme.space(20)

        // Fig. 22 — empty
        Figure {
            Layout.fillHeight: true
            Layout.fillWidth: true
            caption: qsTr("Fig. 22 — Empty server, the teaching empty state.")
            visible: root._only.length === 0 || root._only === "empty"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.space(10)

                    HeroLine {
                        dotColor: Theme.green
                        markColor: Theme.accent
                        serverLine: qsTr("home")
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }

                Hairline {
                    Layout.fillWidth: true
                }

                EmptyState {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                }

                Hairline {
                    Layout.fillWidth: true
                }

                FigureFoot {
                    Layout.fillWidth: true
                    Layout.margins: Theme.space(10)
                    factsText: qsTr("0 torrents · 1.8 TB free")
                    hints: [
                        {
                            "key": "a",
                            "label": qsTr("add")
                        }
                    ]
                    pinnedHint: ({
                                     "key": "?",
                                     "label": qsTr("help")
                                 })
                }
            }
        }

        // Fig. 23 — disconnected
        Figure {
            Layout.fillHeight: true
            Layout.fillWidth: true
            caption: qsTr("Fig. 23 — Disconnected: the last list stays, dimmed and read-only.")
            visible: root._only.length === 0 || root._only === "disconnected"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.space(10)

                    HeroLine {
                        dotColor: Theme.red
                        markColor: Theme.red
                        serverLine: qsTr("home · unreachable")
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Text {
                        color: Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: qsTr("↓ —")
                    }

                    Text {
                        color: Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: qsTr("↑ —")
                    }
                }

                Hairline {
                    Layout.fillWidth: true
                }

                DisconnectedBanner {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space(16)
                    Layout.rightMargin: Theme.space(16)
                    Layout.topMargin: Theme.space(10)
                    // The words App.serverProblem (core/ErrorWords) gives a refused connection.
                    problem: ({
                                  "headline": qsTr("home is unreachable"),
                                  "lineBase": qsTr("Connection refused at 127.0.0.1:8080. Is qBittorrent running?"),
                                  "retry": qsTr("omaseed tries again in 7 s."),
                                  "retryChars": 28,
                                  "details": "network error · GET sync/maindata · Connection refused",
                                  "kind": "unreachable"
                              })
                }

                // The dimmed, read-only list behind the banner (Fig. 23): two lines standing
                // in for the last known table, at the figure's 0.45 opacity.
                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.margins: Theme.space(10)
                    opacity: 0.45
                    spacing: Theme.space(2)

                    Text {
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: qsTr("archlinux-2026.09.01-x86_64.iso … 48.9 % · last known")
                    }

                    Text {
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: qsTr("ubuntu-26.04.1-desktop-amd64.iso … 100 %")
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }

                Hairline {
                    Layout.fillWidth: true
                }

                FigureFoot {
                    Layout.fillWidth: true
                    Layout.margins: Theme.space(10)
                    factsText: qsTr("last sync 09:12")
                    hints: [
                        {
                            "key": "R",
                            "label": qsTr("retry")
                        },
                        {
                            "key": ",",
                            "label": qsTr("settings")
                        }
                    ]
                }
            }
        }

        // Fig. 24 — filtered empty
        Figure {
            Layout.fillHeight: true
            Layout.fillWidth: true
            caption: qsTr("Fig. 24 — Filter with no result.")
            visible: root._only.length === 0 || root._only === "filtered"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.space(10)

                    HeroLine {
                        dotColor: Theme.green
                        markColor: Theme.accent
                        serverLine: qsTr("home")
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Field {
                        Layout.preferredWidth: Theme.space(220)
                        accessibleName: qsTr("filter torrents")
                        hint: Glyphs.search
                        text: qsTr("fedora 45")
                        trailing: qsTr("0 of 142")
                    }
                }

                Hairline {
                    Layout.fillWidth: true
                }

                FilteredEmpty {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    scopeName: ""
                    statusName: qsTr("Downloading")
                    text: qsTr("fedora 45")
                }

                Hairline {
                    Layout.fillWidth: true
                }

                FigureFoot {
                    Layout.fillWidth: true
                    Layout.margins: Theme.space(10)
                    factsText: qsTr("0 of 142")
                    hints: [
                        {
                            "key": "esc",
                            "label": qsTr("clear")
                        }
                    ]
                }
            }
        }

        // Fig. 25 — loading
        Figure {
            Layout.fillHeight: true
            Layout.fillWidth: true
            caption: qsTr("Fig. 25 — Loading. Skeleton rules, then incremental diffs only.")
            visible: root._only.length === 0 || root._only === "loading"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.space(10)

                    HeroLine {
                        dotColor: Theme.yellow
                        markColor: Theme.accent
                        serverLine: qsTr("seedbox · first sync")
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }

                Hairline {
                    Layout.fillWidth: true
                }

                Item {
                    Layout.fillHeight: true
                    Layout.fillWidth: true

                    LoadingState {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(16)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space(16)
                        anchors.top: parent.top
                        anchors.topMargin: Theme.space(12)
                        receivedText: qsTr("1.2 MB")
                        torrentCount: 8400
                    }
                }

                Hairline {
                    Layout.fillWidth: true
                }

                FigureFoot {
                    Layout.fillWidth: true
                    Layout.margins: Theme.space(10)
                    factsText: qsTr("syncing")
                }
            }
        }
    }
}
