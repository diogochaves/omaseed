pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The details pane's tab strip (spec v2 §3 Fig. 8 `.tabs`): General/Files/Trackers/Peers,
// each with a `1`–`4` key hint and an optional error badge (`Trackers 3 ●`, spec §3 Fig. 10).
// `model` is `[{text, key, badge}]`; `badge` is any truthy value (the tracker-error count) and
// renders as a small yellow dot next to the label. The current tab (`currentIndex`) is
// `brightForeground` with a 1 px bottom line; the others are dim. Clicking a tab updates
// `currentIndex` and emits `activated(index)`.
Item {
    id: root

    property var model: []
    property int currentIndex: 0

    signal activated(int index)

    readonly property int _tabHeight: Math.round(Theme.font.body * 2.2)
    /// How far the row slides left so the current tab stays in view when the tabs are wider
    /// than the strip (Settings' nine qBittorrent sections, folded: docs/16 §2). The caller
    /// clips.
    readonly property real _shift: {
        // itemAt() is not a binding: the count and the row's width say when to look again.
        void tabRepeater.count;
        void row.width;
        const tab = tabRepeater.itemAt(root.currentIndex);
        if (!tab)
            return 0;
        const room = root.width - Theme.space(10);
        return Math.max(0, tab.x + tab.width - room);
    }

    height: implicitHeight
    implicitHeight: root._tabHeight

    Row {
        id: row

        anchors.top: parent.top
        spacing: 0
        x: Theme.space(10) - root._shift

        Repeater {
            id: tabRepeater

            model: root.model

            Item {
                id: tab

                required property var modelData
                required property int index

                readonly property bool current: tab.index === root.currentIndex
                readonly property bool hot: tab.current || mouseArea.containsMouse

                Accessible.name: tab.modelData.text
                Accessible.role: Accessible.PageTab
                Accessible.selected: tab.current
                height: root._tabHeight
                implicitWidth: content.implicitWidth + Theme.space(16)

                Row {
                    id: content

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.space(8)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space(6)

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        color: tab.current ? Theme.brightForeground : Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: tab.modelData.text
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        color: tab.current ? Theme.brightForeground : Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.caption
                        text: tab.modelData.key ? tab.modelData.key : ""
                        visible: !!tab.modelData.key
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.yellow
                        height: Theme.space(6)
                        radius: width / 2
                        visible: !!tab.modelData.badge
                        width: Theme.space(6)
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    color: Theme.brightForeground
                    height: 1
                    visible: tab.current
                }

                MouseArea {
                    id: mouseArea

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true

                    onClicked: {
                        root.currentIndex = tab.index;
                        root.activated(tab.index);
                    }
                }
            }
        }
    }

    Hairline {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }
}
