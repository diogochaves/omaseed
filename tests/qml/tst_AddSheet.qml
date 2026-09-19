pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed
import Omaseed.Views

// T4.2. `Views/AddSheet.qml` and `Views/DuplicateSheet.qml` — spec v2 §3 Fig. 5 and Fig. 10,
// against the demo library (`App.loadDemo(16)`, the fictional Fig. 1 library, whose
// categories are exactly the figure's linux / movies / tv / music).
//
// `keyClick` reaches a focused item in the offscreen runner; `mouseClick` does not, so the
// chips are driven through their own API and the buttons through the sheet's signals.
TestCase {
    id: testCase

    /// A magnet whose hash is nobody's in the demo library.
    readonly property string freshMagnet:
        "magnet:?xt=urn:btih:9a4f6c1e2b3d4f50617283940a1b2c3d4e5f6071&dn=Fedora-Workstation-Live-44-1.6.x86_64.iso&xl=2512345678"

    height: 800
    name: "AddSheet"
    visible: true
    when: windowShown
    width: 1100

    Component {
        id: addSheetComponent

        AddSheet {
            anchors.fill: parent
        }
    }

    Component {
        id: duplicateSheetComponent

        DuplicateSheet {
            anchors.fill: parent
        }
    }

    Component {
        id: probeComponent

        Item {
            activeFocusOnTab: true
        }
    }

    Component {
        id: addSpy

        SignalSpy {
            signalName: "addRequested"
        }
    }

    Component {
        id: duplicateSpy

        SignalSpy {
            signalName: "duplicateFound"
        }
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    /// The infohash of one torrent that really is in the demo library.
    function knownHash() {
        return App.filtered.hashAt(0);
    }

    // ---- Fig. 5, the head -------------------------------------------------------------

    function test_a_magnet_fills_the_field_and_the_subtitle() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");

        compare(sheet.opened, true);
        compare(sheet.sourceField.text, testCase.freshMagnet);
        compare(sheet.addable.kind, "magnet");
        compare(sheet.addable.valid, true);
        // "Fedora-Workstation-Live-44-1.6.x86_64.iso · 2.34 GB · from clipboard"
        compare(sheet.subtitle,
                "Fedora-Workstation-Live-44-1.6.x86_64.iso · 2.34 GB · from clipboard");
        compare(sheet.title, "Add torrent");
        compare(sheet.footerRight, "server: " + App.serverName);
    }

    function test_an_empty_sheet_asks_for_a_source() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();
        compare(sheet.addable, null);
        compare(sheet.subtitle, "paste a magnet link, a URL or pick a file");
    }

    function test_typing_reparses_live() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();
        sheet.sourceField.text = "https://example.org/iso/debian-13.1.0-amd64-DVD-1.iso.torrent";
        compare(sheet.addable.kind, "url");
        compare(sheet.addable.name, "debian-13.1.0-amd64-DVD-1.iso.torrent");
        // Typed, not pasted from anywhere: no "from …" part.
        compare(sheet.sourceText, "");
    }

    // ---- the clipboard ------------------------------------------------------------------

    function test_the_clipboard_prefills_an_empty_sheet() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.clipboardAddable = testCase.freshMagnet;
        sheet.openBlank();

        compare(sheet.sourceField.text, testCase.freshMagnet);
        compare(sheet.source, "clipboard");
        compare(sheet.sourceText, "from clipboard");
    }

    function test_a_clipboard_of_prose_leaves_the_field_alone() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        // The clipboard almost always holds something that is not a torrent.
        sheet.clipboardAddable = "the quick brown fox";
        sheet.openBlank();
        compare(sheet.sourceField.text, "");
        compare(sheet.addable, null);
    }

    // ---- the rows -------------------------------------------------------------------------

    function test_the_category_chips_are_the_servers_categories() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();

        const names = sheet.categoryChips.options;
        compare(names.length, 4);
        verify(names.indexOf("linux") >= 0);
        verify(names.indexOf("movies") >= 0);
        verify(names.indexOf("tv") >= 0);
        verify(names.indexOf("music") >= 0);
        // The `…` chip that hands over to T4.4's palette.
        compare(sheet.categoryChips.more, true);
    }

    function test_picking_a_category_fills_the_save_path() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();
        compare(sheet.savePathField.text, "");

        sheet.categoryChips.pick("linux");
        compare(sheet.categoryChips.current, "linux");
        compare(sheet.savePathField.text, "/data/torrents/linux");
        compare(sheet.savePathField.trailing, "from category");
        compare(sheet.autoTMM, true);

        // Unpicking empties it again, because nothing else chose it.
        sheet.categoryChips.pick("linux");
        compare(sheet.categoryChips.current, "");
        compare(sheet.savePathField.text, "");
    }

    function test_typing_a_save_path_turns_auto_tmm_off() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();
        sheet.categoryChips.pick("movies");
        compare(sheet.savePathField.text, "/data/torrents/movies");

        sheet.savePathField.text = "/data/other";
        compare(sheet.autoTMM, false);
        compare(sheet.savePathField.trailing, "");
        // The category no longer moves the path.
        sheet.categoryChips.pick("tv");
        compare(sheet.savePathField.text, "/data/other");
    }

    function test_the_tags_row_is_multi_select() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();
        sheet.tagChips.pick("keep");
        sheet.tagChips.pick("private");
        compare(sheet.tagChips.selected, ["keep", "private"]);
        sheet.tagChips.pick("keep");
        compare(sheet.tagChips.selected, ["private"]);
    }

    function test_the_layout_row_has_the_three_values() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();
        compare(sheet.layoutChips.options, ["Original", "Subfolder", "No subfolder"]);
        compare(sheet.layoutValue(), "Original");
        sheet.layoutChips.pick("No subfolder");
        compare(sheet.layoutValue(), "NoSubfolder");
    }

    // ---- the request --------------------------------------------------------------------

    function test_shift_enter_adds_stopped() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        sheet.openWith(testCase.freshMagnet, "clipboard");
        sheet.categoryChips.pick("linux");
        sheet.tagChips.pick("keep");
        sheet.upLimitField.text = "500";
        sheet.sequentialToggle.checked = true;

        keyClick(Qt.Key_Return, Qt.ShiftModifier);

        compare(added.count, 1);
        const request = added.signalArguments[0][0];
        compare(request.stopped, true);
        compare(request.kind, "magnet");
        compare(request.category, "linux");
        compare(request.tags, ["keep"]);
        compare(request.savePath, "/data/torrents/linux");
        compare(request.upLimitKb, 500);
        compare(request.sequentialDownload, true);
        compare(request.skipChecking, false);
        compare(request.contentLayout, "Original");
        compare(request.value, testCase.freshMagnet);
    }

    function test_enter_adds_running() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        sheet.openWith(testCase.freshMagnet, "drop");

        keyClick(Qt.Key_Return);

        compare(added.count, 1);
        compare(added.signalArguments[0][0].stopped, false);
        compare(sheet.sourceText, "from drop");
    }

    function test_nothing_is_added_from_an_empty_sheet() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        sheet.open();
        sheet.submit(false);
        compare(added.count, 0);
    }

    // ---- Fig. 10 ---------------------------------------------------------------------------

    function test_a_known_infohash_is_a_duplicate_not_an_add() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        const duplicate = createTemporaryObject(duplicateSpy, testCase, {
                                                    "target": sheet
                                                });

        const hash = testCase.knownHash();
        verify(hash.length > 0);
        sheet.openWith("magnet:?xt=urn:btih:" + hash + "&dn=already-here", "clipboard");
        sheet.submit(false);

        compare(added.count, 0);
        compare(duplicate.count, 1);
        compare(duplicate.signalArguments[0][0].hash, hash);
    }

    function test_the_duplicate_sheet_draws_the_row_and_shows_it() {
        const sheet = createTemporaryObject(duplicateSheetComponent, testCase);
        const hash = testCase.knownHash();
        const row = App.torrentRow(hash);

        sheet.openFor(row);
        compare(sheet.opened, true);
        compare(sheet.title, "Already on " + App.serverName);
        compare(sheet.subtitle, row.name);
        verify(sheet.bodyText.indexOf(row.stateText) >= 0);
        verify(sheet.bodyText.indexOf("qBittorrent rejects duplicates.") >= 0);

        let shown = "";
        sheet.showRequested.connect(hash => shown = hash);
        keyClick(Qt.Key_Return); // the primary action is `show it`
        compare(shown, hash);
        compare(sheet.opened, false);
    }

    // ---- the focus trap ---------------------------------------------------------------------

    function test_escape_closes_and_gives_the_focus_back() {
        const probe = createTemporaryObject(probeComponent, testCase);
        probe.forceActiveFocus();
        verify(probe.activeFocus);

        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        verify(!probe.activeFocus, "the sheet takes the keyboard");

        // Spec §7 Fig. 26's escape ladder: the first `esc` clears the field it is in …
        keyClick(Qt.Key_Escape);
        compare(sheet.sourceField.text, "");
        compare(sheet.opened, true);

        // … the next one closes the sheet.
        keyClick(Qt.Key_Escape);
        compare(sheet.opened, false);
        verify(probe.activeFocus, "and gives it back on close");
    }

    function test_a_queued_source_opens_after_the_first_one() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList([testCase.freshMagnet,
                        "magnet:?xt=urn:btih:0102030405060708090a0b0c0d0e0f1011121314&dn=second"],
                       "browser");

        compare(sheet.sourceField.text, testCase.freshMagnet);
        compare(sheet.source, "browser");
        compare(sheet.queue.length, 1);

        sheet.finish(true, "");
        compare(sheet.opened, true, "the sheet stays up for the queued one");
        compare(sheet.addable.name, "second");
        compare(sheet.queue.length, 0);

        sheet.finish(true, "");
        compare(sheet.opened, false, "and closes when the queue is empty");
    }

    function test_a_refused_add_stays_open_in_red() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        sheet.finish(false, "the server refused this torrent");
        compare(sheet.opened, true);
        compare(sheet.errorText, "the server refused this torrent");
    }

    function test_escape_drops_the_queue() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList([testCase.freshMagnet, testCase.freshMagnet], "drop");
        compare(sheet.queue.length, 1);
        keyClick(Qt.Key_Escape); // clears the field
        keyClick(Qt.Key_Escape); // closes the sheet
        compare(sheet.opened, false);
        compare(sheet.queue.length, 0);
    }
}
