pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The details pane's General tab (spec v2 §2 Fig. 1's `.kv` block), grouped as docs/12 G1
// draws it: Transfer (what is moving), Information (what the torrent is) and Options (what
// you can change), each under a tracked heading in the accent, in that order. `properties`
// is `TorrentDetails.properties` — every value is already a formatted string — and `torrent`
// is the cursor row, which owns the Status row's word and its colour.
//
// A row whose value is empty is left out, so a magnet with no metadata shows a short block
// rather than a column of dashes, and a group left with no row at all loses its heading.
//
// `columns: 2` (docs/10 §5, Details under the table) puts Transfer on the left and
// Information and Options on the right, so the pane fits in about half the height and no
// group is cut in two.
Flickable {
    id: root

    /// The controller, for docs/12 G3's pieces bar; null draws no bar.
    property TorrentDetails details: null
    property var properties: ({})
    property var torrent: null
    property int columns: 1
    /// docs/12 G5: the row under the pane's `j k` cursor, an index into `rows`.
    property int cursorIndex: -1
    /// docs/28 G2: the free space on the torrent's disk, "0 B free", when its disk is full.
    property string freeText: ""

    /// A click on a row (G5: Details moves the cursor there and copies the value).
    signal rowClicked(int row)
    /// docs/28: an action of the error block, for the cursor torrent ("location", "start",
    /// "recheck", "rename", "copy").
    signal problemAction(string action)

    /// docs/32: an action of the failure block ("location", "folder", "rename", "copy",
    /// "dismiss").
    signal failureAction(string action)

    /// Board 45 B1: an action of the last move's block ("back", "backThis", "show").
    signal moveAction(string action)

    /// docs/28: why the torrent is in error (TorrentProblem::toMap()), or an empty map.
    readonly property var problem: root.torrent && root.torrent.problem ? root.torrent.problem : ({})
    readonly property bool _hasProblem: (root.problem.headline || "").length > 0
    /// docs/32 (#55): a move, rename or part file that failed (TorrentFailure::toMap()), or an
    /// empty map. It comes under the error, in yellow, when there is both.
    readonly property var failure: root.torrent && root.torrent.failure ? root.torrent.failure : ({})
    readonly property bool _hasFailure: (root.failure.headline || "").length > 0
    /// Board 45 B1 (C18 D7): App.move.lastMoveOf() for this torrent, {} for none.
    property var lastMove: ({})
    readonly property bool _hasMove: (root.lastMove.id || "").length > 0

    /// Every row in reading order (Transfer, Information, Options), each carrying its
    /// `index` there: the list the cursor walks.
    readonly property var rows: root.groups.reduce((all, group) => all.concat(group.rows), [])
    readonly property int count: root.rows.length

    /// Scrolls the cursor row into view.
    function ensureVisible(): void {
        const item = root._rowItem(root.cursorIndex);
        if (!item)
            return;
        const top = item.mapToItem(root.contentItem, 0, 0).y;
        if (top < root.contentY)
            root.contentY = top;
        else if (top + item.height > root.contentY + root.height)
            root.contentY = Math.min(top + item.height - root.height,
                                     Math.max(0, root.contentHeight - root.height));
    }

    function _rowItem(index: int): Item {
        const stack = [first, second];
        while (stack.length > 0) {
            const item = stack.pop();
            if (item.objectName === "generalRow_" + index)
                return item;
            for (const child of item.children)
                stack.push(child);
        }
        return null;
    }

    /// `[{label, rows: [{index, key, value, copy, name, color?, dim?, halves?, option?, on?}]}]`, empty
    /// groups dropped. `option` names a G4 on/off row ("sequential", "firstLast",
    /// "superSeeding", "autoManagement") and `on` its state; `halves` breaks a value that does
    /// not fit at its middle (the hashes). `copy` is what `y` puts on the clipboard: the whole value, never the
    /// shortened text on screen (the magnet); `name` is how the footer says what was copied.
    readonly property var groups: {
        const map = root.properties || {};
        const row = root.torrent;
        const make = () => [];
        const add = (list, key, value, extra) => {
            if (value === undefined || value === null || String(value).length === 0)
                return;
            const entry = {
                "key": key,
                "name": key.toLowerCase(),
                "value": String(value),
                "copy": String(value)
            };
            for (const name in extra || {})
                entry[name] = extra[name];
            list.push(entry);
        };

        const transfer = make();
        // docs/28: the short cause after the state, dim, when the log says why.
        const cause = row && row.problem ? (row.problem.cause || "") : "";
        if (row)
            add(transfer, qsTr("Status"), row.stateText, {
                    "color": row.stateRole ? Theme[row.stateRole] : Theme.foreground,
                    "tail": cause.length > 0 && cause !== row.stateText ? cause : ""
                });
        add(transfer, qsTr("Downloaded"), map.downloaded);
        add(transfer, qsTr("Uploaded"), map.uploaded);
        add(transfer, qsTr("Wasted"), map.wasted);
        add(transfer, qsTr("Seeds"), map.seeds);
        add(transfer, qsTr("Peers"), map.peers);
        add(transfer, qsTr("Connections"), map.connections);
        add(transfer, qsTr("Availability"), map.availability);
        add(transfer, qsTr("Avg speed"), map.avgSpeed);
        add(transfer, qsTr("Time active"), map.timeActive);
        add(transfer, qsTr("Seeding time"), map.seedingTime);
        add(transfer, qsTr("Last seen full"), map.lastSeen);
        add(transfer, qsTr("Reannounce"), map.reannounce);

        const information = make();
        add(information, qsTr("Save path"), map.savePath);
        add(information, qsTr("Pieces"), map.pieces);
        // Broken in half, not wherever the width runs out, when it does not fit on one line.
        add(information, qsTr("Hash v1"), map.hashV1, {
                "halves": true
            });
        add(information, qsTr("Hash v2"), map.hashV2, {
                "halves": true
            });
        // Shortened for the eye, in the dim colour that says so.
        add(information, qsTr("Magnet"), map.magnetText, {
                "dim": true,
                "copy": map.magnet
            });
        add(information, qsTr("Private"), map.private);
        add(information, qsTr("Created by"), map.createdBy);
        add(information, qsTr("Created on"), map.createdOn);
        add(information, qsTr("Completed on"), map.completedOn);
        add(information, qsTr("Comment"), map.comment);

        const options = make();
        add(options, qsTr("Share limit"), map.shareLimit);
        // docs/12 G4: on/off rows off the torrent row; `enter` or a click changes them.
        const option = (key, name, on) => add(options, key, on ? qsTr("on") : qsTr("off"), {
                                                  "option": name,
                                                  "on": on,
                                                  "color": on ? Theme.green : Theme.dimForeground
                                              });
        if (row && row.seqDl !== undefined) {
            option(qsTr("Sequential"), "sequential", row.seqDl === true);
            option(qsTr("First / last"), "firstLast", row.firstLastPiecePrio === true);
            option(qsTr("Super seed"), "superSeeding", row.superSeeding === true);
            option(qsTr("Auto manage"), "autoManagement", row.autoTmm === true);
        }

        let index = 0;
        for (const list of [transfer, information, options]) {
            for (const entry of list)
                entry.index = index++;
        }

        return [{
                "label": qsTr("Transfer"),
                "rows": transfer
            }, {
                "label": qsTr("Information"),
                "rows": information
            }, {
                "label": qsTr("Options"),
                "rows": options
            }].filter(group => group.rows.length > 0);
    }

    readonly property bool _hasBar: root.details !== null && root.details.pieceCount > 0
    readonly property int _gap: Theme.space(24)
    readonly property int _blockWidth: root.columns === 2 ? Math.floor((root.width - root._gap) / 2)
                                                          : root.width

    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: Math.max(first.implicitHeight, second.visible ? second.implicitHeight : 0)
                   + Theme.space(10)
    contentWidth: width
    implicitHeight: Theme.space(200)
    implicitWidth: Theme.space(300)

    Column {
        id: first

        width: root._blockWidth

        // docs/28 G1: why it is in error, over everything else.
        Item {
            height: root._hasProblem ? errorBlock.implicitHeight + Theme.space(12) : 0
            visible: root._hasProblem
            width: parent.width

            DetailsErrorBlock {
                id: errorBlock

                anchors.left: parent.left
                anchors.leftMargin: Theme.space(16)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(16)
                anchors.top: parent.top
                anchors.topMargin: Theme.space(12)
                freeText: root.freeText
                problem: root.problem

                onActionTriggered: action => root.problemAction(action)
            }
        }

        // docs/32 F1–F3: what didn't happen, under the error when there is one.
        Item {
            height: root._hasFailure ? failureBlock.implicitHeight + Theme.space(12) : 0
            visible: root._hasFailure
            width: parent.width

            DetailsErrorBlock {
                id: failureBlock

                anchors.left: parent.left
                anchors.leftMargin: Theme.space(16)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(16)
                anchors.top: parent.top
                anchors.topMargin: Theme.space(12)
                objectName: "failureBlock"
                problem: root.failure

                onActionTriggered: action => root.failureAction(action)
            }
        }

        // Board 45 B1 (C18 D7): the torrent's last move, while the history keeps it.
        Item {
            height: root._hasMove ? moveBlock.implicitHeight + Theme.space(12) : 0
            visible: root._hasMove
            width: parent.width

            DetailsMoveBlock {
                id: moveBlock

                anchors.left: parent.left
                anchors.leftMargin: Theme.space(16)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(16)
                anchors.top: parent.top
                anchors.topMargin: Theme.space(12)
                move: root.lastMove

                onActionTriggered: action => root.moveAction(action)
            }
        }

        // docs/12 G3: at the top of General, over Transfer.
        Item {
            height: root._hasBar ? bar.implicitHeight + Theme.space(12) : 0
            visible: root._hasBar
            width: parent.width

            DetailsPiecesBar {
                id: bar

                anchors.left: parent.left
                anchors.leftMargin: Theme.space(16)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(16)
                anchors.top: parent.top
                anchors.topMargin: Theme.space(12)
                details: root.details
                haveText: (root.properties || {}).piecesHave || ""
                objectName: "piecesBar"
                percentText: root.torrent ? root.torrent.progressText : ""
            }
        }

        Repeater {
            model: root.columns === 2 ? root.groups.slice(0, 1) : root.groups

            delegate: groupDelegate
        }
    }

    Column {
        id: second

        visible: root.columns === 2
        width: root._blockWidth
        x: root._blockWidth + root._gap

        Repeater {
            model: root.columns === 2 ? root.groups.slice(1) : []

            delegate: groupDelegate
        }
    }

    // On the Flickable itself (`parent: root`), not in its content, so they stay put while
    // the rows scroll under them.
    ScrollBar {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.top: parent.top
        flickable: root
        objectName: "generalScrollBar"
        parent: root
        vertical: true
        width: Theme.space(4)
    }

    // A wheel notch moves three rows, and quick notches add up (Kit/WheelScroll).
    WheelScroll {
        anchors.fill: parent
        flickable: root
        objectName: "generalWheel"
        parent: root
        rowHeight: Theme.railRowHeight
        z: 2
    }

    Component {
        id: groupDelegate

        Column {
            id: group

            required property var modelData

            objectName: "generalGroup_" + group.modelData.label
            width: parent ? parent.width : 0

            // The rail's section heading (RailGroup: a tracked caption, 16 u in, 4 u above
            // the rows), in the accent as docs/12 G1 draws it, so it does not read as one more
            // key.
            Label {
                // #79: "Transfer", "Information", "Options": the rows' headings.
                Accessible.name: text
                Accessible.role: Accessible.Heading
                bottomPadding: Theme.space(4)
                color: Theme.accent
                leftPadding: Theme.space(16)
                objectName: "generalHeading_" + group.modelData.label
                text: group.modelData.label
                topPadding: Theme.space(12)
                tracked: true
            }

            Repeater {
                model: group.modelData.rows

                DetailsGeneralRow {
                    required property var modelData

                    entry: modelData
                    hasCursor: modelData.index === root.cursorIndex
                    objectName: "generalRow_" + modelData.index
                    width: group.width

                    onClicked: root.rowClicked(modelData.index)
                }
            }
        }
    }
}
