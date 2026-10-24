pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The details' story tab (board 46 `details()`, DECISIONS C24 H6), in reading order:
//
//   1. docs/28's error block, when qBittorrent's log says why it is in error;
//   2. docs/32's failure block under it, a move, rename or part file that didn't happen;
//   3. C18 D7's last move, while the history keeps it, with "move back…";
//   4. the pieces bar (docs/12 G3), while the torrent has pieces;
//   5. "what happened" (core/TorrentStory), oldest first;
//   6. "its lines in the log" (docs/32 D13), the whole of it in qBittorrent's words.
//
// The blocks are the ones General had (DECISIONS C24 supersedes docs/28's and C18 D7's "in
// General"): same words, same actions, same signals. `torrent` is the cursor row
// (`App.torrentRow()`); `story` is `App.torrentStory()` for it.
Flickable {
    id: root

    /// The controller, for the pieces bar; null draws no bar.
    property TorrentDetails details: null
    property var torrent: null
    /// `App.torrentStory(hash)`: `[{time, kind, title, detail}]`, oldest first.
    property var story: []
    /// docs/28 G2: the free space on the torrent's disk, "0 B free", when its disk is full.
    property string freeText: ""
    /// docs/44 D13: a stuck move's partial copies, as `App.stuckMoveCopies` names them.
    property string partialText: ""
    /// Board 45 B1 (C18 D7): App.move.lastMoveOf() for this torrent, {} for none.
    property var lastMove: ({})

    /// docs/28: an action of the error block ("location", "start", "recheck", "rename",
    /// "copy", "log", …).
    signal problemAction(string action)
    /// docs/32: an action of the failure block ("location", "folder", "rename", "copy",
    /// "dismiss", "log").
    signal failureAction(string action)
    /// Board 45 B1: an action of the last move's block ("back", "backThis", "show").
    signal moveAction(string action)
    /// docs/32 D13: "its lines in the log", the log face narrowed to this torrent.
    signal logRequested

    /// docs/28: why the torrent is in error (TorrentProblem::toMap()), or an empty map.
    readonly property var problem: root.torrent && root.torrent.problem ? root.torrent.problem : (
                                                                              {})

    readonly property bool hasProblem: (root.problem.headline || "").length > 0
    /// docs/32 (#55): what didn't happen (TorrentFailure::toMap()), or an empty map.
    readonly property var failure: root.torrent && root.torrent.failure ? root.torrent.failure : (
                                                                              {})

    readonly property bool hasFailure: (root.failure.headline || "").length > 0
    readonly property bool hasMove: (root.lastMove.id || "").length > 0
    readonly property bool hasBar: root.details !== null && root.details.pieceCount > 0
    readonly property color stateColor: root.torrent && root.torrent.stateRole
                                        ? Theme[root.torrent.stateRole] ?? Theme.foreground :
                                          Theme.foreground

    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: column.implicitHeight + 2 * Theme.detailsBodyPaddingTop
    contentWidth: width
    implicitHeight: Theme.space(200)
    implicitWidth: Theme.space(300)

    Column {
        id: column

        spacing: Theme.detailsBodyGap
        width: root.width - 2 * Theme.detailsBodyInset
        x: Theme.detailsBodyInset
        y: Theme.detailsBodyPaddingTop

        // docs/28 G1: why it is in error, over everything else.
        DetailsErrorBlock {
            id: errorBlock

            freeText: root.freeText
            objectName: "errorBlock"
            partialText: root.partialText
            problem: root.problem
            visible: root.hasProblem
            width: parent.width

            onActionTriggered: action => root.problemAction(action)
        }

        // docs/32 F1–F3: what didn't happen, under the error when there is one.
        DetailsErrorBlock {
            objectName: "failureBlock"
            problem: root.failure
            visible: root.hasFailure
            width: parent.width

            onActionTriggered: action => root.failureAction(action)
        }

        // Board 45 B1 (C18 D7): the torrent's last move, while the history keeps it.
        DetailsMoveBlock {
            move: root.lastMove
            objectName: "moveBlock"
            visible: root.hasMove
            width: parent.width

            onActionTriggered: action => root.moveAction(action)
        }

        // docs/12 G3: the pieces it has, and those arriving.
        DetailsPiecesBar {
            details: root.details
            haveText: root.details ? (root.details.properties.piecesHave || "") : ""
            objectName: "piecesBar"
            percentText: root.torrent ? root.torrent.progressText : ""
            visible: root.hasBar
            width: parent.width
        }

        Column {
            objectName: "storyEvents"
            spacing: Theme.detailsEventSpacing
            visible: root.story.length > 0
            width: parent.width

            Text {
                // #79: the timeline's heading.
                Accessible.name: text
                Accessible.role: Accessible.Heading
                bottomPadding: Theme.detailsHeadingGap - Theme.detailsEventSpacing
                color: Theme.dimForeground
                font.capitalization: Font.AllUppercase
                font.family: Theme.fontFamily
                font.letterSpacing: Theme.detailsHeadingTracking
                font.pixelSize: Theme.detailsHeadingSize
                objectName: "storyHeading"
                text: qsTr("what happened")
            }

            Repeater {
                model: root.story

                DetailsStoryEvent {
                    required property int index
                    required property var modelData

                    event: modelData
                    objectName: "storyEvent_" + index
                    stateColor: root.stateColor
                    width: parent.width
                }
            }
        }

        // docs/32 D13: everything qBittorrent's log says about it.
        Label {
            Accessible.name: text
            Accessible.role: Accessible.Link
            color: Theme.accent
            objectName: "storyLogLink"
            size: "small"
            text: qsTr("its lines in the log ›")
            visible: root.torrent !== null && root.torrent !== undefined && (root.torrent.hash
                                                                             ?? "").length > 0

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor

                onClicked: root.logRequested()
            }
        }
    }

    // On the Flickable itself (`parent: root`), not in its content, so it stays put while the
    // story scrolls under it.
    ScrollBar {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.top: parent.top
        flickable: root
        parent: root
        vertical: true
        width: Theme.space(4)
    }

    WheelScroll {
        anchors.fill: parent
        flickable: root
        parent: root
        rowHeight: Theme.railRowHeight
        z: 2
    }
}
