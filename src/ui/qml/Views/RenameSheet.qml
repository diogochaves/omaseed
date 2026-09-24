pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The actions menu's rename: the table's inline editor (`r`, always) is the primary way to
// rename a torrent; this sheet is what the menu's rename row opens. Always renames one
// torrent — the cursor's — never the whole selection.
Sheet {
    id: root

    property string hash: ""

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": qsTr("rename"),
            "primary": true
        }
    ]
    glyph: "✎" // ✎
    sheetWidth: Theme.space(420)
    title: qsTr("Rename")

    function openFor(hash: string): void {
        root.hash = hash;
        const row = App.torrentRow(hash);
        nameField.text = row.name !== undefined ? row.name : "";
        root.open();
    }

    onAccepted: {
        // Trimmed like the add sheet's Name, and not sent at all when nothing changed.
        const name = nameField.text.trim();
        if (name.length > 0 && name !== App.torrentRow(root.hash).name)
            App.rename(root.hash, name);
        root.close();
    }

    FormRow {
        Layout.fillWidth: true
        label: qsTr("Name")

        Field {
            id: nameField

            Layout.fillWidth: true
            escClears: false

            onAccepted: root.triggerPrimary()
        }
    }
}
