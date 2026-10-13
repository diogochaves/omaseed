pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The Peers tab's `b` (ruling Q4, review VIEW-6), on `Kit/ConfirmDialog` like Fig. 6's
// delete. `transfer/banPeers` puts the address into qBittorrent's own IP filter: it is not
// undone here, it holds for every torrent, and only qBittorrent's settings take it off. So
// the dialog says that, and opens destructive, with the cursor on cancel.
//
// Hand-formatted to qmlformat's own style: qmlformat de-indents typed function bodies here
// (the defect KIT-19 notes for the kit's overlays).
ConfirmDialog {
    id: root

    /// The `host:port` peers being banned: the Peers tab's selection, or its cursor row
    /// (#51 D15). Read once, at `openFor()`: the peer list moves every two seconds and the
    /// question must stay about the rows the user pressed `b` on.
    property var addresses: []
    /// The first of them, the one the message names when there is only one.
    readonly property string address: root.addresses.length > 0 ? root.addresses[0] : ""

    cancelText: qsTr("cancel")
    confirmText: qsTr("ban")
    destructive: true
    message: root.addresses.length > 1 ? qsTr("Ban %n peers?", "", root.addresses.length)
                                       : qsTr("Ban %1?").arg(root._host)
    subtitle: qsTr(
                  "The ban is permanent and applies to every torrent, until you remove it in qBittorrent's settings (IP filtering).")

    /// The address without its port: the ban is on the IP, whatever port it comes back on.
    readonly property string _host: {
        const colon = root.address.lastIndexOf(":");
        return colon > 0 ? root.address.slice(0, colon) : root.address;
    }

    /// `addresses`: one `host:port`, or a list of them.
    function openFor(addresses: var): void {
        root.addresses = typeof addresses === "string" ? [addresses] : addresses;
        root.open();
    }

    onAccepted: App.banPeers(root.addresses)
}
