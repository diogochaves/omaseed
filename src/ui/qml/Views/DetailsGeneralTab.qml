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

    /// A click on a row (G5: Details moves the cursor there and copies the value).
    signal rowClicked(int row)

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

    /// `[{label, rows: [{index, key, value, copy, name, color?, dim?, option?, on?}]}]`, empty
    /// groups dropped. `option` names a G4 on/off row ("sequential", "firstLast",
    /// "superSeeding", "autoManagement") and `on` its state. `copy` is what `y` puts on the clipboard: the whole value, never the
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
        if (row)
            add(transfer, qsTr("Status"), row.stateText, {
                    "color": row.stateRole ? Theme[row.stateRole] : Theme.foreground
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
        add(information, qsTr("Hash v1"), map.hashV1);
        add(information, qsTr("Hash v2"), map.hashV2);
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
