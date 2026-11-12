pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// Settings › ENGINE › Backup for an engine other than qBittorrent (docs/57 §3, §4;
// docs/DECISIONS.md C40 X1, X2, X3; #88): the last section of its group, the torrents' rows of
// qBittorrent's Backup and nothing of its settings: "Export NAME's torrents" and, where the
// engine takes a file (`canImport`; C40 X9: an engine may learn the two one release apart),
// "Import torrents into NAME". A Transmission's and a Deluge's.
//
// The engine keeps its .torrent files on its own disk, so the row says before it starts
// whether this machine reads them (`reach`): "readable", plain, with the folder they are read
// from; "unreadable", the warning lead, what a link costs, and two buttons, "their folder…"
// and "export links…"; anything else (asked, not asked yet), the plain words and a button
// that waits. A Deluge that doesn't tell where its files are is "unreadable" at once, with no
// folder. While an export runs the row says how far it got and its button stops it, as
// qBittorrent's.
//
// The import row is qBittorrent's: "import…" opens the file chooser and then the import sheet;
// while the file and the server are read, or an import runs, it says how far it got, and
// "stop" ends an import.
//
// The pane draws; the modal owns the cursor and the keys, and App the state.
ColumnLayout {
    id: root

    /// The server omaseed is on, as its rows name it.
    property string serverName: ""
    property bool connected: false
    /// App.torrentsBackupRunning and its progress line.
    property string running: ""
    property string progress: ""
    /// App.torrentFilesReach: "asking", "readable", "unreadable" or "".
    property string reach: ""
    /// Where the .torrent files are read from, the home folder as `~`.
    property string folder: ""
    /// What goes in the file: "214 torrents with their folders, labels, limits and renames,
    /// in one private file."
    property string summary: ""
    /// After a folder that wasn't theirs: "That folder doesn't hold NAME's .torrent files."
    property string folderNote: ""
    /// The last export's result, and whether it failed.
    property string note: ""
    property bool noteError: false
    /// The engine takes a torrents file: the second row is there (features.torrentsImport).
    property bool canImport: true
    /// The modal's cursor: 0 the export row, 1 the import row, -1 neither.
    property int cursor: -1

    readonly property bool exporting: root.running === "export"
    /// An import runs, or its file and the server are being read.
    readonly property bool importing: root.running === "import" || root.running === "read"
    readonly property bool ready: root.connected && root.running.length === 0
    /// The files can't be read from here: the row offers their folder, or links.
    readonly property bool unseen: root.connected && !root.exporting && root.reach === "unreadable"
    readonly property bool seen: root.reach === "readable"

    /// "export…", "export links…" or "stop" was pressed.
    signal exportRequested
    /// "their folder…" was pressed.
    signal folderRequested
    /// "import…" or its "stop" was pressed.
    signal importRequested
    /// Row `row` was clicked: the cursor goes to it.
    signal rowClicked(row: int)

    spacing: Theme.space(8)

    ScopeLine {
        Layout.fillWidth: true
        objectName: "engineBackupScopeLine"
        // C40 X2.
        text: qsTr(
                  "%1's own torrents, %2 %1 only. To move them to another server, export here and import there.").arg(
                  root.serverName).arg(root.canImport ? qsTr("read from and sent to") : qsTr("read from"))
    }

    Label {
        Layout.topMargin: Theme.space(6)
        leftPadding: Theme.space(4)
        text: qsTr("torrents")
        tracked: true
    }

    SettingsRow {
        id: exportRow

        Layout.fillWidth: true
        description: {
            if (root.exporting)
                return root.progress;
            if (!root.connected)
                return qsTr("Connect to %1 to export.").arg(root.serverName);
            if (root.unseen)
                return " " + qsTr(
                            "(they are on its own disk). Give their folder as this machine sees it, or export links: the other server then needs peers to fetch each torrent's details before it can check.");
            return root.seen && root.folder.length > 0 ? root.summary + " " + qsTr("Its .torrent files are read from %1.").arg(
                                                             root.folder) : root.summary;
        }
        glyph: Glyphs.arrowDown
        hasCursor: root.cursor === 0
        lead: root.unseen ? qsTr("omaseed can't see %1's .torrent files").arg(root.serverName) : ""
        objectName: "engineExportRow"
        opacity: root.exporting || root.ready ? 1 : 0.55
        title: qsTr("Export %1's torrents").arg(root.serverName)
        warning: root.unseen ? root.folderNote : ""

        onClicked: {
            root.rowClicked(0);
            if (root.unseen)
                root.folderRequested();
            else if (root.exporting || root.seen)
                root.exportRequested();
        }

        ColumnLayout {
            spacing: Theme.space(6)

            Button {
                Layout.alignment: Qt.AlignRight
                block: exportRow.ground
                bordered: true
                objectName: "engineFolderButton"
                primary: true
                text: qsTr("their folder…")
                visible: root.unseen

                onClicked: {
                    root.rowClicked(0);
                    root.folderRequested();
                }
            }

            Button {
                Layout.alignment: Qt.AlignRight
                block: exportRow.ground
                bordered: true
                // Not asked yet, or being asked: the row can't say yet what the file will hold.
                enabled: root.exporting || (root.ready && (root.seen || root.unseen))
                objectName: "engineExportButton"
                primary: !root.exporting && !root.unseen
                text: root.exporting ? qsTr("stop") : root.unseen ? qsTr("export links…") : qsTr(
                                                                        "export…")

                onClicked: {
                    root.rowClicked(0);
                    root.exportRequested();
                }
            }
        }
    }

    // docs/57 §3 (C40 X1): a file from any engine, into this one.
    SettingsRow {
        id: importRow

        Layout.fillWidth: true
        description: root.importing ? root.progress : root.connected ? qsTr(
                                                                           "From a file exported by omaseed, on any engine. Added stopped and checked against the files on disk; nothing is moved.") :
                                                                       qsTr("Connect to %1 to import: what is new is read against it.").arg(
                                                                           root.serverName)
        glyph: Glyphs.arrowUp
        hasCursor: root.cursor === 1
        objectName: "engineImportRow"
        opacity: root.importing || root.ready ? 1 : 0.55
        title: qsTr("Import torrents into %1").arg(root.serverName)
        visible: root.canImport

        onClicked: {
            root.rowClicked(1);
            root.importRequested();
        }

        Button {
            block: importRow.ground
            bordered: true
            enabled: root.running === "import" || root.ready
            objectName: "engineImportButton"
            text: root.running === "import" ? qsTr("stop") : qsTr("import…")

            onClicked: {
                root.rowClicked(1);
                root.importRequested();
            }
        }
    }

    Label {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        color: root.noteError ? Theme.yellow : Theme.green
        objectName: "engineBackupNote"
        size: "small"
        text: root.note
        visible: root.note.length > 0
        wrapMode: Text.WordWrap
    }
}
