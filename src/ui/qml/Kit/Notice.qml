pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The error notice (docs/33 §10 D21's content, in docs/35's look, ruled 2026-10-16): a band
// the full width of what holds it, between that thing's head and its body. Its own 4 % fill,
// a 2 px rule in the kind's colour on the left edge, and a 12 % hairline on the side that
// meets the content (`edge`: "bottom" under the hero or a sheet's head, "top" docked above the
// footer, "both" in a sheet, docs/35 N1, N2, N7, N8).
//
//   row 1   the kind's glyph, the headline (bright, demibold), the actions at the right
//           (accent; "details ▸" dim, last)
//   row 2   the line under it, aligned with the headline's first letter; it wraps, never
//           elides
//   details the technical text in a further 4 % block, `copy` at its bottom right (N4); a long
//           one stops at `detailsMaxHeight` (a third of the window) and scrolls inside it,
//           clipped, so the band never grows past it and what is below moves down (#173)
//
// Kinds (N3): "warning" is the yellow "!" while omaseed retries on its own, "error" the red
// "✕" when it has stopped and needs you or an action failed, "info" the accent "·". Board 42
// (DECISIONS C15 M10, M16) adds "success", the green "✓" of a move's summary; `glyph` puts
// another sign in the kind's place ("↻" while a folder moves), `progress` draws a 2 px line
// along the bottom edge in the kind's colour, and `bodyAlert` runs on after the line in red
// ("2 failed").
//
// Narrow (N5): under `stackWidth` the actions drop to a row under the line. `bodyTail` (the
// retry's countdown) is padded to `bodyTailChars` characters in the monospace font, so the
// countdown counting never changes how the line wraps.
//
// Coming and going (N6): `shown` grows the band from nothing to its height in 120 ms and back;
// `animated: false` (reduced motion) does it at once. The caller decides when it is shown.
//
// Only Theme tokens. The caller does what an action's key says (`actionTriggered`) and copies
// (`copyRequested`).
Item {
    id: root

    /// "error" | "warning" | "info" | "success".
    property string kind: "error"
    /// Drawn under the line (#146: a creation's file chip). Null draws nothing.
    property Component extra: null
    /// The sign at the left; "" draws the kind's own.
    property string glyph: ""
    /// 0–1 draws the progress line on the bottom edge; below 0 draws none.
    property real progress: -1
    /// Red, after the line and its tail: "2 failed".
    property string bodyAlert: ""
    property string title: ""
    property string body: ""
    /// Run on after `body` in a slot of `bodyTailChars` characters: "omaseed tries again in 7 s."
    property string bodyTail: ""
    property int bodyTailChars: 0
    /// `[{label, key}]`, accent links at the right of the headline. At most two (D21).
    property var actions: []
    /// The technical text; "" leaves out "details".
    property string details: ""
    /// The link that unfolds `details` (board 44 F1: "Why?" over a short note).
    property string detailsLabel: qsTr("details")
    /// `copy` under the details; a note that is not technical text has none.
    property bool copyable: true
    property bool detailsOpen: false
    /// Rows above the details' text, each with its own link: `[{text, link}]` (board 43 J3:
    /// the moves waiting behind this one, each with "cancel"). A link says `detailRowTriggered`.
    property var detailRows: []
    /// Where the hairline goes: "bottom", "top" or "both".
    property string edge: "bottom"
    /// N6: false folds the band away.
    property bool shown: true
    /// N6: false (reduced motion) comes and goes at once.
    property bool animated: true
    /// N5: below this width the actions move under the line.
    property real stackWidth: Theme.space(600)
    /// N8: docked over the content (above the footer), the band is opaque under its fill.
    property bool opaque: false
    /// #173: the unfolded details stop here and scroll inside: a third of the window, never
    /// less than a few lines.
    property real detailsMaxHeight: Math.max(Theme.space(96), root.Window.height / 3)

    readonly property bool stacked: root.width < root.stackWidth
    readonly property color kindColor: root.kind === "error" ? Theme.on.idle.red : root.kind === "warning"
                                                               ? Theme.on.idle.yellow : root.kind
                                                                 === "success" ? Theme.on.idle.green :
                                                                                 Theme.on.idle.accent
    readonly property string kindGlyph: root.glyph.length > 0 ? root.glyph : root.kind === "error"
                                                                ? Glyphs.close : root.kind
                                                                  === "warning" ? "!" : root.kind
                                                                                  === "success"
                                                                                  ? Glyphs.check :
                                                                                    Glyphs.middot
    /// The line as drawn: `body`, then the tail in its fixed slot.
    readonly property string lineText: {
        if (root.bodyTail.length === 0)
            return root.body;
        let tail = root.bodyTail;
        while (tail.length < root.bodyTailChars)
            tail += " ";
        return root.body.length > 0 ? root.body + " " + tail : tail;
    }
    /// The full height the band takes while shown.
    readonly property real fullHeight: content.implicitHeight + Theme.space(9) + Theme.space(10)
    readonly property alias detailsBlock: detailsBlock
    readonly property alias detailsScroll: detailsScroll

    signal actionTriggered(string key)
    signal copyRequested(string text)
    signal detailRowTriggered(int index)

    /// Plain text made safe for StyledText.
    function _escaped(text: string): string {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    function toggleDetails(): void {
    root.detailsOpen = !root.detailsOpen;
}

    Accessible.name: root.title
    Accessible.role: Accessible.AlertMessage
    clip: true
    height: root.shown ? root.fullHeight : 0
    implicitHeight: root.shown ? root.fullHeight : 0
    visible: root.height > 0

    Behavior on height {
        enabled: root.animated

        NumberAnimation {
            duration: 120
            easing.type: Easing.OutCubic
        }
    }

    Keys.onEscapePressed: event => {
        if (root.detailsOpen) {
            root.detailsOpen = false;
            event.accepted = true;
        } else {
            event.accepted = false;
        }
    }

    // The box: its own fill, the kind's rule, the hairline where it meets the content.
    Rectangle {
        anchors.fill: parent
        color: root.opaque ? Theme.background : "transparent"
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.on.idle.fill
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        color: root.kindColor
        objectName: "noticeRule"
        width: Theme.space(2)
    }

    Hairline {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.edge !== "top"
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        color: root.kindColor
        height: Theme.space(2)
        objectName: "noticeProgress"
        visible: root.progress >= 0
        width: parent.width * Math.max(0, Math.min(1, root.progress))
    }

    Hairline {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        visible: root.edge !== "bottom"
    }

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(2) + Theme.space(16)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.top: parent.top
        anchors.topMargin: Theme.space(9)
        spacing: Theme.space(3)

        RowLayout {
            id: headRow

            Layout.fillWidth: true
            spacing: Theme.space(8)

            Text {
                id: glyph

                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: Theme.space(12)
                color: root.kindColor
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.body
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                objectName: "noticeGlyph"
                text: root.kindGlyph
            }

            Text {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                color: Theme.on.idle.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.body
                font.weight: Font.DemiBold
                objectName: "noticeTitle"
                text: root.title
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }

            Item {
                id: actionsBeside

                Layout.alignment: Qt.AlignTop
                implicitHeight: actionLinks.implicitHeight
                implicitWidth: actionLinks.implicitWidth
                objectName: "noticeActionsBeside"
                visible: !root.stacked
            }
        }

        Text {
            // #79: the line under the headline, in the alert. Named by `body`, which holds
            // still; the tail counts down every second, so it is the description.
            Accessible.description: root.body.length > 0 ? root.bodyTail : ""
            Accessible.name: root.body.length > 0 ? root.body : root.bodyTail
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            Layout.leftMargin: glyph.Layout.preferredWidth + Theme.space(8)
            color: Theme.on.idle.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "noticeLine"
            text: root.bodyAlert.length === 0 ? root.lineText : root._escaped(root.lineText) + (
                                                    root.lineText.length > 0 ? " · " : "")
                                                + "<font color='%1'>%2</font>".arg(Theme.on.idle.red).arg(
                                                    root._escaped(root.bodyAlert))
            textFormat: root.bodyAlert.length === 0 ? Text.PlainText : Text.StyledText
            visible: text.length > 0
            wrapMode: Text.Wrap
        }

        // #146 (board 49 `.notice .fchip`): something of the caller's under the line, from the
        // headline's first letter (a creation's saved .torrent, to drag). Null draws nothing.
        Loader {
            Layout.leftMargin: glyph.Layout.preferredWidth + Theme.space(8)
            Layout.maximumWidth: content.width - Layout.leftMargin
            active: root.extra !== null
            objectName: "noticeExtra"
            sourceComponent: root.extra
            visible: active
        }

        Item {
            id: actionsUnder

            Layout.leftMargin: glyph.Layout.preferredWidth + Theme.space(8)
            Layout.topMargin: Theme.space(3)
            implicitHeight: actionLinks.implicitHeight
            implicitWidth: actionLinks.implicitWidth
            objectName: "noticeActionsUnder"
            visible: root.stacked
        }

        // N4: the details unfold inside the band. #173: up to detailsMaxHeight; past it they
        // scroll inside the block, clipped, so a move of hundreds of torrents keeps its band a
        // band and the body below it moves down instead of drawing over the list.
        Rectangle {
            id: detailsBlock

            Layout.fillWidth: true
            Layout.leftMargin: glyph.Layout.preferredWidth + Theme.space(8)
            Layout.topMargin: Theme.space(4)
            color: Theme.on.blockIdle.fill
            implicitHeight: Math.min(detailsColumn.implicitHeight + 2 * Theme.space(6),
                                     root.detailsMaxHeight)
            objectName: "noticeDetails"
            visible: root.detailsOpen && (root.details.length > 0 || root.detailRows.length > 0)

            Flickable {
                id: detailsScroll

                /// One line of the details, for the keys.
                readonly property real step: Theme.font.caption * 1.5
                readonly property real maxY: Math.max(0, contentHeight - height)

                function scrollTo(y: real): void {
                    detailsScroll.contentY = Math.max(0, Math.min(detailsScroll.maxY, y));
                }

                // Tab reaches it only when there is something to scroll (half a pixel is
                // rounding); esc goes on up to the band, which folds it.
                activeFocusOnTab: detailsScroll.activeFocus || (detailsBlock.visible
                                                                 && detailsScroll.maxY > 0.5)
                anchors.fill: parent
                anchors.margins: Theme.space(6)
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                contentHeight: detailsColumn.implicitHeight
                contentWidth: width
                objectName: "noticeDetailsScroll"

                Keys.onPressed: event => {
                    const page = Math.max(detailsScroll.step, detailsScroll.height
                                          - detailsScroll.step);
                    if (event.key === Qt.Key_Down || event.key === Qt.Key_J)
                        detailsScroll.scrollTo(detailsScroll.contentY + detailsScroll.step);
                    else if (event.key === Qt.Key_Up || event.key === Qt.Key_K)
                        detailsScroll.scrollTo(detailsScroll.contentY - detailsScroll.step);
                    else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_Space)
                        detailsScroll.scrollTo(detailsScroll.contentY + page);
                    else if (event.key === Qt.Key_PageUp)
                        detailsScroll.scrollTo(detailsScroll.contentY - page);
                    else if (event.key === Qt.Key_Home)
                        detailsScroll.scrollTo(0);
                    else if (event.key === Qt.Key_End)
                        detailsScroll.scrollTo(detailsScroll.maxY);
                    else
                        return;
                    event.accepted = true;
                }

                // Its hairline while it has the keys.
                Rectangle {
                    anchors.fill: parent
                    border.color: Theme.on.blockIdle.accent
                    border.width: Theme.hairWidth
                    color: "transparent"
                    parent: detailsBlock
                    visible: detailsScroll.activeFocus
                }

                ColumnLayout {
                    id: detailsColumn

                    width: detailsScroll.width
                    spacing: Theme.space(2)

                    Repeater {
                        model: root.detailRows

                        RowLayout {
                            id: detailRow

                            required property int index
                            required property var modelData

                            Layout.fillWidth: true
                            objectName: "noticeDetailRow"
                            spacing: Theme.space(10)

                            Text {
                                Accessible.name: text
                                Accessible.role: Accessible.StaticText
                                Layout.fillWidth: true
                                color: Theme.on.blockIdle.foreground
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.caption
                                text: detailRow.modelData.text
                                textFormat: Text.PlainText
                            }

                            Text {
                                id: rowLink

                                Accessible.name: rowLink.text
                                Accessible.role: Accessible.Link
                                Accessible.onPressAction: root.detailRowTriggered(detailRow.index)
                                color: rowLinkMouse.containsMouse ? Theme.on.blockIdle.brightForeground :
                                                                    Theme.on.blockIdle.accent
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.caption
                                objectName: "noticeDetailRowLink"
                                text: detailRow.modelData.link ?? ""
                                visible: text.length > 0

                                MouseArea {
                                    id: rowLinkMouse

                                    anchors.fill: parent
                                    anchors.margins: -Theme.space(4)
                                    cursorShape: Qt.PointingHandCursor
                                    hoverEnabled: true

                                    onClicked: root.detailRowTriggered(detailRow.index)
                                }
                            }
                        }
                    }

                    Text {
                        // #79: the technical text, once "details" unfolds it.
                        Accessible.name: text
                        Accessible.role: Accessible.StaticText
                        Layout.fillWidth: true
                        color: Theme.on.blockIdle.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.caption
                        objectName: "noticeDetailsText"
                        text: root.details
                        textFormat: Text.PlainText
                        visible: root.details.length > 0
                        wrapMode: Text.WrapAnywhere
                    }

                    Text {
                        id: copyLink

                        // #77: a link, as the notice's actions; its press is the click.
                        Accessible.name: copyLink.text
                        Accessible.role: Accessible.Link
                        Accessible.onPressAction: if (copyLink.enabled)
                                                      root.copyRequested(root.details)
                        Layout.alignment: Qt.AlignRight
                        color: copyMouse.containsMouse ? Theme.on.blockIdle.brightForeground : Theme.on.blockIdle.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.caption
                        objectName: "noticeCopy"
                        text: qsTr("copy")
                        visible: root.details.length > 0 && root.copyable

                        MouseArea {
                            id: copyMouse

                            anchors.fill: parent
                            anchors.margins: -Theme.space(4)
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true

                            onClicked: root.copyRequested(root.details)
                        }
                    }
                }
            }
        }
    }

    // The actions: beside the headline, or under the line when the band is narrow (N5). They
    // sit where the two placeholders are but stay the band's own child, declared after the
    // line: a screen reader reads children in declaration order, so the headline and its line
    // come first, then what to do about them (#91).
    Row {
        id: actionLinks

        objectName: "noticeActions"
        spacing: Theme.space(14)
        x: content.x + (root.stacked ? actionsUnder.x : headRow.x + actionsBeside.x)
        y: content.y + (root.stacked ? actionsUnder.y : headRow.y + actionsBeside.y)

        Repeater {
            model: root.actions

            Text {
                id: link

                required property var modelData

                Accessible.name: link.modelData.label
                Accessible.role: Accessible.Link
                // A screen reader's press is the click (#75); disabled, it does nothing, as
                // the pointer.
                Accessible.onPressAction: if (link.enabled)
                                              root.actionTriggered(link.modelData.key)
                color: linkMouse.containsMouse ? Theme.on.idle.brightForeground : Theme.on.idle.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                objectName: "noticeAction-" + link.modelData.key
                text: link.modelData.label

                MouseArea {
                    id: linkMouse

                    anchors.fill: parent
                    anchors.margins: -Theme.space(4)
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true

                    onClicked: root.actionTriggered(link.modelData.key)
                }
            }
        }

        Text {
            id: detailsLink

            // "details", never its chevron; whether it is open is the description, as a rail
            // group's (#78).
            Accessible.description: root.detailsOpen ? qsTr("unfolded") : qsTr("folded")
            Accessible.name: root.detailsLabel
            Accessible.role: Accessible.Link
            // As an action's (#75).
            Accessible.onPressAction: if (detailsLink.enabled)
                                          root.toggleDetails()
            color: detailsMouse.containsMouse ? Theme.on.idle.foreground : Theme.on.idle.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "noticeDetailsToggle"
            text: root.detailsLabel + " " + (root.detailsOpen ? Glyphs.chevronDown :
                                                                Glyphs.chevronRight)
            visible: root.details.length > 0 || root.detailRows.length > 0

            MouseArea {
                id: detailsMouse

                anchors.fill: parent
                anchors.margins: -Theme.space(4)
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: root.toggleDetails()
            }
        }
    }
}
