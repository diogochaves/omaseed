pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// "Add it again" (board 44 §2, DECISIONS C17 D3, #118): what frees a hybrid's or a v2-only
// torrent's (C29) skipped files that hold downloaded pieces, and the one way out of
// libtorrent's torrent_inconsistent_hashes on a hybrid (board 28's error block). The torrent is
// removed keeping its files and added back with the same selection, names, category, tags and
// limits; its cost is named before it runs.
//
// DECISIONS C30 F13: reached straight from shift+f or the band's "Add it again…" when only that
// frees the space, it says why in one line ("Nothing else frees them: this torrent is hybrid,
// …"), whose "why?" opens the band's longer words in place, and names the size it frees.
//
// Hand-formatted to qmlformat's own style (the kit's overlays, KIT-19).
ConfirmDialog {
    id: root

    property string hash: ""
    property string name: ""
    /// The skipped files that go while it is away; 0 for the error block's.
    property int files: 0
    /// What they hold ("1.03 GB"), "" when it isn't known.
    property string bytesText: ""
    /// C30 F13: "hybrid" or "v2" when only adding it again frees them (the one line why);
    /// "" otherwise.
    property string version: ""
    /// The "why?" line is open.
    property bool whyOpen: false

    readonly property string _size: root.bytesText.length > 0 ? " (" + root.bytesText + ")" : ""
    /// C30 F13: the one line why.
    readonly property string whyLine: {
        if (root.version.length === 0)
            return "";
        const one = root.files === 1;
        if (root.version === "v2")
            return one ? qsTr("Nothing else frees it: this torrent is v2, and pieces of it were downloaded.")
                       : qsTr("Nothing else frees them: this torrent is v2, and pieces of them were downloaded.");
        return one ? qsTr("Nothing else frees it: this torrent is hybrid, and pieces of it were downloaded.")
                   : qsTr("Nothing else frees them: this torrent is hybrid, and pieces of them were downloaded.");
    }
    /// The band's Why? words (C30 F12), opened in place.
    readonly property string whyText: root.version === "v2"
                                      ? qsTr("Deleting a file that holds downloaded pieces needs a recheck, and on a v2 torrent a recheck leaves it short of 100 % for good, a libtorrent bug. Adding it again from its own .torrent, with those files gone, checks it from scratch.")
                                      : qsTr("Deleting a file that holds downloaded pieces needs a recheck, and a recheck on a hybrid hits a libtorrent bug that errors the torrent. Adding it again from its own .torrent, with those files gone, checks it from scratch.")

    cancelText: qsTr("cancel")
    confirmText: qsTr("add it again")
    destructive: true
    message: qsTr("Add %1 again?").arg(root.name)
    panelWidth: Theme.space(480)
    subtitle: (root.files === 1 ? qsTr("It is removed from qBittorrent keeping its files, 1 skipped file is deleted%1, and it comes back with the same files ticked, names, category, tags and limits.").arg(root._size)
               : root.files > 1 ? qsTr("It is removed from qBittorrent keeping its files, %1 skipped files are deleted%2, and it comes back with the same files ticked, names, category, tags and limits.").arg(root.files).arg(root._size)
                                : qsTr("It is removed from qBittorrent keeping its files, and comes back with the same files ticked, names, category, tags and limits."))
              + " " + qsTr("It loses its ratio, seeding time and added date.")

    function openFor(hash: string, name: string, files: int): void {
        root.openWhy(hash, name, files, "", "");
    }

    /// C30 F13: straight from shift+f or the band, with the size and the version's one line.
    function openWhy(hash: string, name: string, files: int, bytesText: string,
                     version: string): void {
        root.hash = hash;
        root.name = name;
        root.files = files;
        root.bytesText = bytesText;
        root.version = version;
        root.whyOpen = false;
        root.open();
    }

    onAccepted: App.addTorrentAgain(root.hash, root.files > 0)

    Text {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        objectName: "addAgainWhyLine"
        text: root.whyLine
        visible: root.whyLine.length > 0
        wrapMode: Text.Wrap
    }

    // "why?": the longer words, in place; a click again folds them.
    Text {
        Accessible.name: root.whyOpen ? qsTr("hide why") : qsTr("why?")
        Accessible.role: Accessible.Link
        Accessible.onPressAction: root.whyOpen = !root.whyOpen
        color: Theme.accent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        objectName: "addAgainWhy"
        text: root.whyOpen ? qsTr("why? ‹") : qsTr("why? ›")
        visible: root.whyLine.length > 0

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor

            onClicked: root.whyOpen = !root.whyOpen
        }
    }

    Text {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        objectName: "addAgainWhyText"
        text: root.whyText
        visible: root.whyOpen && root.whyLine.length > 0
        wrapMode: Text.Wrap
    }
}
