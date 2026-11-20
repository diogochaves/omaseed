pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// Board 64 (DECISIONS C47 I15): About this torrent, read-only, under the add sheet's files.
// `about` is the addable's `about` map (App::torrentFileMap, a magnet's parse, the lookup's
// metadata): trackers (count, hosts), private, created (date, program), comment, pieces
// (count × size), hash (v1, v2 or hybrid). A fact the source does not carry has no key, and
// its row is left out; a .torrent's empty comment is the one row that says a dim "none".
ColumnLayout {
    id: root

    property var about: null

    /// The rows as [key, value, dim], in I15's order: what the block shows, and what a test
    /// reads.
    readonly property var rows: root._rowsOf(root.about)

    spacing: Theme.space(4)
    visible: root.rows.length > 0

    /// "9f2c…41ab": the first and last four of the hash.
    function shortHash(hash: string): string {
        return hash.length > 12 ? hash.slice(0, 4) + "…" + hash.slice(-4) : hash;
    }

    function _rowsOf(about: var): var {
        if (!about)
            return [];
        const rows = [];
        const has = key => about[key] !== undefined && about[key] !== null;
        if (has("trackerCount") && about.trackerCount > 0) {
            const hosts = (about.trackers || []).join(", ");
            rows.push([qsTr("trackers"), hosts.length > 0 ? about.trackerCount + " · " + hosts :
                                                            String(about.trackerCount), false]);
        }
        if (has("isPrivate"))
            rows.push([qsTr("private"), about.isPrivate ? qsTr("yes: no DHT, no peer exchange") :
                                                          qsTr("no"), false]);
        const by = has("createdBy") ? about.createdBy : "";
        const on = has("createdOn") ? about.createdOn : "";
        if (by.length > 0 || on.length > 0)
            rows.push([qsTr("created"), on.length > 0 && by.length > 0 ? qsTr("%1 by %2").arg(on).arg(
                                                                             by) : on.length > 0
                                                                         ? on : qsTr("by %1").arg(
                                                                               by), false]);
        if (has("comment"))
            rows.push([qsTr("comment"), about.comment.length > 0 ? about.comment : qsTr("none"),
                       about.comment.length === 0]);
        if (has("piecesText") && about.piecesText.length > 0)
            rows.push([qsTr("pieces"), about.piecesText, false]);
        if (has("infoHash") && about.infoHash.length > 0) {
            const kind = about.hashKind === "hybrid" ? qsTr("v1 + v2 (hybrid)") : about.hashKind;
            rows.push([qsTr("hash"), kind + " · " + root.shortHash(about.infoHash), false]);
        }
        return rows;
    }

    Label {
        objectName: "addAboutTitle"
        text: qsTr("About this torrent")
        tracked: true
    }

    Hairline {
        Layout.fillWidth: true
    }

    GridLayout {
        Layout.fillWidth: true
        columnSpacing: Theme.space(14)
        columns: 2
        rowSpacing: Theme.space(2)

        Repeater {
            model: root.rows

            Label {
                id: keyLabel

                required property int index
                // The row this label was made for: a stale delegate, while the rows change,
                // never reads past the new list's end.
                required property var modelData

                Layout.column: 0
                Layout.row: keyLabel.index
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                dim: true
                objectName: "addAboutKey"
                size: "caption"
                text: keyLabel.modelData[0]
            }
        }

        Repeater {
            model: root.rows

            Label {
                id: valueLabel

                required property int index
                required property var modelData

                Accessible.name: valueLabel.modelData[0] + ": " + valueLabel.text
                Accessible.role: Accessible.StaticText
                Layout.column: 1
                Layout.fillWidth: true
                Layout.row: valueLabel.index
                dim: valueLabel.modelData[2]
                objectName: "addAboutValue"
                size: "caption"
                text: valueLabel.modelData[1]
                wrapMode: Text.WrapAnywhere
            }
        }
    }
}
