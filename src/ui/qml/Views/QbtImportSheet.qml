pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// docs/24 §5, "Importing settings" (#32): what a qBittorrent settings export would change on
// the server, before anything does. `App.readQbtSettings` reads the file and the server and
// describes the rows; this sheet keeps the ticks and hands them to `App.applyQbtImport`.
//
// Only differences are listed, in plain words: a key's title comes from the qBittorrent
// group's own rows (`schema`, QbtSettingsSections) and its values are printed the way those
// rows print them; the API key is in the row's tooltip. Categories and "tags · RSS" are added,
// never replaced. Under "this machine only" come the machine-specific keys (§6) and the
// category and rule folders, unticked. A line says the passwords were never exported, and one
// counts the keys this qBittorrent lacks. "export current first" saves what the server has
// now without closing the sheet.
//
// docs/34 D2 a, D6 a (#70): the head, the columns and that button name both servers ("From
// private's export of …", "public now" / "from private", "export public first"), all from
// App's map. The same sheet shows a copy from another server ("Copy settings from private into
// public", glyph ⇣), whose rows are read the same way.
//
//     QbtImportSheet { anchors.fill: parent; schema: qbtSchema; onExportRequested: … }
//
// Do NOT run `qmlformat` on this file: with the project's 100-column wrap it re-indents the
// typed functions further on every pass (the defect Menu.qml notes). It is hand-formatted.
Sheet {
    id: sheet

    /// `App.readQbtSettings`'s map, through `qbtImportRead`.
    property var info: ({})
    /// The qBittorrent group's sections (QbtSettingsSections), for titles and value words.
    property var schema: null
    /// key -> ticked, for every row.
    property var checks: ({})
    /// What "export current first" did, under the rows.
    property string note: ""
    property bool noteError: false

    /// "export current first": the modal picks a file and exports; the sheet stays.
    signal exportRequested
    /// The import was sent; App says how it went with `qbtImportFinished`.
    signal applied

    readonly property var rows: sheet.info.rows ?? []
    readonly property var mainRows: sheet.rows.filter(row => (row.group ?? "").length === 0)
    readonly property var machineRows: sheet.rows.filter(row => (row.group ?? "").length > 0)
    /// docs/34 D2 a: "export public first", naming the server the sheet writes to.
    readonly property string exportText: sheet.info.exportText ?? qsTr("export current first")
    readonly property string applyText: sheet.count === 1 ? qsTr("apply 1 change")
                                                           : qsTr("apply %1 changes").arg(sheet.count)
    /// The ticked rows that change something (the folders row only says how).
    readonly property int count: {
        let n = 0;
        for (const row of sheet.rows) {
            if (row.kind !== "folders" && sheet.checks[row.key] === true)
                n++;
        }
        return n;
    }

    /// Every `app/preferences` key the group's rows name -> {title, field, choice}.
    readonly property var meta: {
        const out = {};
        for (const section of (sheet.schema ? sheet.schema.sections : [])) {
            for (const spec of section.rows) {
                if (spec.title === undefined)
                    continue;
                const add = (key, extra) => {
                    if (key && out[key] === undefined)
                        out[key] = Object.assign({ "title": spec.title }, extra);
                };
                add(spec.toggle, { "toggle": true });
                for (const field of (spec.fields ?? []))
                    add(field.key, { "field": field });
                if (spec.choice)
                    add(spec.choice.key, { "options": spec.choice.options });
                if (spec.pick)
                    add(spec.pick.key, { "options": spec.pick.options ?? [] });
                if (spec.list)
                    add(spec.list.key, { "list": spec.list });
            }
        }
        return out;
    }

    function openFor(map: var): void {
        sheet.info = map;
        const ticks = {};
        for (const row of map.rows ?? [])
            ticks[row.key] = row.checked === true;
        sheet.checks = ticks;
        sheet.note = "";
        sheet.noteError = false;
        sheet.open();
    }

    function toggle(key: string): void {
        const next = Object.assign({}, sheet.checks);
        next[key] = !next[key];
        sheet.checks = next;
    }

    /// Keys whose row's title says too little out of its section ("Folder" under the log file,
    /// "Address, port" for two keys), or that share one with another key.
    readonly property var words: ({
            "file_log_path": qsTr("log file folder"),
            "temp_path": qsTr("incomplete files folder"),
            "export_dir": qsTr("copy .torrent files to"),
            "export_dir_fin": qsTr("copy finished .torrent files to"),
            "mail_notification_ssl_enabled": qsTr("email over SSL"),
            "mail_notification_encryption_type": qsTr("email encryption"),
            "web_ui_address": qsTr("Web UI address"),
            "web_ui_port": qsTr("Web UI port"),
            "web_ui_username": qsTr("Web UI user"),
            "max_ratio_enabled": qsTr("share ratio limit on"),
            "max_seeding_time_enabled": qsTr("seeding time limit on"),
            "max_inactive_seeding_time_enabled": qsTr("inactive seeding time limit on")
        })

    /// A row's words: the group's own title for the key, else the key in plain letters.
    function labelFor(row: var): string {
        if (row.label !== undefined)
            return row.label;
        if (sheet.words[row.key] !== undefined)
            return sheet.words[row.key];
        const known = sheet.meta[row.key];
        if (!known)
            return row.key.replace(/_/g, " ");
        const title = known.title;
        // "Upload slots" reads "upload slots"; "SSL port" and "DHT" keep their capitals.
        return title.length > 1 && title.charAt(1) === title.charAt(1).toLowerCase()
               ? title.charAt(0).toLowerCase() + title.slice(1) : title;
    }

    /// A value the way the key's row prints it: on/off, a choice's word, a limit in its unit,
    /// "none" for nothing set.
    function valueText(key: string, value: var): string {
        if (value === undefined || value === null)
            return "—";
        if (typeof value === "boolean")
            return value ? qsTr("on") : qsTr("off");
        const known = sheet.meta[key];
        if (known && known.options) {
            for (const option of known.options) {
                if (String(option[0]) === String(value)) // 1 and "1" alike
                    return String(option[1]);
            }
        }
        if (typeof value === "object") {
            const count = Array.isArray(value) ? value.length : Object.keys(value).length;
            return count === 0 ? qsTr("none") : count === 1 ? qsTr("1 entry") : qsTr("%1 entries").arg(count);
        }
        if (typeof value === "string")
            return value.length === 0 ? qsTr("none") : value;
        const field = known ? known.field : undefined;
        if (field && field.none !== undefined && value === field.none)
            return field.noneText ?? qsTr("none");
        if (field && field.scale)
            return (Math.round(value / field.scale * 100) / 100) + (field.unit ? " " + field.unit : "");
        return String(value) + (field && field.unit ? " " + field.unit : "");
    }

    function cellText(row: var, which: string): string {
        return row.kind === "pref" ? sheet.valueText(row.key, row[which]) : String(row[which] ?? "");
    }

    function choice(): var {
        const keys = [];
        for (const row of sheet.rows) {
            if (row.kind === "pref" && sheet.checks[row.key] === true)
                keys.push(row.key);
        }
        return {
            "keys": keys,
            "categories": sheet.checks["@categories"] === true,
            "tagsRss": sheet.checks["@tagsRss"] === true,
            "folders": sheet.checks["@folders"] === true
        };
    }

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": sheet.exportText
        },
        {
            "text": sheet.applyText,
            "primary": true,
            "disabled": sheet.count === 0
        }
    ]
    glyph: sheet.info.copy === true ? Glyphs.copyFrom : Glyphs.backup
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
    sheetWidth: Theme.space(680)
    subtitle: sheet.info.sub ?? ""
    subtitleLines: 3
    subtitleWraps: true
    title: sheet.info.title ?? ""

    onRejected: sheet.close()
    onActionTriggered: text => {
        if (text === sheet.exportText) {
            sheet.exportRequested();
            return;
        }
        if (text !== sheet.applyText || sheet.count === 0)
            return;
        const picked = sheet.choice();
        sheet.close();
        App.applyQbtImport(picked);
        sheet.applied();
    }

    // The columns' heads, as the mock's table has them.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(10)
        visible: sheet.rows.length > 0

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space(24)
            text: qsTr("setting")
            tracked: true
        }

        // docs/34 D2 a: "public now" and "from private" (or, copied, "private").
        Label {
            Layout.preferredWidth: Theme.space(150)
            elide: Text.ElideRight
            objectName: "importNowHead"
            text: sheet.info.nowHead ?? qsTr("now")
            tracked: true
        }

        Label {
            Layout.preferredWidth: Theme.space(150)
            elide: Text.ElideRight
            objectName: "importFromHead"
            text: sheet.info.fromHead ?? qsTr("from the file")
            tracked: true
        }
    }

    Label {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        dim: true
        objectName: "importNothing"
        size: "small"
        text: qsTr("Nothing to change: the server already has every setting in the file.")
        visible: sheet.rows.length === 0
        wrapMode: Text.WordWrap
    }

    Repeater {
        model: sheet.mainRows
        delegate: importRowComponent
    }

    Label {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        Layout.topMargin: Theme.space(6)
        objectName: "importMachineHead"
        text: qsTr("this machine only · left out unless ticked")
        tracked: true
        visible: sheet.machineRows.length > 0
    }

    Repeater {
        model: sheet.machineRows
        delegate: importRowComponent
    }

    // One row: a tick box, the setting in words, what the server has and what the file has.
    Component {
        id: importRowComponent

        Item {
            id: importRow

            required property var modelData
            readonly property bool ticked: sheet.checks[importRow.modelData.key] === true

            // #77: a check box named by the setting, what the server has and what the file has
            // its description; its toggle and its press are the click (and `space`).
            Accessible.checkable: true
            Accessible.checked: importRow.ticked
            Accessible.description: qsTr("now %1 · from the file %2").arg(sheet.cellText(importRow.modelData, "now"))
                                                                        .arg(sheet.cellText(importRow.modelData, "from"))
            // a11y-ignore A6: the setting's words, fixed for the row
            Accessible.name: sheet.labelFor(importRow.modelData)
            Accessible.role: Accessible.CheckBox
            Accessible.onPressAction: if (importRow.enabled)
                                          sheet.toggle(importRow.modelData.key)
            Accessible.onToggleAction: if (importRow.enabled)
                                           sheet.toggle(importRow.modelData.key)
            Layout.fillWidth: true
            activeFocusOnTab: true
            implicitHeight: rowLayout.implicitHeight + Theme.space(6)
            objectName: "importRow-" + importRow.modelData.key

            Keys.onSpacePressed: sheet.toggle(importRow.modelData.key)

            FocusRing {}

            HoverHandler {
                id: rowHover

                onHoveredChanged: {
                    if (!rowHover.hovered) {
                        rowTip.hide();
                        return;
                    }
                    const row = importRow.modelData;
                    rowTip.showFor(importRow, row.kind === "pref"
                                   ? row.key + " · " + sheet.cellText(row, "now") + " → " + sheet.cellText(row, "from")
                                   : sheet.cellText(row, "from"));
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor

                onClicked: sheet.toggle(importRow.modelData.key)
            }

            RowLayout {
                id: rowLayout

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space(10)

                // A tick box drawn from the theme, as the restore sheet's.
                Rectangle {
                    border.color: importRow.ticked ? Theme.accent : Theme.fill(Theme.borderIdle)
                    border.width: 1
                    // The ticked tint, its ✓ lifted against it (docs/DECISIONS.md C30 F5).
                    color: importRow.ticked ? Theme.on.ticked.fill : "transparent"
                    implicitHeight: Theme.space(14)
                    implicitWidth: Theme.space(14)

                    Label {
                        anchors.centerIn: parent
                        color: Theme.on.ticked.brightForeground
                        size: "caption"
                        text: Glyphs.check
                        visible: importRow.ticked
                    }
                }

                Label {
                    Layout.fillWidth: true
                    bright: importRow.ticked
                    elide: Text.ElideRight
                    objectName: "importLabel"
                    text: sheet.labelFor(importRow.modelData)
                }

                Label {
                    Layout.preferredWidth: Theme.space(150)
                    dim: true
                    // A path keeps both ends; a list of names reads from its start.
                    elide: importRow.modelData.kind === "pref" ? Text.ElideMiddle : Text.ElideRight
                    objectName: "importNow"
                    text: sheet.cellText(importRow.modelData, "now")
                }

                Label {
                    Layout.preferredWidth: Theme.space(150)
                    bright: importRow.ticked
                    // A path keeps both ends; a list of names reads from its start.
                    elide: importRow.modelData.kind === "pref" ? Text.ElideMiddle : Text.ElideRight
                    objectName: "importFrom"
                    text: sheet.cellText(importRow.modelData, "from")
                }
            }
        }
    }

    // What is never in the file, and what this qBittorrent could not take.
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space(4)
        spacing: Theme.space(8)

        Label {
            Layout.alignment: Qt.AlignTop
            color: Theme.accent
            size: "small"
            text: Glyphs.about
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(2)

            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                dim: true
                objectName: "importSecrets"
                size: "small"
                text: sheet.info.secrets ?? ""
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: sheet.info.notes ?? []

                Label {
                    required property string modelData

                    Accessible.name: text
                    Accessible.role: Accessible.StaticText
                    Layout.fillWidth: true
                    dim: true
                    objectName: "importNote"
                    size: "small"
                    text: modelData
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    Label {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        color: sheet.noteError ? Theme.yellow : Theme.green
        objectName: "importExportNote"
        size: "small"
        text: sheet.note
        visible: sheet.note.length > 0
        wrapMode: Text.WordWrap
    }

    Tooltip {
        id: rowTip

        objectName: "importTip"
        parent: sheet.Window.window ? sheet.Window.window.contentItem : sheet
        z: 1000
    }
}
