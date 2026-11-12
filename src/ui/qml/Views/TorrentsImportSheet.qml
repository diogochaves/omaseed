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
// A file another engine wrote (docs/57 §5, docs/DECISIONS.md C40 X4, X5; #88; `info.crossing`)
// adds two blocks between "save paths" and "then": what each of its names becomes here (not
// editable; the first `namesShown`, then "and N more"), and what this server can't keep, one
// counted "!" line per thing or the one line saying everything has a place. On a Deluge whose
// Label plugin is off, a "!" line under the names says no label will be set
// (`info.namesWarning`, C40 X6), and the "+" line counts the labels made first.
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
    /// C40 X4, X5: the file is another engine's.
    readonly property bool crossing: sheet.info.crossing === true
    readonly property var names: sheet.info.names ?? []
    readonly property var losses: sheet.info.losses ?? []
    /// How many names the block lists before "and N more".
    readonly property int namesShown: 8
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
                text: Glyphs.arrowRight
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

    // ---- categories and tags (C40 X4) ---------------------------------------------------------
    Label {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space(6)
        objectName: "namesTitle"
        text: sheet.info.namesTitle ?? ""
        tracked: true
        visible: sheet.crossing && sheet.names.length > 0
    }

    Repeater {
        model: sheet.crossing ? sheet.names.slice(0, sheet.namesShown) : []

        delegate: RowLayout {
            id: nameRow

            required property var modelData
            required property int index

            // One label per name ("music label"), what it becomes the description.
            Accessible.description: nameRow.modelData.to ?? ""
            Accessible.name: (nameRow.modelData.name ?? "") + " " + (nameRow.modelData.kind ?? "")
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            objectName: "nameRow-" + nameRow.index
            spacing: Theme.space(8)

            Label {
                Layout.maximumWidth: Theme.space(260)
                bright: true
                elide: Text.ElideRight
                objectName: "nameFrom"
                text: nameRow.modelData.name ?? ""
            }

            Label {
                dim: true
                objectName: "nameKind"
                size: "small"
                text: nameRow.modelData.kind ?? ""
            }

            Label {
                dim: true
                text: Glyphs.arrowRight
            }

            Label {
                Layout.fillWidth: true
                bright: nameRow.modelData.left !== true
                dim: nameRow.modelData.left === true
                elide: Text.ElideRight
                objectName: "nameTo"
                text: nameRow.modelData.to ?? ""
            }
        }
    }

    Label {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        dim: true
        objectName: "namesMore"
        size: "small"
        text: qsTr("and %1 more").arg(sheet.names.length - sheet.namesShown)
        visible: sheet.crossing && sheet.names.length > sheet.namesShown
    }

    // C40 X6: a Deluge whose Label plugin is off sets no label; said before the import.
    RowLayout {
        Layout.fillWidth: true
        objectName: "namesWarningRow"
        spacing: Theme.space(8)
        visible: (sheet.info.namesWarning ?? "").length > 0

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
            objectName: "namesWarning"
            size: "small"
            text: sheet.info.namesWarning ?? ""
            wrapMode: Text.WordWrap
        }
    }

    // ---- can't be kept (C40 X5) ---------------------------------------------------------------
    Label {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space(6)
        objectName: "lostTitle"
        text: sheet.info.lostTitle ?? ""
        tracked: true
        visible: sheet.crossing
    }

    Repeater {
        model: sheet.crossing ? sheet.losses : []

        delegate: RowLayout {
            id: lossRow

            required property string modelData
            required property int index

            Layout.fillWidth: true
            objectName: "lossRow-" + lossRow.index
            spacing: Theme.space(8)

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
                objectName: "lossText"
                size: "small"
                text: lossRow.modelData
                wrapMode: Text.WordWrap
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(8)
        visible: sheet.crossing && sheet.losses.length === 0

        Label {
            Layout.alignment: Qt.AlignTop
            color: Theme.green
            size: "small"
            text: Glyphs.check
        }

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            dim: true
            objectName: "lossNone"
            size: "small"
            text: sheet.info.kept ?? ""
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
            checkable: true
            selected: sheet.check
            text: qsTr("check files, start the complete ones")

            onClicked: sheet.check = true
        }

        Chip {
            focusable: true
            objectName: "thenStopped"
            checkable: true
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
