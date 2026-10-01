pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// One row of the Settings modal's qBittorrent group (docs/16 §4): a `SettingsRow` whose
// controls come from a row of `QbtSettingsSections` (`spec`) and whose values are the
// server's (`prefs`). It never writes anything itself: every change goes out through
// `changeRequested`, so the modal can ask first (docs/16 §3) before it reaches the server.
//
// A field shows the server's value, applies on enter or when the caret leaves it, and `esc`
// puts the server's value back. A value it cannot take (out of range, not a number) never
// leaves the row: `noteRequested` says why and the field goes back.
//
// Do NOT run `qmlformat` on this file: with the project's 100-column wrap it re-indents the
// typed functions further on every pass (the defect Menu.qml notes). It is hand-formatted.
SettingsRow {
    id: row

    /// The row, as `QbtSettingsSections` describes it.
    property var spec: ({})
    property ServerPreferences prefs: null
    /// What the section asks before any change ("webui"), on top of the row's own `confirm`.
    property string sectionConfirm: ""

    /// A change for the server: `{key: value}`, with the confirm it needs ("" for none).
    signal changeRequested(var changes, string confirmKind, string what)
    /// The list sheet for `spec.list`.
    signal listRequested(var list, string what, string confirmKind)
    /// The menu for `spec.pick`, under `anchor`.
    signal pickRequested(var pick, Item anchor, string confirmKind)
    /// `spec.button`'s action.
    signal actionRequested(string action)
    /// A line for the pane's head: a value refused before it was sent.
    signal noteRequested(string text)
    /// A field gave the keyboard back (enter on its last field, or esc).
    signal fieldDone

    readonly property var values: row.prefs ? row.prefs.values : ({})
    readonly property bool loaded: row.prefs !== null && row.prefs.loaded
    readonly property string confirmKind: row.sectionConfirm.length > 0 ? row.sectionConfirm
                                                                          : (row.spec.confirm ?? "")
    /// Every key this row writes.
    readonly property var keys: {
        const keys = [];
        if (row.spec.toggle)
            keys.push(row.spec.toggle);
        for (const field of (row.spec.fields ?? [])) {
            keys.push(field.key);
            if (field.minKey)
                keys.push(field.minKey);
        }
        for (const part of [row.spec.choice, row.spec.pick, row.spec.list]) {
            if (part)
                keys.push(part.key);
        }
        return keys;
    }
    readonly property bool pending: {
        const pendingKeys = row.prefs ? row.prefs.pendingKeys : [];
        return row.keys.some(key => pendingKeys.indexOf(key) >= 0);
    }
    /// `dep` holds: "key" is true, or "key!=value" is not that value; a list needs them all.
    readonly property bool depOk: {
        const deps = row.spec.dep === undefined ? [] : [].concat(row.spec.dep);
        return deps.every(dep => {
                              const at = dep.indexOf("!=");
                              if (at >= 0)
                                  return String(row.values[dep.slice(0, at)]) !== dep.slice(at + 2);
                              return !!row.values[dep];
                          });
    }
    /// The keys whose value is not qBittorrent's default (Advanced, docs/16 §4).
    readonly property var changedDefaults: {
        const defaults = row.spec.defaults ?? {};
        const changed = {};
        let any = false;
        for (const key in defaults) {
            if (row.values[key] !== undefined && row.values[key] !== defaults[key]) {
                changed[key] = defaults[key];
                any = true;
            }
        }
        return any ? changed : null;
    }

    Layout.fillWidth: true
    Layout.leftMargin: row.spec.sub ? Theme.space(22) : 0
    description: row.spec.desc ?? ""
    enabled: row.loaded && row.depOk
    glyph: row.spec.glyph ?? ""
    opacity: row.enabled ? 1 : 0.45
    title: row.spec.title ?? ""

    onClicked: row.activate("click")

    /// `enter` (or a click on the row): edit, open or change, whatever the row holds;
    /// `space`: flip the switch when there is one.
    function activate(how: string): void {
        if (!row.enabled)
            return;
        if (how === "space" && row.spec.toggle) {
            row.flip();
            return;
        }
        if (fieldRepeater.count > 0) {
            row._editField(0);
            return;
        }
        if (row.spec.list) {
            row.listRequested(row.spec.list, row.spec.title, row.confirmKind);
            return;
        }
        if (row.spec.pick) {
            row.pickRequested(row.spec.pick, pickButton, row.confirmKind);
            return;
        }
        if (row.spec.choice) {
            const options = row.spec.choice.options;
            const at = options.findIndex(option => option[0] === row.values[row.spec.choice.key]);
            row.choose(options[(at + 1) % options.length][0]);
            return;
        }
        if (row.spec.toggle) {
            row.flip();
            return;
        }
        if (row.spec.button)
            row.actionRequested(row.spec.button.action);
    }

    /// `d`: back to qBittorrent's defaults.
    function resetDefaults(): void {
        if (row.enabled && row.changedDefaults !== null)
            row.changeRequested(row.changedDefaults, row.confirmKind, row.spec.title);
    }

    function flip(): void {
        const key = row.spec.toggle;
        const on = !row.values[key];
        // Anonymous mode asks only on the way in; the IP filter only when it starts blocking.
        const kind = (row.confirmKind === "anon" || row.confirmKind === "ipfilter") && !on
                ? "" : row.confirmKind;
        row.changeRequested(row.flipChanges(on), kind, row.spec.title);
    }

    /// What turning the switch `on` (or off) writes: the key, and what the row's `enable` /
    /// `disable` say must go with it for qBittorrent to take it (QbtSettingsSections).
    function flipChanges(on: bool): var {
        const changes = {
            [row.spec.toggle]: on
        };
        const extra = (on ? row.spec.enable : row.spec.disable) ?? {};
        for (const name in extra) {
            const field = (row.spec.fields ?? []).find(candidate => candidate.key === name);
            if (on && field !== undefined && field.none !== undefined
                    && row.values[name] !== field.none)
                continue;
            changes[name] = extra[name] === "randomPort"
                    ? 1024 + Math.floor(Math.random() * (65535 - 1024)) : extra[name];
        }
        return changes;
    }

    function choose(value: var): void {
        row.changeRequested({
                                [row.spec.choice.key]: value
                            }, row.confirmKind, row.spec.title);
    }

    /// The value a field shows: scaled to its unit, empty for "no limit".
    function display(field: var): string {
        const value = row.values[field.key];
        if (value === undefined || value === null || field.kind === "password")
            return "";
        if (field.kind === "time") {
            const pad = number => (number < 10 ? "0" : "") + number;
            return pad(value) + ":" + pad(row.values[field.minKey] ?? 0);
        }
        if (field.kind === "text")
            return String(value);
        if (field.none !== undefined && value === field.none)
            return "";
        const scaled = value / (field.scale ?? 1);
        if (field.decimals !== undefined)
            return scaled.toFixed(field.decimals);
        return String(Math.round(scaled * 100) / 100);
    }

    /// What `text` asks the server to store for `field`, or `{error}`.
    function parse(field: var, text: string): var {
        const typed = text.trim();
        if (field.kind === "text")
            return {
                "changes": {
                    [field.key]: typed
                }
            };
        if (field.kind === "password") {
            if (typed.length === 0)
                return {
                    "changes": {}
                };
            if (field.key === "web_ui_password" && typed.length < 6)
                return {
                    "error": qsTr("a password needs at least 6 characters")
                };
            return {
                "changes": {
                    [field.key]: typed
                }
            };
        }
        if (field.kind === "time") {
            const match = /^(\d{1,2})(?::(\d{1,2}))?$/.exec(typed);
            const hour = match ? parseInt(match[1], 10) : -1;
            const minute = match && match[2] !== undefined ? parseInt(match[2], 10) : 0;
            if (!match || hour > 23 || minute > 59)
                return {
                    "error": qsTr("%1 is not a time like 08:30").arg(typed)
                };
            return {
                "changes": {
                    [field.key]: hour,
                    [field.minKey]: minute
                }
            };
        }
        if (typed.length === 0) {
            if (field.none === undefined)
                return {
                    "error": qsTr("%1 needs a number").arg(row.spec.title)
                };
            return {
                "changes": {
                    [field.key]: field.none
                }
            };
        }
        const number = Number(typed.replace(",", "."));
        if (!isFinite(number))
            return {
                "error": qsTr("%1 is not a number").arg(typed)
            };
        if ((field.min !== undefined && number < field.min) || (field.max !== undefined && number
                                                                > field.max)) {
            const range = field.max !== undefined ? qsTr("%1–%2").arg(field.min).arg(field.max)
                                                  : qsTr("%1 or more").arg(field.min);
            return {
                "error": qsTr("%1 takes %2").arg(row.spec.title).arg(range)
            };
        }
        const raw = number * (field.scale ?? 1);
        return {
            "changes": {
                [field.key]: field.decimals !== undefined ? raw : Math.round(raw)
            }
        };
    }

    /// Send what the field holds, when it differs from what the server has.
    /// The field cell at `index` (a `var`, since the Repeater only knows it as an Item).
    function _cell(index: int): var {
        return fieldRepeater.itemAt(index);
    }

    /// The caret into field `index`, at the end of its text (shown from the start at rest).
    function _editField(index: int): void {
        const field = row._cell(index).field;
        field.focusInput();
        field.cursorPosition = field.text.length;
    }

    function commit(field: var, input: var): void {
        if (input.text === input.lastSent)
            return;
        const parsed = row.parse(field, input.text);
        if (parsed.error !== undefined) {
            row.noteRequested(parsed.error);
            input.text = row.display(field);
            input.lastSent = input.text;
            return;
        }
        let differs = false;
        for (const key in parsed.changes) {
            if (row.values[key] !== parsed.changes[key])
                differs = true;
        }
        input.lastSent = input.text;
        if (differs)
            row.changeRequested(parsed.changes, row.confirmKind, row.spec.title);
        if (field.kind === "password")
            input.text = "";
    }

    /// Every field back to the server's value (a change was cancelled or refused).
    function syncFields(): void {
        for (let i = 0; i < fieldRepeater.count; ++i) {
            const cell = row._cell(i);
            if (cell)
                cell.sync();
        }
    }

    Connections {
        function onValuesChanged(): void {
            row.syncFields();
        }

        target: row.prefs
    }

    RowLayout {
        spacing: Theme.space(6)

        Label {
            dim: true
            size: "small"
            text: qsTr("saving…")
            visible: row.pending
        }

        // docs/16 §5: qBittorrent's own word on whether peers can reach the port.
        Chip {
            objectName: "reachChip"
            text: App.globalState.connectionStatus === "firewalled" ? qsTr("not reachable")
                                                                     : App.globalState.connectionStatus
                                                                       === "connected" ? qsTr("reachable")
                                                                                       : qsTr("offline")
            visible: row.spec.reach === true && row.loaded
            warn: App.globalState.connectionStatus !== "connected"
        }

        Toggle {
            id: toggle

            checked: !!row.values[row.spec.toggle]
            objectName: "rowToggle"
            visible: !!row.spec.toggle

            onToggled: {
                // The row decides (and may ask first); the switch shows the server's value.
                toggle.checked = Qt.binding(() => !!row.values[row.spec.toggle]);
                row.flip();
            }
        }

        Repeater {
            id: fieldRepeater

            model: row.spec.fields ?? []

            RowLayout {
                id: fieldCell

                required property int index
                required property var modelData
                readonly property alias field: input

                function sync(): void {
                    if (!input.editing) {
                        input.text = row.display(fieldCell.modelData);
                        input.lastSent = input.text;
                        // A long value (a path, the DHT nodes) reads from its start.
                        input.cursorPosition = 0;
                    }
                }

                spacing: Theme.space(6)

                Component.onCompleted: fieldCell.sync()

                Label {
                    dim: true
                    text: fieldCell.modelData.sep ?? ""
                    visible: (fieldCell.modelData.sep ?? "").length > 0
                }

                Field {
                    id: input

                    /// The text last sent (or shown from the server), so enter and the blur
                    /// that follows it send once.
                    property string lastSent: ""

                    echoMode: fieldCell.modelData.kind === "password" ? TextInput.Password
                                                                      : TextInput.Normal
                    implicitWidth: Theme.space(fieldCell.modelData.width ?? 70)
                    objectName: "field-" + fieldCell.modelData.key
                    placeholder: fieldCell.modelData.noneText ?? ""
                    trailing: fieldCell.modelData.unit ?? ""

                    onAccepted: {
                        row.commit(fieldCell.modelData, input);
                        if (row._cell(fieldCell.index + 1))
                            row._editField(fieldCell.index + 1);
                        else
                            row.fieldDone();
                    }
                    // esc: the server's value again, and the keyboard back to the pane.
                    onCleared: {
                        input.text = row.display(fieldCell.modelData);
                        input.lastSent = input.text;
                        input.cursorPosition = 0;
                        row.fieldDone();
                    }
                    onEditingChanged: {
                        if (!input.editing)
                            row.commit(fieldCell.modelData, input);
                    }
                }
            }
        }

        Repeater {
            model: row.spec.choice ? row.spec.choice.options : []

            Chip {
                id: choiceChip

                required property var modelData

                selected: row.values[row.spec.choice.key] === choiceChip.modelData[0]
                text: choiceChip.modelData[1]

                onClicked: row.choose(choiceChip.modelData[0])
            }
        }

        Button {
            id: pickButton

            bordered: true
            objectName: "pickButton"
            text: {
                const pick = row.spec.pick;
                if (!pick)
                    return "";
                const value = row.values[pick.key];
                if (pick.source === "interfaces")
                    return (value ? value : qsTr("any")) + "  ▾";
                if (pick.source === "addresses")
                    return (value ? value : qsTr("every address")) + "  ▾";
                const option = pick.options.find(entry => entry[0] === value);
                return (option ? option[1] : String(value)) + "  ▾";
            }
            visible: !!row.spec.pick

            onClicked: row.pickRequested(row.spec.pick, pickButton, row.confirmKind)
        }

        Label {
            dim: true
            size: "small"
            text: {
                const list = row.spec.list;
                if (!list)
                    return "";
                const value = row.values[list.key];
                const count = list.dirs ? Object.keys(value ?? {}).length
                                        : String(value ?? "").split(list.sep).filter(
                                              entry => entry.trim().length > 0).length;
                return count === 0 ? qsTr("none yet") : qsTr("%n entries", "", count);
            }
            visible: !!row.spec.list
        }

        Button {
            bordered: true
            objectName: "listButton"
            text: qsTr("edit…")
            visible: !!row.spec.list

            onClicked: row.listRequested(row.spec.list, row.spec.title, row.confirmKind)
        }

        Button {
            bordered: true
            objectName: "actionButton"
            text: row.spec.button ? row.spec.button.text : ""
            visible: !!row.spec.button

            onClicked: row.actionRequested(row.spec.button.action)
        }

        Button {
            objectName: "defaultButton"
            text: qsTr("default")
            visible: row.changedDefaults !== null

            onClicked: row.resetDefaults()
        }
    }
}
