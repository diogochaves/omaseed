pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
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

    // A short window (docs/11 D1): the sheet must still fit, and only the tree scrolls.
    Component {
        id: shortSheetComponent

        // 720 rather than 700 since #63's extras line (the fixture's README is one) takes a
        // caption line under the tree: the tree, at its four rows, still has to scroll.
        AddSheet {
            height: 720
            width: 1100
        }
    }

    Component {
        id: duplicateSheetComponent

        DuplicateSheet {
            anchors.fill: parent
        }
    }

    Component {
        id: freeLineComponent

        AddFreeLine {
            width: 600
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
        id: addFinishedSpy

        SignalSpy {
            signalName: "addFinished"
            target: App
        }
    }

    Component {
        id: duplicateSpy

        SignalSpy {
            signalName: "duplicateFound"
        }
    }

    Component {
        id: categoryPaletteSpy

        SignalSpy {
            signalName: "categoryPaletteRequested"
        }
    }

    Component {
        id: tagPaletteSpy

        SignalSpy {
            signalName: "tagPaletteRequested"
        }
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    /// The infohash of one torrent that really is in the demo library.
    function knownHash() {
        return App.filtered.hashAt(0);
    }

    // ---- In Downloads (manual box M7) ---------------------------------------------------

    /// tests/fixtures holds six .torrent files: the list shows five of them.
    function fixturesFolder() {
        return decodeURIComponent(Qt.resolvedUrl("../fixtures").toString().replace(/^file:\/\//,
                                                                                   ""));
    }

    /// An add sheet opened on an empty field, with Downloads pointed at the fixtures and the
    /// scan it started answered.
    function openOnFixtures() {
        App.downloadsFolder = testCase.fixturesFolder();
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith("", "");
        tryVerify(() => App.downloadsTorrents.length === 5, 10000, "the scan did not answer");
        return sheet;
    }

    // The harness's own Downloads is empty (tests/qml/CMakeLists.txt): no list, and nothing
    // of the next test's is left behind.
    function cleanup() {
        App.setLookupTestClock(-1);
        App.stopMetadataLookup();
        App.setDemoDiskFolder("");
        App.downloadsFolder = "";
        App.scanDownloads();
        tryVerify(() => App.downloadsTorrents.length === 0, 10000);
    }

    function test_downloads_are_listed_under_an_empty_field() {
        const sheet = testCase.openOnFixtures();
        compare(sheet.showDownloads, true);
        compare(sheet.downloadsList.visible, true);
        compare(sheet.downloadsList.count, 5);
        for (let i = 0; i < 5; ++i) {
            const entry = App.downloadsTorrents[i];
            verify(entry.path.endsWith(".torrent"), entry.path);
            verify(entry.name.length > 0);
            verify(entry.sizeText.length > 0);
            verify(entry.ageText.length > 0);
        }
    }

    function test_an_empty_downloads_folder_shows_no_list() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith("", "");
        compare(sheet.showDownloads, false);
        compare(sheet.downloadsList.visible, false);
    }

    // A hand-over to a sheet that is already up (the widget's `m`, the tray's add, a second
    // xdg-open) looks at Downloads again: "in library" may have changed since it opened (#20).
    function test_a_hand_over_to_an_open_sheet_scans_downloads_again() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith("", "");
        compare(App.downloadsTorrents.length, 0);
        App.downloadsFolder = testCase.fixturesFolder();
        sheet.openBlank();
        tryVerify(() => App.downloadsTorrents.length === 5, 10000, "openBlank did not scan");

        App.downloadsFolder = "";
        sheet.openList([testCase.freshMagnet], "handoff");
        tryVerify(() => App.downloadsTorrents.length === 0, 10000, "openList did not scan");
        compare(sheet.opened, true);
    }

    function test_the_list_hides_once_the_field_holds_something() {
        const sheet = testCase.openOnFixtures();
        sheet.sourceField.text = testCase.freshMagnet;
        compare(sheet.showDownloads, false);
        sheet.sourceField.text = "";
        compare(sheet.showDownloads, true);
    }

    function test_enter_on_a_row_fills_the_field_like_the_file_chooser() {
        const sheet = testCase.openOnFixtures();
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        sheet.downloadsList.forceActiveFocus();
        keyClick("j");
        compare(sheet.downloadsList.cursorIndex, 1);
        keyClick("k");
        keyClick("j");
        const path = App.downloadsTorrents[1].path;

        keyClick(Qt.Key_Return);
        compare(added.count, 0, "enter on the list ran `add`");
        compare(sheet.sourceField.text, path);
        compare(sheet.source, "file");
        compare(sheet.showDownloads, false);
        tryVerify(() => sheet.addable !== null && sheet.addable.pending !== true
                  && sheet.addable.valid === true, 10000);
        compare(sheet.addable.value, path);
        verify(sheet.sourceField.editing, "the keyboard did not go back to the field");
        compare(sheet.opened, true);
    }

    function test_a_click_on_a_row_fills_the_field() {
        const sheet = testCase.openOnFixtures();
        const path = App.downloadsTorrents[0].path;
        mouseClick(findChild(sheet.downloadsList, "downloadsRow0"));
        compare(sheet.sourceField.text, path);
        compare(sheet.source, "file");
    }

    function test_tab_reaches_the_list_from_the_field() {
        const sheet = testCase.openOnFixtures();
        verify(sheet.sourceField.editing);
        keyClick(Qt.Key_Tab);
        verify(sheet.downloadsList.activeFocus, "tab skipped the list");
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
        // docs/33 D20: with one server nothing names it, the foot included.
        compare(sheet.note, "");
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

    // ---- a bare info hash (#147) -------------------------------------------------------

    /// A v1 and a v2 hash nobody in the demo library has (nor `freshMagnet`).
    readonly property string freshHash: "1f3e5d7c9b0a2f4e6d8c0b1a3f5e7d9c2b4a6f8e"
    readonly property string freshHashV2: "2e4c6a8f0d1b3e5c7a9f2d4b6e8c0a1f3d5b7e9c4a6f8d0b2e1c3a5f7d9b0e2c"

    function test_a_clipboard_hash_is_not_offered_but_a_paste_takes_it() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        // A commit id copied for something else looks the same: opening never offers one.
        sheet.clipboardAddable = testCase.freshHash.toUpperCase();
        sheet.openBlank();
        compare(sheet.sourceField.text, "");
        compare(sheet.addable, null);

        // Pasted into the field (it arrives whole), the same text is taken.
        sheet.sourceField.text = testCase.freshHash.toUpperCase();
        verify(sheet.addable !== null);
        compare(sheet.addable.kind, "magnet");
        compare(sheet.addable.bareHash, true);
        compare(sheet.addable.value, "magnet:?xt=urn:btih:" + testCase.freshHash);
        compare(sheet.request(false).value, "magnet:?xt=urn:btih:" + testCase.freshHash);
        // The head names it as one: that is what tells it from a magnet.
        compare(sheet.sourceText, "from an info hash");
        compare(sheet.subtitle, testCase.freshHash + " · from an info hash");
    }

    function test_a_pasted_v2_hash_is_added_as_its_magnet() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();
        sheet.sourceField.text = testCase.freshHashV2;
        compare(sheet.addable.value, "magnet:?xt=urn:btmh:1220" + testCase.freshHashV2);
        compare(sheet.addable.infoHashV2, testCase.freshHashV2);
        compare(sheet.sourceText, "from an info hash");
    }

    function test_a_clipboard_sentence_with_a_hash_is_not_offered() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.clipboardAddable = "fixed in " + testCase.freshHash;
        sheet.openBlank();
        compare(sheet.sourceField.text, "");
        compare(sheet.addable, null);
    }

    function test_hashes_and_magnets_pasted_together_are_a_batch() {
        const pasted = createTemporaryObject(addSheetComponent, testCase);
        pasted.open();
        pasted.sourceField.text = testCase.freshHash + " " + testCase.freshMagnet;
        compare(pasted.batchMode, true);
        compare(pasted.batchCount, 2);
        compare(pasted.batch[0].map.bareHash, true);

        // Dropped (or handed over) one per line.
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList(App.splitAddables(testCase.freshHash + "\n" + testCase.freshMagnet + "\n"
                                         + testCase.freshHashV2), "drop");
        compare(sheet.batchMode, true);
        compare(sheet.batchCount, 3);
        compare(sheet.batch[0].map.bareHash, true);
        compare(sheet.batch[1].map.bareHash, false);
        compare(sheet.batch[2].map.value, "magnet:?xt=urn:btmh:1220" + testCase.freshHashV2);
    }

    function test_a_bare_hash_on_a_server_without_dht_says_so() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const line = findChild(sheet, "addNoDhtLine");
        compare(App.serverPrefs.values.dht, true);
        sheet.open();
        sheet.sourceField.text = testCase.freshHash;
        verify(sheet.addable.bareHash);
        verify(!line.visible);

        App.serverPrefs.apply({
            "dht": false
        });
        try {
            verify(line.visible);
            verify(line.text.indexOf("DHT is off") === 0, line.text);
            // A magnet has its own trackers to say where its peers are.
            sheet.loadText(testCase.freshMagnet, "");
            verify(!line.visible);
            sheet.loadText(testCase.freshHash, "");
            verify(line.visible);
        } finally {
            App.serverPrefs.apply({
                "dht": true
            });
        }
        verify(!line.visible);
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
        compare(sheet.savePathField.trailing, "from category · browse");
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
        compare(sheet.savePathField.trailing, "browse");
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

    // ---- the `…` chips and T4.4's palettes (T4.8) ---------------------------------------

    function test_the_more_chip_asks_for_the_palette_and_reveals_no_field() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const wanted = createTemporaryObject(categoryPaletteSpy, testCase, {
                                                 "target": sheet
                                             });
        sheet.open();
        // The `…` chip is the last one in the row.
        sheet.categoryChips.cursorIndex = sheet.categoryChips.options.length;
        sheet.categoryChips.activateCursor();
        compare(wanted.count, 1);
        // The plain-Field fallback is gone: the sheet has exactly the rows of Fig. 5.
        compare(sheet.chosenCategory(), "");
    }

    function test_the_tags_more_chip_asks_for_the_tag_palette() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const wanted = createTemporaryObject(tagPaletteSpy, testCase, {
                                                 "target": sheet
                                             });
        sheet.open();
        sheet.tagChips.cursorIndex = sheet.tagChips.options.length;
        sheet.tagChips.activateCursor();
        compare(wanted.count, 1);
    }

    function test_a_category_chosen_in_the_palette_becomes_the_chip() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();
        sheet.applyChosenCategory("movies");
        compare(sheet.categoryChips.current, "movies");
        compare(sheet.chosenCategory(), "movies");
        compare(sheet.savePathField.text, "/data/torrents/movies");
    }

    function test_a_category_created_in_the_palette_becomes_a_new_chip() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();
        const before = sheet.categoryChips.options.length;

        sheet.applyChosenCategory("anime");
        compare(sheet.categoryChips.options.length, before + 1);
        verify(sheet.categoryChips.options.indexOf("anime") >= 0,
               "the created name is not a chip");
        compare(sheet.categoryChips.current, "anime");
        compare(sheet.chosenCategory(), "anime");

        // And it is gone again on the next sheet.
        sheet.reset();
        compare(sheet.categoryChips.options.length, before);
    }

    function test_tags_chosen_in_the_palette_become_the_chips() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();
        const before = sheet.tagChips.options.length;

        // "keep" is one of the demo library's tags; "anime" is not, so the palette created it.
        sheet.applyChosenTags(["keep", "anime"]);
        compare(sheet.tagChips.selected, ["keep", "anime"]);
        compare(sheet.chosenTags(), ["keep", "anime"]);
        compare(sheet.tagChips.options.length, before + 1);
        verify(sheet.tagChips.options.indexOf("anime") >= 0);
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
        // docs/25 D1 a: known as soon as it loads, before any `add`.
        sheet.openWith("magnet:?xt=urn:btih:" + hash + "&dn=already-here&tr=udp%3A%2F%2Ft.example%3A1%2Fannounce",
                       "handoff");

        compare(added.count, 0);
        compare(duplicate.count, 1);
        compare(duplicate.signalArguments[0][0].hash, hash);
        compare(duplicate.signalArguments[0][1].trackers, ["udp://t.example:1/announce"]);
        compare(duplicate.signalArguments[0][1].isPrivate, false);
        // The form never shows for it: Fig. 10 takes its place.
        compare(sheet.opened, false);
    }

    function test_a_queued_duplicate_gives_way_to_the_next_source() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const duplicate = createTemporaryObject(duplicateSpy, testCase, {
                                                    "target": sheet
                                                });
        const known = "magnet:?xt=urn:btih:" + testCase.knownHash() + "&dn=already-here";
        sheet.openList([known, testCase.freshMagnet], "drop");
        // #40: together they are a batch, the known one left out of it…
        compare(sheet.batchMode, true);
        compare(sheet.batchRows[0].state, "library");
        compare(sheet.batchCount, 1);
        compare(duplicate.count, 0);
        // …and one by one, it gives way to Fig. 10 as before.
        sheet.oneByOne();

        compare(duplicate.count, 1);
        compare(sheet.opened, true);
        compare(sheet.sourceField.text, testCase.freshMagnet);
        compare(sheet.queue.length, 0);
    }

    function test_typing_a_duplicate_waits_for_add_but_a_paste_does_not() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const duplicate = createTemporaryObject(duplicateSpy, testCase, {
                                                    "target": sheet
                                                });
        const known = "magnet:?xt=urn:btih:" + testCase.knownHash() + "&dn=typed";
        sheet.openBlank();
        sheet.clipboardAddable = "";
        sheet.sourceField.text = "";
        // One key at a time: the hash completes halfway, and nothing happens yet.
        for (let i = 1; i <= known.length; ++i)
            sheet.sourceField.text = known.slice(0, i);
        compare(sheet.opened, true);
        compare(duplicate.count, 0);
        sheet.submit(false);
        compare(duplicate.count, 1);
        compare(sheet.opened, false);

        // A paste arrives whole.
        sheet.openBlank();
        sheet.sourceField.text = known;
        compare(duplicate.count, 2);
        compare(sheet.opened, false);
    }

    function test_a_duplicate_left_on_the_clipboard_is_not_offered() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const duplicate = createTemporaryObject(duplicateSpy, testCase, {
                                                    "target": sheet
                                                });
        sheet.clipboardAddable = "magnet:?xt=urn:btih:" + testCase.knownHash() + "&dn=old";
        sheet.openBlank();

        compare(sheet.opened, true);
        compare(sheet.sourceField.text, "");
        compare(duplicate.count, 0);
    }

    function test_the_duplicate_sheet_offers_to_merge_new_trackers() {
        const sheet = createTemporaryObject(duplicateSheetComponent, testCase);
        const hash = testCase.knownHash();
        const row = App.torrentRow(hash);
        verify(row.isPrivate !== true, "the demo library is public");
        const urls = ["udp://tracker.opentrackr.example:1337/announce",
                      "udp://open.demonii.example:1337/announce"];

        sheet.openFor(row, {
                          "trackers": urls,
                          "isPrivate": false
                      });
        // The demo answers at once: the torrent has none of them.
        compare(sheet.missingTrackers, urls);
        compare(sheet.canMerge, true);
        compare(sheet._mergeText, "merge 2 trackers");
        compare(sheet.mergeHeadText, "This source lists 2 trackers it doesn't have.");
        compare(sheet.bodyText.indexOf("qBittorrent rejects duplicates."), -1);

        let merged = null;
        sheet.mergeRequested.connect((h, list) => merged = {
                                         "hash": h,
                                         "urls": list
                                     });
        tryVerify(() => sheet.primaryButton() && sheet.primaryButton().activeFocus, 2000,
                  "the keyboard is on merge");
        keyClick(Qt.Key_Return); // D2 a: merge is the primary action
        compare(sheet.opened, false);
        compare(merged.hash, hash);
        compare(merged.urls, urls);
    }

    function test_a_duplicate_without_metadata_is_not_merged_yet() {
        const sheet = createTemporaryObject(duplicateSheetComponent, testCase);
        // A magnet still fetching its metadata: qBittorrent reports it public either way.
        sheet.openFor({
                          "hash": "0123456789abcdef0123456789abcdef01234567",
                          "name": "tiny-notes.txt",
                          "stateText": "metadata",
                          "addedOnText": "added today 12:00",
                          "isPrivate": false,
                          "hasMetadata": false
                      }, {
                          "trackers": ["udp://other.example:1/announce"],
                          "isPrivate": false
                      });
        compare(sheet.metadataPending, true);
        compare(sheet.canMerge, false);
        compare(sheet.missingTrackers.length, 0, "the server is not even asked");
        verify(sheet.privateText.indexOf("metadata hasn't arrived") >= 0);
        sheet.close();
    }

    function test_a_private_duplicate_is_never_merged() {
        const sheet = createTemporaryObject(duplicateSheetComponent, testCase);
        const hash = testCase.knownHash();
        sheet.openFor(App.torrentRow(hash), {
                          "trackers": ["udp://other.example:1/announce"],
                          "isPrivate": true
                      });
        compare(sheet.isPrivate, true);
        compare(sheet.canMerge, false);
        compare(sheet.missingTrackers.length, 0, "the server is not even asked");
        compare(sheet.actions.length, 2);

        let shown = "";
        sheet.showRequested.connect(h => shown = h);
        keyClick(Qt.Key_Return); // `show it` stays the primary action
        compare(shown, hash);
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

        // … the next one puts the keyboard on `cancel` (the sheet is guarded, manual box
        // M5) …
        keyClick(Qt.Key_Escape);
        compare(sheet.opened, true);
        compare(sheet.focusables()[sheet.focusedIndex()], sheet.cancelButton());

        // … and `esc` on `cancel` closes it.
        keyClick(Qt.Key_Escape);
        compare(sheet.opened, false);
        verify(probe.activeFocus, "and gives it back on close");
    }

    // Manual box M5: nothing but `cancel` throws a half-filled add away. A click on the scrim
    // does nothing, and neither does a click on the panel's own ground.
    function test_a_click_outside_the_sheet_keeps_it_open() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        mouseClick(sheet, 4, 4);
        mouseClick(sheet, sheet.width - 4, sheet.height - 4);
        mouseClick(sheet.panel, 4, 4);
        compare(sheet.opened, true);
        compare(sheet.sourceField.text, testCase.freshMagnet);
    }

    // Manual box M6: `browse` is a button in the field, next to its key. The click itself
    // opens the portal's chooser, which a test cannot answer, so only the wiring is checked.
    function test_the_source_field_has_a_browse_button() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith("", "");
        compare(sheet.sourceField.trailing, "browse");
        compare(sheet.sourceField.trailingKey, "ctrl+o");
        compare(sheet.sourceField.trailingClickable, true);
        verify(findChild(sheet.sourceField, "fieldTrailingButton").enabled);
    }

    // #41 (docs/27 L4): Save to has a browse button too; the pick fills the field and, being
    // a path of your own, turns automatic management off as typing does.
    function test_save_to_browse_fills_the_field() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.open();
        compare(sheet.savePathField.trailing, "browse");
        compare(sheet.savePathField.trailingClickable, true);
        verify(findChild(sheet.savePathField, "fieldTrailingButton").enabled);
        sheet.categoryChips.pick("movies");
        compare(sheet.autoTMM, true);
        compare(sheet.savePathField.trailing, "from category · browse");

        const starts = [];
        sheet.pickDirectory = start => {
            starts.push(start);
            return "/data/elsewhere";
        };
        sheet.savePathField.trailingClicked();
        compare(starts, ["/data/torrents/movies"]);
        compare(sheet.savePathField.text, "/data/elsewhere");
        compare(sheet.autoTMM, false);
        compare(sheet.saveBrowsedLine.visible, false);

        sheet.pickDirectory = start => "";
        sheet.savePathField.trailingClicked();
        compare(sheet.savePathField.text, "/data/elsewhere");
    }

    function test_the_cancel_button_still_closes_it() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        waitForRendering(sheet); // the foot's Flow places its buttons when it lays out
        mouseClick(sheet.cancelButton());
        compare(sheet.opened, false);
    }

    function test_a_queued_source_opens_after_the_first_one() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList([testCase.freshMagnet,
                        "magnet:?xt=urn:btih:0102030405060708090a0b0c0d0e0f1011121314&dn=second"],
                       "handoff");
        sheet.oneByOne(); // #40: a batch, turned into the queue

        compare(sheet.sourceField.text, testCase.freshMagnet);
        compare(sheet.source, "handoff");
        compare(sheet.queue.length, 1);

        sheet.finish(true, "");
        compare(sheet.opened, true, "the sheet stays up for the queued one");
        compare(sheet.addable.name, "second");
        compare(sheet.queue.length, 0);

        sheet.finish(true, "");
        compare(sheet.opened, false, "and closes when the queue is empty");
    }

    // Review VIEW-18: the limit fields read as the Limits sheets read theirs.
    function test_a_limit_that_is_not_a_number_is_refused() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        compare(sheet._limitKb(""), 0);
        compare(sheet._limitKb("1.5"), 2);
        compare(sheet._limitKb("300"), 300);
        verify(Number.isNaN(sheet._limitKb("abc")));
    }

    // Review RUN-8: enter on text that is not addable says why instead of doing nothing.
    function test_enter_on_something_that_is_not_addable_says_why() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openBlank();
        sheet.sourceField.text = "magnet:?xt=urn:btih:zzzz";
        sheet.submit(false);
        verify(sheet.errorText.length > 0, "a bad magnet was ignored silently");
        compare(sheet.opened, true);
        sheet.sourceField.text = "/etc/passwd";
        compare(sheet.errorText, "", "the error outlived the edit");
        sheet.submit(false);
        verify(sheet.errorText.length > 0);
        sheet.dismiss();
    }

    // Review VIEW-11: the Name row is the torrent's own; the next queued one starts without it.
    function test_the_next_queued_source_does_not_inherit_the_name() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList([testCase.freshMagnet,
                        "magnet:?xt=urn:btih:0102030405060708090a0b0c0d0e0f1011121314&dn=second"],
                       "handoff");
        sheet.oneByOne();
        sheet.renameField.text = "my custom name";
        sheet.finish(true, "");
        compare(sheet.addable.name, "second");
        compare(sheet.renameField.text, "", "the second torrent kept the first one's name");
    }

    // Review VIEW-11: a hand-off while the form is up queues instead of wiping it.
    function test_a_hand_off_while_open_joins_the_queue() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        sheet.renameField.text = "half typed";
        sheet.openList(["magnet:?xt=urn:btih:0102030405060708090a0b0c0d0e0f1011121314&dn=second"],
                       "handoff");
        compare(sheet.sourceField.text, testCase.freshMagnet, "the open form was replaced");
        compare(sheet.renameField.text, "half typed");
        compare(sheet.queue.length, 1);
        sheet.openBlank();
        compare(sheet.renameField.text, "half typed", "`a` while open wiped the form");
    }

    // #124 (DECISIONS C20): one add at a time. A second press while the first is out sends
    // nothing; qBittorrent's OK closes the sheet.
    function test_a_second_press_while_adding_sends_nothing() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        sheet.openWith(testCase.freshMagnet, "handoff");
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Return);
        sheet.submit(true);
        sheet.triggerPrimary();
        compare(added.count, 1, "a second press sent the torrent again");
        compare(sheet.opened, true, "the sheet waits for the answer");

        sheet.finish(true, "");
        compare(sheet.opened, false, "qBittorrent's OK closes it");
    }

    // #124: a wait the eye can see says so on the button; the answer gives `add` back.
    function test_the_button_says_adding_while_the_add_is_out() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "handoff");
        sheet.oneByOne();
        compare(sheet._addText, "add");
        sheet.submit(false);
        compare(sheet._addText, "add", "no flash for an answer that comes at once");
        tryCompare(sheet, "_addText", "adding…", 2000);
        sheet.finish(false, "the server refused this torrent");
        compare(sheet._addText, "add");
        compare(sheet.opened, true);
        // Refused: the form is still there to send again.
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        sheet.submit(false);
        compare(added.count, 1);
    }

    // #124: a hand-off while the add is out is the next torrent, in the queue, not part of the
    // one leaving (a batch made then would close with its answer, and be lost).
    function test_a_hand_off_while_adding_waits_for_the_answer() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        sheet.openWith(testCase.freshMagnet, "handoff");
        sheet.submit(false);
        compare(added.count, 1);
        sheet.openList(["magnet:?xt=urn:btih:0102030405060708090a0b0c0d0e0f1011121314&dn=second"],
                       "handoff");
        compare(sheet.batchMode, false);
        compare(sheet.queue.length, 1);
        compare(sheet.sourceField.text, testCase.freshMagnet);

        sheet.finish(true, "");
        compare(sheet.opened, true, "the next one is up");
        compare(sheet.addable.name, "second");
        sheet.submit(false);
        compare(added.count, 2);
        compare(added.signalArguments[1][0].name, "second");
        sheet.finish(true, "");
        compare(sheet.opened, false);
    }

    function test_a_refused_add_stays_open_in_red() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        sheet.finish(false, "the server refused this torrent");
        compare(sheet.opened, true);
        compare(sheet.errorText, "the server refused this torrent");
    }

    // ---- docs/11 D1: rename, download limit, first/last pieces, the file picker -----------

    /// Opens the sheet on the demo's eight-file `.torrent` and waits for its parse.
    function openTorrent(component) {
        const sheet = createTemporaryObject(component, testCase);
        const path = App.demoTorrentPath();
        verify(path.length > 0, "demo mode writes the example .torrent");
        sheet.openWith(path, "file");
        tryCompare(sheet, "showFiles", true, 5000);
        return sheet;
    }

    /// The tree row whose `name` is `name`, or -1.
    function rowNamed(sheet, name) {
        const view = sheet.filesTree.view;
        for (let row = 0; row < view.count; ++row) {
            view.currentIndex = row;
            if (view.currentItem && view.currentItem.name === name)
                return row;
        }
        return -1;
    }

    function test_the_new_fields_travel_with_the_add() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        sheet.openWith(testCase.freshMagnet, "clipboard");
        // The name's placeholder is the parsed name: empty keeps it.
        compare(sheet.renameField.placeholder, "Fedora-Workstation-Live-44-1.6.x86_64.iso");
        sheet.renameField.text = "Fedora 44";
        sheet.dlLimitField.text = "2048";
        sheet.firstLastToggle.checked = true;

        keyClick(Qt.Key_Return);

        compare(added.count, 1);
        const request = added.signalArguments[0][0];
        compare(request.rename, "Fedora 44");
        compare(request.dlLimitKb, 2048);
        compare(request.firstLastPiecePrio, true);
        compare(request.filePriorities, []);
    }

    function test_a_magnet_has_no_file_picker() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        compare(sheet.showFiles, false);
        compare(sheet.filesTree.visible, false);
    }

    // ---- a magnet's files before the add (docs/29, #44) ---------------------------------
    // The demo lookup answers the demo .torrent's magnet with its files and looks for ever for
    // anything else; App.setLookupTestClock walks the board's timings.

    /// The board's minute, frozen at `ms` after the lookup started at 0.
    function lookupAt(ms) {
        App.setLookupTestClock(ms);
    }

    // D1 a, F2: the magnet's list comes in and the tree is a .torrent's: the head's size, the
    // free line's need, the Name placeholder. The keyboard stays in the field.
    function test_a_magnets_files_show_once_its_metadata_is_in() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(App.demoMagnet(), "handoff");
        sheet.sourceField.focusInput();
        tryVerify(() => sheet.showFiles, 3000, "the demo lookup did not answer");
        compare(App.addLookup.phase, "done");
        compare(App.addFiles.fileCount, 8);
        compare(sheet.filesTree.visible, true);
        compare(sheet.fetchLine.visible, false);
        compare(sheet.addable.kind, "magnet");
        verify(sheet.addable.token.indexOf("meta-") === 0, sheet.addable.token);
        compare(sheet.subtitle.indexOf("Blender Open Movies · "), 0);
        verify(sheet.subtitle.indexOf("from magnet link") > 0, sheet.subtitle);
        compare(sheet.freeLine.needsText.indexOf("needs "), 0);
        compare(sheet.renameField.placeholder, "Blender Open Movies");
        compare(sheet.filesTree.activeFocus, false); // the tree does not take the keyboard
    }

    // F2: the choices made in a magnet's tree travel with the add, as a .torrent's do.
    function test_a_magnets_file_choices_travel_with_the_add() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        sheet.openWith(App.demoMagnet(), "handoff");
        tryVerify(() => sheet.showFiles, 3000);
        App.addFiles.toggleWanted(testCase.rowNamed(sheet, "Sintel/"));
        sheet.submit(false);
        compare(added.count, 1);
        const request = added.signalArguments[0][0];
        compare(request.kind, "magnet");
        compare(request.value, App.demoMagnet());
        compare(request.filePriorities, [1, 1, 1, 0, 0, 1, 1, 1]);
        compare(request.untilMetadata, false);
    }

    // D2 a, D3 a: nothing under 0.5 s, then fetching, still looking at 10 s, may never come at
    // 60 s (yellow), the seconds in the Files head. The rest of the form works throughout.
    function test_the_wait_goes_through_the_boards_states() {
        testCase.lookupAt(0);
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        compare(App.addLookup.phase, "quiet");
        compare(App.addLookup.source, testCase.freshMagnet);
        compare(sheet.fetchLine.visible, false);

        testCase.lookupAt(3200);
        compare(App.addLookup.phase, "fetching");
        compare(sheet.fetchLine.visible, true);
        compare(sheet.fetchLine.summaryText, "fetching · 3 s");
        compare(sheet.fetchLine.headline, "Asking qBittorrent for the file list");
        compare(sheet.showFiles, false);

        testCase.lookupAt(24000);
        compare(sheet.fetchLine.summaryText, "still looking · 24 s");
        compare(sheet.fetchLine.headline, "No peer has sent the file list yet");
        compare(sheet.fetchLine.glyph, Glyphs.lookupSlow);

        testCase.lookupAt(72000);
        compare(sheet.fetchLine.summaryText, "no list after 1 min 12 s");
        compare(sheet.fetchLine.summaryWarn, true);
        compare(sheet.fetchLine.lines.length, 2);
        verify(sheet.fetchLine.lines[1].indexOf("even after you close this sheet") > 0);
        // The free line still waits for the size.
        compare(sheet.freeLine.needsText, "needs 2.34 GB");
    }

    // D4 a and D5 a: before the list, add takes every file; add stopped stops it once its list
    // arrives.
    function test_add_before_the_list_is_today_s_add() {
        testCase.lookupAt(0);
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        sheet.openWith(testCase.freshMagnet, "clipboard");
        testCase.lookupAt(15000);
        sheet.submit(true);
        compare(added.count, 1);
        let request = added.signalArguments[0][0];
        compare(request.stopped, true);
        compare(request.untilMetadata, true);
        compare(request.filePriorities, []);
        sheet.finish(false, "refused"); // #124: one add at a time; a refusal frees the button
        sheet.submit(false);
        request = added.signalArguments[1][0];
        compare(request.stopped, false);
        compare(request.untilMetadata, false);
    }

    // D6 a: closing lets the lookup go; only the polling stops.
    function test_closing_lets_the_lookup_go() {
        testCase.lookupAt(0);
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        compare(App.addLookup.phase, "quiet");
        sheet.dismiss();
        compare(App.addLookup.phase, "idle");
    }

    // D7 a, F5: the demo library's films come from tracker.example.org, a private tracker: its
    // magnet is not looked up, and add stopped is a plain stopped add.
    function test_a_private_trackers_magnet_is_not_looked_up() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        sheet.openWith(testCase.freshMagnet + "&tr=udp%3A%2F%2Ftracker.example.org%3A1337%2Fannounce",
                       "clipboard");
        compare(App.addLookup.phase, "private");
        compare(sheet.fetchLine.visible, true);
        compare(sheet.fetchLine.summaryText, "private tracker");
        verify(sheet.fetchLine.lines[0].indexOf("tracker.example.org") > 0, sheet.fetchLine.lines[0]);
        sheet.submit(true);
        compare(added.signalArguments[0][0].untilMetadata, false);
    }

    // D10 a: no lookups in a batch; docs/25 D1 a: a duplicate goes to Fig. 10 before any.
    function test_a_batch_or_a_duplicate_is_not_looked_up() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        compare(App.addLookup.phase === "idle", false);
        sheet.openList(["magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=b"], "handoff");
        compare(sheet.batchMode, true);
        compare(App.addLookup.phase, "idle");
        compare(sheet.fetchLine.visible, false);

        const other = createTemporaryObject(addSheetComponent, testCase);
        other.openWith("magnet:?xt=urn:btih:" + testCase.knownHash(), "clipboard");
        compare(App.addLookup.phase, "idle");
    }

    function test_a_torrent_file_shows_its_tree() {
        const sheet = testCase.openTorrent(addSheetComponent);
        compare(App.addFiles.fileCount, 8);
        compare(sheet.filesTree.visible, true);
        // Six folders (four films, two subs) and eight files, every folder open.
        compare(sheet.filesTree.count, 14);
        compare(App.addFiles.summaryText.indexOf("8 files · "), 0);
        compare(sheet.subtitle.indexOf("Blender Open Movies · "), 0);
        compare(sheet.sourceText, "from file");
    }

    // #63 (DECISIONS C28): its README is an extra: a dim line under the tree, a dim "extra" on
    // its row, and nothing unticked.
    function test_extras_are_said_under_the_tree() {
        const sheet = testCase.openTorrent(addSheetComponent);
        compare(App.addFiles.wantedCount, 8);
        const line = findChild(sheet, "addExtrasLine");
        verify(line.visible);
        compare(line.text.indexOf("1 extra: a text file ("), 0);
        verify(line.text.endsWith(" Ticked, so the torrent seeds whole."), line.text);
        const readme = testCase.rowItem(sheet, "README.txt");
        verify(findChild(readme, "addFilesExtra").visible);
        const film = testCase.rowItem(sheet, "Sintel/");
        verify(!findChild(film, "addFilesExtra").visible);
    }

    function test_the_tree_is_keyboard_driven() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const tree = sheet.filesTree;
        tree.forceActiveFocus();
        compare(tree.activeFocus, true);
        // C24 H10: the foot teaches no keys; the `?` sheet's block "in a sheet" does.
        compare(findChild(sheet, "keyHint_j k"), null);

        keyClick("j");
        compare(tree.cursorIndex, 1);
        keyClick(Qt.Key_Space); // skip bbb_sunflower_2160p_60fps.mp4
        compare(App.addFiles.wantedCount, 7);
        compare(App.addFiles.priority(1), 0);

        keyClick("h"); // up to Big Buck Bunny/
        compare(tree.cursorIndex, 0);
        keyClick("h"); // and fold it
        compare(App.addFiles.isExpanded(0), false);
        compare(tree.count, 10);
        keyClick("l");
        compare(tree.count, 14);

        keyClick("k");
        compare(tree.cursorIndex, 0);
        keyClick("+"); // the whole folder: mixed -> high
        compare(App.addFiles.priority(0), 6);
        keyClick("x");
        compare(App.addFiles.priority(0), 0);
        keyClick(Qt.Key_Space); // all skipped -> back to what it was (docs/27 D3): high
        compare(App.addFiles.priority(0), 6);
        keyClick("-");
        compare(App.addFiles.priority(0), 1);
    }

    // ---- manual box M17: the tree with the pointer -----------------------------------

    /// The delegate of the row named `name`, scrolled into view.
    function rowItem(sheet, name) {
        const row = testCase.rowNamed(sheet, name);
        verify(row >= 0, name);
        const view = sheet.filesTree.view;
        view.positionViewAtIndex(row, ListView.Contain);
        view.currentIndex = row;
        waitForRendering(view);
        return view.currentItem;
    }

    function test_a_click_on_the_check_mark_checks_and_unchecks() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const row = testCase.rowItem(sheet, "Sintel/");
        const mark = findChild(row, "addFilesMark");
        verify(mark, "the check mark is a click target");
        mouseClick(mark);
        compare(App.addFiles.wantedCount, 6);
        compare(sheet.filesTree.cursorIndex, row.index);
        verify(sheet.filesTree.activeFocus, "a click puts the keyboard in the tree, as before");
        mouseClick(mark);
        compare(App.addFiles.wantedCount, 8);
        // Two quick clicks are two toggles, not the row's double click.
        mouseDoubleClickSequence(mark);
        compare(App.addFiles.wantedCount, 8);
    }

    function test_a_click_on_a_folder_arrow_folds_and_unfolds() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const row = testCase.rowItem(sheet, "Big Buck Bunny/");
        const fold = findChild(row, "addFilesFold");
        verify(fold && fold.enabled);
        mouseClick(fold);
        compare(sheet.filesTree.count, 10);
        // The fold rebuilt the rows: find the arrow again.
        mouseClick(findChild(testCase.rowItem(sheet, "Big Buck Bunny/"), "addFilesFold"));
        compare(sheet.filesTree.count, 14);
        const file = testCase.rowItem(sheet, "README.txt");
        compare(findChild(file, "addFilesFold").enabled, false);
    }

    function test_a_click_on_the_priority_cycles_it() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const row = testCase.rowItem(sheet, "README.txt");
        const priority = findChild(row, "addFilesPriority");
        verify(priority);
        mouseClick(priority);
        compare(App.addFiles.priority(row.index), 6);
        mouseClick(priority);
        compare(App.addFiles.priority(row.index), 7);
        mouseClick(priority);
        compare(App.addFiles.priority(row.index), 1);
        compare(App.addFiles.wantedCount, 8);
    }

    function test_a_click_on_the_name_only_moves_the_cursor() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const row = testCase.rowItem(sheet, "Sintel/");
        mouseClick(row, row.width / 2, row.height / 2);
        compare(sheet.filesTree.cursorIndex, row.index);
        compare(App.addFiles.wantedCount, 8);
        compare(App.addFiles.isExpanded(row.index), true);
    }

    // docs/27 D1: the header row's tick is every file: all ticked, then none, then all.
    function test_the_head_tick_ticks_every_file_then_none() {
        const sheet = testCase.openTorrent(addSheetComponent);
        verify(!findChild(sheet, "addFilesAll") && !findChild(sheet, "addFilesNone"),
               "the dim words are gone");
        const head = findChild(sheet, "addFilesHeadTick");
        verify(head, "the head tick");
        compare(head.wanted, 2);
        mouseClick(head);
        compare(App.addFiles.wantedCount, 0);
        compare(head.wanted, 0);
        verify(sheet.filesTree.activeFocus, "a click puts the keyboard in the tree");
        mouseClick(head);
        compare(App.addFiles.wantedCount, 8);
        // Mixed reads "–", and a click fills it.
        App.addFiles.toggleWanted(testCase.rowNamed(sheet, "Sintel/"));
        compare(head.wanted, 1);
        mouseClick(head);
        compare(App.addFiles.wantedCount, 8);
    }

    // docs/27 D2: ctrl+a in the tree does the same, and the hint line names it.
    function test_ctrl_a_ticks_every_file_then_none() {
        const sheet = testCase.openTorrent(addSheetComponent);
        testCase.focusRow(sheet, "README.txt");
        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(App.addFiles.wantedCount, 0);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(App.addFiles.wantedCount, 8);
    }

    // docs/27 D3: a folder skipped and ticked again keeps the priority it had.
    function test_ticking_back_keeps_the_priority() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const row = testCase.focusRow(sheet, "README.txt");
        keyClick("+");
        compare(App.addFiles.priority(row), 6);
        keyClick(Qt.Key_Space);
        compare(App.addFiles.priority(row), 0);
        keyClick(Qt.Key_Space);
        compare(App.addFiles.priority(row), 6);
    }

    // ---- manual box M18: renaming before the add ------------------------------------

    /// The tree with the keyboard, its cursor on `name`.
    function focusRow(sheet, name) {
        const tree = sheet.filesTree;
        const row = testCase.rowNamed(sheet, name);
        verify(row >= 0, name);
        tree.forceActiveFocus();
        tree.cursorIndex = row;
        return row;
    }

    function renameInput(sheet) {
        const view = sheet.filesTree.view;
        view.currentIndex = sheet.filesTree.editingRow;
        return findChild(view.currentItem, "addFilesRenameInput");
    }

    function test_r_opens_the_field_and_enter_keeps_the_name() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        testCase.focusRow(sheet, "README.txt");
        keyClick("r");
        verify(sheet.filesTree.editing);
        const input = testCase.renameInput(sheet);
        tryVerify(() => input.activeFocus, 1000, "the field takes the keyboard");
        // The stem is selected, the extension kept.
        compare(input.selectedText, "README");
        keyClick("n");
        keyClick("o");
        keyClick("t");
        keyClick("e");
        keyClick("s");
        keyClick(Qt.Key_Return);

        verify(!sheet.filesTree.editing);
        compare(added.count, 0, "enter in the field renames, it does not add");
        verify(testCase.rowNamed(sheet, "notes.txt") >= 0);
        compare(App.addFiles.renameList().length, 1);
        compare(sheet.filesTree.cursorIndex, testCase.rowNamed(sheet, "notes.txt"));
        verify(sheet.filesTree.activeFocus, "the keyboard is back in the tree");
        const view = sheet.filesTree.view;
        view.currentIndex = sheet.filesTree.cursorIndex;
        verify(findChild(view.currentItem, "addFilesWas").visible);
        compare(findChild(view.currentItem, "addFilesWas").text, "was README.txt");
    }

    function test_esc_in_the_field_cancels_and_stops_there() {
        const sheet = testCase.openTorrent(addSheetComponent);
        testCase.focusRow(sheet, "Sintel/");
        keyClick(Qt.Key_F2);
        verify(sheet.filesTree.editing);
        const input = testCase.renameInput(sheet);
        tryVerify(() => input.activeFocus, 1000);
        compare(input.selectedText, "Sintel", "a folder's whole name is selected");
        keyClick("x");
        keyClick(Qt.Key_Escape);

        verify(!sheet.filesTree.editing);
        compare(sheet.opened, true);
        verify(sheet.filesTree.activeFocus, "esc went to the field, not to the sheet's cancel");
        compare(App.addFiles.renameList().length, 0);
        verify(testCase.rowNamed(sheet, "Sintel/") >= 0);
    }

    function test_a_refused_name_stays_in_the_field_in_red() {
        const sheet = testCase.openTorrent(addSheetComponent);
        testCase.focusRow(sheet, "Sintel/");
        keyClick("r");
        const input = testCase.renameInput(sheet);
        tryVerify(() => input.activeFocus, 1000);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        keyClick("a");
        keyClick("/");
        keyClick("b");
        keyClick(Qt.Key_Return);

        verify(sheet.filesTree.editing, "still in the field");
        verify(sheet.filesTree.renameError.length > 0);
        const view = sheet.filesTree.view;
        view.currentIndex = sheet.filesTree.editingRow;
        verify(findChild(view.currentItem, "addFilesRenameError").visible);
        // #79: the complaint is an alert a screen reader hears.
        compare(findChild(view.currentItem, "addFilesRenameError").Accessible.role, Accessible.AlertMessage);
        compare(App.addFiles.renameList().length, 0);
        // Typing again clears the complaint.
        keyClick(Qt.Key_Backspace);
        compare(sheet.filesTree.renameError, "");
        keyClick(Qt.Key_Escape);
    }

    // #67: a name that something the torrent will not own already has where the add saves to
    // is refused, as the library's renames do (#64): qBittorrent would replace it. The demo
    // has no disk, so App.setDemoDiskFolder gives it one holding a stray file in the torrent's
    // folder.
    function test_a_name_already_on_disk_is_refused() {
        const root = "Blender Open Movies";
        verify(TestFiles.write(root + "/notes on disk.txt", "keep me")); // qmllint disable unqualified
        App.setDemoDiskFolder(TestFiles.folder()); // qmllint disable unqualified
        const sheet = testCase.openTorrent(addSheetComponent);
        testCase.focusRow(sheet, "README.txt");
        keyClick("r");
        const input = testCase.renameInput(sheet);
        tryVerify(() => input.activeFocus, 1000);
        input.text = "notes on disk.txt";
        keyClick(Qt.Key_Return);

        verify(sheet.filesTree.editing, "still in the field");
        compare(sheet.filesTree.renameError, "already exists on disk");
        const view = sheet.filesTree.view;
        view.currentIndex = sheet.filesTree.editingRow;
        verify(findChild(view.currentItem, "addFilesRenameError").visible);
        compare(App.addFiles.renameList().length, 0);
        // A name nothing holds goes through.
        input.text = "notes.txt";
        keyClick(Qt.Key_Return);
        verify(!sheet.filesTree.editing);
        compare(App.addFiles.renameList().length, 1);

        // "No subfolder" puts the files straight in the save path, where that stray is not.
        sheet.layoutChips.current = "No subfolder";
        verify(App.addFiles.renameError(testCase.rowNamed(sheet, "notes.txt"), "notes on disk.txt").length === 0);
        verify(TestFiles.write("loose.txt", "keep me")); // qmllint disable unqualified
        compare(App.addFiles.renameError(testCase.rowNamed(sheet, "notes.txt"), "loose.txt"),
                "already exists on disk");
        sheet.layoutChips.current = "Original";

        // Something put there after the rename: the add refuses rather than replace it.
        verify(TestFiles.write(root + "/notes.txt", "keep me too")); // qmllint disable unqualified
        const finished = createTemporaryObject(addFinishedSpy, testCase);
        App.addTorrent(sheet.request(false));
        compare(finished.count, 1);
        compare(finished.signalArguments[0][0], false);
        compare(finished.signalArguments[0][1], "notes.txt already exists on disk");

        // Without a disk to read, nothing is seen: as before #67.
        App.setDemoDiskFolder("");
        App.addTorrent(sheet.request(false));
        compare(finished.count, 2);
        compare(finished.signalArguments[1][0], true);
    }

    function test_the_pencil_opens_the_field() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const row = testCase.rowItem(sheet, "Tears of Steel/");
        const pencil = findChild(row, "addFilesPencil");
        verify(pencil);
        mouseMove(pencil);
        mouseClick(pencil);
        verify(sheet.filesTree.editing);
        compare(sheet.filesTree.editingRow, row.index);
        keyClick(Qt.Key_Escape);
    }

    function test_the_renames_travel_with_the_add() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        verify(App.addFiles.rename(testCase.rowNamed(sheet, "Sintel/"), "Sintel (2010)"));
        sheet.submit(false);

        compare(added.count, 1);
        const request = added.signalArguments[0][0];
        compare(request.fileRenames.length, 1);
        compare(request.fileRenames[0].kind, "folder");
        compare(request.fileRenames[0].from, "Sintel");
        compare(request.fileRenames[0].to, "Sintel (2010)");
        compare(request.renameAnchor.index, 0);
    }

    function test_the_file_choices_travel_with_the_add() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        const sintel = testCase.rowNamed(sheet, "Sintel/");
        verify(sintel > 0);
        App.addFiles.toggleWanted(sintel);
        const readme = testCase.rowNamed(sheet, "README.txt");
        App.addFiles.setPriority(readme, 7);

        sheet.filesTree.forceActiveFocus();
        keyClick(Qt.Key_Return, Qt.ShiftModifier);

        compare(added.count, 1);
        const request = added.signalArguments[0][0];
        compare(request.kind, "file");
        compare(request.stopped, true);
        compare(request.filePriorities, [1, 1, 1, 0, 0, 1, 1, 7]);
    }

    // #43: every file unticked still adds, stopped, with every file skipped; the sheet
    // says so before the click.
    function test_every_file_skipped_adds_it_stopped() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        const line = findChild(sheet, "addNothingTicked");
        verify(line && !line.visible);
        compare(sheet.actions.length, 3);

        App.addFiles.setAllWanted(false);
        compare(App.addFiles.wantedCount, 0);
        verify(line.visible, "the line says why the button changed");
        compare(sheet.actions.length, 2);
        compare(sheet.actions[1].text, "add stopped");
        verify(sheet.actions[1].primary === true);

        sheet.submit(false); // `enter`, or the primary button
        compare(added.count, 1);
        compare(sheet.errorText, "");
        const request = added.signalArguments[0][0];
        compare(request.stopped, true);
        compare(request.filePriorities, [0, 0, 0, 0, 0, 0, 0, 0]);
    }

    function test_the_tree_scrolls_in_a_short_window() {
        const sheet = testCase.openTorrent(shortSheetComponent);
        const tree = sheet.filesTree;
        tryVerify(() => tree.height > 0);
        verify(tree.height < tree.count * Theme.railRowHeight,
               "14 rows cannot fit under the form in 720 px");
        compare(tree.scrollBar.needed, true);
        // The panel still fits the window, margins included.
        verify(sheet.panel.height <= sheet.height - 2 * Theme.space(16) + 1,
               "panel " + sheet.panel.height + " in " + sheet.height);
        verify(sheet.panel.implicitHeight <= sheet.panel.height + 1,
               "the body does not need to scroll: only the tree does");

        tree.forceActiveFocus();
        for (let i = 0; i < 13; ++i)
            keyClick("j");
        compare(tree.cursorIndex, 13);
        verify(tree.view.contentY > 0, "the cursor row is scrolled into view");
    }

    // ---- #40: a batch (docs/27 B1–B3) ---------------------------------------------------

    readonly property string secondMagnet: "magnet:?xt=urn:btih:0102030405060708090a0b0c0d0e0f1011121314&dn=second"
    readonly property string thirdMagnet:
        "magnet:?xt=urn:btih:2222222222222222222222222222222222222222&dn=third&xl=1048576"

    function test_several_at_once_open_a_batch() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList([testCase.freshMagnet, testCase.secondMagnet, testCase.thirdMagnet], "drop");
        compare(sheet.opened, true);
        compare(sheet.batchMode, true);
        compare(sheet.batchCount, 3);
        compare(sheet.queue.length, 0);
        compare(sheet.title, "Add 3 torrents");
        compare(sheet.sourceField.text, "");
        compare(findChild(sheet, "batchHead").text, "Torrents · 3");
        verify(!sheet.renameField.visible, "the Name row belongs to one torrent");
        compare(sheet.showDownloads, false);
        compare(sheet.actions[2].text, "add 3");
        compare(sheet.actions[1].text, "add 3 stopped");
        // Sizes: two magnets say (xl=), one does not.
        compare(sheet.batchRows[1].flag, "magnet · size after metadata");
        verify(sheet.subtitle.indexOf("1 without a size yet") >= 0, sheet.subtitle);
    }

    function test_a_batch_adds_in_one_request_without_what_is_left_out() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        const known = "magnet:?xt=urn:btih:" + testCase.knownHash() + "&dn=already-here";
        sheet.openList([testCase.freshMagnet, known, testCase.freshMagnet, testCase.thirdMagnet],
                       "drop");
        compare(sheet.batchRows.map(row => row.state), ["ok", "library", "twice", "ok"]);
        compare(sheet.batchRows[2].flag, "twice in this list · left out");
        compare(sheet.batchCount, 2);
        compare(sheet.title, "Add 2 torrents");

        sheet.submit(true);
        compare(added.count, 1);
        const request = added.signalArguments[0][0];
        compare(request.items.length, 2);
        compare(request.items[0].value, testCase.freshMagnet);
        compare(request.items[1].value, testCase.thirdMagnet);
        compare(request.stopped, true);
        verify(request.rename === undefined, "a batch carries no name");
    }

    function test_a_paste_of_several_starts_a_batch() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith("", "");
        sheet.sourceField.text = testCase.freshMagnet + "\n" + testCase.secondMagnet;
        compare(sheet.batchMode, true);
        compare(sheet.batchCount, 2);
        compare(sheet.sourceField.text, "");
        // A paste into the batch's field joins it.
        sheet.sourceField.text = testCase.thirdMagnet;
        compare(sheet.batchCount, 3);
        compare(sheet.sourceField.text, "");
    }

    function test_removing_down_to_one_is_the_ordinary_sheet() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList([testCase.freshMagnet, testCase.secondMagnet], "handoff");
        sheet.batchList.forceActiveFocus();
        keyClick("j");
        keyClick("d");
        compare(sheet.batchMode, false);
        compare(sheet.sourceField.text, testCase.freshMagnet);
        compare(sheet.title, "Add torrent");
        verify(sheet.renameField.visible);
    }

    // docs/27 D7 a: a hand-off joins a source as it arrived; a form already worked on queues.
    function test_a_hand_off_joins_an_untouched_source_as_a_batch() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        sheet.openList([testCase.secondMagnet], "handoff");
        compare(sheet.batchMode, true);
        compare(sheet.batchRows.map(row => row.text), [testCase.freshMagnet, testCase.secondMagnet]);
        compare(sheet.queue.length, 0);
    }

    function test_browse_with_several_files_makes_a_batch() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith("", "");
        const folder = testCase.fixturesFolder();
        sheet.pickTorrents = () => [folder + "/single_file_v1.torrent", folder + "/open_movies_v1.torrent"];
        sheet.sourceField.trailingClicked();
        compare(sheet.batchMode, true);
        tryVerify(() => sheet.batchRows.every(row => row.state !== "reading"), 10000);
        compare(sheet.batchCount, 2);
        compare(sheet.batchRows[1].flag, "8 files");
        verify(sheet.neededBytes > 0);
        // One file is the ordinary sheet, as before.
        sheet.reset();
        sheet.pickTorrents = () => [folder + "/single_file_v1.torrent"];
        sheet.sourceField.trailingClicked();
        compare(sheet.batchMode, false);
        compare(sheet.sourceField.text, folder + "/single_file_v1.torrent");
    }

    // docs/27 B3: ticks in In Downloads, then enter: the ticked files are the batch.
    function test_ticks_in_downloads_make_a_batch() {
        const sheet = testCase.openOnFixtures();
        sheet.downloadsList.forceActiveFocus();
        keyClick(Qt.Key_Space);
        keyClick("j");
        keyClick(Qt.Key_Space);
        compare(sheet.downloadsList.ticked.length, 2);
        verify(findChild(sheet, "downloadsTicked").visible);
        keyClick(Qt.Key_Return);
        compare(sheet.batchMode, true);
        compare(sheet.batchRows.length, 2);
        compare(sheet.batchRows[0].text, App.downloadsTorrents[0].path);
        compare(sheet.downloadsList.ticked.length, 0);
    }

    // The shared mouse rules: ctrl+click ticks a row and sets the anchor, shift+click ticks
    // anchor..row instead of what was ticked, ctrl+shift+click adds that range.
    function test_shift_click_ticks_a_range_in_downloads() {
        const sheet = testCase.openOnFixtures();
        const list = sheet.downloadsList;
        verify(list.count >= 5);
        const paths = App.downloadsTorrents.map(t => t.path);
        const row = i => findChild(list, "downloadsRow" + i);

        mouseClick(row(1), 20, 5, Qt.LeftButton, Qt.ControlModifier);
        compare(list.ticked, [paths[1]]);
        mouseClick(row(3), 20, 5, Qt.LeftButton, Qt.ShiftModifier);
        compare(list.ticked.slice().sort(), paths.slice(1, 4).sort());
        compare(sheet.opened, true, "a shift+click chose a file");
        compare(sheet.sourceField.text, "", "a shift+click filled the field");

        // From the same anchor the other way: the range replaces the ticks.
        mouseClick(row(0), 20, 5, Qt.LeftButton, Qt.ShiftModifier);
        compare(list.ticked.slice().sort(), paths.slice(0, 2).sort());

        // ctrl+shift adds: a new anchor on 4 (ctrl+click), then 4..3 joins 0..1.
        mouseClick(row(4), 20, 5, Qt.LeftButton, Qt.ControlModifier);
        mouseClick(row(3), 20, 5, Qt.LeftButton, Qt.ControlModifier | Qt.ShiftModifier);
        compare(list.ticked.slice().sort(), [paths[0], paths[1], paths[3], paths[4]].sort());
        compare(findChild(sheet, "downloadsTicked").text, "4 ticked · add takes them together");
    }

    // Ticks in In Downloads, then the `add` button: it reads `add 2`, and adds both once read.
    function test_add_button_takes_the_ticks_in_downloads() {
        const sheet = testCase.openOnFixtures();
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        verify(!App.downloadsTorrents[0].inLibrary && !App.downloadsTorrents[1].inLibrary);
        sheet.downloadsList.toggleTick(0);
        sheet.downloadsList.toggleTick(1);
        compare(sheet.primaryButton().text, "add 2");

        mouseClick(sheet.primaryButton());
        compare(sheet.downloadsList.ticked.length, 0);
        compare(sheet.batchMode, true);
        tryCompare(added, "count", 1, 10000);
        const request = added.signalArguments[0][0];
        compare(request.items.length, 2);
        compare(request.items[0].value, App.downloadsTorrents[0].path);
        compare(request.items[1].value, App.downloadsTorrents[1].path);
        compare(request.stopped, false);
    }

    // #51 D18: In Downloads' line counts what the button adds, and says how many ticks are
    // on torrents already in the library, which the add leaves out.
    function test_the_ticked_line_says_what_the_add_takes() {
        const sheet = testCase.openOnFixtures();
        compare(sheet.tickedText(3, 0), "3 ticked · add takes them together");
        compare(sheet.tickedText(5, 2), "5 ticked · 2 already in the library · add takes 3");
        compare(sheet.tickedText(1, 1), "1 ticked · already in the library");
        compare(sheet.tickedText(2, 2), "2 ticked · all already in the library");

        const fresh = App.downloadsTorrents.filter(t => !t.inLibrary).length;
        verify(fresh >= 2);
        sheet.downloadsList.toggleTick(App.downloadsTorrents.findIndex(t => !t.inLibrary));
        compare(sheet._tickedInLibrary, 0);
        compare(findChild(sheet, "downloadsTicked").text, "1 ticked · add takes them together");
        const library = App.downloadsTorrents.length - fresh;
        for (let i = 0; i < App.downloadsTorrents.length; ++i) {
            if (App.downloadsTorrents[i].inLibrary)
                sheet.downloadsList.toggleTick(i);
        }
        compare(sheet._tickedInLibrary, library);
        compare(findChild(sheet, "downloadsTicked").text, sheet.tickedText(1 + library, library));
    }

    // One tick and `add stopped`: that file, added stopped, once it is read.
    function test_add_stopped_takes_a_single_tick_in_downloads() {
        const sheet = testCase.openOnFixtures();
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        const at = App.downloadsTorrents.findIndex(t => !t.inLibrary);
        const entry = App.downloadsTorrents[at];
        sheet.downloadsList.toggleTick(at);
        compare(sheet.downloadsList.ticked.length, 1);
        compare(sheet.primaryButton().text, "add");

        sheet.triggerSecondary();
        compare(sheet.sourceField.text, entry.path);
        tryCompare(added, "count", 1, 10000);
        const request = added.signalArguments[0][0];
        compare(request.value, entry.path);
        compare(request.stopped, true);
    }

    // docs/27 D10: a batch `.torrent` whose files are on disk gets the yellow line.
    function test_a_files_match_in_a_batch_says_so() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList([testCase.freshMagnet, testCase.secondMagnet], "drop");
        sheet.batch = sheet.batch.concat([{
                                              "text": "x.torrent",
                                              "map": {
                                                  "kind": "file",
                                                  "valid": true,
                                                  "token": "file-test",
                                                  "name": "On Disk",
                                                  "value": "x.torrent",
                                                  "infoHashV1": "3333333333333333333333333333333333333333"
                                              }
                                          }]);
        const matches = {};
        matches["file-test"] = [{
                                    "savePath": "/media/library",
                                    "name": "On Disk"
                                }];
        sheet._batchMatches = matches;
        compare(sheet.batchRows[2].flag, "files on disk");
        const line = findChild(sheet, "batchMatchLine");
        verify(line.visible);
        verify(line.text.indexOf("/media/library") >= 0, line.text);
    }

    function test_escape_drops_the_queue() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList([testCase.freshMagnet, "magnet:?xt=urn:btih:0102030405060708090a0b0c0d0e0f1011121314&dn=second"], "drop");
        sheet.oneByOne();
        compare(sheet.queue.length, 1);
        keyClick(Qt.Key_Escape); // clears the field
        keyClick(Qt.Key_Escape); // goes to `cancel`
        keyClick(Qt.Key_Escape); // closes the sheet
        compare(sheet.opened, false);
        compare(sheet.queue.length, 0);
    }

    // ---- Free space under Save to (docs/25 §4, #29) --------------------------------------

    function test_a_magnet_with_a_size_says_what_it_needs_and_the_room() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");

        const line = sheet.freeLine;
        compare(line.visible, true);
        compare(line.needsText, "needs 2.34 GB");
        // Save to is empty: the server's own figure for its default folder (D9 a).
        compare(line.room.source, "default");
        verify(line.room.bytesFree > 0, "the demo server reports free space");
        compare(line.roomText, App.sizeText(line.room.bytesFree) + " free on this drive");
        compare(line.level, "normal");
    }

    function test_a_magnet_without_a_size_waits_for_its_metadata() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith("magnet:?xt=urn:btih:9a4f6c1e2b3d4f50617283940a1b2c3d4e5f6071&dn=x", "clipboard");

        compare(sheet.freeLine.needsText, "size known once the metadata arrives");
        compare(sheet.freeLine.showMeter, false);
    }

    function test_a_folder_this_machine_cannot_see_is_unknown() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        sheet.savePathField.text = "/elsewhere/films";

        const line = sheet.freeLine;
        compare(line.room.source, "unknown");
        compare(line.roomText, "free space here unknown · " + App.serverName
                + " reports only its default folder: " + App.sizeText(line.room.defaultFree));
        compare(line.showMeter, false);
        compare(line.level, "normal");
    }

    function test_a_torrent_file_needs_its_ticked_files() {
        const sheet = testCase.openTorrent(addSheetComponent);
        compare(sheet.neededBytes, App.addFiles.wantedSize);
        compare(sheet.freeLine.totalBytes > 0, true);
        compare(sheet.freeLine.needsText, "needs " + App.sizeText(sheet.neededBytes));
    }

    function test_the_line_turns_yellow_then_red_then_green() {
        const line = createTemporaryObject(freeLineComponent, testCase);
        const free = line.free;
        verify(free > 0);

        line.neededBytes = free / 4;
        line.thresholdBytes = 0;
        compare(line.level, "normal");

        line.thresholdBytes = free; // what is left is under the threshold
        compare(line.level, "warn");
        verify(line.roomText.indexOf(" · leaves ") > 0, line.roomText);

        line.neededBytes = free * 2;
        compare(line.level, "bad");
        compare(line.roomText.indexOf("only "), 0);
        verify(line.roomText.endsWith("untick files or pick another folder"));

        // #27: saved where the files already are, nothing new is written.
        line.alreadyThere = true;
        compare(line.level, "good");
        compare(line.needsText, "needs nothing new");
        compare(line.showMeter, false);
    }

    function test_unticked_files_read_as_part_of_the_whole() {
        const line = createTemporaryObject(freeLineComponent, testCase);
        line.neededBytes = 1000;
        line.totalBytes = 4000;
        compare(line.needsText, "needs " + App.sizeText(1000) + " of " + App.sizeText(4000));
    }

    // ---- Files already on disk (docs/25 §3, #27) ------------------------------------------

    /// What `App.filesMatchFound` would answer (the demo has no server to ask).
    function fakeMatch(savePath, layout, seeding) {
        return {
            "hash": "0123456789abcdef0123456789abcdef01234567",
            "name": "Blender Open Movies",
            "tracker": "tracker.a.example",
            "stateText": seeding ? "seeding" : "stopped",
            "ratioText": "2.10",
            "seeding": seeding,
            "savePath": savePath,
            "layout": layout,
            "fileCount": 8
        };
    }

    function test_a_files_match_offers_to_save_there() {
        const sheet = testCase.openTorrent(addSheetComponent);
        compare(sheet.matchLine.visible, false);
        sheet.filesMatches = [testCase.fakeMatch("/media/films", "Original", true)];

        compare(sheet.matchLine.visible, true);
        compare(sheet.matchLine.headText,
                "<b>These files are already on disk.</b> Same 8 files and sizes as <b>Blender Open Movies</b>, seeding from tracker.a.example.");
        compare(sheet.matchLine.subText, "/media/films · nothing to download if it's saved there");
    }

    function test_save_there_and_undo() {
        const sheet = testCase.openTorrent(addSheetComponent);
        sheet.filesMatches = [testCase.fakeMatch("/media/films/Blender", "NoSubfolder", true)];

        const typed = sheet.sourceField.text;
        keyClick(Qt.Key_S, Qt.AltModifier); // the keyboard is in the source field
        compare(sheet.sourceField.text, typed, "the field does not type the s");
        compare(sheet.savedThere, true);
        compare(sheet.savePathField.text, "/media/films/Blender");
        compare(sheet.savePathField.trailing, "from the files match · browse");
        compare(sheet.autoTMM, false);
        compare(sheet.layoutValue(), "NoSubfolder");
        compare(sheet.skipCheckToggle.checked, false, "D5 a: the hash check stays on");
        compare(sheet.freeLine.level, "good");
        compare(sheet.freeLine.needsText, "needs nothing new");
        verify(sheet.matchLine.headText.indexOf("<b>Saving beside Blender Open Movies.</b>") === 0);

        const request = sheet.request(false);
        compare(request.savePath, "/media/films/Blender");
        compare(request.autoTMM, false);
        compare(request.contentLayout, "NoSubfolder");

        // A category chosen afterwards keeps Save to where the files are.
        sheet.categoryChips.current = sheet.categoryNames[0];
        compare(sheet.savePathField.text, "/media/films/Blender");

        sheet.undoSaveThere();
        compare(sheet.savedThere, false);
        compare(sheet.savePathField.text, "");
        compare(sheet.autoTMM, true);
        compare(sheet.layoutValue(), "Original");
    }

    function test_typing_a_path_leaves_the_match() {
        const sheet = testCase.openTorrent(addSheetComponent);
        sheet.filesMatches = [testCase.fakeMatch("/media/films", "Original", true)];
        sheet.saveThere(0);
        sheet.savePathField.text = "/media/films2";
        compare(sheet.savedThere, false);
        compare(sheet.freeLine.alreadyThere, false);
        compare(sheet.matchLine.visible, true, "the offer is back");
    }

    function test_two_copies_pick_one() {
        const sheet = testCase.openTorrent(addSheetComponent);
        sheet.filesMatches = [testCase.fakeMatch("/media/films", "Original", true),
                              testCase.fakeMatch("/backup/films", "Original", false)];
        compare(sheet.matchLine.headText,
                "<b>These files are already on disk, 2 times.</b> Pick the copy to share:");
        sheet.matchLine.button.forceActiveFocus();
        keyClick(Qt.Key_J);
        compare(sheet.matchIndex, 1);
        keyClick(Qt.Key_Return);
        compare(sheet.savePathField.text, "/backup/films");
    }

    function test_a_magnet_asks_for_no_match() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        compare(sheet._matchToken, "");
        compare(sheet.matchLine.visible, false);
    }

    // ---- docs/33 #72 (D13–D15, D20) -----------------------------------------------------
    function _twoServers() {
        App.addServer({
                          "name": "public",
                          "url": "http://10.99.0.2:8080",
                          "authMode": "none"
                      }, "");
        App.addServer({
                          "name": "private",
                          "url": "http://10.99.0.3:8080",
                          "authMode": "none"
                      }, "");
    }

    function _noServers() {
        while (App.serverList.count > 0)
            App.serverList.remove(App.serverList.idAt(0));
    }

    // D13 (A1): with two servers the head names the server in a chip; ctrl+s asks for the
    // server palette (D14).
    function test_two_servers_name_the_target_in_the_head() {
        testCase._twoServers();
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        compare(sheet.title, "Add torrent to");
        compare(sheet.note, "");
        const chip = findChild(sheet, "addServerChip");
        verify(chip !== null);
        compare(findChild(chip, "addServerName").text, App.serverName);
        let asked = 0;
        sheet.serverPaletteRequested.connect(() => asked++);
        keyClick(Qt.Key_S, Qt.ControlModifier);
        compare(asked, 1);
        sheet.dismiss();
        testCase._noServers();
    }

    function test_one_server_has_no_chip() {
        testCase._noServers();
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openBlank();
        compare(findChild(sheet, "addServerChip"), null);
        sheet.dismiss();
    }

    // D15 (A3): the server down, the sheet still opens; "add when back" holds the torrent and
    // the band says so in yellow; free space says it doesn't know.
    function test_add_when_back_holds_the_torrent() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        const spy = createTemporaryObject(addSpy, testCase, {
                                              "target": sheet
                                          });
        sheet._testServerDown = true;
        sheet.openWith(testCase.freshMagnet, "clipboard");
        compare(sheet._addText, "add when back");
        const band = findChild(sheet, "addNotice");
        verify(band.shown);
        compare(band.kind, "warning");
        compare(band.title, App.serverName + " isn't answering");
        verify(sheet.freeLine.roomText.indexOf("unknown while") >= 0);
        sheet.submit(false);
        compare(spy.count, 0);
        verify(sheet._held !== null);
        // The server answers: the held add goes.
        sheet._testServerDown = false;
        sheet._releaseHeld();
        compare(spy.count, 1);
        compare(sheet._held, null);
        sheet.dismiss();
    }

    // N3: an add the server never took is red, in plain words.
    function test_a_refused_add_is_red_in_plain_words() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        sheet.errorText = "nothing to add";
        const band = findChild(sheet, "addNotice");
        verify(band.shown);
        compare(band.kind, "error");
        compare(band.title, "nothing to add");
        sheet.errorText = "";
        sheet.failure = {
            "headline": "private didn't take the torrent",
            "line": "It stopped answering.",
            "details": "network error · POST torrents/add · Connection refused",
            "sentence": "x"
        };
        compare(band.title, "private didn't take the torrent");
        compare(band.kind, "error");
        verify(band.details.indexOf("Connection refused") > 0);
        sheet.dismiss();
    }

    // D14 (A2): after a switch, what was typed stays and one line says what moved.
    function test_a_switch_keeps_what_was_typed_and_says_what_moved() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openWith(testCase.freshMagnet, "clipboard");
        sheet.applyChosenTags(["keep-for-omaseed-tests"]);
        sheet._serverSwitched();
        verify(sheet._switchPending);
        sheet._afterSwitch();
        compare(sheet.sourceField.text, testCase.freshMagnet);
        verify(sheet.movedText.indexOf("Moved to " + App.serverName + ": save path back to its default") === 0,
               sheet.movedText);
        verify(sheet.movedText.indexOf("tag “keep-for-omaseed-tests” is new there") > 0, sheet.movedText);
        compare(sheet.tagChips.selected, ["keep-for-omaseed-tests"]);
        sheet.dismiss();
    }

    // #77: "in Downloads" rows are list items named by the torrent; a screen reader's press
    // is a plain click: it chooses the file.
    function test_a_downloads_row_takes_the_accessible_press() {
        const sheet = testCase.openOnFixtures();
        const row = findChild(sheet.downloadsList, "downloadsRow0");
        compare(row.Accessible.role, Accessible.ListItem);
        compare(row.Accessible.name, App.downloadsTorrents[0].name);
        row.Accessible.pressAction();
        compare(sheet.sourceField.text, App.downloadsTorrents[0].path);
        compare(sheet.source, "file");
    }

    // #77: a batch row is named by its torrent, its flag the description, and its ✕ is a
    // "remove" button whose press drops the row.
    function test_a_batch_row_and_its_remove_button() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList([testCase.freshMagnet, testCase.secondMagnet, testCase.thirdMagnet], "drop");
        waitForRendering(sheet.batchList);
        const row = findChild(sheet.batchList, "batchRow1");
        compare(row.Accessible.role, Accessible.ListItem);
        compare(row.Accessible.name, sheet.batchRows[1].name);
        compare(row.Accessible.description, "magnet · size after metadata");
        const remove = findChild(row, "batchRemove").parent;
        compare(remove.Accessible.role, Accessible.Button);
        compare(remove.Accessible.name, "remove");
        remove.Accessible.pressAction();
        compare(sheet.batchCount, 2);
        sheet.dismiss();
    }

    // #77: in the tree, a folder's arrow, a row's pencil and its priority are buttons whose
    // press is their click.
    function test_the_trees_buttons_take_the_accessible_press() {
        const sheet = testCase.openTorrent(addSheetComponent);
        let row = testCase.rowItem(sheet, "Big Buck Bunny/");
        compare(row.Accessible.name, "Big Buck Bunny/");
        const fold = findChild(row, "addFilesFold");
        compare(fold.Accessible.role, Accessible.Button);
        compare(fold.Accessible.name, "fold");
        fold.Accessible.pressAction();
        compare(sheet.filesTree.count, 10);
        row = testCase.rowItem(sheet, "Big Buck Bunny/");
        compare(findChild(row, "addFilesFold").Accessible.name, "unfold");
        findChild(row, "addFilesFold").Accessible.pressAction();
        compare(sheet.filesTree.count, 14);
        verify(findChild(testCase.rowItem(sheet, "README.txt"), "addFilesFold").Accessible.ignored);

        row = testCase.rowItem(sheet, "README.txt");
        const priority = findChild(row, "addFilesPriority");
        compare(priority.Accessible.name, "priority");
        priority.Accessible.pressAction();
        compare(App.addFiles.priority(row.index), 6);
        compare(priority.Accessible.description, row.priorityText);

        row = testCase.rowItem(sheet, "Tears of Steel/");
        findChild(row, "addFilesPencil").Accessible.pressAction();
        verify(sheet.filesTree.editing);
        compare(sheet.filesTree.editingRow, row.index);
        keyClick(Qt.Key_Escape);
    }

    Component {
        id: matchLineComponent

        AddMatchLine {
            matches: [{
                    "savePath": "/data/a",
                    "seeding": true,
                    "tracker": "one.example",
                    "ratioText": "1.20"
                }, {
                    "savePath": "/data/b",
                    "seeding": false,
                    "stateText": "stopped",
                    "tracker": "",
                    "ratioText": "0.40"
                }]
        }
    }

    Component {
        id: sharedChoiceComponent

        SharedChoice {
            rows: [{
                    "title": "keep the files",
                    "detail": "the other torrent still uses them"
                }, {
                    "title": "delete them too"
                }]
        }
    }

    // #77: each copy of a files match is a list item named by where it is; a screen reader's
    // press picks it, as a click does.
    function test_a_match_copy_takes_the_accessible_press() {
        const line = createTemporaryObject(matchLineComponent, testCase);
        const copies = [findChild(line, "matchCopy_0"), findChild(line, "matchCopy_1")];
        compare(copies[0].Accessible.role, Accessible.ListItem);
        compare(copies[1].Accessible.name, "/data/b");
        compare(copies[1].Accessible.description, "stopped · 0.40");
        verify(copies[0].Accessible.selected);
        copies[1].Accessible.pressAction();
        compare(line.index, 1);
        verify(copies[1].Accessible.selected);
    }

    // #77: a shared-data choice is a radio button; its press and its toggle pick it.
    function test_a_shared_choice_is_a_radio_button() {
        const choice = createTemporaryObject(sharedChoiceComponent, testCase);
        const second = findChild(choice, "sharedChoice_1");
        compare(second.Accessible.role, Accessible.RadioButton);
        compare(second.Accessible.name, "delete them too");
        verify(findChild(choice, "sharedChoice_0").Accessible.checked);
        verify(!second.Accessible.checked);
        second.Accessible.toggleAction();
        compare(choice.choice, 1);
        findChild(choice, "sharedChoice_0").Accessible.pressAction();
        compare(choice.choice, 0);
        choice.enabled = false;
        second.Accessible.pressAction();
        compare(choice.choice, 0, "a disabled choice picked");
    }
}
