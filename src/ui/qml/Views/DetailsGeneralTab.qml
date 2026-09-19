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
Flickable {
    id: root

    property var properties: ({})
    property var torrent: null

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

    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: block.implicitHeight
    contentWidth: width
    implicitHeight: Theme.space(200)
    implicitWidth: Theme.space(300)

    KeyValue {
        id: block

        items: root.rows
        width: root.width
    }
}
