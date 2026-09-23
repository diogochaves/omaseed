pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The details pane's General tab (spec v2 §2 Fig. 1's `.kv` block): the rows of the figure,
// in the figure's order, through the Kit's KeyValue. `properties` is
// `TorrentDetails.properties` — every value is already a formatted string — and `torrent` is
// the cursor row, which owns the Status row's word and its colour.
//
// A row whose value is empty is left out, so a magnet with no metadata shows a short block
// rather than a column of dashes.
//
// `columns: 2` (docs/10 §5, Details under the table) splits the rows into two blocks side by
// side, the first half on the left, so the pane fits in about half the height.
Flickable {
    id: root

    property var properties: ({})
    property var torrent: null
    property int columns: 1

    readonly property var rows: {
        const map = root.properties || {};
        const list = [];
        const add = (key, value, color) => {
            if (value !== undefined && value !== null && String(value).length > 0)
                list.push({
                              "key": key,
                              "value": String(value),
                              "color": color
                          });
        };

        add(qsTr("Save path"), map.savePath);
        add(qsTr("Hash v1"), map.hashV1);
        add(qsTr("Hash v2"), map.hashV2);
        if (root.torrent)
            add(qsTr("Status"), root.torrent.stateText, root.torrent.stateRole
                ? Theme[root.torrent.stateRole] : Theme.foreground);
        add(qsTr("Downloaded"), map.downloaded);
        add(qsTr("Uploaded"), map.uploaded);
        add(qsTr("Wasted"), map.wasted);
        add(qsTr("Pieces"), map.pieces);
        add(qsTr("Availability"), map.availability);
        add(qsTr("Seeds"), map.seeds);
        add(qsTr("Peers"), map.peers);
        add(qsTr("Time active"), map.timeActive);
        add(qsTr("Share limit"), map.shareLimit);
        add(qsTr("Created by"), map.createdBy);
        add(qsTr("Comment"), map.comment);
        add(qsTr("Created on"), map.createdOn);
        return list;
    }

    readonly property int _half: Math.ceil(root.rows.length / 2)
    readonly property int _gap: Theme.space(24)
    readonly property int _blockWidth: root.columns === 2 ? Math.floor((root.width - root._gap) / 2)
                                                          : root.width

    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: Math.max(block.implicitHeight, secondBlock.visible ? secondBlock.implicitHeight : 0)
    contentWidth: width
    implicitHeight: Theme.space(200)
    implicitWidth: Theme.space(300)

    KeyValue {
        id: block

        items: root.columns === 2 ? root.rows.slice(0, root._half) : root.rows
        width: root._blockWidth
    }

    KeyValue {
        id: secondBlock

        items: root.columns === 2 ? root.rows.slice(root._half) : []
        visible: root.columns === 2
        width: root._blockWidth
        x: root._blockWidth + root._gap
    }
}
