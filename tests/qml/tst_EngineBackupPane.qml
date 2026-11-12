import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// Settings › ENGINE › Backup for an engine other than qBittorrent (docs/57 §3, §4; DECISIONS
// C40 X1, X2, X3; #88): the scope line, the export row in each of its states (the engine's
// .torrent files read from here, out of sight, being asked about; not connected; an export on
// its way), and the import row: there where the engine takes a file, qBittorrent's while
// reading, running and away.
TestCase {
    id: testCase

    name: "EngineBackupPane"

    height: 600
    visible: true
    when: windowShown
    width: 800

    Component {
        id: paneComponent

        EngineBackupPane {
            connected: true
            folder: "~/.config/transmission-daemon/torrents"
            reach: "readable"
            serverName: "seedbox"
            summary: "214 torrents with their folders, labels, limits and renames, in one private file."
            width: 700
        }
    }

    SignalSpy {
        id: exportSpy

        signalName: "exportRequested"
    }

    SignalSpy {
        id: folderSpy

        signalName: "folderRequested"
    }

    function childNamed(item, name) {
        if (item.objectName === name)
            return item;
        for (let i = 0; i < item.children.length; ++i) {
            const found = testCase.childNamed(item.children[i], name);
            if (found)
                return found;
        }
        return null;
    }

    SignalSpy {
        id: importSpy

        signalName: "importRequested"
    }

    SignalSpy {
        id: rowSpy

        signalName: "rowClicked"
    }

    function make(properties) {
        const pane = createTemporaryObject(paneComponent, testCase, properties || {});
        for (const spy of [exportSpy, folderSpy, importSpy, rowSpy]) {
            spy.target = pane;
            spy.clear();
        }
        return pane;
    }

    // X2: whose torrents, and how a library moves; the two rows, nothing of the engine's
    // settings.
    function test_the_scope_line_says_how_a_library_moves() {
        const pane = testCase.make();
        compare(testCase.childNamed(pane, "engineBackupScopeLine").text,
                "seedbox's own torrents, read from and sent to seedbox only. To move them to another server, export here and import there.");
        const exportRow = testCase.childNamed(pane, "engineExportRow");
        const importRow = testCase.childNamed(pane, "engineImportRow");
        compare(exportRow.title, "Export seedbox's torrents");
        compare(importRow.title, "Import torrents into seedbox");
        verify(importRow.visible);
        verify(exportRow.mapToItem(pane, 0, 0).y < importRow.mapToItem(pane, 0, 0).y);
        verify(!testCase.childNamed(pane, "engineBackupNote").visible);
        // The modal's cursor is on one of them.
        verify(!exportRow.hasCursor && !importRow.hasCursor);
        pane.cursor = 1;
        verify(!exportRow.hasCursor && importRow.hasCursor);
        pane.cursor = 0;
        verify(exportRow.hasCursor && !importRow.hasCursor);

        // C40 X9: an engine that only exports yet. One row, and "read from" alone.
        pane.canImport = false;
        verify(!importRow.visible);
        compare(testCase.childNamed(pane, "engineBackupScopeLine").text,
                "seedbox's own torrents, read from seedbox only. To move them to another server, export here and import there.");
    }

    // docs/57 §3: the import row, as qBittorrent's. Its words, "import…", the progress line
    // while the file and the server are read and while it runs, "stop" for a run only.
    function test_the_import_row_is_qbittorrents() {
        const pane = testCase.make();
        const row = testCase.childNamed(pane, "engineImportRow");
        const button = testCase.childNamed(pane, "engineImportButton");
        compare(row.description,
                "From a file exported by omaseed, on any engine. Added stopped and checked against the files on disk; nothing is moved.");
        compare(row.opacity, 1);
        compare(button.text, "import…");
        verify(button.enabled);
        button.clicked();
        compare(importSpy.count, 1);
        compare(rowSpy.signalArguments[0][0], 1);
        row.clicked();
        compare(importSpy.count, 2);
        compare(exportSpy.count, 0);

        // Reading the file and the server: the line says so, and there is nothing to stop.
        pane.running = "read";
        pane.progress = "reading seedbox…";
        compare(row.description, "reading seedbox…");
        compare(row.opacity, 1);
        compare(button.text, "import…");
        verify(!button.enabled);

        pane.running = "import";
        pane.progress = "checking files: 3 of 12…";
        compare(row.description, "checking files: 3 of 12…");
        compare(button.text, "stop");
        verify(button.enabled);
        // The export row waits meanwhile.
        verify(!testCase.childNamed(pane, "engineExportButton").enabled);

        // An export on its way: this row waits.
        pane.running = "export";
        compare(row.description,
                "From a file exported by omaseed, on any engine. Added stopped and checked against the files on disk; nothing is moved.");
        verify(!button.enabled);
        verify(row.opacity < 1);

        pane.running = "";
        pane.connected = false;
        compare(row.description, "Connect to seedbox to import: what is new is read against it.");
        verify(!button.enabled);
        verify(row.opacity < 1);
    }

    // X3: the files seen from here. The row is plain and says where they are read from.
    function test_readable_is_plain_and_names_the_folder() {
        const pane = testCase.make();
        const row = testCase.childNamed(pane, "engineExportRow");
        compare(row.description,
                "214 torrents with their folders, labels, limits and renames, in one private file. Its .torrent files are read from ~/.config/transmission-daemon/torrents.");
        compare(row.lead, "");
        compare(row.warning, "");
        compare(testCase.childNamed(row, "settingsRowDescription").textFormat, Text.PlainText);
        compare(row.opacity, 1);
        verify(!testCase.childNamed(pane, "engineFolderButton").visible);
        const button = testCase.childNamed(pane, "engineExportButton");
        compare(button.text, "export…");
        verify(button.enabled);
        verify(button.primary);
        button.clicked();
        compare(exportSpy.count, 1);
        row.clicked();
        compare(exportSpy.count, 2);
        compare(folderSpy.count, 0);
    }

    // X3: out of sight. The warning lead, what a link costs, and the two buttons.
    function test_unreadable_offers_their_folder_or_links() {
        const pane = testCase.make({
                                       "reach": "unreadable",
                                       "folder": "/config/torrents"
                                   });
        verify(pane.unseen);
        const row = testCase.childNamed(pane, "engineExportRow");
        compare(row.lead, "omaseed can't see seedbox's .torrent files");
        compare(row.description,
                " (they are on its own disk). Give their folder as this machine sees it, or export links: the other server then needs peers to fetch each torrent's details before it can check.");
        // The lead is the line's first words, in the warning colour; a screen reader gets the
        // sentence without the markup.
        const text = testCase.childNamed(row, "settingsRowDescription");
        compare(text.textFormat, Text.StyledText);
        verify(text.text.indexOf("<font color='" + row.ink.yellow
                                 + "'>omaseed can't see seedbox's .torrent files</font> (they are")
               === 0, text.text);
        compare(row.Accessible.description, row.lead + row.description);
        const folder = testCase.childNamed(pane, "engineFolderButton");
        verify(folder.visible);
        verify(folder.primary);
        compare(folder.text, "their folder…");
        const links = testCase.childNamed(pane, "engineExportButton");
        compare(links.text, "export links…");
        verify(links.enabled);
        verify(!links.primary);
        // Stacked, their folder… first.
        verify(folder.mapToItem(pane, 0, 0).y < links.mapToItem(pane, 0, 0).y);
        folder.clicked();
        compare(folderSpy.count, 1);
        links.clicked();
        compare(exportSpy.count, 1);
        // The row itself is its first button.
        row.clicked();
        compare(folderSpy.count, 2);

        // A folder that wasn't theirs: said under the row, which keeps offering.
        compare(row.warning, "");
        pane.folderNote = "That folder doesn't hold seedbox's .torrent files.";
        compare(row.warning, "That folder doesn't hold seedbox's .torrent files.");
        verify(folder.visible);
    }

    // Being asked (and before the first answer): the plain words, no folder, a button that
    // waits.
    function test_asking_waits() {
        for (const reach of ["asking", ""]) {
            const pane = testCase.make({
                                           "reach": reach
                                       });
            const row = testCase.childNamed(pane, "engineExportRow");
            compare(row.description,
                    "214 torrents with their folders, labels, limits and renames, in one private file.");
            compare(row.lead, "");
            verify(!testCase.childNamed(pane, "engineFolderButton").visible);
            const button = testCase.childNamed(pane, "engineExportButton");
            compare(button.text, "export…");
            verify(!button.enabled);
            row.clicked();
            compare(exportSpy.count, 0);
            compare(folderSpy.count, 0);
        }
    }

    // As qBittorrent's row: how far it got, and its button stops it; dim and saying so while
    // the server is away.
    function test_running_and_not_connected_are_qbittorrents() {
        const pane = testCase.make({
                                       "reach": "unreadable",
                                       "running": "export",
                                       "progress": "exporting 40 of 214…"
                                   });
        const row = testCase.childNamed(pane, "engineExportRow");
        compare(row.description, "exporting 40 of 214…");
        compare(row.lead, "");
        verify(!testCase.childNamed(pane, "engineFolderButton").visible);
        const button = testCase.childNamed(pane, "engineExportButton");
        compare(button.text, "stop");
        verify(button.enabled);
        verify(!button.primary);
        button.clicked();
        compare(exportSpy.count, 1);
        compare(row.opacity, 1);

        // Another run (an import): the row waits.
        pane.reach = "readable";
        pane.running = "import";
        verify(!button.enabled);
        verify(row.opacity < 1);

        pane.running = "";
        pane.connected = false;
        compare(row.description, "Connect to seedbox to export.");
        compare(row.lead, "");
        verify(!button.enabled);
        verify(row.opacity < 1);

        // The result, under the row.
        pane.note
                = "Exported seedbox's 214 torrents to ~/torrents-seedbox-2026-11-11.omaseed-torrents.";
        const note = testCase.childNamed(pane, "engineBackupNote");
        verify(note.visible);
        compare(note.text, pane.note);
    }
}
