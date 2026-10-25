pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// Settings › qBittorrent › Search plugins (board 48 `pluginsPane()`, DECISIONS C26 SR10, SR11,
// C11's sections): the server's own plugins, under C11's scope line. Install from a URL (the
// server downloads it) or, only when the server is this computer, from a file here; qBittorrent
// answers an install with nothing either way, so omaseed watches the list for 30 s and says
// installed or didn't install. A switch per plugin, ⋯ for uninstall, and check for updates (qBittorrent's own plugin list, its answer read from its log).
//
// SR11: no catalogue, no suggestion, no link to any list of plugins. What a row shows is the
// plugin's own name, version and types, as the user's server lists them.
//
// Cursor rows (the modal's `j`/`k`): 0 the install field, 1 check for updates, then one per
// plugin. `enter` on 0 puts the caret in the field, on 1 checks, on a plugin opens its ⋯;
// `space` on a plugin turns it on or off.
Item {
    id: root

    property int cursor: -1
    property bool keyboard: false
    property string serverName: ""
    /// SR10: a file here reaches the server only when it is this computer.
    property bool serverLocal: false
    /// Another server to switch to (C11's `s`).
    property string otherText: ""
    readonly property var plugins: App.searchPlugins
    readonly property int cursorCount: 2 + root.plugins.count
    readonly property alias field: installField

    /// A row was clicked: the modal's cursor goes there.
    signal rowClicked(int index)
    signal nextServerRequested
    /// The pane's own keys are done (the field left): the modal takes them back.
    signal fieldDone

    function activate(index: int, how: string): void {
        if (index === 0) {
            installField.focusInput();
            return;
        }
        if (index === 1) {
            root.plugins.checkUpdates();
            return;
        }
        const row = index - 2;
        if (row < 0 || row >= root.plugins.count)
            return;
        const at = root.plugins.index(row, 0);
        const name = root.plugins.data(at, SearchPlugins.NameRole);
        if (how === "space") {
            root.plugins.setEnabled(name, !root.plugins.data(at, SearchPlugins.EnabledRole));
            return;
        }
        const item = pluginRepeater.itemAt(row);
        if (item !== null)
            root.openPluginMenu(name, item);
    }

    /// ⋯ on plugin `name`, under `anchor`.
    function openPluginMenu(name: string, anchor: Item): void {
        const point = anchor.mapToItem(root, anchor.width, anchor.height);
        pluginMenu.target = name;
        pluginMenu.openAt(Math.round(point.x - Theme.searchMenuWidth), Math.round(point.y));
    }

    function install(): void {
        const source = installField.text.trim();
        if (source.length === 0)
            return;
        root.plugins.install(source);
    }

    implicitHeight: column.implicitHeight
    objectName: "searchPluginsPane"

    ColumnLayout {
        id: column

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Theme.space(8)

        ScopeLine {
            Layout.fillWidth: true
            linkKey: root.otherText.length > 0 ? "s" : ""
            linkText: root.otherText.length > 0 ? qsTr("switch server") : ""
            objectName: "searchScopeLine"
            text: {
                const own = qsTr(
                          "%1's plugins: installed on %1's machine and run by its qBittorrent.").arg(
                          root.serverName);
                return root.otherText.length > 0 ? own + " " + root.otherText : own;
            }

            onLinkActivated: root.nextServerRequested()
        }

        // The install field (`.inst`): a URL, or a file on this computer for a server that is it.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.searchPluginGap

            Label {
                text: qsTr("install a plugin")
                tracked: true
            }

            Field {
                id: installField

                Layout.fillWidth: true
                accessibleName: qsTr("a search plugin to install")
                hasCursor: root.keyboard && root.cursor === 0
                objectName: "searchInstallField"
                placeholder: root.serverLocal ? qsTr("a URL, or a .py file here") : qsTr(
                                                    "a URL %1 can reach").arg(root.serverName)
                trailing: root.serverLocal ? qsTr("browse…") : ""
                trailingClickable: root.serverLocal

                Keys.onEscapePressed: event => {
                    root.fieldDone();
                    event.accepted = true;
                }
                onAccepted: root.install()
                onTrailingClicked: {
                    const path = App.pickSearchPluginFile();
                    if (path.length > 0)
                        installField.text = path;
                }
            }

            Button {
                bordered: true
                enabled: installField.text.trim().length > 0 && root.plugins.installState
                         !== "installing"
                objectName: "searchInstallButton"
                primary: installField.text.trim().length > 0
                text: qsTr("install")

                onClicked: root.install()
            }
        }

        // SR10: a remote server takes URLs only; said once, under the field.
        Text {
            Layout.fillWidth: true
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.searchLineSize
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            objectName: "searchInstallRemote"
            text: qsTr(
                      "A file on this computer can't reach %1: qBittorrent installs from a URL, or a path on its own machine.").arg(
                      root.serverName)
            textFormat: Text.PlainText
            visible: !root.serverLocal
            wrapMode: Text.Wrap
        }

        // The install being watched (`.pline`): installing, installed, or didn't install.
        RowLayout {
            Layout.fillWidth: true
            objectName: "searchInstallLine"
            spacing: Theme.searchPillGap
            visible: root.plugins.installState.length > 0

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                color: root.plugins.installState === "installed" ? Theme.green :
                                                                   root.plugins.installState
                                                                   === "failed" ? Theme.yellow :
                                                                                  Theme.accent
                Layout.preferredHeight: Theme.searchPillDot
                Layout.preferredWidth: Theme.searchPillDot
                radius: Theme.searchPillDot / 2
            }

            Text {
                Layout.fillWidth: true
                color: root.plugins.installState === "failed" ? Theme.yellow : Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.searchLineSize
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                objectName: "searchInstallWords"
                text: {
                    switch (root.plugins.installState) {
                    case "installing":
                        return qsTr("installing · waiting for %1 to list it · %2 s").arg(
                                    root.serverName).arg(root.plugins.installSeconds);
                    case "installed":
                        return qsTr("installed: %1").arg(root.plugins.installName);
                    case "failed":
                        return qsTr(
                                    "%1 didn't install. After 30 s %2 lists no new plugin, and qBittorrent gives no reason. It takes a search plugin (.py) at a URL %2 can reach, or a file on %2's own machine.").arg(
                                    root.plugins.installSource).arg(root.serverName);
                    }
                    return "";
                }
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }

            Button {
                objectName: "searchInstallDismiss"
                text: qsTr("ok")
                visible: root.plugins.installState !== "installing"

                onClicked: {
                    root.plugins.dismissInstall();
                    if (root.plugins.installState === "")
                        installField.text = "";
                }
            }
        }

        // The list's head: how many, and check for updates.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space(4)
            spacing: Theme.searchPluginGap

            Label {
                Layout.fillWidth: true
                text: root.plugins.count === 1 ? qsTr("1 installed") : qsTr("%1 installed").arg(
                                                     root.plugins.count)
                tracked: true
            }

            Button {
                hasCursor: root.keyboard && root.cursor === 1
                objectName: "searchCheckUpdates"
                text: root.plugins.updateState === "checking" ? qsTr("checking…") : qsTr(
                                                                    "check for updates")
                tooltip: qsTr("qBittorrent checks its own plugin list")

                onClicked: {
                    root.rowClicked(1);
                    root.plugins.checkUpdates();
                }
            }
        }

        Text {
            Layout.fillWidth: true
            color: root.plugins.updateState === "silent" ? Theme.yellow : Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.searchLineSize
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            objectName: "searchUpdateWords"
            text: root.plugins.updateState === "silent" ? qsTr(
                                                              "qBittorrent said nothing about its plugins in 30 s.") :
                                                          root.plugins.updateState === "done" ? qsTr(
                                                                                                    "qBittorrent, at %1. A plugin from anywhere else never updates this way: install it again from where you got it.").arg(
                                                                                                    root.plugins.updateText) :
                                                                                                ""
            textFormat: Text.PlainText
            visible: text.length > 0
            wrapMode: Text.Wrap
        }

        // No plugins yet: what one is, naming none (SR11).
        Text {
            Layout.fillWidth: true
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.searchEmptyWordsSize
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            objectName: "searchNoPlugins"
            text: qsTr(
                      "No plugins yet. A search plugin is a small Python file that asks one site. You choose them and install them here, from a URL or a file; they run inside %1's qBittorrent. omaseed doesn't name, ship or suggest any.").arg(
                      root.serverName)
            textFormat: Text.PlainText
            visible: root.plugins.loaded && root.plugins.count === 0
            wrapMode: Text.Wrap
        }

        Repeater {
            id: pluginRepeater

            model: root.plugins

            delegate: Rectangle {
                id: plugin

                required property int index
                required property string name
                required property string fullName
                required property string version
                required property bool pluginOn
                required property string typesText
                required property string note

                readonly property bool hasCursor: root.keyboard && root.cursor === plugin.index + 2
                readonly property Ink ink: Ink {
                    ground: Theme.ground(false, plugin.hasCursor, rowArea.containsMouse, false)
                }

                Accessible.name: plugin.fullName
                Accessible.role: Accessible.ListItem
                Layout.fillWidth: true
                border.color: Theme.fill(Theme.hairlineAlpha)
                border.width: Theme.hairWidth
                color: plugin.ink.ground.fill
                implicitHeight: pluginColumn.implicitHeight + 2 * Theme.searchPluginInset
                objectName: "searchPlugin_" + plugin.name

                MouseArea {
                    id: rowArea

                    anchors.fill: parent
                    hoverEnabled: true

                    onClicked: root.rowClicked(plugin.index + 2)
                }

                ColumnLayout {
                    id: pluginColumn

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.searchPluginInset
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.searchPluginInset
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.libraryRowLineGap

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.searchPluginGap

                        Toggle {
                            checked: plugin.pluginOn
                            hasCursor: plugin.hasCursor
                            Accessible.name: qsTr("use %1").arg(plugin.fullName)
                            objectName: "searchPluginSwitch_" + plugin.name

                            onToggled: {
                                root.rowClicked(plugin.index + 2);
                                root.plugins.setEnabled(plugin.name, !plugin.pluginOn);
                            }
                        }

                        Text {
                            Layout.preferredWidth: Theme.searchPluginNameWidth
                            color: plugin.pluginOn ? plugin.ink.brightForeground :
                                                     plugin.ink.dimForeground
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.libraryRowSentenceSize
                            font.weight: Theme.libraryRowNameWeight
                            text: plugin.fullName
                            textFormat: Text.PlainText
                        }

                        Text {
                            Layout.preferredWidth: Theme.searchPluginVersionWidth
                            color: plugin.ink.dimForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.libraryRowSentenceSize
                            text: plugin.version
                            textFormat: Text.PlainText
                        }

                        Text {
                            Layout.fillWidth: true
                            color: plugin.ink.dimForeground
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.libraryRowMetaSize
                            text: plugin.typesText
                            textFormat: Text.PlainText
                        }

                        Button {
                            id: more

                            objectName: "searchPluginMore_" + plugin.name
                            text: Glyphs.searchLink
                            tooltip: qsTr("uninstall")

                            onClicked: {
                                root.rowClicked(plugin.index + 2);
                                root.openPluginMenu(plugin.name, more);
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        color: plugin.ink.yellow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.searchNoteSize
                        text: plugin.note
                        textFormat: Text.PlainText
                        visible: plugin.note.length > 0
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.searchNoteSize
            text: qsTr("Types are each plugin's own (qBittorrent's categories).")
            textFormat: Text.PlainText
            visible: root.plugins.count > 0
            wrapMode: Text.Wrap
        }
    }

    // ⋯ on a plugin: uninstall.
    Menu {
        id: pluginMenu

        property string target: ""

        anchors.fill: parent
        dimmed: false
        items: [
            {
                "text": qsTr("uninstall"),
                "action": "uninstall",
                "danger": true
            }
        ]
        menuWidth: Theme.searchMenuWidth
        modal: false
        objectName: "searchPluginMenu"

        onActivated: action => {
            pluginMenu.close();
            if (action === "uninstall")
                root.plugins.uninstall(pluginMenu.target);
        }
        onRejected: pluginMenu.close()
    }
}
