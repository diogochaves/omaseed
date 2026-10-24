import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// #146 (board 49, DECISIONS C27): the create sheet on the demo library. The folder read before
// anything is hashed (CR17), anyone's trackers from the library (CR4), the private path's
// announce only as dots, its facts and its saved name (CR5–CR7, CR15), format and pieces lists
// (CR10, CR11), more's fields, what App.startCreate gets, and "create when back" (board 49 §7).
// The keys and hosts typed here are invented.
TestCase {
    id: testCase

    height: 900
    name: "CreateSheet"
    visible: true
    when: windowShown
    width: 1200

    CreateSheet {
        id: sheet

        anchors.fill: parent
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    function cleanup() {
        sheet.serverDown = false;
        sheet.close();
    }

    /// Opens on FOLDER and waits for its check.
    function openOn(folder) {
        sheet.openFor(folder);
        tryVerify(() => sheet.checked, 3000);
    }

    /// Every text the sheet draws, for the passkey's check.
    function allTexts(item, out) {
        if (item.text !== undefined && typeof item.text === "string")
            out.push(item.text);
        for (const child of item.children)
            testCase.allTexts(child, out);
        return out;
    }

    function test_the_folder_and_anyone() {
        openOn("/mnt/media/share/photo-walks");
        compare(sheet.title, "Create a torrent");
        compare(sheet.forChoice.choice, 0);
        compare(sheet.check.state, "ok");
        const line = findChild(sheet, "createFolderLine");
        verify(line.text.indexOf("212 files · 1.68 GB") === 0, line.text);
        verify(line.text.indexOf("1 hidden file left out (.DS_Store)") > 0, line.text);
        verify(sheet.canCreate);
        compare(sheet.primaryText, "create and seed");
        compare(sheet.actions[1].text, "create and seed");
        verify(sheet.note.indexOf("hashing runs on") === 0);
        // CR11: automatic pieces, shown ahead.
        compare(sheet.autoRow.text, "512 KiB"); // board 49: 1.8 GB → 512 KiB
        verify(findChild(sheet, "createPiecesButton").text.indexOf("automatic · 512 KiB") === 0);
        const request = sheet.request();
        compare(request.folder, "/mnt/media/share/photo-walks");
        compare(request.forWhom, "anyone");
        compare(request.format, "v1");
        compare(request.pieceSize, 0);
        compare(request.ignoreDotfiles, true);
        compare(request.totalBytes, 1800000000);
        compare(request.announce, "");
        // The when-done line: seed mode at the folder's parent.
        verify(findChild(sheet, "createDoneLine").text.indexOf("in seed mode at /mnt/media/share") > 0);
    }

    function test_trackers_are_added_and_removed() {
        openOn("/mnt/media/share/photo-walks");
        const before = sheet.trackers.length;
        sheet.trackerField.text = "udp://open.tracker-z.example:1337/announce https://tracker-y.example/announce";
        sheet._addTracker();
        compare(sheet.trackers.length, before + 2);
        compare(sheet.trackers[before], "udp://open.tracker-z.example:1337/announce");
        sheet._removeTracker("udp://open.tracker-z.example:1337/announce");
        compare(sheet.trackers.length, before + 1);
        verify(findChild(sheet, "createTrackersLine").text.indexOf("none is fine") > 0);
    }

    function test_format_and_pieces() {
        openOn("/mnt/media/share/photo-walks");
        sheet._openList("pieces");
        compare(sheet.listOpen, "pieces");
        compare(sheet.optionsList.rows.length, 14); // automatic, then 16 KiB … 64 MiB
        compare(sheet.optionsList.rows[0].value, 0);
        sheet.optionsList.current = 7; // 1 MiB
        sheet._pickOption();
        compare(sheet.pieceSize, 1048576);
        compare(findChild(sheet, "createPiecesButton").text, "1 MiB ▾");
        sheet._openList("format");
        compare(sheet.optionsList.rows.map(row => row.value), ["v1", "hybrid", "v2"]);
        // The keys: ↓ then enter picks; esc closes a list before the sheet.
        sheet.forceActiveFocus();
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(sheet.listOpen, "");
        sheet._openList("format");
        keyClick(Qt.Key_Escape);
        compare(sheet.listOpen, "");
        verify(sheet.opened);
        compare(sheet.request().format, "hybrid");
        compare(sheet.request().pieceSize, 1048576);
    }

    // CR5–CR7: a pasted announce URL shows in full only while typed; nowhere else.
    function test_private_with_a_pasted_announce() {
        openOn("/mnt/media/share/field-recordings");
        sheet.forChoice.choice = 1;
        verify(sheet.isPrivate);
        compare(sheet.primaryText, "create .torrent");
        sheet._openList("hosts");
        const rows = sheet.optionsList.rows;
        compare(rows[rows.length - 1].text, "paste an announce URL…");
        sheet.optionsList.current = rows.length - 1;
        sheet._pickOption();
        verify(sheet.pasting);
        verify(!sheet.canCreate);
        const key = "a1b2c3d4e5f60718";
        sheet.pasteField.text = "https://tracker-q.example/announce/" + key;
        verify(sheet.canCreate);
        sheet.folderField.focusInput(); // the paste field is left: dots
        compare(sheet.pasteField.echoMode, TextInput.Password);
        const texts = testCase.allTexts(sheet, []).filter(text => text !== sheet.pasteField.text);
        verify(!texts.some(text => text.indexOf(key) >= 0), "the passkey shows");
        verify(findChild(sheet, "createAnnounceLine").text.indexOf("https://tracker-q.example/announce/••••••••") === 0);
        compare(sheet.fileHost, "tracker-q.example");
        verify(findChild(sheet, "createSavedName").text.indexOf("field-recordings [tracker-q.example].torrent") > 0);
        verify(findChild(sheet, "createRoundTripNote").visible);
        verify(findChild(sheet, "createFormatFact").visible);
        // The source tag, typed.
        compare(findChild(sheet, "createSourceFact").title, "Source tag: none yet");
        findChild(sheet, "createSourceFact").linkClicked();
        sheet.sourceField.text = "TQ";
        sheet._takeSource();
        compare(sheet.source, "TQ");
        const request = sheet.request();
        compare(request.forWhom, "private");
        compare(request.announce, "https://tracker-q.example/announce/" + key);
        compare(request.source, "TQ");
        compare(request.sourceTyped, true);
        compare(request.trackers, []);
        compare(request.category, "");
    }

    function test_more() {
        openOn("/mnt/media/share/photo-walks");
        verify(!findChild(sheet, "createComment").visible);
        sheet.moreOpen = true;
        sheet.webSeedsField.text = "https://mirror.example/a/ https://mirror.example/b/";
        sheet.commentField.text = "walks, 2026";
        findChild(sheet, "createHidden").toggled();
        const request = sheet.request();
        compare(request.webSeeds, ["https://mirror.example/a/", "https://mirror.example/b/"]);
        compare(request.comment, "walks, 2026");
        compare(request.ignoreDotfiles, false);
    }

    // Board 49 §7: a server that doesn't answer keeps the choices; create when back sends them.
    function test_create_when_back() {
        openOn("/mnt/media/share/photo-walks");
        sheet.serverDown = true;
        compare(sheet.primaryText, "create when back");
        verify(findChild(sheet, "createServerDown").visible);
        sheet.create();
        verify(sheet.held);
        verify(sheet.opened);
        sheet.serverDown = false;
        verify(!sheet.opened); // sent (the demo takes it) and closed
    }

    function test_nothing_to_create_from() {
        sheet.openBlank();
        verify(!sheet.canCreate);
        compare(findChild(sheet, "createFolderLine").visible, false);
    }
}
