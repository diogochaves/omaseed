pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// docs/24 §5, "Importing torrents" (#33): what a torrents export would add to the server,
// before anything is. `App.readTorrentsImport` reads the file and the server and describes the
// plan; this sheet keeps the save path rows and the "then" choice and hands them to
// `App.applyTorrentsImport`.
//
// The plan: new (added), already here by the same hash (skipped, D8 a), magnets without
// metadata (added as links), and those an earlier run left unfinished. Under "save paths" the
// "from → to" rows (D7 a), prefilled from the path maps omaseed has, "+ another" for more, and
// a "!" line counting the paths no row covers. Under "then", D6 a's choice: check the files and
// start the complete ones, or leave them stopped. A "+" line says which categories and tags are
// created first.
//
//     TorrentsImportSheet { anchors.fill: parent; onApplied: … }
//
// Do NOT run `qmlformat` on this file: with the project's 100-column wrap it re-indents the
// typed functions further on every pass (the defect Menu.qml notes). It is hand-formatted.
Sheet {
    id: sheet

    /// `App.readTorrentsImport`'s map, through `torrentsImportRead`.
    property var info: ({})
    /// The "from → to" rows: [{from, to, source}]. Edited in place; `revision` says so.
    property var rows: []
    property int revision: 0
    /// D6 a: check the files and start the complete ones; false leaves them all stopped.
    property bool check: true
    /// The "!" line for some rows (App's count of the save paths none of them covers). A test
    /// can put its own here.
    property var notesFor: list => App.torrentsImportPathsNote(list)

    /// The import was sent; App says how it went with `torrentsImportFinished`.
    signal applied

    readonly property var plan: sheet.info.plan ?? []
    readonly property int importCount: sheet.info.importCount ?? 0
    readonly property string importText: sheet.importCount > 0 ? qsTr("import %1").arg(sheet.importCount)
                                                               : qsTr("nothing to import")
    readonly property string pathsNote: sheet.revision >= 0 ? sheet.notesFor(sheet.rows) : ""
    /// "from home's path map", for the first row a path map made.
    readonly property string sourceText: {
        for (const row of sheet.rows) {
            if ((row.source ?? "").length > 0)
                return qsTr("from %1's path map").arg(row.source);
        }
        return "";
    }

    function openFor(map: var): void {
        sheet.info = map;
        // Emptied first, so a sheet opened again builds its rows afresh: a field the person
        // typed into no longer follows its row's value.
        sheet.rows = [];
        sheet.rows = (map.paths ?? []).map(p => ({ "from": p.from ?? "", "to": p.to ?? "", "source": p.source ?? "" }));
        sheet.check = map.check !== false;
        sheet.revision++;
        sheet.open();
    }

    function setRow(index: int, key: string, text: string): void {
        if (index < 0 || index >= sheet.rows.length || sheet.rows[index][key] === text)
            return;
        sheet.rows[index][key] = text;
        sheet.revision++;
    }

    /// "+ another": an empty row, its "from" field focused.
    function addRow(): void {
        const next = sheet.rows.slice();
        next.push({ "from": "", "to": "", "source": "" });
        sheet.rows = next;
        sheet.revision++;
        Qt.callLater(() => {
            const field = sheet.fieldAt(sheet.rows.length - 1, "from");
            if (field)
                field.focusInput();
        });
    }

    function fieldAt(index: int, key: string): var {
        const row = pathRepeater.itemAt(index);
        return row ? (key === "from" ? row.fromField : row.toField) : null;
    }

    function choice(): var {
        return {
            "rows": sheet.rows.filter(r => r.from.trim().length > 0 && r.to.trim().length > 0)
                              .map(r => ({ "from": r.from.trim(), "to": r.to.trim() })),
            "check": sheet.check
        };
    }

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": sheet.importText,
            "primary": true,
            "disabled": sheet.importCount === 0
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
            "label": qsTr("pick")
        }
    ]
    sheetWidth: Theme.space(640)
    subtitle: sheet.info.sub ?? ""
    subtitleWraps: true
    title: sheet.info.title ?? ""

    onRejected: sheet.close()
    onActionTriggered: text => {
        if (text !== sheet.importText || sheet.importCount === 0)
            return;
        const picked = sheet.choice();
        sheet.close();
        App.applyTorrentsImport(picked);
        sheet.applied();
    }

    // ---- the plan, as the mock's table has it -----------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(12)

        Label {
            Layout.fillWidth: true
            text: qsTr("torrents")
            tracked: true
        }

        Label {
            Layout.preferredWidth: Theme.space(70)
            horizontalAlignment: Text.AlignRight
            text: qsTr("count")
            tracked: true
        }

        Label {
            Layout.preferredWidth: Theme.space(80)
            horizontalAlignment: Text.AlignRight
            text: qsTr("size")
            tracked: true
        }
    }

    Repeater {
        model: sheet.plan

        delegate: RowLayout {
            id: planRow

            required property var modelData

            // #79: one label per row of the plan ("new"), what happens, how many and how much
            // the description; its four texts publish nothing themselves.
            Accessible.description: [planRow.modelData.verb ?? "", String(planRow.modelData.count ?? ""),
                                     planRow.modelData.sizeText ?? ""].filter(part => part.length > 0).join(" · ")
            Accessible.name: planRow.modelData.label ?? ""
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            objectName: "planRow-" + planRow.modelData.key
            spacing: Theme.space(12)

            Label {
                Layout.fillWidth: true
                bright: planRow.modelData.dim !== true
                dim: planRow.modelData.dim === true
                elide: Text.ElideRight
                objectName: "planLabel"
                text: planRow.modelData.label
            }

            Label {
                dim: true
                objectName: "planVerb"
                size: "small"
                text: planRow.modelData.verb
            }

            Label {
                Layout.preferredWidth: Theme.space(70)
                bright: planRow.modelData.dim !== true
                dim: planRow.modelData.dim === true
                horizontalAlignment: Text.AlignRight
                objectName: "planCount"
                text: String(planRow.modelData.count)
            }

            Label {
                Layout.preferredWidth: Theme.space(80)
                dim: true
                horizontalAlignment: Text.AlignRight
                objectName: "planSize"
                text: planRow.modelData.sizeText
            }
        }
    }

    // ---- save paths (D7 a) --------------------------------------------------------------------
    Label {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space(6)
        text: qsTr("save paths")
        tracked: true
    }

    Repeater {
        id: pathRepeater

        model: sheet.rows

        delegate: RowLayout {
            id: pathRow

            required property var modelData
            required property int index
            readonly property alias fromField: fromField
            readonly property alias toField: toField

            Layout.fillWidth: true
            objectName: "pathRow-" + pathRow.index
            spacing: Theme.space(8)

            Field {
                id: fromField

                Layout.fillWidth: true
                escClears: false
                objectName: "pathFrom"
                placeholder: qsTr("from, as the file has it")
                text: pathRow.modelData.from

                onAccepted: sheet.triggerPrimary()
                onTextChanged: sheet.setRow(pathRow.index, "from", text)
            }

            Label {
                dim: true
                text: "→"
            }

            Field {
                id: toField

                Layout.fillWidth: true
                escClears: false
                objectName: "pathTo"
                placeholder: qsTr("to, on %1").arg(App.serverName)
                text: pathRow.modelData.to

                onAccepted: sheet.triggerPrimary()
                onTextChanged: sheet.setRow(pathRow.index, "to", text)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(8)

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            dim: true
            objectName: "pathSource"
            size: "small"
            text: sheet.sourceText
            visible: sheet.sourceText.length > 0
        }

        Label {
            dim: true
            size: "small"
            text: "·"
            visible: sheet.sourceText.length > 0
        }

        Button {
            fontSize: Theme.font.small
            objectName: "pathAdd"
            text: qsTr("+ another")

            onClicked: sheet.addRow()
        }

        Item {
            Layout.fillWidth: true
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(8)
        visible: sheet.pathsNote.length > 0

        Label {
            Layout.alignment: Qt.AlignTop
            color: Theme.yellow
            size: "small"
            text: "!"
        }

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            dim: true
            objectName: "pathsNote"
            size: "small"
            text: sheet.pathsNote
            wrapMode: Text.WordWrap
        }
    }

    // ---- then (D6 a) --------------------------------------------------------------------------
    Label {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space(6)
        text: qsTr("then")
        tracked: true
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(8)

        Chip {
            focusable: true
            objectName: "thenCheck"
            selected: sheet.check
            text: qsTr("check files, start the complete ones")

            onClicked: sheet.check = true
        }

        Chip {
            focusable: true
            objectName: "thenStopped"
            selected: !sheet.check
            text: qsTr("leave them stopped")

            onClicked: sheet.check = false
        }

        Item {
            Layout.fillWidth: true
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(8)
        visible: (sheet.info.created ?? "").length > 0

        Label {
            Layout.alignment: Qt.AlignTop
            color: Theme.accent
            size: "small"
            text: "+"
        }

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            dim: true
            objectName: "importCreated"
            size: "small"
            text: sheet.info.created ?? ""
            wrapMode: Text.WordWrap
        }
    }
}
