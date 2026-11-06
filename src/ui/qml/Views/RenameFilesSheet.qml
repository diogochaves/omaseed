pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// Rename many files at once (#11, docs/18, approved as recommended): the Files tab's `r` with
// two or more files selected, or its menu's "rename files…" row. Find and replace over the
// files' names, plain text or regex, with a preview of every new name; App.planFileRenames()
// does the work (core/RenamePlan), App.renameTorrentFiles() runs it. The button says why it
// cannot run while names collide or the regex does not compile.
//
// The fields keep what was typed between openings: a second album usually takes the same
// pattern as the first.
//
// Do NOT run `qmlformat` on this file (typed functions; see Kit/Menu.qml): hand-formatted.
Sheet {
    id: root

    property string hash: ""
    /// The files to rename, in the Files tab's order.
    property var paths: []
    /// Whether `paths` is the selection (else every file the tab shows).
    property bool fromSelection: true
    property bool regex: false
    property bool includeExtension: false
    /// App.planFileRenames()'s answer for the fields as they are.
    property var plan: ({
            "rows": [],
            "error": "",
            "changed": 0,
            "collisions": 0,
            "invalid": 0,
            "canRun": false
        })

    readonly property alias findField: findField
    readonly property alias replaceField: replaceField
    readonly property alias everyToggle: everyToggle
    readonly property alias caseToggle: caseToggle
    readonly property alias preview: preview

    readonly property string primaryText: root.plan.error.length > 0 ? qsTr("fix the regex")
            : root.plan.collisions > 0 ? qsTr("%n names collide", "", root.plan.collisions)
            : root.plan.invalid > 0 ? qsTr("%n names not valid", "", root.plan.invalid)
            : root.plan.changed > 0 ? qsTr("rename %n", "", root.plan.changed)
            : qsTr("nothing changes")

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": root.primaryText,
            "primary": true,
            "disabled": !root.plan.canRun
        }
    ]
    glyph: Glyphs.rename
    sheetWidth: Theme.space(640)
    subtitle: {
        const row = App.torrentRow(root.hash);
        const name = row.name !== undefined ? row.name : "";
        return name + " · " + (root.fromSelection ? qsTr("the selected files") : qsTr("the shown files"));
    }
    title: qsTr("Rename %n files", "", root.paths.length)

    function openFor(hash: string, paths: var, fromSelection: bool): void {
        root.hash = hash;
        root.paths = paths;
        root.fromSelection = fromSelection;
        root.replan();
        root.open();
        findField.selectAll();
    }

    function options(): var {
        return {
            "find": findField.text,
            "replace": replaceField.text,
            "regex": root.regex,
            "everyMatch": everyToggle.checked,
            "matchCase": caseToggle.checked,
            "includeExtension": root.includeExtension
        };
    }

    function replan(): void {
        root.plan = App.planFileRenames(root.paths, root.options());
    }

    function toggleRegex(): void {
        root.regex = !root.regex;
        root.replan();
    }

    /// `name` as StyledText with the replaced parts (`marks`, [[start, length]…]) in `accent`,
    /// the row's on its ground (docs/DECISIONS.md C30 F1).
    function markedName(name: string, marks: var, accent: color): string {
        const escape = text => text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        let out = "";
        let at = 0;
        for (const mark of marks) {
            out += escape(name.slice(at, mark[0]));
            out += "<font color=\"" + accent + "\">" + escape(name.slice(mark[0], mark[0] + mark[1])) + "</font>";
            at = mark[0] + mark[1];
        }
        return out + escape(name.slice(at));
    }

    onAccepted: {
        if (!root.plan.canRun)
            return;
        App.renameTorrentFiles(root.hash, root.paths, root.options());
        // The selection is kept by path: the renamed files would drop out of it and leave the
        // ones that did not change selected on their own.
        if (root.fromSelection)
            App.details.files.clearSelection();
        root.close();
    }

    // Everything the keyboard lands on sits in here, so `ctrl+e` reaches it from any field,
    // chip or switch on its way up.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space(10)

        Keys.onPressed: event => {
            if (event.key === Qt.Key_E && (event.modifiers & Qt.ControlModifier)) {
                root.toggleRegex();
                event.accepted = true;
            }
        }

        FormRow {
            Layout.fillWidth: true
            label: qsTr("Find")

            Field {
                id: findField

                Layout.fillWidth: true
                escClears: false
                shortcuts: [{
                        "key": Qt.Key_E,
                        "modifiers": Qt.ControlModifier
                    }]
                placeholder: root.regex ? qsTr("a regular expression, (\\d+) - …") : qsTr("text in the names")

                onAccepted: root.triggerPrimary()
                onShortcutPressed: root.toggleRegex()
                onTextChanged: root.replan()
            }
        }

        FormRow {
            Layout.fillWidth: true
            label: qsTr("Replace with")

            Field {
                id: replaceField

                Layout.fillWidth: true
                escClears: false
                shortcuts: [{
                        "key": Qt.Key_E,
                        "modifiers": Qt.ControlModifier
                    }]
                placeholder: root.regex ? qsTr("$1 for a group, {n} for a number") : qsTr("{n} for a number")

                onAccepted: root.triggerPrimary()
                onShortcutPressed: root.toggleRegex()
                onTextChanged: root.replan()
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.space(8)

            Chip {
                focusable: true
                objectName: "textChip"
                checkable: true
                selected: !root.regex
                text: qsTr("text")

                onClicked: if (root.regex) root.toggleRegex()
            }

            Chip {
                focusable: true
                objectName: "regexChip"
                checkable: true
                selected: root.regex
                text: qsTr("regex")

                onClicked: if (!root.regex) root.toggleRegex()
            }

            Item {
                height: 1
                width: Theme.space(10)
            }

            Chip {
                focusable: true
                objectName: "nameChip"
                checkable: true
                selected: !root.includeExtension
                text: qsTr("name")

                onClicked: {
                    root.includeExtension = false;
                    root.replan();
                }
            }

            Chip {
                focusable: true
                objectName: "extensionChip"
                checkable: true
                selected: root.includeExtension
                text: qsTr("name and extension")

                onClicked: {
                    root.includeExtension = true;
                    root.replan();
                }
            }

            Item {
                height: 1
                width: Theme.space(10)
            }

            Toggle {
                id: everyToggle

                text: qsTr("every match")

                onToggled: root.replan()
            }

            Toggle {
                id: caseToggle

                text: qsTr("match case")

                onToggled: root.replan()
            }
        }
    }

    Hairline {
        Layout.fillWidth: true
    }

    Label {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        dim: true
        objectName: "previewHead"
        size: "caption"
        text: {
            const parts = [qsTr("preview"), qsTr("%1 of %2 change").arg(root.plan.changed + root.plan.collisions
                                                                        + root.plan.invalid).arg(root.paths.length)];
            if (root.plan.collisions > 0)
                parts.push(qsTr("%n collide", "", root.plan.collisions));
            if (root.plan.invalid > 0)
                parts.push(qsTr("%n not valid", "", root.plan.invalid));
            return parts.join(" · ");
        }
    }

    Label {
        Accessible.name: text
        Accessible.role: Accessible.AlertMessage
        Layout.fillWidth: true
        color: Theme.red
        objectName: "regexError"
        text: qsTr("regex: %1").arg(root.plan.error)
        visible: root.plan.error.length > 0
        wrapMode: Text.WordWrap
    }

    ListView {
        id: preview

        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(preview.contentHeight, Theme.rowHeight * 10)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        interactive: preview.contentHeight > preview.height
        model: root.plan.rows
        objectName: "renamePreview"
        visible: root.plan.error.length === 0

        delegate: Item {
            id: previewRow

            required property int index
            required property var modelData

            readonly property bool changed: previewRow.modelData.status !== "unchanged"
            readonly property bool bad: previewRow.modelData.status === "collides"
                                        || previewRow.modelData.status === "invalid"
            /// docs/DECISIONS.md C30 F1: every other row is striped with the 4 % fill, and its
            /// words are on it.
            readonly property Ink ink: Ink {
                animated: false
                ground: previewRow.index % 2 === 0 ? Theme.on.idle : Theme.on.window
            }

            height: Theme.rowHeight
            width: ListView.view.width

            Rectangle {
                anchors.fill: parent
                color: previewRow.ink.fill
                visible: previewRow.index % 2 === 0
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space(8)
                anchors.rightMargin: Theme.space(8)
                spacing: Theme.space(8)

                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    dim: true
                    elide: Text.ElideMiddle
                    ink: previewRow.ink
                    text: previewRow.modelData.name
                }

                Label {
                    Layout.preferredWidth: Theme.space(14)
                    dim: true
                    horizontalAlignment: Text.AlignHCenter
                    ink: previewRow.ink
                    text: previewRow.changed ? Glyphs.arrowRight : ""
                }

                Text {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    color: previewRow.bad ? previewRow.ink.red : previewRow.changed
                                                                 ? previewRow.ink.brightForeground
                                                                 : previewRow.ink.dimForeground
                    elide: Text.ElideMiddle
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.body
                    objectName: "newName"
                    text: !previewRow.changed ? qsTr("unchanged")
                            : previewRow.bad ? previewRow.modelData.newName
                                               + (previewRow.modelData.status === "collides" ? qsTr(" · collides")
                                                                                             : qsTr(" · not a valid name"))
                            : root.markedName(previewRow.modelData.newName, previewRow.modelData.marks,
                                              previewRow.ink.accent)
                    textFormat: previewRow.changed && !previewRow.bad ? Text.StyledText : Text.PlainText
                }
            }
        }
    }
}
