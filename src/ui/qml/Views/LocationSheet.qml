pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// "Set location" (spec v2 §3 Fig. 7, `.sheet`): one path field, prefilled with the cursor
// torrent's save path, `ctrl+o` in the field opens a native directory picker
// (`App.pickDirectory`) when a portal answers and just leaves the typed text otherwise.
//
// #28 (docs/26 L1, D4 a): when torrents outside the selection use the same data, moving only
// the selection would leave them without their files. The sheet names them and asks; "move
// both" is picked every time and sends them along to the same new folder.
Sheet {
    id: root

    property var hashes: []
    property var _totalSize: 0
    /// #28: `App.sharedData(hashes)` at `openFor()`.
    property var _shared: ({})

    /// Torrents outside the selection share the data: the sheet asks.
    readonly property bool asking: (root._shared.outside ?? []).length > 0
    /// The pick: 0 moves the sharers too, 1 only the selection.
    readonly property alias choice: sharedChoice.choice

    /// What `move` sends to the new folder.
    function moveHashes(): var {
        return root.asking && sharedChoice.choice === 0 ? root.hashes.concat(root._shared.outside) : root.hashes;
    }

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": root.asking && sharedChoice.choice === 0 ? qsTr("move %1").arg(root.moveHashes().length) : qsTr(
                                                                   "move"),
            "primary": true
        }
    ]
    glyph: "⌂" // ⌂, spec Fig. 7
    sheetWidth: root.asking ? Theme.space(520) : Theme.space(420)
    subtitle: qsTr("%1 · %2 moves with it").arg(root.hashes.length === 1 ? qsTr("1 torrent") : qsTr(
                                                    "%1 torrents").arg(root.hashes.length)).arg(
                 App.sizeText(root._totalSize))
    title: qsTr("Set location")

    /// Opens on `hashes`, prefilling the field with the cursor torrent's (the first hash's)
    /// save path.
    function openFor(hashes: var): void {
        root.hashes = hashes;
        root._totalSize = 0;
        let prefill = "";
        for (let i = 0; i < hashes.length; ++i) {
            const row = App.torrentRow(hashes[i]);
            if (row.name === undefined)
                continue;
            root._totalSize += row.size;
            if (i === 0)
                prefill = row.savePath;
        }
        pathField.text = prefill;
        root._shared = App.sharedData(hashes, true);
        // "Move both" is picked every time.
        sharedChoice.choice = 0;
        root.open();
    }

    onAccepted: {
        // An emptied field is no place to move to (the server would answer a bare 400/409):
        // `move` does nothing and the sheet stays until there is a path or `esc`.
        const path = pathField.text.trim();
        if (path.length === 0)
            return;
        App.setLocation(root.moveHashes(), path);
        root.close();
    }

    FormRow {
        Layout.fillWidth: true
        label: qsTr("New path on %1").arg(App.serverName)

        Field {
            id: pathField

            Layout.fillWidth: true
            escClears: false
            trailing: qsTr("ctrl+o browse")

            // Field's own `Keys.onReturnPressed` already marks the key accepted (it emits
            // `accepted()` either way), so `enter` never reaches the Sheet's own handling
            // unless the field's `accepted` signal is wired to trigger it here.
            onAccepted: root.triggerPrimary()

            Keys.onPressed: event => {
                if ((event.modifiers & Qt.ControlModifier) !== 0 && event.key === Qt.Key_O) {
                    const picked = App.pickDirectory(pathField.text);
                    if (picked.length > 0)
                        pathField.text = picked;
                    event.accepted = true;
                }
            }
        }
    }

    // #28: who else uses these files, and whether they move too.
    SharedDataLine {
        mode: "move"
        selectionCount: root.hashes.length
        shared: root._shared
        visible: root.asking
    }

    SharedChoice {
        id: sharedChoice

        rows: {
            const others = (root._shared.outside ?? []).length;
            return [{
                        "title": root.hashes.length === 1 && others === 1 ? qsTr("move both") : qsTr("move all %1").arg(
                                                                                root.hashes.length + others),
                        "detail": qsTr("they keep sharing one copy in the new folder")
                    }, {
                        "title": root.hashes.length === 1 ? qsTr("move only this one") : qsTr("move only these %1").arg(
                                                                root.hashes.length),
                        "detail": others === 1 ? qsTr("the other one loses its files") : qsTr(
                                                     "the other %1 lose their files").arg(others)
                    }];
        }
        visible: root.asking
    }

    Connections {
        function onSharedFilesLoaded(): void {
            if (root.opened)
                root._shared = App.sharedData(root.hashes);
        }

        target: App
    }
}
