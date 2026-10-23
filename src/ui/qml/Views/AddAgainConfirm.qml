pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// "Add it again" (board 44 §2, DECISIONS C17 D3, #118): what frees a hybrid's or a v2-only
// torrent's (C29) skipped files that hold downloaded pieces, and the one way out of
// libtorrent's torrent_inconsistent_hashes on a hybrid (board 28's error block). The torrent is
// removed keeping its files and added back with the same selection, names, category, tags and
// limits; its cost is named before it runs.
//
// Hand-formatted to qmlformat's own style (the kit's overlays, KIT-19).
ConfirmDialog {
    id: root

    property string hash: ""
    property string name: ""
    /// The skipped files that go while it is away; 0 for the error block's.
    property int files: 0

    cancelText: qsTr("cancel")
    confirmText: qsTr("add it again")
    destructive: true
    message: qsTr("Add %1 again?").arg(root.name)
    subtitle: (root.files === 1 ? qsTr("It is removed from qBittorrent keeping its files, 1 skipped file is deleted, and it comes back with the same files ticked, names, category, tags and limits.")
               : root.files > 1 ? qsTr("It is removed from qBittorrent keeping its files, %1 skipped files are deleted, and it comes back with the same files ticked, names, category, tags and limits.").arg(root.files)
                                : qsTr("It is removed from qBittorrent keeping its files, and comes back with the same files ticked, names, category, tags and limits."))
              + " " + qsTr("It loses its ratio, seeding time and added date.")

    function openFor(hash: string, name: string, files: int): void {
        root.hash = hash;
        root.name = name;
        root.files = files;
        root.open();
    }

    onAccepted: App.addTorrentAgain(root.hash, root.files > 0)
}
