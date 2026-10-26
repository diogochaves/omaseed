import QtQuick
import Omaseed.Kit

// One line of the story's "what happened" (board 46 `details()`'s `.tl div`, DECISIONS C24
// H6): the time in its column, a small square in the event's colour (the accent for a move,
// yellow for a failure, red for the error; #201 F19: a plain one, an add or a finish, is
// neutral, the dim foreground, so on a torrent in error "Added" doesn't read as the error),
// then the event over its dim line. `event` is one of `App.torrentStory()`'s maps.
Item {
    id: root

    /// `{time, kind ("plain", "move", "warn", "error"), title, detail}`.
    property var event: ({})
    readonly property color markColor: {
        switch (root.event.kind) {
        case "error":
            return Theme.red;
        case "warn":
            return Theme.yellow;
        case "move":
            return Theme.accent;
        default:
            return Theme.dimForeground;
        }
    }

    // #79: "09:15, Added, into films".
    Accessible.name: [root.event.time ?? "", root.event.title ?? "", root.event.detail ?? ""].filter(
        part => part.length > 0).join(", ")
    Accessible.role: Accessible.StaticText
    implicitHeight: Math.max(timeText.implicitHeight, words.implicitHeight)

    Text {
        id: timeText

        color: Theme.dimForeground
        elide: Text.ElideRight
        font.family: Theme.valueFamily
        font.features: ({
                            "tnum": 1
                        })
        font.pixelSize: Theme.detailsEventTitleSize
        text: root.event.time ?? ""
        textFormat: Text.PlainText
        width: Theme.detailsEventTimeWidth
    }

    Rectangle {
        color: root.markColor
        height: Theme.detailsEventMarkSize
        objectName: "storyMark"
        width: Theme.detailsEventMarkSize
        x: Theme.detailsEventTimeWidth + Theme.detailsEventGap
        y: Theme.detailsEventMarkTop
    }

    Column {
        id: words

        width: Math.max(0, root.width - words.x)
        x: Theme.detailsEventTimeWidth + Theme.detailsEventMarkWidth + 2 * Theme.detailsEventGap

        Text {
            color: Theme.foreground
            // Board 46 `.tl b`: the skin's name voice.
            font.family: Theme.labelFamily
            font.letterSpacing: Theme.detailsEventTitleSize * Theme.nameTrack
            font.pixelSize: Theme.detailsEventTitleSize
            font.weight: Theme.nameWeight
            objectName: "storyEventTitle"
            text: root.event.title ?? ""
            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.Wrap
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.valueFamily
            font.pixelSize: Theme.detailsEventDetailSize
            text: root.event.detail ?? ""
            textFormat: Text.PlainText
            visible: text.length > 0
            width: parent.width
            wrapMode: Text.Wrap
        }
    }
}
