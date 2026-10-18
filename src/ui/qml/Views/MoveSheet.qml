pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// "Move a folder" (board 42 §3 S1–S3, DECISIONS C15 M4–M9, #97): one sheet. The field holds
// the folder's new path, prefilled with its current one; browse picks a parent and the sheet
// adds the folder's name (M4). Under it, every save path in the folder and where it goes,
// then the preflight, read again whenever the field changes, and the files no torrent owns as
// a dry run. The move button says how many torrents it sends; nothing is sent before it is
// pressed.
//
// The words are App.move.plan()'s. It fetches what it still needs (file lists, the walk for
// loose files) as it goes and moves `App.move.planRevision` when there is more: the sheet
// reads the plan again then.
//
// "show" on a line, or the foot's "show the N", filters the table under the sheet to those
// torrents (`showRequested`); the host brings the sheet back with `esc` (reopen()).
Sheet {
    id: root

    /// The folder being moved, as the server writes it.
    property string folder: ""
    /// The folder picker, as LocationSheet's: `App.pickDirectory`, which a test replaces.
    property var pickDirectory: start => App.pickDirectory(start)
    /// The tick of "carry the N files after the torrents" (M17), on by default when offered.
    property bool carry: true
    property bool _carryTouched: false
    /// App.move.plan() for the field as it stands.
    property var plan: ({})
    readonly property alias pathField: pathField

    /// "show": the table under the sheet lists these torrents.
    signal showRequested(var hashes)
    /// "list ▸": the loose files' list (S2).
    signal looseListRequested(string folder)

    function _readPlan(): void {
    if (root.folder.length === 0)
    return;
    root.plan = App.move.plan(root.folder, pathField.text.trim(), root.carry);
    if (!root._carryTouched)
    root.carry = root.plan.carryDefault === true;
}

    function _kindColor(kind: string): color {
        switch (kind) {
            case "ok":
            return Theme.green;
            case "warn":
                case "unknown":
                return Theme.yellow;
            case "bad":
            return Theme.red;
            case "loose":
                return Theme.accent;
            default:
            return Theme.dimForeground;
        }
        }

            /// Opens on `folder`, the field on `newPath` when given (J3's "move the rest…", T2's
            /// "change the folder…"), else on the folder's own path.
            function openFor(folder: string, newPath: var): void {
            root.folder = folder;
            root._carryTouched = false;
            root.carry = true;
            pathField.text = typeof newPath === "string" && newPath.length > 0 ? newPath : folder;
            root._readPlan();
            root.open();
            pathField.selectAll();
        }

            /// Back after "show", as it was left.
            function reopen(): void {
                                   root._readPlan();
                                   root.open();
                               }

            /// `browse` and `ctrl+o` (M4): the pick is the new parent; the folder's name goes after it.
            function browse(): void {
            const start = pathField.text.trim();
            const parentStart = start.lastIndexOf("/") > 0 ? start.substring(0, start.lastIndexOf("/")) :
                start;
            const picked = root.pickDirectory(App.browseStartFor(parentStart));
            if (!picked || picked.length === 0)
            return;
            const answer = App.serverPathForPick(picked);
            const name = root.folder.substring(root.folder.lastIndexOf("/") + 1);
            const parentPath = answer.path.replace(/\/+$/, "");
            pathField.text = (parentPath.length > 0 ? parentPath : "") + "/" + name;
        }

            actions: [
                {
                    "text": qsTr("cancel"),
                    "cancel": true
                },
                {
                    "text": qsTr("show the %1").arg(root.plan.moveCount ?? 0),
                    "key": "show",
                    "disabled": (root.plan.moveCount ?? 0) === 0
                },
                {
                    "text": root.plan.moveText ?? qsTr("move"),
                    "primary": true,
                    "disabled": root.plan.canMove !== true
                }
            ]
            glyph: "▱"
            hints: [
                {
                    "key": "enter",
                    "label": qsTr("move")
                },
                {
                    "key": "tab",
                    "label": qsTr("next")
                },
                {
                    "key": "esc",
                    "label": qsTr("cancel")
                }
            ]
            sheetWidth: Theme.space(640)
            subtitle: root.plan.header ?? ""
            title: root.plan.title ?? qsTr("Move a folder")

            onAccepted: {
                if (root.plan.canMove !== true)
                    return;
                if (App.move.start(root.folder, pathField.text.trim(), root.carry
                                   && root.plan.carryOffered === true))
                    root.close();
            }
            onActionTriggered: text => {
                if (text === qsTr("show the %1").arg(root.plan.moveCount ?? 0)) {
                    root.close();
                    root.showRequested(root.plan.moveHashes ?? []);
                }
            }

            // The field changes: the plan is read again, a moment after the typing stops.
            Timer {
                id: replan

                interval: 250

                onTriggered: root._readPlan()
            }

            Connections {
                function onPlanRevisionChanged(): void {
                if (root.opened)
                root._readPlan();
            }

                target: App.move
            }

            FormRow {
                Layout.fillWidth: true
                label: qsTr("new path of the folder")

                Field {
                    id: pathField

                    Layout.fillWidth: true
                    escClears: false
                    objectName: "movePathField"
                    trailing: qsTr("browse")
                    trailingClickable: true
                    trailingKey: "ctrl+o"

                    onAccepted: root.triggerPrimary()
                    onTextChanged: replan.restart()
                    onTrailingClicked: root.browse()

                    Keys.onPressed: event => {
                        if ((event.modifiers & Qt.ControlModifier) !== 0 && event.key
                                === Qt.Key_O) {

                            root.browse();
                            event.accepted = true;
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                dim: true
                objectName: "moveExplain"
                size: "small"
                text: root.plan.explain ?? ""
                visible: text.length > 0
                wrapMode: Text.Wrap
            }

            // Every save path in the folder, and where it goes (M4).
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space(2)
                visible: (root.plan.pathRows ?? []).length > 0

                Repeater {
                    model: root.plan.pathRows ?? []

                    RowLayout {
                        id: pathRow

                        required property var modelData

                        Layout.fillWidth: true
                        spacing: Theme.space(8)

                        Label {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            elide: Text.ElideLeft
                            size: "small"
                            text: pathRow.modelData.from
                        }

                        Label {
                            dim: true
                            size: "small"
                            text: "→"
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            bright: true
                            elide: Text.ElideLeft
                            size: "small"
                            text: pathRow.modelData.to
                        }

                        Label {
                            Layout.preferredWidth: Theme.space(40)
                            dim: true
                            horizontalAlignment: Text.AlignRight
                            size: "small"
                            text: String(pathRow.modelData.count)
                        }
                    }
                }
            }

            // A red line about the whole job turns the move off and says why (S1).
            Label {
                Layout.fillWidth: true
                color: Theme.red
                objectName: "moveBlocker"
                size: "small"
                text: root.plan.blocker ?? ""
                visible: text.length > 0
                wrapMode: Text.Wrap
            }

            Label {
                Layout.topMargin: Theme.space(6)
                text: qsTr("before anything moves")
                tracked: true
                visible: (root.plan.checks ?? []).length > 0
            }

            Repeater {
                model: root.plan.checks ?? []

                MoveCheckLine {
                    required property var modelData

                    Layout.fillWidth: true
                    line: modelData
                    tone: root._kindColor(modelData.kind)

                    onShowRequested: hashes => {
                        root.close();
                        root.showRequested(hashes);
                    }
                }
            }

            Label {
                Layout.topMargin: Theme.space(6)
                text: qsTr("files no torrent owns · a dry run")
                tracked: true
            }

            Repeater {
                model: (root.plan.loose ?? {}).rows ?? []

                MoveCheckLine {
                    required property var modelData

                    Layout.fillWidth: true
                    line: modelData
                    linkText: modelData.kind === "loose" || modelData.kind === "warn"
                              || modelData.kind === "unknown" ? qsTr("list ▸") : ""
                    tone: root._kindColor(modelData.kind)

                    onLinkActivated: root.looseListRequested(root.folder)
                }
            }

            Toggle {
                checked: root.carry
                objectName: "moveCarry"
                text: qsTr("carry the %1 files after the torrents").arg((root.plan.loose
                                                                         ?? {}).count ?? 0)
                visible: root.plan.carryOffered === true

                onToggled: {
                    root._carryTouched = true;
                    root.carry = checked;
                    root._readPlan();
                }
            }
        }
