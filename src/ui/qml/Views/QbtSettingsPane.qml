pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// One section of the Settings modal's qBittorrent group (docs/16 §4): its rows, small group
// headings and notes, from a `QbtSettingsSections` section. A row whose key the server's
// version does not have is left out. While the server has not answered, every row is drawn
// and greyed, under one line that says why.
//
// The modal owns the keyboard: `cursor` is its row cursor, counted over the rows only
// (headings and notes are skipped), and `activate`/`resetDefaults` act on that row.
//
// Do NOT run `qmlformat` on this file: with the project's 100-column wrap it re-indents the
// typed functions further on every pass (the defect Menu.qml notes). It is hand-formatted.
ColumnLayout {
    id: root

    property var section: null
    property ServerPreferences prefs: null
    /// The row cursor, -1 for none; drawn only while `keyboard` is true.
    property int cursor: -1
    property bool keyboard: false
    /// The server's name, for the waiting line.
    property string serverName: ""

    signal changeRequested(var changes, string confirmKind, string what)
    signal listRequested(var list, string what, string confirmKind)
    signal pickRequested(var pick, Item anchor, string confirmKind)
    signal actionRequested(string action)
    signal noteRequested(string text)
    /// A click put the cursor on row `index`.
    signal rowClicked(int index)
    signal fieldDone

    readonly property bool loaded: root.prefs !== null && root.prefs.loaded
    // How many keys the server has: moves when it first answers, not on each change, so the
    // rows are not rebuilt (and a field does not lose its caret) every time a value moves.
    readonly property int _keyCount: root.prefs ? Object.keys(root.prefs.values).length : 0
    /// The section's rows the server can take, in order.
    readonly property var rows: {
        void root._keyCount;
        if (!root.section)
            return [];
        return root.section.rows.filter(spec => {
                                            if (!root.loaded || spec.title === undefined)
                                                return true;
                                            const keys = [];
                                            if (spec.toggle)
                                                keys.push(spec.toggle);
                                            for (const field of (spec.fields ?? [])) {
                                                // Write-only (the Web UI password): never sent back.
                                                if (field.kind === "password")
                                                    return true;
                                                keys.push(field.key);
                                            }
                                            for (const part of [spec.choice, spec.pick, spec.list]) {
                                                if (part)
                                                    keys.push(part.key);
                                            }
                                            // A row with nothing to read (a button) always shows.
                                            return keys.length === 0 || keys.some(key => root.prefs.has(key));
                                        });
    }
    /// For each cursor position, the index of its row in `rows`.
    readonly property var cursorRows: {
        const indexes = [];
        root.rows.forEach((spec, index) => {
                              if (spec.title !== undefined)
                                  indexes.push(index);
                          });
        return indexes;
    }
    readonly property int cursorCount: root.cursorRows.length

    function rowItem(cursor: int): var {
        if (cursor < 0 || cursor >= root.cursorRows.length)
            return null;
        const holder = rowRepeater.itemAt(root.cursorRows[cursor]);
        return holder ? holder.item : null;
    }

    function activate(cursor: int, how: string): void {
        const item = root.rowItem(cursor);
        if (item)
            item.activate(how);
    }

    function resetDefaults(cursor: int): void {
        const item = root.rowItem(cursor);
        if (item)
            item.resetDefaults();
    }

    /// docs/31 D8: a row's preset by its key ("n"), from anywhere in the section. False when
    /// no row here has one under that key.
    function preset(key: string): bool {
        for (let i = 0; i < rowRepeater.count; ++i) {
            const holder = rowRepeater.itemAt(i);
            const spec = root.rows[i];
            if (holder && holder.item && spec.preset && spec.preset.key === key) {
                holder.item.applyPreset();
                return true;
            }
        }
        return false;
    }

    /// The section's preset keys and words, for the footer's hints: [{key, label}].
    readonly property var presets: root.rows.filter(spec => spec.preset !== undefined).map(
                                       spec => ({
                                                    "key": spec.preset.key,
                                                    "label": spec.preset.text
                                                }))

    /// Every field back to the server's value, after a change was cancelled.
    function syncFields(): void {
        for (let i = 0; i < rowRepeater.count; ++i) {
            const holder = rowRepeater.itemAt(i);
            if (holder && holder.item && holder.item.syncFields)
                holder.item.syncFields();
        }
    }

    spacing: Theme.space(8)

    // The server has not answered (yet): one line, and every row greyed below it.
    RowLayout {
        Layout.fillWidth: true
        objectName: "qbtWaiting"
        spacing: Theme.space(8)
        visible: !root.loaded

        Label {
            Layout.alignment: Qt.AlignTop
            color: root.prefs && root.prefs.loadError.length > 0 ? Theme.red : Theme.yellow
            size: "small"
            text: "!"
        }

        Label {
            Layout.fillWidth: true
            dim: true
            size: "small"
            text: root.prefs && root.prefs.loadError.length > 0
                  ? qsTr("%1 is not answering (%2), so there is nothing to change until it does.").arg(
                        root.serverName).arg(root.prefs.loadError)
                  : !App.hasServer ? qsTr("No server yet. Add one in Servers first.")
                                   : qsTr("Reading %1's settings…").arg(root.serverName)
            wrapMode: Text.Wrap
        }
    }

    Repeater {
        id: rowRepeater

        model: root.rows

        Loader {
            id: holder

            required property int index
            required property var modelData
            readonly property int cursorIndex: root.cursorRows.indexOf(holder.index)

            Layout.fillWidth: true
            Layout.topMargin: holder.modelData.group !== undefined && holder.index > 0 ? Theme.space(8)
                                                                                       : 0
            sourceComponent: holder.modelData.group !== undefined ? groupComponent
                                                                  : holder.modelData.note !== undefined
                                                                    ? noteComponent
                                                                    : holder.modelData.line !== undefined
                                                                      ? lineComponent : rowComponent

            Component {
                id: groupComponent

                Label {
                    leftPadding: Theme.space(4)
                    text: holder.modelData.group
                    tracked: true
                }
            }

            Component {
                id: noteComponent

                // docs/16's banners: a fact about the section, in the kit Banner's look (a
                // 1 px rule in the kind's colour), but wrapping: these run to two lines.
                Rectangle {
                    id: noteBlock

                    /// Its words on its 4 % fill (docs/DECISIONS.md C30 F1).
                    readonly property Ink ink: Ink {
                        ground: Theme.on.idle
                    }

                    color: noteBlock.ink.fill
                    implicitHeight: noteText.implicitHeight + Theme.space(14)
                    radius: Theme.cornerRadius

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.top: parent.top
                        color: holder.modelData.warn ? Theme.yellow : Theme.accent
                        width: 1
                    }

                    Label {
                        id: noteText

                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(12)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space(12)
                        anchors.verticalCenter: parent.verticalCenter
                        ink: noteBlock.ink
                        size: "small"
                        text: holder.modelData.note
                        wrapMode: Text.Wrap
                    }
                }
            }

            Component {
                id: lineComponent

                // docs/31 P1–P3: a line from App.altSpeed, drawn as the board's `.line`: a 1 px
                // rule in its tone (green for burst, yellow for turtle and for a warning, the
                // hairline otherwise), a glyph, the sentence, and a dim second line.
                Rectangle {
                    id: line

                    readonly property bool isSet: holder.modelData.line === "set"
                    readonly property string text: line.isSet ? App.altSpeed.setLine
                                                              : App.altSpeed.scheduleSummary
                    readonly property string note: line.isSet ? "" : App.altSpeed.scheduleNote
                    readonly property bool warn: !line.isSet && App.altSpeed.scheduleWarn
                    readonly property color tone: {
                        if (line.warn)
                            return Theme.yellow;
                        if (line.isSet && App.altSpeed.setKind === "burst")
                            return Theme.green;
                        if (line.isSet && App.altSpeed.setKind === "turtle")
                            return Theme.yellow;
                        return Theme.fill(Theme.hairlineAlpha);
                    }
                    readonly property bool plain: line.tone !== Theme.green && line.tone !== Theme.yellow
                    /// Its words on its 4 % fill; `tone` is a window role, painted as that
                    /// ground's of the same (docs/DECISIONS.md C30 F1, F3).
                    readonly property Ink ink: Ink {
                        ground: Theme.on.idle
                    }

                    color: line.ink.fill
                    implicitHeight: lineText.implicitHeight + Theme.space(12)
                    objectName: "qbtLine-" + holder.modelData.line
                    // The schedule's line reads dim while the schedule is off, as its rows do.
                    opacity: !line.isSet && root.prefs && !root.prefs.values.scheduler_enabled ? 0.6 : 1
                    radius: Theme.cornerRadius
                    visible: root.loaded && line.text.length > 0

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.top: parent.top
                        color: line.tone
                        width: 1
                    }

                    Label {
                        id: lineGlyph

                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(12)
                        anchors.top: lineText.top
                        color: line.plain ? line.ink.dimForeground : line.ink.of(line.tone)
                        objectName: "qbtLineGlyph"
                        size: "small"
                        text: line.isSet ? App.altSpeed.setGlyph : line.warn ? Glyphs.settingWarn : Glyphs.settingPending
                    }

                    Column {
                        id: lineText

                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(34)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space(12)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.space(2)

                        // #79: the pane's line by its words (its glyph left out); a warning is
                        // an alert, the note under it text.
                        Label {
                            Accessible.name: text
                            Accessible.role: line.warn ? Accessible.AlertMessage : Accessible.StaticText
                            ink: line.ink
                            objectName: "qbtLineText"
                            size: "small"
                            text: line.text
                            width: parent.width
                            wrapMode: Text.Wrap
                        }

                        Label {
                            Accessible.name: text
                            Accessible.role: Accessible.StaticText
                            dim: true
                            ink: line.ink
                            objectName: "qbtLineNote"
                            size: "caption"
                            text: line.note
                            visible: line.note.length > 0
                            width: parent.width
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            Component {
                id: rowComponent

                QbtSettingRow {
                    hasCursor: root.keyboard && root.cursor === holder.cursorIndex
                    objectName: "qbtRow-" + (holder.modelData.toggle ?? (holder.modelData.fields
                                                                         ? holder.modelData.fields[0].key
                                                                         : holder.modelData.choice
                                                                           ? holder.modelData.choice.key
                                                                           : holder.modelData.pick
                                                                             ? holder.modelData.pick.key
                                                                             : holder.modelData.list
                                                                               ? holder.modelData.list.key
                                                                               : holder.index))
                    prefs: root.prefs
                    sectionConfirm: root.section && root.section.webui ? "webui" : ""
                    spec: holder.modelData

                    onActionRequested: action => root.actionRequested(action)
                    onChangeRequested: (changes, confirmKind, what) => root.changeRequested(changes,
                                                                                            confirmKind,
                                                                                            what)
                    onClicked: root.rowClicked(holder.cursorIndex)
                    onFieldDone: root.fieldDone()
                    onListRequested: (list, what, confirmKind) => root.listRequested(list, what,
                                                                                     confirmKind)
                    onNoteRequested: text => root.noteRequested(text)
                    onPickRequested: (pick, anchor, confirmKind) => root.pickRequested(pick, anchor,
                                                                                       confirmKind)
                }
            }
        }
    }

    Item {
        Layout.fillHeight: true
    }
}
