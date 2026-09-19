pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The menu/multi-selection fallback rename: the table's inline editor is the primary way to
// rename one torrent, this is what `r` (from the menu, or with more than one row selected)
// falls back to. Always renames one torrent — the cursor's — never the whole selection.
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
        if (nameField.text.length > 0)
            App.rename(root.hash, nameField.text);
        root.close();
    }

    FormRow {
        Layout.fillWidth: true
        label: qsTr("Name")

        Field {
            id: nameField

            Layout.fillWidth: true

            onAccepted: root.triggerPrimary()
        }
    }
}
