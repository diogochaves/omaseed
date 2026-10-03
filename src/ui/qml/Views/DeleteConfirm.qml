pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The delete confirmation of spec v2 §3 Fig. 6 (`.confirm`), on `Kit/ConfirmDialog`. The
// title only counts ("Delete this torrent?", "Delete 3 torrents?"); the names and the total
// size are said once, in the line under it, one name per line, the first three and then "and
// N more" (batch-2 J7: the title used to repeat the name and wrap in the middle of it). The
// body's toggle remembers whether the files come with it (`App.deleteFilesDefault`), but only
// once a delete with it flipped is confirmed: a flip and a cancel leave the default alone. `d`
// opens with the toggle as remembered; `D` opens it forced on.
ConfirmDialog {
    id: root

    /// The hashes to delete. Read once, at `openFor()`, so a sync mid-dialog cannot pull the
    /// rug from under the question the user is answering.
    property var hashes: []
    property var _names: []
    property var _totalSize: 0
    /// The toggle was flipped by hand since `openFor()`: confirming then remembers it.
    property bool _flipped: false
    /// #24 T6: `App.console.rarity(hashes)` at `openFor()`: who has you as their last seed.
    property var _rarity: ({})

    cancelText: qsTr("cancel")
    confirmText: qsTr("delete")
    destructive: true
    message: root.hashes.length === 1 ? qsTr("Delete this torrent?")
                                      : qsTr("Delete %1 torrents?").arg(root.hashes.length)
    subtitle: {
        const shown = root._names.slice(0, root._listed);
        const size = qsTr("%1 on disk").arg(App.sizeText(root._totalSize));
        const more = root._names.length - shown.length;
        const last = more > 0 ? qsTr("and %1 more · %2").arg(more).arg(size) : size;
        return shown.concat([last]).join("\n");
    }

    /// How many names the line under the title spells out before "and N more".
    readonly property int _listed: 3

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
        root._rarity = App.console.rarity(hashes);
        alsoDelete.checked = filesOn;
        root._flipped = false;
        root.open();
    }

    Toggle {
        id: alsoDelete

        Layout.fillWidth: true
        text: qsTr("also delete files from disk")

        onToggled: root._flipped = true
    }

    // #24 T6: the last-seed guard. Deleting a torrent you are the only seed of takes it out
    // of the swarm; the sheet says so, with the peers waiting, and offers to stop instead.
    Rectangle {
        Layout.fillWidth: true
        color: Qt.alpha(Theme.yellow, 0.08)
        implicitHeight: guardText.implicitHeight + Theme.space(12)
        visible: (root._rarity.rare ?? 0) > 0

        Rectangle {
            color: Theme.yellow
            height: parent.height
            width: Theme.space(2)
        }

        Text {
            id: guardText

            anchors.left: parent.left
            anchors.leftMargin: Theme.space(10)
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(10)
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: {
                const r = root._rarity;
                const waiting = (r.waiting ?? 0) > 0 ? qsTr(" %1 waiting for it.").arg(r.waiting) : "";
                if (root.hashes.length === 1 && r.onlyYou === 1)
                    return qsTr("<font color='%1'>You are its only seed.</font>%2 Deleting it takes it out of the swarm.").arg(Theme.yellow).arg(waiting);
                if (root.hashes.length === 1)
                    return qsTr("<font color='%1'>It has %2 seeds in the swarm, you included.</font>%3").arg(Theme.yellow).arg(r.seeds).arg(waiting);
                return qsTr("<font color='%1'>%2 of these are rare</font>, %3 with you as the only seed.%4").arg(Theme.yellow).arg(r.rare).arg(r.onlyYou ?? 0).arg(waiting);
            }
            textFormat: Text.StyledText
            wrapMode: Text.Wrap
        }
    }

    Button {
        text: qsTr("stop instead")
        visible: (root._rarity.rare ?? 0) > 0

        onClicked: {
            App.stopTorrents(root.hashes);
            root.close();
        }
    }

    onAccepted: {
        // The remembered default follows deliberate choices only: a flip that was then
        // cancelled, or `D`'s forced "on", does not change what `d` opens with.
        if (root._flipped)
            App.deleteFilesDefault = alsoDelete.checked;
        App.remove(root.hashes, alsoDelete.checked);
    }
}
