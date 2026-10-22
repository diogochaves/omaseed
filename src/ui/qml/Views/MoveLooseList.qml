pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The files no torrent owns in a folder (board 42 S2, DECISIONS C15 M9): every file, what
// will happen to it and why, from the last walk. The same list is the summary's record
// afterwards. `c` copies it.
Sheet {
    id: root

    property string folder: ""
    property var rows: []
    /// The summary's record (L2's "show what stayed") rather than the preflight's dry run.
    property bool record: false

    signal copyRequested(string text)

    function openFor(folder: string): void {
    root.record = false;
    root.folder = folder;
    root.rows = App.move.looseRows(folder);
    root.open();
}

    function openStayed(folder: string): void {
    root.record = true;
    root.folder = folder;
    root.rows = App.move.carryRows();
    root.open();
}

    function listText(): string {
        return root.record ? App.move.carryText() : App.move.looseText(root.folder);
    }

    actions: [
        {
            "text": qsTr("copy the list"),
            "key": "c"
        },
        {
            "text": qsTr("back"),
            "cancel": true,
            "primary": true
        }
    ]
    glyph: "▱"
    keyHandler: event => {
        if (event.key === Qt.Key_C && (event.modifiers & (Qt.ControlModifier | Qt.AltModifier))
                === 0) {
            root.copyRequested(root.listText());
            event.accepted = true;
        }
    }
    sheetWidth: Theme.space(640)
    subtitle: root.record ? qsTr("what was carried, and what stayed and why") : qsTr(
                                "a dry run: what would be carried, and what stays")
    title: (root.record ? qsTr("Loose files of %1") : qsTr("Files no torrent owns, in %1")).arg(
               root.folder.substring(root.folder.lastIndexOf("/") + 1))

    onAccepted: root.close()
    onActionTriggered: text => {
        if (text === qsTr("copy the list"))
            root.copyRequested(root.listText());
    }

    Repeater {
        model: root.rows

        RowLayout {
            id: looseRow

            required property var modelData

            Layout.fillWidth: true
            spacing: Theme.space(10)

            Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                size: "small"
                text: looseRow.modelData.path
            }

            Label {
                Layout.preferredWidth: Theme.space(70)
                dim: true
                horizontalAlignment: Text.AlignRight
                size: "small"
                text: looseRow.modelData.size
            }

            Label {
                Layout.preferredWidth: Theme.space(200)
                color: looseRow.modelData.kind === "warn" || looseRow.modelData.kind === "unknown"
                       ? Theme.yellow : looseRow.modelData.kind === "dim" ? Theme.dimForeground :
                                                                            Theme.foreground
                elide: Text.ElideRight
                size: "small"
                text: looseRow.modelData.what
            }
        }
    }

    Label {
        Layout.fillWidth: true
        dim: true
        size: "caption"
        text: root.rows.length === 0 ? qsTr("Nothing here yet: the folder is still being read.") :
                                       qsTr("A file counts as a torrent's when it lies under the torrent's content path, or is one of the files of a torrent added without a subfolder.")
        wrapMode: Text.Wrap
    }
}
