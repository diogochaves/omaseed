pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// "Recheck the 2…" after a move (board 42 E3, DECISIONS C15 M16): the failed or stuck ones,
// rechecked one at a time, smallest first; each that comes out complete starts, if it was
// running when the move sent it (C20, #138: a stopped one stays stopped, and its row says so).
// "set location…" is today's sheet with those torrents.
// A hybrid (v1+v2) is never rechecked (DECISIONS C21, #144): it is listed below the others as
// not rechecked, with why, and offered "move again" (to the move's target, as retry) and "move
// back" (as move back), which bring its files together. With only hybrids the button says
// there is nothing to recheck.
Sheet {
    id: root

    property var hashes: []
    property var rows: []
    /// C21: what a hybrid's row sent, by hash: "again" or "back".
    property var sent: ({})
    readonly property var rechecked: root.rows.filter(row => row.hybrid !== true)
    readonly property var hybrids: root.rows.filter(row => row.hybrid === true)

    signal setLocationRequested(var hashes)

    function openFor(hashes: var): void {
        root.openWith(hashes, App.move.recheckRows(hashes));
    }

    /// The sheet on rows App.move.recheckRows() gave (a test gives its own).
    function openWith(hashes: var, rows: var): void {
        root.hashes = hashes;
        root.rows = rows;
        root.sent = ({});
        root.open();
        // `enter` rechecks, as before: the keyboard on the primary, not on a hybrid's "move
        // again", the body's first button. With only hybrids it stays there.
        const primary = root.primaryButton();
        if (primary && primary.enabled)
            primary.forceActiveFocus();
    }

    /// C21: a hybrid's "move again" or "move back", sent as its own move.
    function sendHybrid(hash: string, how: string): void {
        const started = how === "back" ? App.move.moveFailedBack(hash) : App.move.moveAgain(hash);
        if (!started)
            return;
        const sent = Object.assign({}, root.sent);
        sent[hash] = how;
        root.sent = sent;
    }

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": qsTr("move…")
        },
        {
            "text": root.rechecked.length > 0 ? qsTr("recheck %1").arg(root.rechecked.length)
                                              : qsTr("nothing to recheck"),
            "primary": true,
            "disabled": root.rechecked.length === 0
        }
    ]
    glyph: Glyphs.recheck
    sheetWidth: Theme.space(520)
    subtitle: root.rechecked.length > 0 ? qsTr("smallest first, one at a time")
                                        : root.hybrids.length === 1 ? qsTr("move it again or back instead")
                                                                    : qsTr("move them again or back instead")
    title: root.rechecked.length === 0 ? qsTr("Nothing to recheck")
                                       : root.rechecked.length === 1 ? qsTr("Recheck 1 torrent")
                                                                     : qsTr("Recheck %1 torrents").arg(
                                                                           root.rechecked.length)

    onAccepted: {
        if (root.rechecked.length === 0)
            return;
        // As the rows said when the sheet opened: a hybrid moved again since replaced the job.
        App.move.recheckListed(root.rechecked);
        root.close();
    }
    onActionTriggered: text => {
        if (text === qsTr("move…")) {
            root.close();
            root.setLocationRequested(root.hashes);
        }
    }

    Repeater {
        model: root.rechecked

        RowLayout {
            id: recheckRow

            required property var modelData

            Layout.fillWidth: true
            objectName: "recheckRow-" + recheckRow.modelData.hash
            spacing: Theme.space(10)
            // One line for a screen reader: the name, its size and what follows (C20, #138).
            Accessible.name: qsTr("%1, %2, %3").arg(recheckRow.modelData.name).arg(
                                 recheckRow.modelData.bytes).arg(recheckRow.modelData.then)
            Accessible.role: Accessible.StaticText

            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                size: "small"
                text: recheckRow.modelData.name
            }

            Label {
                Layout.preferredWidth: Theme.space(70)
                dim: true
                horizontalAlignment: Text.AlignRight
                size: "small"
                text: recheckRow.modelData.bytes
            }

            Label {
                Layout.preferredWidth: Theme.space(190)
                dim: true
                size: "small"
                text: recheckRow.modelData.then
            }
        }
    }

    Label {
        Layout.fillWidth: true
        dim: true
        size: "caption"
        text: qsTr(
                  "The ones running before the move start again once they come out complete. The others stay stopped and are listed.")
        visible: root.rechecked.length > 0
        wrapMode: Text.Wrap
    }

    // C21: the hybrids, not rechecked.
    Label {
        Layout.topMargin: Theme.space(6)
        text: qsTr("not rechecked")
        tracked: true
        visible: root.hybrids.length > 0
    }

    Repeater {
        model: root.hybrids

        ColumnLayout {
            id: hybridRow

            required property var modelData
            readonly property string sentHow: root.sent[hybridRow.modelData.hash] ?? ""

            Layout.fillWidth: true
            objectName: "hybridRow-" + hybridRow.modelData.hash
            spacing: Theme.space(2)

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space(10)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space(10)
                    // One line for a screen reader: the name, its size, what it is and why.
                    Accessible.name: qsTr("%1, %2, %3: %4").arg(hybridRow.modelData.name).arg(
                                         hybridRow.modelData.bytes).arg(
                                         hybridRow.sentHow === "back" ? qsTr("moving back")
                                                                      : hybridRow.sentHow === "again"
                                                                        ? qsTr("moving again")
                                                                        : hybridRow.modelData.then).arg(
                                         hybridRow.modelData.why)
                    Accessible.role: Accessible.StaticText

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        size: "small"
                        text: hybridRow.modelData.name
                    }

                    Label {
                        Layout.preferredWidth: Theme.space(70)
                        dim: true
                        horizontalAlignment: Text.AlignRight
                        size: "small"
                        text: hybridRow.modelData.bytes
                    }
                }

                Button {
                    Accessible.name: qsTr("move %1 again").arg(hybridRow.modelData.name)
                    fontSize: Theme.font.small
                    objectName: "moveAgain"
                    text: qsTr("move again")
                    visible: hybridRow.sentHow.length === 0

                    onClicked: root.sendHybrid(hybridRow.modelData.hash, "again")
                }

                Button {
                    Accessible.name: qsTr("move %1 back").arg(hybridRow.modelData.name)
                    fontSize: Theme.font.small
                    objectName: "moveBack"
                    text: qsTr("move back")
                    visible: hybridRow.sentHow.length === 0

                    onClicked: root.sendHybrid(hybridRow.modelData.hash, "back")
                }

                Label {
                    dim: true
                    objectName: "sent"
                    size: "small"
                    text: hybridRow.sentHow === "back" ? qsTr("moving back") : qsTr("moving again")
                    visible: hybridRow.sentHow.length > 0
                }
            }

            Label {
                Layout.fillWidth: true
                dim: true
                objectName: "why"
                size: "caption"
                text: hybridRow.modelData.why
                wrapMode: Text.Wrap
            }
        }
    }
}
