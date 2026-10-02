pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The details pane's `a` (ruling Q4, review VIEW-6): one field that takes tracker URLs in
// the Trackers tab (Fig. 3, `torrents/addTrackers`), peers in the Peers tab (Fig. 4,
// `torrents/addPeers`) or web seeds in the HTTP sources tab (#10, `torrents/addWebSeeds`). Several at once are separated by spaces (peers also by commas), which
// is what a pasted list turns into in a one-line field. The check is light, the one
// `App.checkTrackerUrls`/`App.checkPeers`/`App.checkWebSeedUrls` make: something typed,
// trackers with a scheme and a host, peers as `host:port` with IPv6 in brackets, web seeds as
// http or https with a host. The first entry that fails is named in
// the kit's error Banner and the sheet stays open; the server has the last word, and a
// refusal from it comes back through `App.actionFailed` like any other action.
//
// Hand-formatted to qmlformat's own style: qmlformat de-indents typed function bodies here
// (the defect KIT-19 notes for the kit's overlays).
Sheet {
    id: root

    /// "tracker", "peer" or "webseed".
    property string kind: "tracker"
    property string hash: ""
    property string errorText: ""
    readonly property alias field: entryField

    readonly property bool _peer: root.kind === "peer"
    readonly property bool _webSeed: root.kind === "webseed"

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": qsTr("add"),
            "primary": true
        }
    ]
    glyph: Glyphs.add
    sheetWidth: Theme.space(420)
    subtitle: {
        const row = App.torrentRow(root.hash);
        return row.name !== undefined ? row.name : "";
    }
    title: root._peer ? qsTr("Add peers") : root._webSeed ? qsTr("Add HTTP sources") : qsTr(
                                                                 "Add trackers")

    /// Opens empty on `hash`'s `kind` ("tracker", "peer" or "webseed").
    function openFor(kind: string, hash: string): void {
        root.kind = kind;
        root.hash = hash;
        root.errorText = "";
        entryField.text = "";
        root.open();
    }

    onAccepted: {
        root.errorText = "";
        if (root._peer) {
            const peers = App.checkPeers(entryField.text);
            if (peers.invalid.length > 0)
                root.errorText = qsTr("%1 is not a host:port").arg(peers.invalid);
            else if (peers.entries.length === 0)
                root.errorText = qsTr("Type a peer first");
            else
                App.addPeers(root.hash, peers.entries);
        } else if (root._webSeed) {
            const urls = App.checkWebSeedUrls(entryField.text);
            if (urls.invalid.length > 0)
                root.errorText = qsTr("%1 is not an http or https URL").arg(urls.invalid);
            else if (urls.entries.length === 0)
                root.errorText = qsTr("Type a URL first");
            else
                App.addWebSeeds(root.hash, urls.entries);
        } else {
            const urls = App.checkTrackerUrls(entryField.text);
            if (urls.invalid.length > 0)
                root.errorText = qsTr("%1 is not a tracker URL").arg(urls.invalid);
            else if (urls.entries.length === 0)
                root.errorText = qsTr("Type a tracker URL first");
            else
                App.addTrackers(root.hash, urls.entries);
        }
        if (root.errorText.length === 0)
            root.close();
    }

    Banner {
        Layout.fillWidth: true
        kind: "error"
        text: root.errorText
        visible: root.errorText.length > 0
    }

    FormRow {
        Layout.fillWidth: true
        label: root._peer ? qsTr("Peers · ip:port, [ipv6]:port") : root._webSeed ? qsTr(
                                                                         "HTTP source URLs") :
                                                                     qsTr("Tracker URLs")

        Field {
            id: entryField

            Layout.fillWidth: true
            escClears: false
            placeholder: root._peer ? qsTr("203.0.113.5:6881") : root._webSeed ? qsTr(
                                                                         "https://mirror.example/pub/") :
                                                                     qsTr("udp://tracker.example:1337/announce")
            trailing: qsTr("space separates")

            onAccepted: root.triggerPrimary()
            onTextChanged: root.errorText = ""
        }
    }
}
