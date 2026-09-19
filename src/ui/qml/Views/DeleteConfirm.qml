pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The delete confirmation of spec v2 §3 Fig. 6 (`.confirm`), on `Kit/ConfirmDialog`: one
// torrent names itself ("Delete "linux-distro.iso"?"), several are counted, the subtitle
// lists the names and the total size, and the body's toggle remembers whether the files come
// with it (`App.deleteFilesDefault`). `d` opens with the toggle as remembered; `D` opens it
// forced on.
ConfirmDialog {
    id: root

    /// The hashes to delete. Read once, at `openFor()`, so a sync mid-dialog cannot pull the
    /// rug from under the question the user is answering.
    property var hashes: []
    property var _names: []
    property var _totalSize: 0

    cancelText: qsTr("cancel")
    confirmText: qsTr("delete")
    destructive: true
    message: root.hashes.length === 1 ? qsTr("Delete “%1”?").arg(
                                            root._names.length > 0 ? root._names[0] : "")
                                       : qsTr("Delete %1 torrents and their files?").arg(
                                            root.hashes.length)
    subtitle: qsTr("%1 · %2 on disk").arg(root._names.join(", ")).arg(App.sizeText(
                                                    root._totalSize))

    /// Opens the dialog on `hashes`, with the "also delete files" toggle set to `filesOn`
    /// (the remembered default for `d`, forced true for `D`).
    function openFor(hashes: var, filesOn: bool): void {
        root.hashes = hashes;
        // Built as plain JS locals and assigned once: mutating a `var` property's array with
        // `.push()` in place does not raise the property's changed signal, so `message` and
        // `subtitle` would keep showing whatever they last saw.
        const names = [];
        let totalSize = 0;
        for (const hash of hashes) {
            const row = App.torrentRow(hash);
            if (row.name !== undefined) {
                names.push(row.name);
                totalSize += row.size;
            }
        }
        root._names = names;
        root._totalSize = totalSize;
        alsoDelete.checked = filesOn;
        root.open();
    }

    Toggle {
        id: alsoDelete

        Layout.fillWidth: true
        text: qsTr("also delete files from disk")

        onToggled: App.deleteFilesDefault = alsoDelete.checked
    }

    onAccepted: App.remove(root.hashes, alsoDelete.checked)
}
