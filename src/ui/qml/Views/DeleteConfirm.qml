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
//
// #28 (docs/26): with the files toggle on, torrents outside the selection that use the same
// data (`App.sharedData`, a cross-seed over the same folder) are named in a yellow line, and
// two rows ask what to do: keep the files (picked every time) or delete them along with those
// torrents. The button says what will happen. When every sharer is in the selection a dim
// line says they share one copy, and the size counts it once. The files question comes before
// #24 T6's last-seed line.
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
    /// #28: `App.sharedData(hashes)` at `openFor()`, again when a file list it waited for
    /// comes in.
    property var _shared: ({})

    /// The files question is asked: files on, and torrents outside the selection share them.
    readonly property bool asking: alsoDelete.checked && (root._shared.outside ?? []).length > 0
    /// The pick: 0 keeps the shared files, 1 deletes them with the torrents that share them.
    readonly property alias choice: sharedChoice.choice
    /// What "delete those too" adds to the delete.
    readonly property var _outside: root._shared.outside ?? []
    /// The hashes the question is about: the selection, plus the sharers when they join.
    readonly property var _effective: root.asking && sharedChoice.choice === 1 ? root.hashes.concat(root._outside) :
                                                                                  root.hashes
    /// #24 T6 again when sharers join: the last-seed guard is about what will be deleted.
    readonly property var _guard: root.asking && sharedChoice.choice === 1 ? App.console.rarity(root._effective) :
                                                                             root._rarity

    cancelText: qsTr("cancel")
    confirmText: {
        if (!root.asking)
            return qsTr("delete");
        if (sharedChoice.choice === 1)
            return qsTr("delete %1 with files").arg(root._effective.length);
        return root.hashes.length === 1 ? qsTr("delete, keep files") : qsTr("delete %1").arg(root.hashes.length);
    }
    destructive: true
    keyHandler: event => {
        if (!root.asking || sharedChoice.activeFocus)
            return false;
        if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
            sharedChoice.move(1);
            return true;
        }
        if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
            sharedChoice.move(-1);
            return true;
        }
        return false;
    }
    panelWidth: root.asking ? Theme.space(520) : Theme.space(370)
    message: root.hashes.length === 1 ? qsTr("Delete this torrent?")
                                      : qsTr("Delete %1 torrents?").arg(root.hashes.length)
    subtitle: {
        const shown = root._names.slice(0, root._listed);
        const size = qsTr("%1 on disk").arg(App.sizeText(root._shared.diskSize ?? root._totalSize));
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
        root._shared = App.sharedData(hashes, true);
        // "Keep the files" is picked every time, whatever was picked before.
        sharedChoice.choice = 0;
        alsoDelete.checked = filesOn;
        root._flipped = false;
        root.open();
    }

    /// The deletes `accept` sends: `[{hashes, files}]`. Keeping shared files splits the
    /// selection in two: with files for the torrents that share nothing, without for the rest.
    function plan(): var {
        if (!root.asking)
            return [{
                        "hashes": root.hashes,
                        "files": alsoDelete.checked
                    }];
        if (sharedChoice.choice === 1)
            return [{
                        "hashes": root._effective,
                        "files": true
                    }];
        const sharing = (root._shared.sharing ?? []).map(entry => entry.hash);
        const alone = root.hashes.filter(hash => sharing.indexOf(hash) < 0);
        const out = [];
        if (alone.length > 0)
            out.push({
                         "hashes": alone,
                         "files": true
                     });
        out.push({
                     "hashes": sharing,
                     "files": false
                 });
        return out;
    }

    Toggle {
        id: alsoDelete

        Layout.fillWidth: true
        text: qsTr("also delete files from disk")

        onToggled: root._flipped = true
    }

    // #28: who else uses these files, and what to do about it.
    SharedDataLine {
        id: sharedLine

        selectionCount: root.hashes.length
        shared: root._shared
        visible: root.asking
    }

    SharedChoice {
        id: sharedChoice

        rows: {
            const many = root.hashes.length > 1;
            const sharingCount = (root._shared.sharing ?? []).length;
            const others = root._outside.length;
            const total = App.sizeText(root._shared.diskSizeWithOutside ?? 0);
            // A selection: the files of the torrents that share stay, the others' go.
            const kept = sharingCount === 1 ? qsTr("the files of the one that shares stay") : qsTr(
                                                  "the files of the %1 that share stay").arg(sharingCount);
            return [{
                        "title": many ? qsTr("keep their files") : qsTr("keep the files"),
                        "detail": many ? qsTr("delete %1 torrents; %2").arg(root.hashes.length).arg(kept) :
                                         others === 1 ? qsTr("delete only this torrent; the other one keeps seeding") :
                                                        qsTr("delete only this torrent; the other %1 keep seeding").arg(others)
                    }, {
                        "title": others === 1 ? qsTr("delete the files and that torrent too") : qsTr(
                                                    "delete the files and those %1 too").arg(others),
                        "detail": qsTr("%1 torrents, %2").arg(root.hashes.length + others).arg(total)
                    }];
        }
        visible: root.asking
    }

    // D3 a: every torrent that shares is in the selection, so nothing is lost but the copy.
    Label {
        Layout.fillWidth: true
        dim: true
        size: "small"
        text: qsTr("These %1 share one copy of their files: %2 is deleted once.").arg(root._shared.insideCount ?? 0).arg(
                  App.sizeText(root._shared.diskSize ?? 0))
        visible: alsoDelete.checked && !root.asking && (root._shared.insideCount ?? 0) > 1
        wrapMode: Text.WordWrap
    }

    Connections {
        function onSharedFilesLoaded(): void {
            if (root.opened)
                root._shared = App.sharedData(root.hashes);
        }

        target: App
    }

    // #24 T6: the last-seed guard. Deleting a torrent you are the only seed of takes it out
    // of the swarm; the sheet says so, with the peers waiting, and offers to stop instead.
    Rectangle {
        Layout.fillWidth: true
        color: Qt.alpha(Theme.yellow, 0.08)
        implicitHeight: guardText.implicitHeight + Theme.space(12)
        visible: (root._guard.rare ?? 0) > 0

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
                const r = root._guard;
                const waiting = (r.waiting ?? 0) > 0 ? qsTr(" %1 waiting for it.").arg(r.waiting) : "";
                if (root._effective.length === 1 && r.onlyYou === 1)
                    return qsTr("<font color='%1'>You are its only seed.</font>%2 Deleting it takes it out of the swarm.").arg(Theme.yellow).arg(waiting);
                if (root._effective.length === 1)
                    return qsTr("<font color='%1'>It has %2 seeds in the swarm, you included.</font>%3").arg(Theme.yellow).arg(r.seeds).arg(waiting);
                return qsTr("<font color='%1'>%2 of these are rare</font>, %3 with you as the only seed.%4").arg(Theme.yellow).arg(r.rare).arg(r.onlyYou ?? 0).arg(waiting);
            }
            textFormat: Text.StyledText
            wrapMode: Text.Wrap
        }
    }

    Button {
        bordered: true
        text: qsTr("stop instead")
        visible: (root._guard.rare ?? 0) > 0

        onClicked: {
            App.stopTorrents(root._effective);
            root.close();
        }
    }

    onAccepted: {
        // The remembered default follows deliberate choices only: a flip that was then
        // cancelled, or `D`'s forced "on", does not change what `d` opens with.
        if (root._flipped)
            App.deleteFilesDefault = alsoDelete.checked;
        for (const step of root.plan())
            App.remove(step.hashes, step.files);
    }
}
