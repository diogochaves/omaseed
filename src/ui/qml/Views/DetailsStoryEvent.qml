import QtQuick
import Omaseed.Kit

// One line of the story's "what happened" (board 46 `details()`'s `.tl div`, DECISIONS C24
// H6): the time in its column, a small square in the event's colour (the torrent's state for an
// add or a finish, the accent for a move, yellow for a failure, red for the error), then the
// event over its dim line. `event` is one of `App.torrentStory()`'s maps.
Item {
    id: root

    /// `{time, kind ("plain", "move", "warn", "error"), title, detail}`.
    property var event: ({})
    /// The torrent's state colour, for a plain event's square.
    property color stateColor: Theme.foreground

    readonly property color markColor: root.event.kind === "error" ? Theme.red : root.event.kind
                                                                     === "warn" ? Theme.yellow :
                                                                                  root.event.kind
                                                                                  === "move"
                                                                                  ? Theme.accent :
                                                                                    root.stateColor

    // #79: "09:15, Added, into films".
    Accessible.name: [root.event.time ?? "", root.event.title ?? "", root.event.detail ?? ""].filter(
        part => part.length > 0).join(", ")
    Accessible.role: Accessible.StaticText
    implicitHeight: Math.max(timeText.implicitHeight, words.implicitHeight)

    Text {
        id: timeText

        color: Theme.dimForeground
        elide: Text.ElideRight
        font.family: Theme.fontFamily
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
            font.family: Theme.fontFamily
            font.pixelSize: Theme.detailsEventTitleSize
            objectName: "storyEventTitle"
            text: root.event.title ?? ""
            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.Wrap
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.detailsEventDetailSize
            text: root.event.detail ?? ""
            textFormat: Text.PlainText
            visible: text.length > 0
            width: parent.width
            wrapMode: Text.Wrap
        }
    }
}
