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

    /// The wheel over the strip moves the current tab, a notch a tab, as a click would
    /// (Settings' folded sections, which clip at the right edge and only slide to keep the
    /// current one in view). Off by default: the details pane's four tabs always fit.
    property bool wheelSteps: false

    signal activated(int index)

    /// Make tab `index` the current one and say so: what a click and a screen reader's press
    /// do (#75).
    function select(index: int): void {
        root.currentIndex = index;
        root.activated(index);
    }

    /// Move the current tab `delta` tabs, stopping at the ends, and say so as a click does.
    function step(delta: int): void {
        const to = Math.max(0, Math.min(tabRepeater.count - 1, root.currentIndex + delta));
        if (to < 0 || to === root.currentIndex)
            return;
        root.currentIndex = to;
        root.activated(to);
    }

    /// A wheel's angle, in eighths of a degree: 120 a notch, smaller pieces from a fine wheel
    /// or a touchpad adding up to one. Down or right is the next tab.
    function _wheel(angleX: real, angleY: real): void {
        root._wheelRest += angleY !== 0 ? angleY : angleX;
        while (root._wheelRest <= -120) {
            root._wheelRest += 120;
            root.step(1);
        }
        while (root._wheelRest >= 120) {
            root._wheelRest -= 120;
            root.step(-1);
        }
    }

    property real _wheelRest: 0

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

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        enabled: root.wheelSteps
        target: null

        onWheel: event => root._wheel(event.angleDelta.x, event.angleDelta.y)
    }

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
                // A screen reader's press is the click (#75); disabled, it does nothing, as
                // the pointer.
                Accessible.onPressAction: if (tab.enabled)
                                              root.select(tab.index)
                height: root._tabHeight
                implicitWidth: content.implicitWidth + Theme.space(16)
                objectName: "tab_" + tab.index

                Row {
                    id: content

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.space(8)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space(6)

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        color: tab.current ? Theme.brightForeground : Theme.dimForeground
                        // Board 46 `.tab`: the skin's caption voice.
                        font.capitalization: Theme.captionUpper ? Font.AllUppercase : Font.MixedCase
                        font.family: Theme.labelFamily
                        font.letterSpacing: Theme.font.small * Theme.captionTrack(0)
                        font.pixelSize: Theme.font.small
                        font.weight: Theme.captionWeight(Font.Normal)
                        text: tab.modelData.text
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        color: tab.current ? Theme.brightForeground : Theme.dimForeground
                        font.family: Theme.valueFamily
                        font.pixelSize: Theme.font.caption
                        text: tab.modelData.key ? tab.modelData.key : ""
                        visible: !!tab.modelData.key
                    }

                    // The skin's lamp (Console's square, board 46 `.dot`).
                    Lamp {
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.yellow
                        size: Theme.space(6)
                        visible: !!tab.modelData.badge
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

                    onClicked: root.select(tab.index)
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
