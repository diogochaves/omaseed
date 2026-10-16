pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// docs/24 §5, "Restoring omaseed" (#31): what a backup holds, as parts to tick, before
// anything changes. `App.readBackup` reads the file and describes the parts; this sheet keeps
// the ticks and hands them to `App.restoreBackup`, which writes a copy of what is here first.
// A part may arrive unticked and disabled: this machine's history for a server is longer than
// the backup's, so it stays (D4 a). A history part whose server is new here needs the servers
// part ticked too.
//
//     RestoreSheet { anchors.fill: parent; onRestored: text => … }
Sheet {
    id: sheet

    /// `App.readBackup`'s map.
    property var info: ({})
    /// key -> ticked, for every part.
    property var checks: ({})

    /// The restore ran: what it did, or why it could not.
    signal restored(string text, bool ok)

    readonly property var parts: sheet.info.parts ?? []
    readonly property string _restoreText: sheet.count === 1 ? qsTr("restore 1 part") : qsTr("restore %1 parts").arg(sheet.count)
    /// The parts that would be restored as ticked now.
    readonly property int count: {
        let n = 0;
        for (const part of sheet.parts) {
            if (sheet.effective(part))
                n++;
        }
        return n;
    }

    function openFor(map: var): void {
    sheet.info = map;
    const ticks = {};
    for (const part of map.parts ?? [])
        ticks[part.key] = part.checked === true;
    sheet.checks = ticks;
    sheet.open();
}

    /// Whether `part` can be ticked: its own flag, and the servers part for a new server.
    function available(part: var): bool {
        if (part.enabled !== true)
            return false;
        return part.needsServers !== true || sheet.checks.servers === true;
    }

    function effective(part: var): bool {
        return sheet.available(part) && sheet.checks[part.key] === true;
    }

    function toggle(key: string): void {
        const next = Object.assign({}, sheet.checks);
        next[key] = !next[key];
        sheet.checks = next;
    }

    function _choice(): var {
        const history = [];
        for (const part of sheet.parts) {
            if (part.key.startsWith("history:") && sheet.effective(part))
                history.push(part.key.slice("history:".length));
        }
        return {
            "settings": sheet.checks.settings === true,
            "servers": sheet.checks.servers === true,
            "history": history
        };
    }

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": sheet._restoreText,
            "primary": true,
            "disabled": sheet.count === 0
        }
    ]
    glyph: Glyphs.backup
    hints: [
        {
            "key": "tab",
            "label": qsTr("move")
        },
        {
            "key": "space",
            "label": qsTr("tick")
        }
    ]
    sheetWidth: Theme.space(560)
    subtitle: sheet.info.sub ?? ""
    title: sheet.info.title ?? ""

    onRejected: sheet.close()
    onActionTriggered: text => {
        if (text !== sheet._restoreText || sheet.count === 0)
            return;
        const choice = sheet._choice();
        sheet.close();
        const result = App.restoreBackup(choice);
        sheet.restored(result.text, result.ok === true);
    }

    Repeater {
        model: sheet.parts

        delegate: Item {
            id: partRow

            required property var modelData
            readonly property bool available: sheet.available(partRow.modelData)
            readonly property bool ticked: sheet.effective(partRow.modelData)

            // #77: a check box named by the part, its detail the description; its toggle and
            // its press are the click (and `space`), and do nothing on a part the file lacks.
            Accessible.checkable: true
            Accessible.checked: partRow.ticked
            Accessible.description: [partRow.modelData.side ?? "", partRow.modelData.detail ?? ""]
                                    .filter(part => part.length > 0).join(" · ")
            Accessible.name: partRow.modelData.label
            Accessible.role: Accessible.CheckBox
            Accessible.onPressAction: if (partRow.enabled && partRow.available)
                                          sheet.toggle(partRow.modelData.key)
            Accessible.onToggleAction: if (partRow.enabled && partRow.available)
                                           sheet.toggle(partRow.modelData.key)
            Layout.fillWidth: true
            activeFocusOnTab: partRow.available
            implicitHeight: partColumn.implicitHeight + Theme.space(8)
            objectName: "restorePart-" + partRow.modelData.key
            opacity: partRow.available ? 1 : 0.55

            Keys.onSpacePressed: sheet.toggle(partRow.modelData.key)

            FocusRing {}

            MouseArea {
                anchors.fill: parent
                cursorShape: partRow.available ? Qt.PointingHandCursor : Qt.ArrowCursor
                enabled: partRow.available

                onClicked: sheet.toggle(partRow.modelData.key)
            }

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: Theme.space(4)
                spacing: Theme.space(10)

                // A tick box drawn from the theme: an outline, filled with a check when ticked.
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Theme.space(2)
                    border.color: partRow.ticked ? Theme.accent : Theme.fill(Theme.borderIdle)
                    border.width: 1
                    color: partRow.ticked ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                                                    Theme.fillPressed) : "transparent"
                    implicitHeight: Theme.space(14)
                    implicitWidth: Theme.space(14)

                    Label {
                        anchors.centerIn: parent
                        bright: true
                        size: "caption"
                        text: Glyphs.check
                        visible: partRow.ticked
                    }
                }

                ColumnLayout {
                    id: partColumn

                    Layout.fillWidth: true
                    spacing: Theme.space(2)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space(10)

                        Label {
                            Layout.fillWidth: true
                            bright: true
                            elide: Text.ElideRight
                            text: partRow.modelData.label
                        }

                        Label {
                            dim: true
                            size: "small"
                            text: partRow.modelData.side ?? ""
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        dim: true
                        size: "small"
                        text: partRow.modelData.detail ?? ""
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space(4)
        spacing: Theme.space(8)

        Label {
            Layout.alignment: Qt.AlignTop
            color: Theme.accent
            size: "small"
            text: Glyphs.check
        }

        Label {
            Layout.fillWidth: true
            dim: true
            objectName: "restoreSafety"
            size: "small"
            text: sheet.info.safety ?? ""
            wrapMode: Text.WordWrap
        }
    }
}
