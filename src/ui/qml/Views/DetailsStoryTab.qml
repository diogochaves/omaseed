pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The details' story tab (board 46 `details()`, DECISIONS C24 H6), in reading order:
//
//   1. docs/28's error block, when qBittorrent's log says why it is in error;
//      on an engine with no log, its own text (C25 K24);
//   2. docs/32's failure block under it, a move, rename or part file that didn't happen;
//   3. C18 D7's last move, while the history keeps it, with "move back…";
//   4. the pieces bar (docs/12 G3), while the torrent has pieces;
//   5. "what happened" (core/TorrentStory), newest first (C32 D15);
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
    /// `App.torrentStory(hash)`: `[{time, kind, title, detail}]`, newest first (C32 D15).
    property var story: []
    /// docs/28 G2: the free space on the torrent's disk, "0 B free", when its disk is full.
    property string freeText: ""
    /// docs/44 D13: a stuck move's partial copies, as `App.stuckMoveCopies` names them.
    property string partialText: ""
    /// Board 45 B1 (C18 D7): App.move.lastMoveOf() for this torrent, {} for none.
    property var lastMove: ({})
    /// DECISIONS C43 Y6: a place line since that move (App.placeMovedSinceLastMove), for the
    /// block's sentence.
    property bool movedOutside: false
    /// The engine has a log to narrow to this torrent (App.features.engineLog, C25 K30).
    property bool logOffered: App.features.engineLog !== false

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
    /// #189 (ruled 2026-11-12): the percent is the wanted progress and the bar counts every
    /// piece, so with skipped files they can part ways ("384 of 512" beside "100 %"). Then the
    /// percent says what it counts, "100 % of wanted"; while the two read the same, or every
    /// file is wanted, it stays the plain percent.
    readonly property bool someSkipped: root.details !== null && root.details.files.skippedCount > 0
    readonly property string progressText: root.torrent ? (root.torrent.progressText ?? "") : ""
    readonly property bool wantedDiffers: root.someSkipped && root.details.pieceCount > 0
                                          && root._tenths(root.torrent?.progress ?? 0)
                                          !== root._tenths(root.details.piecesHaveCount / root.details.pieceCount)
    readonly property string percentText: root.wantedDiffers ? qsTr("%1 of wanted").arg(root.progressText)
                                                             : root.progressText
    /// #246: the time's column fits the longest time the story writes ("yesterday 09:15",
    /// "22 Oct 17:26", "22 Oct 2025"), measured in the time's own font, and is never narrower
    /// than the skin's column, so a story of today's times keeps it.
    readonly property int timeWidth: {
        let widest = 0;
        for (const event of root.story)
            widest = Math.max(widest, timeMetrics.advanceWidth(event.time ?? ""));
        return Math.max(Theme.detailsEventTimeWidth, Math.ceil(widest) + 1);
    }

    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: column.implicitHeight + 2 * Theme.detailsBodyPaddingTop
    contentWidth: width
    implicitHeight: Theme.space(200)
    implicitWidth: Theme.space(300)

    /// A fraction as the percent shows it, in tenths (core::Format::percentCompact): truncated,
    /// 100 only when whole, so two fractions compare as their words would.
    function _tenths(fraction: real): int {
        if (!(fraction > 0))
            return 0;
        return fraction >= 1 ? 1000 : Math.min(999, Math.floor(fraction * 1000 + 1e-6));
    }

    Column {
        id: column

        spacing: Theme.detailsBodyGap
        width: root.width - 2 * Theme.detailsBodyInset
        x: Theme.detailsBodyInset
        y: Theme.detailsBodyPaddingTop

        // docs/28 G1: why it is in error, over everything else.
        DetailsErrorBlock {
            id: errorBlock

            canAddAgain: App.features.addAgain !== false
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
            movedOutside: root.movedOutside
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
            percentText: root.percentText
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
                    timeWidth: root.timeWidth
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
            // C25 K30: only where the engine has a log (App.features.engineLog).
            visible: root.logOffered && root.torrent !== null && root.torrent !== undefined && (root.torrent.hash
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

    // #246: DetailsStoryEvent's time font, to measure `timeWidth`.
    FontMetrics {
        id: timeMetrics

        font.family: Theme.valueFamily
        font.features: ({
                            "tnum": 1
                        })
        font.pixelSize: Theme.detailsEventTitleSize
    }
}
