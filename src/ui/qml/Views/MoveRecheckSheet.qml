pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// "Recheck the 2…" after a move (board 42 E3, DECISIONS C15 M16): the failed or stuck ones,
// rechecked one at a time, smallest first; each that comes out complete starts. "set
// location…" is today's sheet with those torrents.
Sheet {
    id: root

    property var hashes: []
    property var rows: []

    signal setLocationRequested(var hashes)

    function openFor(hashes: var): void {
    root.hashes = hashes;
    root.rows = App.move.recheckRows(hashes);
    root.open();
}

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": qsTr("move…")
        },
        {
            "text": qsTr("recheck %1").arg(root.rows.length),
            "primary": true,
            "disabled": root.rows.length === 0
        }
    ]
    glyph: Glyphs.recheck
    sheetWidth: Theme.space(520)
    subtitle: qsTr("smallest first, one at a time")
    title: root.rows.length === 1 ? qsTr("Recheck 1 torrent") : qsTr("Recheck %1 torrents").arg(
                                        root.rows.length)

    onAccepted: {
        App.move.recheckSmallestFirst(root.hashes);
        root.close();
    }
    onActionTriggered: text => {
        if (text === qsTr("move…")) {
            root.close();
            root.setLocationRequested(root.hashes);
        }
    }

    Repeater {
        model: root.rows

        RowLayout {
            id: recheckRow

            required property var modelData

            Layout.fillWidth: true
            spacing: Theme.space(10)

            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                size: "small"
                text: recheckRow.modelData.name
            }

            Label {
                Layout.preferredWidth: Theme.space(70)
                dim: true
                horizontalAlignment: Text.AlignRight
                size: "small"
                text: recheckRow.modelData.bytes
            }

            Label {
                Layout.preferredWidth: Theme.space(190)
                dim: true
                size: "small"
                text: recheckRow.modelData.then
            }
        }
    }

    Label {
        Layout.fillWidth: true
        dim: true
        size: "caption"
        text: qsTr(
                  "The ones that come out complete start again. The others stay stopped and are listed.")
        wrapMode: Text.Wrap
    }
}
