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

        AddSheet {
            height: 700
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
        compare(sheet.hints, sheet._downloadsHints);
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
        mouseClick(sheet.cancelButton());
        compare(sheet.opened, false);
    }

    function test_a_queued_source_opens_after_the_first_one() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList([testCase.freshMagnet,
                        "magnet:?xt=urn:btih:0102030405060708090a0b0c0d0e0f1011121314&dn=second"],
                       "handoff");

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

    function test_the_tree_is_keyboard_driven() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const tree = sheet.filesTree;
        tree.forceActiveFocus();
        compare(tree.activeFocus, true);
        // The hint line teaches the tree's keys while it has the keyboard.
        compare(sheet.hints[0].key, "j k");
        compare(sheet.hints[1].key, "space");

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
        keyClick(Qt.Key_Space); // all skipped -> back to normal
        compare(App.addFiles.priority(0), 1);
        keyClick("-");
        compare(App.addFiles.priority(0), 0);
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

    function test_all_and_none_in_the_files_header() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const none = findChild(sheet, "addFilesNone");
        const all = findChild(sheet, "addFilesAll");
        verify(none && all);
        mouseClick(none);
        compare(App.addFiles.wantedCount, 0);
        mouseClick(all);
        compare(App.addFiles.wantedCount, 8);
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
        compare(sheet.hints[5].key, "r");
        keyClick("r");
        verify(sheet.filesTree.editing);
        const input = testCase.renameInput(sheet);
        tryVerify(() => input.activeFocus, 1000, "the field takes the keyboard");
        // The stem is selected, the extension kept.
        compare(input.selectedText, "README");
        compare(sheet.hints[0].key, "enter");
        compare(sheet.hints[1].key, "esc");
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
        compare(App.addFiles.renameList().length, 0);
        // Typing again clears the complaint.
        keyClick(Qt.Key_Backspace);
        compare(sheet.filesTree.renameError, "");
        keyClick(Qt.Key_Escape);
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

    function test_every_file_skipped_is_not_an_add() {
        const sheet = testCase.openTorrent(addSheetComponent);
        const added = createTemporaryObject(addSpy, testCase, {
                                                "target": sheet
                                            });
        for (let row = sheet.filesTree.count - 1; row >= 0; --row) {
            if (App.addFiles.depthAt(row) === 0)
                App.addFiles.setPriority(row, 0);
        }
        compare(App.addFiles.wantedCount, 0);
        sheet.submit(false);
        compare(added.count, 0);
        verify(sheet.errorText.length > 0);
    }

    function test_the_tree_scrolls_in_a_short_window() {
        const sheet = testCase.openTorrent(shortSheetComponent);
        const tree = sheet.filesTree;
        tryVerify(() => tree.height > 0);
        verify(tree.height < tree.count * Theme.railRowHeight,
               "14 rows cannot fit under the form in 700 px");
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

    function test_escape_drops_the_queue() {
        const sheet = createTemporaryObject(addSheetComponent, testCase);
        sheet.openList([testCase.freshMagnet, testCase.freshMagnet], "drop");
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
        compare(sheet.hints[sheet.hints.length - 1].key, "alt+s");
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
}
