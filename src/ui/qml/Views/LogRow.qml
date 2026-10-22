import QtQuick
import Omaseed
import Omaseed.Kit

// One line of qBittorrent's log on the log face (docs/32 §6 L1): time, level and the whole
// message, wrapped. The level's colour is its Theme role (warning yellow, critical red, info
// accent, normal dim), and the quoted parts of the message, where qBittorrent puts a torrent's
// name, are bright so the torrent stands out in a long line. The cursor's line has the hover
// fill and a hairline edge; a marked line the selected fill.
Item {
    id: root

    required property int index
    required property int level
    required property string levelName
    required property string timeText
    required property string message
    /// Board 45 B2 (C18 D7): one of omaseed's own lines, a move it made: "move back…" at the
    /// end. "" on qBittorrent's.
    required property string moveId
    required property bool marked
    /// The view's cursor is on this line.
    property bool current: false
    /// The view's column widths, so every row lines up under the header.
    property real timeWidth: Theme.space(118)
    property real levelWidth: Theme.space(84)

    signal clicked(int index, int modifiers)
    /// Board 45 B2: "move back…" on a move's line.
    signal moveBackRequested(string id)

    // #77: a list item named by its message, its level and time the description, selected
    // while marked; its press is a plain click, and does nothing while disabled.
    Accessible.description: root.levelName + " · " + root.timeText
    Accessible.name: root.message
    Accessible.role: Accessible.ListItem
    Accessible.selectable: true
    Accessible.selected: root.marked
    Accessible.onPressAction: if (root.enabled)
                                  root.clicked(root.index, Qt.NoModifier)

    /// A level's Theme role.
    function levelColor(level: int): color {
        switch (level) {
        case ServerLog.Critical:
            return Theme.red;
        case ServerLog.Warning:
            return Theme.yellow;
        case ServerLog.Info:
            return Theme.accent;
        default:
            return Theme.dimForeground;
        }
    }

    /// The message as styled text: escaped, its "quoted" parts in `bright`.
    function styled(message: string, bright: color): string {
        const escaped = message.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        return escaped.replace(/"([^"]*)"/g, "<font color=\"" + bright + "\">\"$1\"</font>");
    }

    implicitHeight: Math.max(messageText.implicitHeight, timeLabel.implicitHeight) + Theme.space(6)

    Rectangle {
        anchors.fill: parent
        border.color: Theme.fill(Theme.fillSelected)
        border.width: root.current ? 1 : 0
        color: root.marked ? Theme.fill(Theme.fillSelected) : root.current || mouse.containsMouse
                                                              ? Theme.fill(Theme.fillHover) : "transparent"
        objectName: "logRowFill"
    }

    Text {
        id: timeLabel

        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.features: {
            "tnum": 1
        }
        font.pixelSize: Theme.font.small
        objectName: "logTime"
        text: root.timeText
        width: root.timeWidth
        x: Theme.space(16)
        y: Theme.space(3)
    }

    Text {
        color: root.levelColor(root.level)
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        objectName: "logLevel"
        text: root.levelName
        width: root.levelWidth
        x: timeLabel.x + root.timeWidth + Theme.space(14)
        y: Theme.space(3)
    }

    Text {
        id: messageText

        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        objectName: "logMessage"
        text: root.styled(root.message, Theme.brightForeground)
        textFormat: Text.StyledText
        width: root.width - x - Theme.space(16) - (moveBack.visible ? moveBack.width + Theme.space(12) : 0)
        wrapMode: Text.WrapAnywhere
        x: timeLabel.x + root.timeWidth + root.levelWidth + 2 * Theme.space(14)
        y: Theme.space(3)
    }

    // a11y-ignore A2: the line itself is the list item; the lint misreads `styled()`'s regex (#77)
    MouseArea {
        id: mouse

        anchors.fill: parent
        hoverEnabled: true

        onClicked: event => root.clicked(root.index, event.modifiers)
    }

    // Board 45 B2: over the row's own click area, so a click on it opens the sheet only. A list
    // item with a control inside is read as the control alone, so the link carries the line.
    Text {
        id: moveBack

        Accessible.name: qsTr("move back: %1").arg(root.message)
        Accessible.role: Accessible.Link
        Accessible.onPressAction: root.moveBackRequested(root.moveId)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(16)
        color: moveBackMouse.containsMouse ? Theme.brightForeground : Theme.accent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        objectName: "logMoveBack"
        text: qsTr("move back…")
        visible: root.moveId.length > 0
        y: Theme.space(3)

        MouseArea {
            id: moveBackMouse

            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            onClicked: root.moveBackRequested(root.moveId)
        }
    }
}
