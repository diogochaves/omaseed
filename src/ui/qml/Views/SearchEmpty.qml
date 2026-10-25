pragma ComponentBehavior: Bound

import QtQuick
import Omaseed.Kit

// Search with nothing to search with (board 48 `search()`'s empty states, DECISIONS C26 SR13,
// SR11): no plugins, all of them off, or a server whose machine has no Python. The view stays in
// the rail and says so in one breath, naming no plugin and no site (SR11), with a link to
// Settings › qBittorrent › Search plugins (`paneRequested`).
Item {
    id: root

    /// "none", "off" or "nopython".
    property string kind: ""
    property int pluginCount: 0
    /// The installed plugins' own names, in words ("plugin a, plugin b and plugin c").
    property string pluginNames: ""
    property string serverName: ""
    property bool multiServer: false

    signal paneRequested

    readonly property string _on: root.multiServer && root.serverName.length > 0 ? qsTr(
                                                                                       " on %1").arg(
                                                                                       root.serverName) :
                                                                                   ""
    readonly property string _server: root.serverName.length > 0 ? root.serverName : qsTr(
                                                                       "the server")
    readonly property string titleText: {
        switch (root.kind) {
        case "none":
            return qsTr("No search plugins%1").arg(root._on);
        case "off":
            return root.pluginCount === 1 ? qsTr("The plugin is turned off") : qsTr(
                                                "All %1 plugins are turned off").arg(
                                                root.pluginCount);
        case "nopython":
            return qsTr("qBittorrent can't run plugins here");
        }
        return "";
    }
    readonly property string wordsText: {
        switch (root.kind) {
        case "none":
            return qsTr(
                        "Search runs through qBittorrent's own search plugins: small Python files you choose and install on the server, each asking one site. omaseed doesn't name, ship or suggest any.");
        case "off":
            return qsTr("%1 %2 installed on %3 but none is on, so a search would ask nobody.").arg(
                        root.pluginNames).arg(root.pluginCount === 1 ? qsTr("is") : qsTr("are")).arg(
                        root._server);
        case "nopython":
            return qsTr(
                        "Search plugins are Python, and %1's machine has no Python: qBittorrent said so when the search started. Install Python 3 there; nothing changes in omaseed.").arg(
                        root._server);
        }
        return "";
    }
    readonly property string linkText: {
        switch (root.kind) {
        case "none":
            return qsTr("install a plugin…");
        case "off":
            return qsTr("turn them on…");
        }
        return qsTr("the plugins…");
    }

    implicitHeight: column.implicitHeight + Theme.searchEmptyTop
    objectName: "searchEmpty"

    Column {
        id: column

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.searchEmptyTop
        spacing: Theme.searchEmptyGap
        width: Math.min(Theme.searchEmptyWidth, root.width - 2 * Theme.searchBarInset)

        Text {
            color: Theme.brightForeground
            // The skin's heading voice (board 46 `.dh b`, board 48 `.empty b`).
            font.capitalization: Theme.titleUpper ? Font.AllUppercase : Font.MixedCase
            font.family: Theme.labelFamily
            font.letterSpacing: Theme.searchEmptyTitleSize * Theme.labelTrackTitle
            font.pixelSize: Theme.searchEmptyTitleSize
            font.weight: Theme.titleWeight
            horizontalAlignment: Text.AlignHCenter
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            objectName: "searchEmptyTitle"
            text: root.titleText
            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.Wrap
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.valueFamily
            font.pixelSize: Theme.searchEmptyWordsSize
            horizontalAlignment: Text.AlignHCenter
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            objectName: "searchEmptyWords"
            text: root.wordsText
            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.Wrap
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            bordered: true
            objectName: "searchEmptyLink"
            primary: true
            text: root.linkText
            tooltip: qsTr("Settings › qBittorrent › Search plugins")

            onClicked: root.paneRequested()
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.valueFamily
            font.pixelSize: Theme.searchNoteSize
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Settings › qBittorrent › Search plugins")
            textFormat: Text.PlainText
            width: parent.width
        }
    }
}
