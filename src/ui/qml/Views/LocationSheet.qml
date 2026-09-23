pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// "Set location" (spec v2 §3 Fig. 7, `.sheet`): one path field, prefilled with the cursor
// torrent's save path, `ctrl+o` in the field opens a native directory picker
// (`App.pickDirectory`) when a portal answers and just leaves the typed text otherwise.
Sheet {
    id: root

    property var hashes: []
    property var _totalSize: 0

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": qsTr("move"),
            "primary": true
        }
    ]
    glyph: "⌂" // ⌂, spec Fig. 7
    sheetWidth: Theme.space(420)
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
        root.open();
    }

    onAccepted: {
        App.setLocation(root.hashes, pathField.text);
        root.close();
    }

    FormRow {
        Layout.fillWidth: true
        label: qsTr("New path on %1").arg(App.serverName)

        Field {
            id: pathField

            Layout.fillWidth: true
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
}
