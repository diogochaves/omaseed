import QtQuick
import Omaseed.Kit

// What a quick action on torrents came to (board 45 §2 O1–O3, DECISIONS C18 D1–D3), in the
// selection bar's place and shape: the bottom middle of the table, over the rows. A rule at
// its start in the outcome's colour, the mark, the headline naming the torrent (or the count),
// one dim line on why, at most two actions, and ×.
//
//   done    green, ✓, "Arcade Set 01 moved to /srv/library/archive"   show ×
//           it goes after 6 s, its line running down; the pointer resting on it holds them
//   failed  red, ✕, "Short Film 07 didn't move" · the cause            details  move…  ×
//           no timer: it stays until × / esc or the next action
//   mixed   yellow, !, "2 moved to /mnt/fast/keep, 1 didn't" · "NAME: why"   details  show  ×
//
// `outcome` is App.outcomes.current (core::ActionOutcome::words()). It runs nothing itself:
// the host answers its signals. Hand-formatted to qmlformat's own style (typed functions).
Rectangle {
    id: root

    /// The outcome on show; {} for none.
    property var outcome: ({})
    /// The widest it may be: the headline and the line elide past it.
    property real availableWidth: 0
    /// D3: how long a done strip stays.
    property int doneSeconds: 6
    /// Reduced motion: the line stays whole and the strip still goes on time.
    property bool reducedMotion: false
    /// What is left of a done strip's time, 1 to 0.
    property real timeLeft: 1

    readonly property string kind: root.outcome.kind ?? ""
    readonly property bool shown: root.kind.length > 0
    readonly property color tone: root.kind === "failed" ? Theme.red : root.kind === "mixed" ? Theme.yellow
                                                                                            : Theme.green
    /// "show" for what landed, while there is something left to show (never after a delete).
    readonly property bool canShow: root.kind !== "failed" && (root.outcome.doneRow ?? "").length > 0
                                    && (root.outcome.doneHashes ?? []).length > 0
    readonly property bool canDetail: root.kind !== "done" && (root.outcome.details ?? "").length > 0
    /// A failed move offers to try again, as the error block's "set location" does (docs/32 D1).
    readonly property bool canMove: root.kind === "failed" && (root.outcome.action === "move")
                                    && (root.outcome.failedHashes ?? []).length > 0

    /// "show": the cursor on the one torrent, or the several selected.
    signal showRequested(var hashes)
    /// "details": the failure's band, above the strip.
    signal detailsRequested
    /// "move…": the move sheet again, on the ones that didn't move.
    signal moveRequested(var hashes, string target)
    /// ×: the strip goes.
    signal closeRequested
    /// A done strip's time ran out.
    signal expired(int serial)

    readonly property int _padding: Theme.space(12)
    readonly property int _spacing: Theme.space(12)
    /// The padding on both sides and the colour rule.
    readonly property int _frameWidth: 2 * root._padding + Theme.space(2)

    border.color: Theme.fill(Theme.borderIdle)
    border.width: 1
    color: Theme.background
    implicitHeight: Theme.controlHeight + 2 * Theme.space(4)
    implicitWidth: Math.min(root.availableWidth > 0 ? root.availableWidth : Infinity,
                            root._frameWidth + content.fixedWidth + headline.implicitWidth
                            + (lineText.text.length > 0 ? lineText.implicitWidth + root._spacing : 0))
    objectName: "outcomeStrip"
    radius: Theme.cornerRadius
    visible: root.shown

    // A new outcome starts its own time, the line full again.
    onOutcomeChanged: {
        root.timeLeft = 1;
        if (root.outcome.kind === "done") { // not root.kind: its binding may not have run yet
            run.restart();
            if (hover.hovered)
                run.pause();
        } else {
            run.stop();
        }
    }

    // The strip floats over the rows: a click on its own ground must not reach the row
    // under it. The wheel still scrolls the table.
    // a11y-ignore A2: the strip's ground, not a control
    MouseArea {
        acceptedButtons: Qt.AllButtons
        anchors.fill: parent
    }

    // `.strip { box-shadow: inset 2px 0 0 var(--k) }`: the outcome's colour at its start.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        color: root.tone
        width: Math.max(1, Math.round(Theme.space(2)))
    }

    Row {
        id: content

        objectName: "outcomeContent"
        anchors.left: parent.left
        anchors.leftMargin: root._padding + Theme.space(2)
        anchors.verticalCenter: parent.verticalCenter
        spacing: root._spacing

        /// The mark, the actions and the gaps around the words.
        readonly property real fixedWidth: markText.implicitWidth + actions.implicitWidth + 2 * root._spacing
        /// What the headline and the line share, the headline first.
        readonly property real textRoom: Math.max(0, root.width - root._frameWidth - content.fixedWidth)

        Label {
            id: markText

            anchors.baseline: headline.baseline
            color: root.tone
            objectName: "outcomeMark"
            text: root.outcome.mark ?? ""
        }

        Label {
            id: headline

            // #79: the strip comes on its own: an alert by its words.
            Accessible.name: [root.outcome.headline ?? "", root.outcome.line ?? ""].filter(t => t.length > 0).join(". ")
            Accessible.role: Accessible.AlertMessage
            anchors.verticalCenter: parent.verticalCenter
            bright: true
            elide: Text.ElideMiddle
            objectName: "outcomeHeadline"
            text: root.outcome.headline ?? ""
            width: Math.min(implicitWidth, content.textRoom)
        }

        Label {
            id: lineText

            anchors.baseline: headline.baseline
            dim: true
            elide: Text.ElideRight
            objectName: "outcomeLine"
            size: "small"
            text: root.outcome.line ?? ""
            visible: text.length > 0 && width > Theme.space(24)
            width: Math.min(implicitWidth, content.textRoom - headline.width - root._spacing)
        }

        Row {
            id: actions

            objectName: "outcomeActions"
            anchors.verticalCenter: parent.verticalCenter
            spacing: root._spacing

            StripLink {
                objectName: "outcomeDetails"
                text: qsTr("details")
                visible: root.canDetail

                onActivated: root.detailsRequested()
            }

            StripLink {
                objectName: "outcomeMove"
                text: qsTr("move…")
                visible: root.canMove

                onActivated: root.moveRequested(root.outcome.failedHashes, root.outcome.target ?? "")
            }

            StripLink {
                objectName: "outcomeShow"
                text: qsTr("show")
                visible: root.canShow

                onActivated: root.showRequested(root.outcome.doneHashes)
            }

            IconButton {
                id: closeButton

                // #78: the action is the name, the key the description.
                Accessible.description: qsTr("shortcut %1").arg("esc")
                Accessible.name: qsTr("close")
                accent: closeButton.hot
                activeFocusOnTab: false
                anchors.verticalCenter: parent.verticalCenter
                glyph: Glyphs.close
                height: Theme.font.body + Theme.space(4)
                objectName: "outcomeClose"
                tooltip: qsTr("close · esc")
                tooltipAbove: true
                width: closeButton.height

                onClicked: root.closeRequested()
            }
        }
    }

    // D3: a done strip's line runs down over its 6 s.
    Rectangle {
        id: timeLine

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        color: root.tone
        height: Math.max(1, Math.round(Theme.space(2)))
        objectName: "outcomeTimeLine"
        visible: root.kind === "done"
        width: root.reducedMotion ? root.width : Math.round(root.width * root.timeLeft)
    }

    NumberAnimation {
        id: run

        duration: root.doneSeconds * 1000
        from: 1
        objectName: "outcomeRun"
        property: "timeLeft"
        target: root
        to: 0

        // Ran to its end, not stopped by a new outcome (that sets the time back to 1 first).
        onRunningChanged: {
            if (!run.running && root.timeLeft <= 0)
                root.expired(root.outcome.serial ?? -1);
        }
    }

    // The pointer resting on the strip holds its time, as the seed toast's (#24 T7), so a
    // strip being read is not taken away from under the pointer.
    HoverHandler {
        id: hover

        onHoveredChanged: {
            if (!run.running)
                return;
            if (hover.hovered)
                run.pause();
            else
                run.resume();
        }
    }

    // An action in the strip: the accent's words, a pointing hand, a button for a screen
    // reader (its press is the click).
    component StripLink: Label {
        id: link

        signal activated

        Accessible.name: link.text
        Accessible.role: Accessible.Button
        Accessible.onPressAction: if (link.visible)
                                      link.activated()
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.accent
        size: "small"

        MouseArea {
            anchors.fill: parent
            anchors.margins: -Theme.space(4)
            cursorShape: Qt.PointingHandCursor

            onClicked: link.activated()
        }
    }
}
