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
                                                                    ? noteComponent : rowComponent

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
                    color: Theme.fill(Theme.fillIdle)
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
                        size: "small"
                        text: holder.modelData.note
                        wrapMode: Text.Wrap
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
