pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Omaseed
import Omaseed.Kit

// The ask (docs/62 §5 V17, DECISIONS C45 V17, #293): when Settings › General › Agents says to
// ask, an agent's `omaseed do` waits here for the person. A small window of its own where you
// are, under the add window's rules (C35 W1, W3): its first title, `omaseed · agent`, is fixed
// and never translated (the Hyprland snippet's rule matches it); `transientParent` is null, so
// it maps on the workspace the user is on, not the main window's; its size is the sheet's,
// fixed (minimum = maximum), so Hyprland floats and centres it. The sheet's edge gives way to
// the window's (ARCHITECTURE rule 5).
//
// It says what, on which torrents (four, then "and N more"), and which program started the
// word, read from /proc; and that omaseed can't tell who is behind a command.
//
// No key allows it (V17): the person was probably typing in a terminal when it took the
// keyboard, so ↵ does nothing until a button has the cursor (tab walks them; a focused button
// takes ↵ and space itself). esc, refuse and the window's close refuse; two minutes without an
// answer are AgentAsk's, which closes the ask and this window with it.
//
// `ask` is App.agentAsk: {open, request, allow(ticks), refuse()} (ui/app/AgentAsk.hpp). A test
// hands it a stand-in.
//
//     AgentAskWindow {}
//     askWindow.openAsk();   // when App.agentAsk.open turns true
Window {
    id: root

    /// The ask it draws and answers.
    property var ask: App.agentAsk
    /// What it asks (AgentAsk's `request`); empty while nothing is asked.
    readonly property var request: root.ask && root.ask.open ? root.ask.request : ({})
    readonly property alias sheet: sheet
    /// C47 I2's work area, for a narrow screen. A test sets its own.
    property real screenWidth: Screen.desktopAvailableWidth > 0 ? Screen.desktopAvailableWidth : 1e6
    readonly property size sheetSize: Qt.size(Math.min(sheet.sheetWidth, root.screenWidth), Math.max(
                                                  1, Math.ceil(sheet.panel.implicitHeight)))
    /// How long, in ms, the window goes on asking for the keyboard after it maps (the add
    /// window's #218, #230: a bar popup or Omarchy's menu may hold it). Tests shorten it.
    property int keyboardAskLimit: 30000
    property double _askedSince: 0

    /// Map the window on the ask now open, where the user is (C35 W1, W7).
    function openAsk(): void {
        if (!root.ask || !root.ask.open)
            return;
        sheet.open();
        // V17: nothing that ↵ would press holds the keyboard at first; the sheet's own keys
        // (esc, tab) still reach it from its panel.
        sheet.panel.forceActiveFocus();
        for (let pass = 0; pass < 3; ++pass) {
            const height = root.sheetSize.height;
            root._polish(sheet);
            if (pass > 0 && root.sheetSize.height === height)
                break;
        }
        if (root.visible && !root.active)
            root.hide();
        root.show();
        root.requestActivate();
        root._askedSince = Date.now();
        activateAgain.restart();
    }

    /// The allow button.
    function allow(): void {
        if (root.ask && root.ask.open)
            root.ask.allow([]);
        root._close();
    }

    /// refuse, esc and the window's close.
    function refuse(): void {
        if (root.ask && root.ask.open)
            root.ask.refuse();
        root._close();
    }

    function _close(): void {
        activateAgain.stop();
        sheet.close();
        root.hide();
    }

    function _polish(item: Item): void {
        const children = item.children;
        for (let i = 0; i < children.length; ++i)
            root._polish(children[i]);
        item.ensurePolished();
    }

    /// `s` as StyledText's plain words.
    function _escape(s: string): string {
        return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    /// The board's `code`: a program's name in the foreground, among dim words.
    function _code(s: string): string {
        return "<font color='%1'>%2</font>".arg(Theme.foreground).arg(root._escape(s));
    }

    /// "Asked by omaseed do, started from claude (process 48213), at 15:19." (docs/62 §5).
    readonly property string whoText: {
        const r = root.request;
        if (!r.word)
            return "";
        if (r.parent)
            return qsTr("Asked by %1, started from %2 (process %3), at %4.").arg(root._code(
                                                                                     r.word)).arg(
                        root._code(r.parent)).arg(r.parentPid).arg(r.at);
        if (r.wordPid > 0)
            return qsTr(
                        "Asked by %1 (process %2), at %3. omaseed couldn't read what started it.").arg(
                        root._code(r.word)).arg(r.wordPid).arg(r.at);
        return qsTr("Asked by %1, at %2. omaseed couldn't read what started it.").arg(root._code(
                                                                                          r.word)).arg(
                    r.at);
    }

    title: "omaseed · agent"
    transientParent: null
    color: Theme.background
    width: root.sheetSize.width
    height: root.sheetSize.height
    minimumWidth: root.sheetSize.width
    maximumWidth: root.sheetSize.width
    minimumHeight: root.sheetSize.height
    maximumHeight: root.sheetSize.height

    // V17: super+w refuses. The window hides itself rather than close, so a close never counts
    // as the app's last window.
    onClosing: close => {
        close.accepted = false;
        root.refuse();
    }

    Timer {
        id: activateAgain

        interval: 100
        repeat: true

        onTriggered: {
            if (!root.visible || root.active || Date.now() - root._askedSince
                    >= root.keyboardAskLimit) {
                activateAgain.stop();
                return;
            }
            root.requestActivate();
        }
    }

    // Two minutes without an answer (AgentAsk's timer), or an answer from elsewhere: the ask
    // closed, the window goes.
    Connections {
        function onOpenChanged(): void {
            if (!root.ask.open)
                root._close();
        }

        target: root.ask
    }

    // The sheet reaches the margin past every side, so its panel is the whole window.
    Sheet {
        id: sheet

        actions: [
            {
                "text": qsTr("refuse"),
                "cancel": true
            },
            {
                "text": root.request.allowText ?? qsTr("allow"),
                "primary": true
            }
        ]
        framed: false
        height: root.height + 2 * Theme.sheetMargin
        // V17: no key allows. The sheet's ↵ would press `allow` from anywhere; here only a
        // button with the cursor takes it, and it does so itself.
        keyHandler: event => {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                event.accepted = true;
        }
        // The board's foot: the only keys there are (docs/62 §5).
        note: qsTr("tab · ↵ on a button · esc refuses")
        sheetWidth: Theme.space(460)
        subtitle: "omaseed · agent"
        title: root.request.head ?? ""
        titleId: "agentAsk"
        width: root.width + 2 * Theme.sheetMargin
        x: -Theme.sheetMargin
        y: -Theme.sheetMargin

        onAccepted: root.allow()
        onRejected: root.refuse()

        // What, on which torrents (or the whole server), on a 4 % block.
        Item {
            Layout.fillWidth: true
            implicitHeight: namesColumn.implicitHeight + 2 * Theme.space(8)
            objectName: "askNames"

            Plate {
                anchors.fill: parent
                edgeWidth: 0
                fill: Theme.on.idle.fill
            }

            ColumnLayout {
                id: namesColumn

                anchors.left: parent.left
                anchors.leftMargin: Theme.space(10)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(10)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space(2)

                Repeater {
                    model: root.request.server ? [root.request.server] : (root.request.torrents
                                                                          ?? [])

                    ColumnLayout {
                        id: nameRow

                        required property var modelData

                        Layout.fillWidth: true
                        spacing: 0
                        Accessible.name: nameRow.modelData.name + ", " + nameRow.modelData.line
                        Accessible.role: Accessible.StaticText

                        Label {
                            Layout.fillWidth: true
                            bright: true
                            elide: Text.ElideRight
                            ink: Ink {
                                animated: false
                                ground: Theme.on.idle
                            }
                            objectName: "askName"
                            size: "small"
                            text: nameRow.modelData.name
                            voice: "name"
                        }

                        Label {
                            Layout.fillWidth: true
                            dim: true
                            elide: Text.ElideMiddle
                            ink: Ink {
                                animated: false
                                ground: Theme.on.idle
                            }
                            size: "caption"
                            text: nameRow.modelData.line
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    dim: true
                    ink: Ink {
                        animated: false
                        ground: Theme.on.idle
                    }
                    objectName: "askMore"
                    size: "caption"
                    text: root.request.more ?? ""
                    visible: text.length > 0
                }
            }
        }

        Label {
            Layout.fillWidth: true
            dim: true
            objectName: "askWho"
            size: "caption"
            text: root.whoText
            textFormat: Text.StyledText
            wrapMode: Text.Wrap
        }

        // The yellow line (docs/62 §5 `.warnl`): the warning tint, its words lifted against it
        // (DECISIONS C30 F5).
        Rectangle {
            Layout.fillWidth: true
            color: Theme.on.warning.fill
            implicitHeight: warnText.implicitHeight + Theme.space(12)

            Text {
                id: warnText

                anchors.left: parent.left
                anchors.leftMargin: Theme.space(10)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(10)
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.on.warning.yellow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.caption
                objectName: "askWarning"
                text: qsTr(
                          "omaseed can't tell who is behind a command. Allow it only if you asked for this.")
                wrapMode: Text.Wrap
            }
        }
    }

    // The 1 px window edge, as Main.qml draws it: only without Hyprland, whose border is the
    // edge when it is there.
    Rectangle {
        anchors.fill: parent
        border.color: root.active ? Theme.accent : Theme.muted
        border.width: Theme.edgeWidth
        color: "transparent"
        radius: Theme.cornerRadius
        visible: Theme.windowEdge
        z: 3
    }
}
