import QtQuick
import Omaseed.Kit

// #24 T7 (docs/22 §6): a seeding moment in the floating selection bar's spot, in the same
// frame: "<name> paid back · ratio 1.00 · 42 days", "<name> · 5×", "You are now the only seed
// of <name> · 2 waiting". One at a time, five seconds each, never over a selection (the host
// hides it then and the queue waits). A click shows the torrent.
Rectangle {
    id: root

    /// Whether the host lets it show now (nothing selected, the table on screen).
    property bool allowed: true
    property var _queue: []
    property var current: null

    signal showRequested(string hash)

    function push(kind: string, hash: string, title: string, detail: string): void {
        root._queue = root._queue.concat([{
                                               "kind": kind,
                                               "hash": hash,
                                               "title": title,
                                               "detail": detail
                                           }]).slice(-5);
        root._next();
    }

    function _next(): void {
        if (root.current !== null || !root.allowed || root._queue.length === 0)
            return;
        root.current = root._queue[0];
        root._queue = root._queue.slice(1);
        if (!hover.hovered)
            timer.restart();
    }

    /// Show the toast's torrent and go on to the next: a click, or a screen reader's press.
    function activate(): void {
        if (root.current !== null)
            root.showRequested(root.current.hash);
        root.current = null;
        timer.stop();
        root._next();
    }

    onAllowedChanged: root._next()

    // #77: a button named by its moment, the detail the description; its press is the click,
    // and does nothing while disabled.
    Accessible.description: root.current !== null ? root.current.detail : ""
    // #78: no moment, no toast: an unnamed button otherwise.
    Accessible.ignored: root.current === null
    Accessible.name: root.current !== null ? root.current.title : ""
    Accessible.role: Accessible.Button
    Accessible.onPressAction: if (root.enabled)
                                  root.activate()

    border.color: Qt.alpha(root.current !== null && root.current.kind === "lastSeed" ? Theme.yellow
                                                                                     : Theme.stateSeeding, 0.55)
    border.width: 1
    color: Theme.background
    implicitHeight: Theme.controlHeight + 2 * Theme.space(4)
    implicitWidth: row.implicitWidth + Theme.space(28)
    radius: Theme.cornerRadius
    visible: root.current !== null && root.allowed

    Timer {
        id: timer

        interval: 5000
        objectName: "seedToastTimer"

        onTriggered: {
            root.current = null;
            root._next();
        }
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: Theme.space(12)

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: root.current !== null && root.current.kind === "lastSeed" ? Theme.yellow : Theme.stateSeeding
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: "◆"
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            bright: true
            text: root.current !== null ? root.current.title : ""
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            dim: true
            size: "small"
            text: root.current !== null ? root.current.detail : ""
            visible: text.length > 0
        }
    }

    // The pointer resting on the toast holds its five seconds; they start over when it
    // leaves, so a toast being read is not taken away from under the pointer.
    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor

        onHoveredChanged: {
            if (hover.hovered)
                timer.stop();
            else if (root.current !== null)
                timer.restart();
        }
    }

    TapHandler {
        onTapped: root.activate()
    }
}
