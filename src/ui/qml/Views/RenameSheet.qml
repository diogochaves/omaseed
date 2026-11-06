pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The actions menu's rename: the table's inline editor (`r`, always) is the primary way to
// rename a torrent; this sheet is what the menu's rename row opens. Always renames one
// torrent — the cursor's — never the whole selection.
//
// C25 K28 (board 47 §G): where the engine renames the torrent's top folder (or its one file)
// on disk too (Transmission's torrent_rename_path), the sheet says so under the name, with
// both paths; `r` opens this sheet there instead of the inline editor, so it is always read.
Sheet {
    id: root

    property string hash: ""
    /// What the server can do (App.features); a test gives its own.
    property var features: App.features
    property string engineName: App.serverEngineName || "qBittorrent"
    /// The torrent's content on disk (its folder, or its one file), as the server says.
    property string contentPath: ""
    /// The rename changes the name on disk too.
    readonly property bool onDisk: root.features.renameOnDisk === true

    /// "/srv/films/Old becomes …/New": the parent folder stays, so it is shortened.
    readonly property string pathsText: {
        const from = root.contentPath.replace(/\/+$/, "");
        const name = nameField.text.trim();
        if (from.length === 0 || name.length === 0)
            return "";
        return qsTr("%1 becomes …/%2").arg(from).arg(name);
    }

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
    glyph: Glyphs.rename
    sheetWidth: Theme.space(420)
    title: qsTr("Rename")

    function openFor(hash: string): void {
        root.hash = hash;
        const row = App.torrentRow(hash);
        nameField.text = row.name !== undefined ? row.name : "";
        root.contentPath = row.contentPath !== undefined ? row.contentPath : "";
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

    // C25 K28: "On Transmission this renames its folder on disk too", both paths under it.
    ColumnLayout {
        Layout.fillWidth: true
        objectName: "renameOnDiskBlock"
        spacing: Theme.space(2)
        visible: root.onDisk

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            color: Theme.yellow
            objectName: "renameOnDiskLine"
            text: qsTr("On %1 this renames its folder on disk too").arg(root.engineName)
            wrapMode: Text.Wrap
        }

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            dim: true
            elide: Text.ElideMiddle
            objectName: "renameOnDiskPaths"
            size: "caption"
            text: root.pathsText
            visible: text.length > 0
        }
    }
}
