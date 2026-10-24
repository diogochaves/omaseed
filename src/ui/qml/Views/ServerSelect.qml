pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// docs/34 D3 a, D4 a, D5 a (#70): the qBittorrent group's head in Settings, the server it
// edits, drawn as the panes' own pick-one select: "● public 5.2.3  s ▾". It opens a list of the
// servers with their state (the Servers pane's dots: green when it answers, red while the one
// omaseed is on doesn't, dim when nothing is known or it is offline), headed "switch omaseed
// to", the one omaseed is on marked "on it ✓", the cursor on the next one, and a last line
// saying the switch is the app's. Picking another sends `switchRequested`; the owner switches
// (App.setDefaultServer, docs/DECISIONS.md C8: one connection, so the library follows).
//
// With one server there is nothing to pick: plain dim text, as before, no border, no ▾.
//
// It takes the server list as a model and knows nothing of App, so another list of servers
// (board 33's palette, if it is ruled) can share the same model. `menuParent` is the item the
// list opens over (the Settings modal), where its scrim catches the clicks around it.
//
//     ServerSelect { serverList: App.serverList; currentId: …; menuParent: modal; onSwitchRequested: id => … }
//
// Do NOT run `qmlformat` on this file: it re-indents typed functions on every pass (the defect
// Menu.qml notes). It is hand-formatted.
Item {
    id: root

    property ServerListModel serverList: null
    /// The id of the server omaseed is on.
    property string currentId: ""
    property string currentName: ""
    /// "5.2.3"; empty while unknown.
    property string version: ""
    /// The server omaseed is on answers.
    property bool connected: false
    /// The keyboard's cursor is on it (Settings' nav).
    property bool hasCursor: false
    /// The item the list opens over.
    property Item menuParent: null

    /// Another server was picked.
    signal switchRequested(string id)
    /// The list opened: a good moment to probe the other servers.
    signal listOpened
    /// The list closed, with or without a pick.
    signal listClosed

    readonly property bool switches: root.serverList !== null && root.serverList.count > 1
    readonly property bool listOpen: list.opened
    readonly property alias list: list
    readonly property bool hot: root.hasCursor || area.containsMouse

    /// Bumped when the list's model changes, so `items` is built again.
    property int _revision: 0

    /// The list's rows, one per server: `[{text, glyph, glyphColor, note, dim, action: id}]`.
    readonly property var items: {
        const out = [];
        const model = root.serverList;
        if (model === null || root._revision < 0)
            return out;
        for (let i = 0; i < model.count; ++i) {
            const id = model.idAt(i);
            const map = model.serverMap(id);
            const current = id === root.currentId;
            const failing = map.reachableText.length > 0 && !map.reachable;
            const answers = current ? root.connected : map.reachable === true;
            let note = "";
            if (current)
                note = qsTr("on it ✓");
            else if (failing)
                note = qsTr("offline");
            else if (map.reachable === true && map.torrentCount >= 0)
                note = map.torrentCount === 1 ? qsTr("1 torrent") : qsTr("%1 torrents").arg(map.torrentCount);
            out.push({
                         "text": map.name,
                         "glyph": Glyphs.dot,
                         "glyphColor": current && !root.connected ? Theme.red
                                       : answers ? Theme.green : Theme.dimForeground,
                         "note": note,
                         "dim": !current && failing,
                         "action": id
                     });
        }
        return out;
    }

    /// The list, under the select, the cursor on the server after the one omaseed is on (where
    /// `s` would go).
    function openList(): void {
        if (!root.switches || list.opened)
            return;
        root.listOpened();
        const host = root.menuParent ?? root;
        const at = root.mapToItem(host, 0, root.height + Theme.space(4));
        list.openAt(Math.round(at.x), Math.round(at.y));
        const ids = root.items.map(item => item.action);
        const here = ids.indexOf(root.currentId);
        list.cursorIndex = here < 0 ? 0 : (here + 1) % ids.length;
    }

    implicitHeight: root.switches ? Theme.controlHeight : plain.implicitHeight
    implicitWidth: root.switches ? selectRow.implicitWidth + Theme.space(16) : plain.implicitWidth

    Connections {
        function onDataChanged(): void {
            root._revision++;
        }

        function onChanged(): void {
            root._revision++;
        }

        function onCountChanged(): void {
            root._revision++;
        }

        target: root.serverList
    }

    // One server: the name and version, dim, as a label.
    Label {
        id: plain

        // #79: which server the panes edit, as text.
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        anchors.left: parent.left
        anchors.right: parent.right
        dim: true
        elide: Text.ElideRight
        objectName: "serverSelectPlain"
        size: "small"
        text: root.currentName + (root.version.length > 0 ? " " + root.version : "")
        visible: !root.switches
    }

    // More: the select, with the bordered Button's chrome; the accent edge under the
    // keyboard's cursor or the pointer, and while the list is open.
    Rectangle {
        id: selectBox

        /// Its words on its fill, idle or hot (docs/DECISIONS.md C30 F1).
        readonly property Ink ink: Ink {
            animated: false
            ground: root.hot || list.opened ? Theme.on.hover : Theme.on.idle
        }

        // #77: a "switch server" button, the server it edits its description; its press
        // opens the list, as the click does.
        Accessible.description: root.currentName
        Accessible.name: qsTr("switch server")
        Accessible.role: Accessible.Button
        Accessible.onPressAction: if (selectBox.enabled)
                                      root.openList()
        anchors.fill: parent
        border.color: root.hot || list.opened ? Theme.accent : Theme.fill(Theme.borderIdle)
        border.width: 1
        color: selectBox.ink.fill
        objectName: "serverSelectBox"
        radius: Theme.cornerRadius
        visible: root.switches

        RowLayout {
            id: selectRow

            anchors.left: parent.left
            anchors.leftMargin: Theme.space(8)
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space(6)

            Lamp {
                Layout.preferredHeight: Theme.space(7)
                Layout.preferredWidth: Theme.space(7)
                color: root.connected ? selectBox.ink.green : selectBox.ink.red
                objectName: "serverSelectDot"
                size: Theme.space(7)
            }

            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: Theme.space(24)
                bright: true
                elide: Text.ElideRight
                ink: selectBox.ink
                objectName: "serverSelectName"
                size: "small"
                text: root.currentName
            }

            Label {
                dim: true
                ink: selectBox.ink
                size: "small"
                text: root.version
                visible: root.version.length > 0
            }

            Label {
                dim: true
                ink: selectBox.ink
                objectName: "serverSelectKey"
                size: "caption"
                text: "s " + Glyphs.chevronDown
            }
        }

        MouseArea {
            id: area

            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            onClicked: root.openList()
        }
    }

    Menu {
        id: list

        anchors.fill: parent
        dimmed: false
        footer: qsTr("The library and the tray follow, and it becomes the default.")
        heading: qsTr("switch omaseed to")
        items: root.items
        menuWidth: Theme.space(260)
        objectName: "serverSelectList"
        parent: root.menuParent ?? root
        z: 3

        onActivated: action => {
            root.listClosed();
            if (action !== root.currentId)
                root.switchRequested(action);
        }
        onRejected: root.listClosed()
    }
}
