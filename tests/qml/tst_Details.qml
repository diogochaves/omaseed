import QtQuick
import QtQuick.Layouts
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// Details against the demo library: the head and strip of spec §2 Fig. 1's right column,
// the four tabs and the tracker badge, and the Files tab's tree, folding and priorities
// against Fig. 2 row by row.
TestCase {
    id: testCase

    name: "Details"
    visible: true
    when: windowShown

    readonly property string bbbName: "Big Buck Bunny 4K (CC-BY)"
    readonly property string archName: "archlinux-2026.09.01-x86_64.iso"
    readonly property string elephantsName: "Elephants Dream 1080p (CC-BY)"

    Component {
        id: detailsComponent

        Details {
            details: App.details
            height: 600
            width: 340
        }
    }

    Component {
        id: errorBlockComponent

        DetailsErrorBlock {
            width: 340
        }
    }

    // #108: an EINVAL on a name every disk takes. The block says the file or the disk may be
    // damaged, its first hint is advice with no key and no click, and retry comes second.
    function test_a_refused_open_says_check_the_disk_first() {
        const block = createTemporaryObject(errorBlockComponent, testCase, {
                                                "problem": {
                                                    "found": true,
                                                    "headline": "The disk refused to open a file",
                                                    "detail": "Invalid argument, opening file.mkv",
                                                    "advice": "The file or the disk may be damaged.",
                                                    "folder": "/media/disk/Some.Film.2017.2160p",
                                                    "actions": ["checkDisk", "start"]
                                                }
                                            });
        verify(findChild(block, "errorAdvice").visible);
        compare(findChild(block, "errorAdvice").text, "The file or the disk may be damaged.");
        compare(block.hints.map(hint => (hint.key + " " + hint.label).trim()),
                ["check the disk first", "e retry", "copy the line", "its lines in the log"]);
        compare(block.hints[0].clickable, false);
        compare(block.hints[1].clickable, true);
    }

    // docs/44 D13 (#121): a stuck move is red in board 28's place: show both folders, c recheck
    // (DECISIONS C28, #158) and set location…, then the log's links; its partial copies named
    // when they are seen.
    function test_a_stuck_move_is_a_red_block() {
        const problem = {
            "found": true,
            "stuckMove": true,
            "causeKey": "stuckMove",
            "headline": "Finished, but its files never reached /mnt/media/Downloads",
            "detail": "qBittorrent couldn't move them: No space left on device.",
            "folder": "It's seeding from /mnt/fast/incomplete.",
            "advice": "Partial copies may be left in the destination.",
            "destination": "/mnt/media/Downloads",
            "actions": ["bothFolders", "recheck", "location"]
        };
        const block = createTemporaryObject(errorBlockComponent, testCase, {
                                                "problem": problem
                                            });
        compare(block.kindColor, Theme.red);
        compare(block.hints.map(hint => (hint.key + " " + hint.label).trim()),
                ["show both folders", "c recheck", "m set location…", "copy the line",
                 "its lines in the log"]);
        verify(block.hints[0].clickable);
        compare(findChild(block, "errorAdvice").text, "Partial copies may be left in the destination.");
        block.partialText = "Partial copies are left in /mnt/media/Downloads/Album: 699 files, 22 MB.";
        compare(findChild(block, "errorAdvice").text, block.partialText);

        // No line: no reason guessed, and no word about the log's limits.
        const unlogged = Object.assign({}, problem, {
                                           "found": false,
                                           "detail": "Its files are still in its incomplete folder."
                                       });
        block.problem = unlogged;
        compare(block.metaText, "");
        verify(!findChild(block, "errorMeta").visible);
        compare(block.hints.map(hint => (hint.key + " " + hint.label).trim()),
                ["show both folders", "c recheck", "m set location…"]);
    }

    function hashOf(name) {
        for (let row = 0; row < App.filtered.count; ++row) {
            const hash = App.filtered.hashAt(row);
            if (App.torrentRow(hash).name === name)
                return hash;
        }
        return "";
    }

    /// Every case starts from the same fictional library and the same selected torrent.
    function select(name) {
        App.loadDemo(16);
        const hash = testCase.hashOf(name);
        verify(hash.length > 0, "the demo library has no row named " + name);
        App.selectTorrent(hash);
        // The demo keeps the same hash between cases, so the files model keeps its state:
        // start every case from the torrent's own order, unfiltered and unselected.
        const files = App.details.files;
        App.details.rememberViewState = false;
        files.filterText = "";
        files.hideSkipped = false;
        files.sortKey = "";
        files.sortDescending = false;
        files.clearSelection();
        files.expandAll();
        const view = createTemporaryObject(detailsComponent, testCase);
        view.torrent = App.torrentRow(hash);
        return view;
    }

    // DECISIONS C24 H6: the head is the name, its meta line, then the rows display's one fact
    // in its state's colour, its caption, and the sentence; the pane opens on the story.
    function test_head_and_hero_come_from_the_cursor_row() {
        const view = testCase.select(testCase.archName);

        compare(view.torrent.name, testCase.archName);
        compare(view.torrent.category, "linux");
        compare(findChild(view, "detailsFact").text, view.torrent.rowFact);
        compare(findChild(view, "detailsFact").text, "3 min");
        compare(findChild(view, "detailsFact").color, Theme.stateDownloading);
        compare(findChild(view, "detailsCaption").text, "to ready");
        const sentence = findChild(view, "detailsSentence").text;
        verify(sentence.startsWith("48.9 % of 1.31 GB"), sentence);
        verify(sentence.endsWith("."), sentence);
        compare(view.currentTab, view.tabStory);
        // The story has no list: the cursor waits for one.
        compare(view.rowCount, 0);
        // docs/12 G5: facts' rows are a list, and the cursor starts on the first.
        view.activateTab(view.tabFacts);
        compare(view.cursorIndex, 0);
        compare(view.rowCount, findChild(view, "factsTab").count);
        view.activateTab(view.tabStory);
    }

    // DECISIONS C24 H6: the four facts the strip had are in facts now, Size first; the head's
    // meta joins Information.
    function test_facts_lead_with_what_the_strip_had() {
        const view = testCase.select(testCase.archName);
        const facts = findChild(view, "factsTab");
        const transfer = facts.groups[0].rows;
        compare(transfer.slice(0, 5).map(row => row.key), ["Size", "Done", "Ratio", "ETA", "Status"]);
        compare(transfer[0].value, "1.31 GB");
        compare(transfer[1].value, "48.9 %");
        compare(transfer[3].value, "2 min 40 s");
        const information = facts.groups[1].rows.map(row => row.key);
        compare(information.slice(0, 3), ["Category", "Tags", "Added"].filter(
                    key => key !== "Tags" || view.torrent.tagsText.length > 0));
        // C30 F17: the size's tail, when the reading has one.
        facts.sizeTail = "2 skipped, 16.0 MB still on disk";
        compare(facts.groups[0].rows[0].tail, "2 skipped, 16.0 MB still on disk");
        facts.sizeTail = "";
    }

    // #79: a screen reader hears the head (the name as the heading, the line under it whole),
    // the fact and its sentence, the pieces line and facts' group headings; an error block is an
    // alert named by its headline, its lines text.
    function test_a_screen_reader_hears_the_head_and_the_figures() {
        const view = testCase.select(testCase.archName);
        const name = findChild(view, "headName");
        compare(name.Accessible.role, Accessible.Heading);
        compare(name.Accessible.name, testCase.archName);
        const meta = findChild(view, "headMeta");
        compare(meta.Accessible.role, Accessible.StaticText);
        verify(meta.Accessible.name.startsWith("linux · "), meta.Accessible.name);
        verify(meta.Accessible.name.indexOf("added ") >= 0, meta.Accessible.name);
        const hero = findChild(view, "detailsHero");
        compare(hero.Accessible.name, "3 min to ready");
        verify(hero.Accessible.description.startsWith("48.9 % of 1.31 GB"), hero.Accessible.description);
        const pieces = findChild(view, "piecesLabel");
        compare(pieces.Accessible.name, "pieces");
        compare(pieces.Accessible.description, "617 of 1 264 · 48.9 %");
        const transfer = findChild(view, "factsHeading_Transfer");
        compare(transfer.Accessible.role, Accessible.Heading);
        compare(transfer.Accessible.name, "Transfer");

        const errored = testCase.select(testCase.elephantsName);
        const block = findChild(errored, "errorBlock");
        compare(block.Accessible.role, Accessible.AlertMessage);
        compare(block.Accessible.name, "The disk is full");
        const detail = findChild(errored, "errorDetail");
        compare(detail.Accessible.role, Accessible.StaticText);
        compare(detail.Accessible.name, detail.text);
    }

    // docs/28 G2: an errored torrent says why at the top of the story (C24 H6), and facts'
    // Status row carries the short cause; a healthy one shows neither.
    function test_an_errored_torrent_says_why() {
        const view = testCase.select(testCase.elephantsName);
        const block = findChild(view, "errorBlock");
        verify(block, "no error block");
        verify(block.visible);
        compare(findChild(view, "errorHeadline").text, "The disk is full");
        verify(findChild(view, "errorDetail").text.startsWith("No space left on device, writing "));
        verify(findChild(view, "errorMeta").text.startsWith("qBittorrent's log · "));
        verify(!findChild(view, "errorAdvice").visible, "a full disk needs no advice line");
        // docs/32 D13: and its lines in the log, last.
        compare(block.hints.map(hint => hint.action), ["location", "start", "copy", "log"]);
        // DECISIONS C20 (#107): `p` stops an errored torrent; the block's retry is `e`.
        compare(block.hints.map(hint => (hint.key + " " + hint.label).trim()),
                ["m move…", "e retry", "copy the line", "its lines in the log"]);
        const facts = findChild(view, "factsTab");
        const status = facts.rows.findIndex(row => row.key === "Status");
        compare(findChild(findChild(view, "factsRow_" + status), "factsTail").text, "· disk full");

        const healthy = testCase.select(testCase.archName);
        verify(!findChild(healthy, "errorBlock").visible);
        verify(!findChild(healthy, "failureBlock").visible);
    }

    SignalSpy {
        id: failureSpy

        signalName: "failureActionRequested"
    }

    SignalSpy {
        id: problemSpy

        signalName: "problemActionRequested"
    }

    SignalSpy {
        id: moveBackSpy

        signalName: "moveBackRequested"
    }

    // DECISIONS C24 H6: the story reads in order: the error block, the last move, the pieces,
    // what happened, then its lines in the log.
    function test_the_story_reads_in_order() {
        const view = testCase.select(testCase.elephantsName);
        const story = findChild(view, "storyTab");
        verify(story.visible);
        story.lastMove = {
            "id": "m1",
            "title": "films",
            "when": "16:24",
            "today": true,
            "from": "/data/a",
            "to": "/data/b",
            "others": 2,
            "hashes": [App.details.hash],
            "canBack": true,
            "canBackThis": true
        };
        wait(0);
        const order = ["errorBlock", "moveBlock", "piecesBar", "storyEvents", "storyLogLink"].map(
                    name => findChild(story, name));
        for (const item of order)
            verify(item.visible, item.objectName + " is not shown");
        for (let at = 1; at < order.length; ++at)
            verify(order[at].mapToItem(story, 0, 0).y > order[at - 1].mapToItem(story, 0, 0).y,
                   order[at].objectName + " is not under " + order[at - 1].objectName);

        // What happened starts with the add, in the torrent's state colour, and ends with the
        // error the log timed.
        const first = findChild(story, "storyEvent_0");
        compare(findChild(first, "storyEventTitle").text, "Added");
        const events = story.story;
        verify(events.length >= 1);
        compare(events[0].kind, "plain");
        const last = events[events.length - 1];
        if (last.kind === "error")
            compare(last.title, "The disk is full");

        // The last move's actions and the log link go up as before (C18 D7, docs/32 D13).
        moveBackSpy.target = view;
        moveBackSpy.clear();
        findChild(story, "moveBlock").actionTriggered("backThis");
        compare(moveBackSpy.count, 1);
        compare(moveBackSpy.signalArguments[0][0], "m1");
        compare(moveBackSpy.signalArguments[0][1], App.details.hash);
        problemSpy.target = view;
        problemSpy.clear();
        story.logRequested();
        compare(problemSpy.count, 1);
        compare(problemSpy.signalArguments[0][0], "log");
        // "rename" on the error block opens files; "copy" copies the line.
        findChild(story, "errorBlock").actionTriggered("rename");
        compare(view.currentTab, view.tabFiles);
        view.activateTab(view.tabStory);
    }

    // A healthy torrent's story has no blocks: the pieces and what happened.
    function test_a_quiet_story_has_no_blocks() {
        const view = testCase.select(testCase.bbbName);
        const story = findChild(view, "storyTab");
        verify(!findChild(story, "errorBlock").visible);
        verify(!findChild(story, "failureBlock").visible);
        verify(!findChild(story, "moveBlock").visible);
        verify(findChild(story, "storyEvents").visible);
        compare(findChild(story, "storyHeading").text, "what happened");
        // Finished: added, then finished downloading.
        const titles = story.story.map(event => event.title);
        compare(titles.slice(0, 2), ["Added", "Finished downloading"]);
    }

    // docs/32 F1 (#55): the demo's move of Spring to /mnt/archive/movies didn't happen. The
    // story opens with #54's block in yellow: what didn't happen, why, where the files still are,
    // and set location, open folder, copy the line, dismiss. No red block: it isn't an error.
    function test_a_move_that_did_not_happen_says_so_in_yellow() {
        const view = testCase.select("Spring (2019) 4K (CC-BY)");
        verify(!findChild(view, "errorBlock").visible);
        const block = findChild(view, "failureBlock");
        verify(block.visible);
        compare(block.kindColor, Theme.yellow);
        compare(findChild(block, "errorHeadline").text, "The move to archive/movies didn't happen");
        compare(findChild(block, "errorDetail").text, "Permission denied, moving spring.mkv");
        compare(findChild(block, "errorFolder").text, "Its files are still in /data/torrents/movies");
        verify(findChild(block, "errorMeta").text.startsWith("qBittorrent's log · "));
        compare(block.hints.map(hint => (hint.key + " " + hint.label).trim()),
                ["m move…", "o open folder", "copy the line", "its lines in the log", "x dismiss"]);

        // `x` on the story asks for the dismissal; on files it would skip a file instead.
        failureSpy.target = view;
        failureSpy.clear();
        view.forceActiveFocus();
        keyClick("x");
        compare(failureSpy.count, 1);
        compare(failureSpy.signalArguments[0][0], "dismiss");

        // docs/32 D13: "its lines in the log" goes up to the window, which opens the log face.
        block.actionTriggered("log");
        compare(failureSpy.count, 2);
        compare(failureSpy.signalArguments[1][0], "log");

        // Dismissed, it is gone from the row for good (docs/32 D3).
        App.dismissFailure(App.details.hash);
        view.torrent = App.torrentRow(App.details.hash);
        verify(!findChild(view, "failureBlock").visible);
    }

    // docs/32 F2: a rename's `2` opens Files with the cursor on the file that kept its name.
    function test_show_file_puts_the_files_cursor_on_it() {
        const view = testCase.select(testCase.bbbName);
        const files = App.details.files;
        verify(files.count > 2);
        const path = files.pathAt(2);
        view.showFile(path);
        compare(view.currentTab, 1);
        compare(view.filesCursor, 2);
        compare(view.filesCursorPath, path);
        view.activateTab(view.tabStory);
    }

    // docs/32 W1: under each URL, what the log says: the demo's geo mirror doesn't resolve,
    // twice; nothing was logged about the others.
    function test_each_http_source_says_what_the_log_says() {
        const view = testCase.select(testCase.archName);
        view.activateTab(view.tabWebSeeds);
        waitForRendering(view, 500);
        compare(findChild(view, "webSeedError_1").text, "host not found");
        verify(findChild(view, "webSeedStatus_1").text.startsWith(" · 2 times, last "),
               findChild(view, "webSeedStatus_1").text);
        compare(findChild(view, "webSeedDot_1").color, Theme.red);
        verify(!findChild(view, "webSeedError_0").visible);
        compare(findChild(view, "webSeedStatus_0").text, "no errors logged");
        compare(findChild(view, "webSeedDot_0").color, Theme.muted);
        view.activateTab(view.tabStory);
    }

    function test_the_tracker_badge_follows_the_torrent() {
        const arch = testCase.select(testCase.archName);
        // Spec §2 Fig. 3's middle row times out, so Fig. 1's tab strip badges Trackers.
        verify(App.details.trackers.count > 0);
        verify(arch.hasTrackerError);

        const bbb = testCase.select(testCase.bbbName);
        verify(!bbb.hasTrackerError);
    }

    function test_general_tab_rows_are_the_figure() {
        testCase.select(testCase.archName);
        const properties = App.details.properties;

        compare(properties.savePath, "/data/torrents/linux/");
        compare(properties.pieces, "1 264 × 1 MiB");
        compare(properties.piecesHave, "617 of 1 264");
        compare(properties.availability, "38.20");
        compare(properties.seeds, "21 connected · 1 204 total");
        compare(properties.comment, "Arch Linux 2026.09.01 release");
        compare(properties.createdBy, "mktorrent 1.1");
        // Spec §2 Fig. 1 has no "Created on" row; the demo leaves the date out.
        compare(properties.createdOn, "");
    }

    // docs/12 G1: Transfer, Information, Options, in that order, each row in its group.
    function test_general_is_grouped_transfer_information_options() {
        const view = testCase.select(testCase.archName);
        const general = findChild(view, "factsTab");
        verify(general !== null);

        const groups = general.groups;
        compare(groups.map(group => group.label), ["Transfer", "Information", "Options"]);
        const keys = groups.map(group => group.rows.map(row => row.key));
        compare(keys[0], ["Size", "Done", "Ratio", "ETA", "Status", "Downloaded", "Uploaded",
                          "Wasted", "Seeds", "Peers", "Connections", "Availability", "Avg speed",
                          "Time active", "Last seen full", "Reannounce"]);
        // No hash v2, no creation or completion date in the demo's arch row.
        compare(keys[1].filter(key => ["Category", "Tags", "Added"].indexOf(key) < 0),
                ["Save path", "Pieces", "Hash v1", "Magnet", "Private", "Created by", "Comment"]);
        compare(keys[2][0], "Share limit");

        // Each group has its heading on screen.
        view.activateTab(view.tabFacts);
        for (const label of ["Transfer", "Information", "Options"]) {
            const heading = findChild(view, "factsHeading_" + label);
            verify(heading !== null && heading.visible, "no heading for " + label);
        }

        // The magnet is shown short; its row is dim.
        const magnet = groups[1].rows.find(row => row.key === "Magnet");
        verify(magnet.value.endsWith("…"));
        compare(magnet.dim, true);
    }

    // docs/10 §5 with G1: two columns put Transfer alone on the left.
    function test_two_columns_keep_the_groups_whole() {
        const view = testCase.select(testCase.archName);
        view.width = 1100;
        view.wide = true;
        wait(0); // let the columns lay out
        const general = findChild(view, "factsTab");
        compare(general.columns, 2);
        const transfer = findChild(view, "factsGroup_Transfer");
        const information = findChild(view, "factsGroup_Information");
        const options = findChild(view, "factsGroup_Options");
        verify(transfer.mapToItem(general, 0, 0).x < information.mapToItem(general, 0, 0).x);
        compare(information.mapToItem(general, 0, 0).x, options.mapToItem(general, 0, 0).x);
        verify(options.mapToItem(general, 0, 0).y > information.mapToItem(general, 0, 0).y);
    }

    // docs/13: under the table the head and the fact share one band (the fact where the strip
    // was, DECISIONS C24 H6), and the tabs and their body take the pane's whole width below it.
    function test_wide_puts_the_head_and_fact_in_one_band_over_full_width_tabs() {
        const view = testCase.select(testCase.archName);
        view.width = 1100;
        view.wide = true;
        wait(0);
        const hero = findChild(view, "detailsHero");
        const story = findChild(view, "storyTab");
        const heroAt = hero.mapToItem(view, 0, 0);
        compare(heroAt.x + hero.width, view.width);
        compare(hero.width, Theme.space(440));
        compare(hero.rule, false);
        const storyAt = story.mapToItem(view, 0, 0);
        compare(storyAt.x, 0);
        compare(story.width, view.width);
        verify(storyAt.y >= heroAt.y + hero.height, "the tabs overlap the band");
        const tabs = findChild(view, "detailsTabs");
        verify(tabs.mapToItem(view, 0, 0).y >= heroAt.y + hero.height, "the fact is not in the band");

        // Narrow, the fact gives the name half the pane at least.
        view.width = 600;
        wait(0);
        compare(hero.width, 300);

        // Docked at the side, the fact is back under the head, full width, with its rule.
        view.wide = false;
        wait(0);
        compare(hero.width, view.width);
        compare(hero.rule, true);
    }

    // docs/15 Q2 A: below 700 px (base 14) the band stops sharing its line. The name goes on
    // top at the full width, the fact and its sentence across the whole width under it, then
    // the tabs.
    function test_a_narrow_band_puts_the_fact_under_the_name() {
        const view = testCase.select(testCase.archName);
        view.wide = true;
        const hero = findChild(view, "detailsHero");
        const tabs = findChild(view, "detailsTabs");
        const name = findChild(view, "headLabels").parent.parent; // the head's column

        view.width = Theme.space(600);
        wait(0);
        verify(view.band);
        compare(hero.width, Math.floor(view.width / 2));

        view.width = Theme.space(599);
        wait(0);
        verify(!view.band);
        compare(hero.width, view.width);
        compare(hero.mapToItem(view, 0, 0).x, 0);
        compare(name.width, view.width - 2 * Theme.space(16));
        verify(hero.mapToItem(view, 0, 0).y >= name.mapToItem(view, 0, 0).y + name.height,
               "the fact is not under the name");
        verify(tabs.mapToItem(view, 0, 0).y >= hero.mapToItem(view, 0, 0).y + hero.height,
               "the tabs overlap the fact");
        compare(hero.rule, true);

        // And at the 560 px floor the fact keeps the whole width.
        view.width = Theme.space(480);
        wait(0);
        verify(!view.band);
        compare(hero.width, view.width);
    }

    // docs/12 G3: the pieces bar is in the story (C24 H6), drawn from the demo's piece states.
    function test_the_pieces_bar_is_in_the_story() {
        const view = testCase.select(testCase.archName);
        const bar = findChild(view, "piecesBar");
        verify(bar !== null);
        verify(bar.visible, "no pieces bar for a torrent with pieces");
        compare(App.details.pieceCount, 1264);
        compare(App.details.piecesHaveCount, 617);
        compare(findChild(view, "piecesLabel").text, "pieces · 617 of 1 264");
        compare(findChild(view, "piecesPercent").text, "48.9 %");

        // In the story, above what happened.
        wait(0);
        const story = findChild(view, "storyTab");
        verify(bar.parent.parent === story.contentItem, "the bar is not the story's");
        const events = findChild(view, "storyEvents");
        verify(bar.mapToItem(story, 0, 0).y < events.mapToItem(story, 0, 0).y);

        // The Meter asked for one cell per slot it draws (a pixel each in Omarchy's face, a
        // cell each in Console's, #171 step 6): some had, some in flight, some missing.
        const canvas = findChild(view, "piecesCanvas");
        tryVerify(() => canvas.cells.length > 0);
        compare(canvas.slots, Theme.skin.name === "console" ? canvas.face.columns : Math.floor(canvas.width));
        compare(canvas.cells.length, Math.min(1264, canvas.slots));
        verify(canvas.cells.indexOf(2) >= 0);
        verify(canvas.cells.indexOf(1) >= 0);
        verify(canvas.cells.indexOf(0) >= 0);

        // A finished torrent is had from end to end.
        testCase.select(testCase.bbbName);
        compare(App.details.piecesHaveCount, App.details.pieceCount);
    }

    // docs/12 G5: j k walk facts' rows across the groups; y copies the whole value.
    function test_facts_rows_take_a_cursor_and_y_copies_the_whole_value() {
        const view = testCase.select(testCase.archName);
        const general = findChild(view, "factsTab");
        view.activateTab(view.tabFacts);
        view.forceActiveFocus();
        valueSpy.target = view;
        valueSpy.clear();

        compare(view.cursorIndex, 0);
        verify(findChild(view, "factsRow_0").hasCursor);
        // #77: a row is a list item named by its key, the value its description; a screen
        // reader's press is the click: the cursor goes there and the value is copied.
        const second = findChild(view, "factsRow_1");
        compare(second.Accessible.role, Accessible.ListItem);
        compare(second.Accessible.name, second.entry.key);
        verify(second.Accessible.description.indexOf(second.entry.value) === 0);
        second.Accessible.pressAction();
        compare(view.cursorIndex, 1);
        compare(valueSpy.count, 1);
        valueSpy.clear();
        keyClick("k");
        compare(view.cursorIndex, 0);
        keyClick("j");
        compare(view.cursorIndex, 1);
        verify(findChild(view, "factsRow_1").hasCursor);
        verify(!findChild(view, "factsRow_0").hasCursor);
        keyClick("k");
        keyClick("k");
        compare(view.cursorIndex, 0); // clamped at the top

        // G goes to the last row, in Options; the cursor crosses the group headings.
        keyClick(Qt.Key_G, Qt.ShiftModifier);
        compare(view.cursorIndex, general.count - 1);
        compare(general.rows[view.cursorIndex].key, "Auto manage");

        // The hash, whole.
        const hashRow = general.rows.findIndex(row => row.key === "Hash v1");
        verify(hashRow > 0);
        view.moveCursor(hashRow - view.cursorIndex);
        compare(view.cursorIndex, hashRow);
        keyClick("y");
        compare(valueSpy.count, 1);
        compare(valueSpy.signalArguments[0][0], App.details.properties.hashV1);
        compare(valueSpy.signalArguments[0][1], "hash v1");

        // The magnet: the screen shows it short, y copies all of it.
        const magnetRow = general.rows.findIndex(row => row.key === "Magnet");
        view.moveCursor(magnetRow - view.cursorIndex);
        keyClick("y");
        compare(valueSpy.count, 2);
        compare(valueSpy.signalArguments[1][0], App.details.properties.magnet);
        verify(valueSpy.signalArguments[1][0].length > general.rows[magnetRow].value.length);
        compare(valueSpy.signalArguments[1][1], "magnet");
    }

    // A hash that does not fit breaks at its middle, never two characters over; wide, it
    // stays on one line. Either way y copies it whole (above).
    function test_a_hash_that_does_not_fit_breaks_in_half() {
        const view = testCase.select(testCase.archName);
        const general = findChild(view, "factsTab");
        const hashRow = general.rows.findIndex(row => row.key === "Hash v1");
        const hash = App.details.properties.hashV1;
        compare(hash.length, 40);
        const value = findChild(findChild(view, "factsRow_" + hashRow), "factsValue");
        compare(value.text, hash.slice(0, 20) + "\n" + hash.slice(20));

        view.width = 1400;
        tryCompare(value, "text", hash);
    }

    // docs/12 G5: a click on a row moves the cursor there and copies the value.
    function test_a_click_on_a_facts_row_copies_it() {
        const view = testCase.select(testCase.archName);
        view.activateTab(view.tabFacts);
        const general = findChild(view, "factsTab");
        valueSpy.target = view;
        valueSpy.clear();

        const pathRow = general.rows.findIndex(row => row.key === "Save path");
        // Offscreen delivers no mouse events: the row's own signal stands in for the click.
        findChild(view, "factsRow_" + pathRow).clicked();
        compare(view.cursorIndex, pathRow);
        compare(valueSpy.count, 1);
        compare(valueSpy.signalArguments[0][0], "/data/torrents/linux/");
        compare(valueSpy.signalArguments[0][1], "save path");
    }

    // docs/12 G4: the Options group's on/off rows, and `enter` on one turns it over.
    function test_enter_toggles_an_option_row() {
        const view = testCase.select(testCase.archName);
        view.activateTab(view.tabFacts);
        const hash = App.details.hash;
        const general = findChild(view, "factsTab");
        const keys = general.groups[2].rows.map(row => row.key);
        compare(keys, ["Share limit", "Sequential", "First / last", "Super seed",
                       "Auto manage"]);

        const before = App.torrentRow(hash).seqDl;
        const sequential = general.rows.findIndex(row => row.option === "sequential");
        compare(general.rows[sequential].value, before ? "on" : "off");
        view.forceActiveFocus();
        view.moveCursor(sequential - view.cursorIndex);
        keyClick(Qt.Key_Return);
        // The demo flips the fictional row; the pane redraws from the next snapshot.
        compare(App.torrentRow(hash).seqDl, !before);
        view.torrent = App.torrentRow(hash);
        compare(general.rows[sequential].value, !before ? "on" : "off");
        compare(general.rows[sequential].on, !before);

        // The cursor stays on the row, so `enter` again turns it back.
        compare(view.cursorIndex, sequential);
        keyClick(Qt.Key_Return);
        compare(App.torrentRow(hash).seqDl, before);

        // Each option reaches its own field.
        for (const [option, field] of [["firstLast", "firstLastPiecePrio"],
                                       ["superSeeding", "superSeeding"],
                                       ["autoManagement", "autoTmm"]]) {
            view.torrent = App.torrentRow(hash);
            const row = general.rows.findIndex(entry => entry.option === option);
            const was = App.torrentRow(hash)[field];
            view.moveCursor(row - view.cursorIndex);
            keyClick(Qt.Key_Enter);
            compare(App.torrentRow(hash)[field], !was, option);
            view.toggleGeneralOption(row); // and back, for the next case
            view.torrent = App.torrentRow(hash);
        }
    }

    // docs/12 G4: `enter` on a row that is not an option is not the pane's to take.
    function test_enter_on_a_plain_row_changes_nothing() {
        const view = testCase.select(testCase.archName);
        compare(view.toggleGeneralOption(0), false); // Size
        const general = findChild(view, "factsTab");
        const shareLimit = general.rows.findIndex(row => row.key === "Share limit");
        compare(view.toggleGeneralOption(shareLimit), false);
    }

    // docs/12 G5: the pane's own hint row teaches facts too; the story has no keys of its own.
    function test_facts_has_hints_of_its_own() {
        const view = testCase.select(testCase.archName);
        const hints = findChild(view, "detailsHints");
        verify(hints !== null && hints.visible);
        compare(hints.model.length, 0);
        view.activateTab(view.tabFacts);
        compare(hints.model.map(hint => hint.key + " " + hint.label), ["j k move", "y copy value",
                                                                          "enter change"]);
        view.activateTab(view.tabStory);
    }

    // Ruling Q4: Fig. 3/4's `a` add and `b` ban are built, so the pane teaches them again.
    function test_trackers_and_peers_teach_add_and_ban() {
        const view = testCase.select(testCase.archName);
        const hints = findChild(view, "detailsHints");
        view.activateTab(view.tabTrackers);
        compare(hints.model.map(hint => hint.key + " " + hint.label), ["a add", "d remove",
                                                                          "R reannounce", "y copy",
                                                                          "space mark", "ctrl+a mark all"]);
        view.activateTab(view.tabSwarm);
        compare(hints.model.map(hint => hint.key + " " + hint.label), ["b ban", "a add peer",
                                                                          "y copy", "space mark",
                                                                          "ctrl+a mark all"]);
        view.activateTab(view.tabStory);
    }

    // docs/12 G2: the demo fills every new row, so `--demo` shows them all.
    function test_the_demo_fills_the_docs12_rows() {
        testCase.select(testCase.archName);
        const properties = App.details.properties;

        compare(properties.connections, "63 of 100");
        compare(properties.avgSpeed, "↓ 1.72 MB/s · ↑ 96 KB/s");
        verify(properties.reannounce.startsWith("in "));
        verify(properties.lastSeen.length > 0);
        verify(properties.private === "yes" || properties.private === "no");
        verify(properties.magnet.startsWith("magnet:?xt=urn:btih:"));
        verify(properties.magnetText.endsWith("…"));

        // A finished one has a seeding time and a completion date as well.
        testCase.select(testCase.bbbName);
        const finished = App.details.properties;
        verify(finished.seedingTime.length > 0, "no seeding time for a complete torrent");
        verify(finished.completedOn.length > 0, "no completion date for a complete torrent");
    }

    function test_files_tab_is_figure_two() {
        const view = testCase.select(testCase.bbbName);
        view.activateTab(1);
        compare(view.currentTab, 1);

        const files = App.details.files;
        compare(files.count, 7);

        const names = ["Big Buck Bunny 4K/", "bbb_sunflower_2160p_60fps.mp4",
                       "bbb_sunflower_1080p_30fps.mp4", "subs/", "bbb.en.srt", "bbb.pt.srt",
                       "LICENSE.txt"];
        const depths = [0, 1, 1, 1, 2, 2, 1];
        const priorities = ["mixed", "high", "normal", "skip", "skip", "skip", "normal"];

        for (let row = 0; row < files.count; ++row) {
            const index = files.index(row, 0);
            compare(files.data(index, TorrentFilesModel.NameRole), names[row]);
            compare(files.priorityText(files.priority(row)), priorities[row]);
            compare(files.pathAt(row).split("/").length - 1, depths[row]);
        }

        compare(files.isFolder(0), true);
        compare(files.isFolder(1), false);
        compare(files.pathAt(4), "Big Buck Bunny 4K/subs/bbb.en.srt");
    }

    function test_fold_hides_the_subtree() {
        const view = testCase.select(testCase.bbbName);
        view.activateTab(1);
        compare(App.details.files.count, 7);

        // The cursor starts on the root folder; `h` folds it away.
        compare(view.cursorIndex, 0);
        view.fold();
        compare(App.details.files.count, 1);

        // `l` opens it again.
        view.unfold();
        compare(App.details.files.count, 7);

        // `h` on a file row is not a fold: it hands the focus back to the table.
        focusSpy.target = view;
        focusSpy.clear();
        view.moveCursor(1);
        compare(view.cursorIndex, 1);
        view.fold();
        compare(focusSpy.count, 1);
        compare(App.details.files.count, 7);
    }

    function test_move_cursor_is_clamped_to_the_list() {
        const view = testCase.select(testCase.bbbName);
        view.activateTab(1);

        view.moveCursor(-1);
        compare(view.cursorIndex, 0);
        view.moveCursor(3);
        compare(view.cursorIndex, 3);
        view.moveCursor(99);
        compare(view.cursorIndex, 6);

        // Each tab keeps its own place.
        view.activateTab(view.tabTrackers);
        compare(view.cursorIndex, 0);
        view.moveCursor(1);
        compare(view.cursorIndex, 1);
        view.activateTab(1);
        compare(view.cursorIndex, 6);
    }

    function test_trackers_and_peers_tabs_are_figures_three_and_four() {
        const view = testCase.select(testCase.archName);

        view.activateTab(view.tabTrackers);
        const trackers = App.details.trackers;
        compare(trackers.count, 5);
        compare(trackers.data(trackers.index(0, 0), TorrentTrackersModel.HostRole),
                "tracker.example.net");
        compare(trackers.data(trackers.index(3, 0), TorrentTrackersModel.HostRole), "DHT");

        view.activateTab(view.tabSwarm);
        const peers = App.details.peers;
        compare(peers.count, 4);
        // Spec §2 Fig. 4's first row, fastest first.
        compare(peers.data(peers.index(0, 0), TorrentPeersModel.IpRole), "185.21.216.140");
        compare(peers.addressAt(0), "185.21.216.140:51413");
    }

    // Review VIEW-13: a shorter list pulls the Trackers cursor back onto a row.
    function test_the_trackers_cursor_stays_on_a_row_when_the_list_shrinks() {
        const view = testCase.select(testCase.archName);
        view.activateTab(view.tabTrackers);
        const trackers = App.details.trackers;
        view.moveCursor(trackers.count);
        compare(view.trackersCursor, trackers.count - 1);
        const longest = trackers.count;
        let shorter = "";
        for (let row = 0; row < App.filtered.count && shorter.length === 0; ++row) {
            App.selectTorrent(App.filtered.hashAt(row));
            if (trackers.count > 0 && trackers.count < longest)
                shorter = App.filtered.hashAt(row);
        }
        if (shorter.length === 0)
            skip("the demo library has no torrent with fewer trackers");
        verify(view.trackersCursor < trackers.count, "the cursor was left past the end");
    }

    function test_copy_emits_the_row_the_cursor_is_on() {
        const view = testCase.select(testCase.archName);
        copySpy.target = view;
        copySpy.clear();

        view.activateTab(view.tabTrackers);
        view.copyCurrent();
        compare(copySpy.count, 1);
        compare(copySpy.signalArguments[0][0], "https://tracker.example.net:443/announce");

        view.activateTab(view.tabSwarm);
        view.copyCurrent();
        compare(copySpy.count, 2);
        compare(copySpy.signalArguments[1][0], "185.21.216.140:51413");
    }

    function test_open_emits_the_file_path() {
        const view = testCase.select(testCase.bbbName);
        view.activateTab(1);
        view.moveCursor(4);

        openSpy.target = view;
        openSpy.clear();
        view.forceActiveFocus();
        keyClick(Qt.Key_O);
        compare(openSpy.count, 1);
        compare(openSpy.signalArguments[0][0], "Big Buck Bunny 4K/subs/bbb.en.srt");
    }

    function test_number_keys_switch_tabs() {
        const view = testCase.select(testCase.archName);
        view.forceActiveFocus();

        // DECISIONS C24 H6: story · files · swarm · trackers · facts.
        keyClick(Qt.Key_2);
        compare(view.currentTab, view.tabFiles);
        keyClick(Qt.Key_3);
        compare(view.currentTab, view.tabSwarm);
        keyClick(Qt.Key_4);
        compare(view.currentTab, view.tabTrackers);
        keyClick(Qt.Key_5);
        compare(view.currentTab, view.tabFacts);
        keyClick(Qt.Key_1);
        compare(view.currentTab, view.tabStory);
        compare(findChild(view, "detailsTabs").model.slice(0, 5).map(tab => tab.text),
                ["story", "files", "swarm", "trackers", "facts"]);

        // j/k move the cursor of whichever list is on show.
        keyClick(Qt.Key_4);
        keyClick(Qt.Key_J);
        compare(view.cursorIndex, 1);
        keyClick(Qt.Key_K);
        compare(view.cursorIndex, 0);
    }

    function test_collapsed_pane_has_no_width() {
        const view = testCase.select(testCase.archName);
        verify(view.implicitWidth > 0);

        view.collapsed = true;
        compare(view.implicitWidth, 0);
        compare(view.visible, false);
    }

    // ---- N1: the Files tab ------------------------------------------------------------

    function filesView() {
        const view = testCase.select(testCase.bbbName);
        view.activateTab(1);
        view.forceActiveFocus();
        return view;
    }

    SignalSpy {
        id: freeSpy

        signalName: "freeSpaceRequested"
    }

    /// A reading of Big Buck Bunny's two skipped subtitles as App.skippedOnDisk gives it.
    function heldReading(outlook, version) {
        return {
            "hash": App.details.hash,
            "seen": true,
            "holding": 2,
            "held": 1105954816,
            "heldText": "1.03 GB",
            "band": true,
            "where": "/data",
            "fileSystem": "ext4",
            "outlook": outlook,
            "version": version,
            "readdText": "1.03 GB",
            "readdFiles": outlook === "readd" ? 2 : 1,
            "stillText": "1.03 GB still on disk",
            "busy": false
        };
    }

    // DECISIONS C30 F12: on a hybrid whose space-holding skipped files all have downloaded
    // pieces, the band keeps its title, says only adding it again frees them, and offers that;
    // Why? says hybrid. F17: the summary says what they still hold.
    function test_the_band_says_add_it_again_when_nothing_else_frees_them() {
        const view = testCase.filesView();
        const tab = findChild(view, "filesTab");
        tab.skipped = testCase.heldReading("readd", "hybrid");
        const band = findChild(view, "filesHeldBand");
        verify(band.shown);
        compare(band.title, "2 skipped files hold 1.03 GB on /data");
        compare(band.body, "They were written while wanted, and pieces of them were downloaded. qBittorrent "
                + "keeps them; on this torrent only adding it again frees them.");
        compare(band.actions.map(action => action.label), ["Add it again…  F"]);
        verify(band.details.startsWith("This torrent is hybrid (v1 and v2)."), band.details);
        compare(band.detailsLabel, "Why?");
        compare(findChild(view, "filesSkippedText").text, "2 skipped, 1.03 GB still on disk");
        verify(!findChild(view, "filesSkippedKey").visible, "the key cap sits inside the words");

        // v2-only says v2 (C29).
        tab.skipped = testCase.heldReading("readd", "v2");
        verify(band.details.startsWith("This torrent is v2."), band.details);

        // Its action and shift+f both go up; the window picks the confirm (MainView).
        freeSpy.target = view;
        freeSpy.clear();
        band.actionTriggered("free");
        compare(freeSpy.count, 1);
        keyClick(Qt.Key_F, Qt.ShiftModifier);
        compare(freeSpy.count, 2);
    }

    // F15's case keeps the band's "Free the space…" (the sheet says the rest), as does a plain
    // one; their body is C17's.
    function test_the_band_keeps_free_the_space_when_some_can_go() {
        const view = testCase.filesView();
        const tab = findChild(view, "filesTab");
        const band = findChild(view, "filesHeldBand");
        for (const outlook of ["mixed", "free"]) {
            tab.skipped = testCase.heldReading(outlook, outlook === "mixed" ? "hybrid" : "v1");
            compare(band.actions.map(action => action.label), ["Free the space…"], outlook);
            compare(band.body, "They were written while wanted. qBittorrent keeps them; it never deletes a "
                    + "skipped file.");
            verify(band.details.startsWith("Wanting a file makes qBittorrent create it"), band.details);
        }
        // Nothing held on a disk it sees: no tail, the key cap back.
        tab.skipped = {};
        compare(findChild(view, "filesSkippedText").text, "2 skipped");
    }

    // C30 F16: shift+f before the reading is this torrent's goes up anyway, so the sheet reads
    // first; with no skipped file at all it does nothing.
    function test_shift_f_before_the_reading_asks_for_the_sheet() {
        const view = testCase.filesView();
        const tab = findChild(view, "filesTab");
        tab.skipped = {};
        verify(view.canFree);
        freeSpy.target = view;
        freeSpy.clear();
        keyClick(Qt.Key_F, Qt.ShiftModifier);
        compare(freeSpy.count, 1);
    }

    function test_slash_filters_the_files_and_esc_clears_it() {
        const view = testCase.filesView();
        const files = App.details.files;

        keyClick("/");
        verify(view.filterEditing, "`/` did not put the caret in the files filter");
        verify(view.editing);
        keyClick("s");
        keyClick("r");
        keyClick("t");
        compare(files.filterText, "srt");
        // The folders of a match stay, so the tree still reads.
        compare(files.count, 4);
        compare(files.matchCount, 2);

        // `enter` hands the keyboard back to the list and keeps the filter.
        keyClick(Qt.Key_Return);
        verify(!view.filterEditing);
        compare(files.filterText, "srt");
        // `esc` on the list clears it, and the emptied field goes with it.
        keyClick(Qt.Key_Escape);
        compare(files.filterText, "");
        compare(files.count, 7);
        verify(!findChild(view, "filesFilter").visible, "an emptied filter stayed on screen");
    }

    function test_esc_closes_an_empty_files_filter() {
        const view = testCase.filesView();
        const field = findChild(view, "filesFilter");

        keyClick("/");
        verify(field.visible);
        verify(view.filterEditing);
        // Nothing typed: `esc` closes the field and gives the list the keyboard back.
        keyClick(Qt.Key_Escape);
        verify(!field.visible, "`esc` left the empty filter open");
        verify(!view.filterEditing);
        verify(view.activeFocus);
    }

    function test_esc_while_typing_clears_and_closes_the_files_filter() {
        const view = testCase.filesView();
        const files = App.details.files;
        const field = findChild(view, "filesFilter");

        keyClick("/");
        keyClick("s");
        keyClick("r");
        keyClick("t");
        compare(files.matchCount, 2);
        keyClick(Qt.Key_Escape);
        compare(files.filterText, "");
        verify(!field.visible);
        verify(!view.filterEditing);
        verify(view.activeFocus);
    }

    // docs/12 F1: the summary line is always there, and `/` turns the same row into the field.
    function test_the_summary_line_turns_into_the_filter_and_back() {
        const view = testCase.filesView();
        const summary = findChild(view, "filesSummary");
        const field = findChild(view, "filesFilter");
        const header = findChild(view, "fileHeader_name");
        verify(summary !== null && field !== null && header !== null);
        wait(0);

        verify(summary.visible);
        verify(!field.visible);
        compare(findChild(view, "filesSummaryCount").text, "5 files");
        compare(findChild(view, "filesSummaryFacts").text, " · 2 folders · 3.91 GB");
        verify(findChild(view, "filesSkippedToggle").visible);
        compare(findChild(view, "filesSkippedText").text, "2 skipped");
        verify(findChild(view, "filesSummaryGo").visible);
        const headerY = header.mapToItem(view, 0, 0).y;

        keyClick("/");
        verify(!summary.visible);
        verify(field.visible);
        compare(field.trailingKey, "esc");
        keyClick("s");
        keyClick("r");
        keyClick("t");
        compare(field.trailing, "2 of 5 files");
        // The same row: the headers and the list do not move.
        compare(header.mapToItem(view, 0, 0).y, headerY);

        keyClick(Qt.Key_Escape);
        verify(summary.visible);
        verify(!field.visible);
        compare(header.mapToItem(view, 0, 0).y, headerY);
    }

    // #9: `X` hides the skipped files (Fig. 2's two subtitles, and `subs/` with them), the
    // summary says so, and the cursor stays on its file.
    function test_X_hides_the_skipped_files_and_keeps_the_cursor() {
        const view = testCase.filesView();
        const files = App.details.files;
        const text = findChild(view, "filesSkippedText");
        view.filesCursor = files.rowOfPath("Big Buck Bunny 4K/LICENSE.txt");
        compare(view.filesCursor, 6);

        keyClick(Qt.Key_X, Qt.ShiftModifier);
        verify(files.hideSkipped);
        compare(files.count, 4);
        compare(files.rowOfPath("Big Buck Bunny 4K/subs"), -1);
        compare(text.text, "2 hidden");
        compare(text.color, Theme.foreground);
        compare(files.pathAt(view.filesCursor), "Big Buck Bunny 4K/LICENSE.txt");

        // `x` alone is still skip, and the file it skips goes at once while they are hidden.
        keyClick("x");
        verify(files.hideSkipped);
        compare(files.rowOfPath("Big Buck Bunny 4K/LICENSE.txt"), -1);
        compare(text.text, "3 hidden");

        keyClick(Qt.Key_X, Qt.ShiftModifier);
        verify(!files.hideSkipped);
        compare(files.count, 7);
        compare(text.text, "3 skipped");
        compare(text.color, Theme.dimForeground);
    }

    // #9: the file menu carries the switch too, worded for the way it will go.
    function test_the_file_menu_hides_and_shows_the_skipped_files() {
        const view = testCase.filesView();
        const files = App.details.files;
        view.openFileMenu(-1, -1);
        verify(view.editing, "enter did not open the file menu");
        // A menu runs a row by the key's text, which is "X" with shift held.
        keyClick("X");
        verify(files.hideSkipped);
        verify(!view.editing);
        view.openFileMenu(-1, -1);
        keyClick("X");
        verify(!files.hideSkipped);
    }

    // #9: everything skipped and hidden leaves an empty list that says why.
    function test_every_file_skipped_and_hidden_says_so() {
        const view = testCase.filesView();
        const files = App.details.files;
        const note = findChild(view, "filesAllSkipped");
        files.setLocalPriority([0, 1, 2, 3, 4], 0);
        keyClick(Qt.Key_X, Qt.ShiftModifier);
        compare(files.count, 0);
        verify(note.visible);
        compare(note.text, "every file is skipped · X shows them");
        keyClick(Qt.Key_X, Qt.ShiftModifier);
        verify(!note.visible);
        App.loadDemo(16); // the priorities back as the demo has them
    }

    // #9: with rememberViewState on (a real run), each torrent gets its own filter back. The
    // test run's XDG_CONFIG_HOME is a throwaway folder, so this state.json is not anyone's.
    function test_each_torrent_gets_its_files_filter_back() {
        const view = testCase.filesView();
        const files = App.details.files;
        const bbb = testCase.hashOf(testCase.bbbName);
        const arch = testCase.hashOf(testCase.archName);
        App.details.rememberViewState = true;

        keyClick("/");
        keyClick("s");
        keyClick("r");
        keyClick("t");
        keyClick(Qt.Key_Return);
        compare(files.filterText, "srt");

        App.selectTorrent(arch);
        compare(files.filterText, "");
        verify(!findChild(view, "filesFilter").visible);

        App.selectTorrent(bbb);
        compare(files.filterText, "srt");
        compare(files.matchCount, 2);
        verify(findChild(view, "filesFilter").visible, "the restored filter is not on screen");
        compare(findChild(view, "filesFilter").text, "srt");

        // Leave nothing behind for the next cases.
        files.filterText = "";
        App.details.rememberViewState = false;
    }

    // docs/12 F2: one file has nothing to filter; `/` says so instead of opening a field.
    function test_a_single_file_offers_no_filter() {
        const view = testCase.select(testCase.archName);
        view.activateTab(1);
        view.forceActiveFocus();
        compare(App.details.files.fileCount, 1);
        wait(0);

        compare(findChild(view, "filesSummaryCount").text, "1 file");
        compare(findChild(view, "filesSummaryFacts").text, " · 1.31 GB");
        verify(!findChild(view, "filesSummaryGo").visible, "a single file still offers a filter");

        noteSpy.target = view;
        noteSpy.clear();
        keyClick("/");
        verify(!findChild(view, "filesFilter").visible, "`/` opened a field on one file");
        verify(!view.filterEditing);
        verify(findChild(view, "filesSummary").visible);
        compare(noteSpy.count, 1);
        compare(noteSpy.signalArguments[0][0], "one file, nothing to filter");
    }

    // docs/12 F3: `ctrl+a` selects what the filter shows, and `p` then acts on all of it.
    function test_ctrl_a_selects_the_filtered_files_and_p_applies_to_them() {
        const view = testCase.filesView();
        const files = App.details.files;

        keyClick("/");
        keyClick("s");
        keyClick("r");
        keyClick("t");
        keyClick(Qt.Key_Return);
        verify(!view.filterEditing);
        compare(files.matchCount, 2);

        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(files.selectedCount, 2);
        verify(!files.isSelected(0), "a folder around the matches was selected");

        keyClick("p");
        verify(view.editing, "`p` did not open the priority menu");
        keyClick("n");
        // Both subtitles go from skip to normal; the videos stay as they were.
        files.filterText = "";
        compare(files.priority(files.rowOfPath("Big Buck Bunny 4K/subs/bbb.en.srt")), 1);
        compare(files.priority(files.rowOfPath("Big Buck Bunny 4K/subs/bbb.pt.srt")), 1);
        compare(files.priority(files.rowOfPath("Big Buck Bunny 4K/bbb_sunflower_2160p_60fps.mp4")),
                6);

        // With no filter, every file (the folders count through them).
        keyClick(Qt.Key_Escape);
        compare(files.selectedCount, 0);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(files.selectedCount, 5);
    }

    // docs/12 F4: the placeholder shows the syntax, and `*` and `|` work as it says.
    function test_the_filter_takes_wildcards_and_alternatives() {
        const view = testCase.filesView();
        const files = App.details.files;
        const field = findChild(view, "filesFilter");

        keyClick("/");
        compare(field.placeholder, "filter files · *.srt · mkv|mp4");
        for (const key of "*.srt")
            keyClick(key);
        compare(files.filterText, "*.srt");
        compare(files.matchCount, 2);
        compare(field.trailing, "2 of 5 files");

        field.text = "txt|2160p";
        compare(files.matchCount, 2);
        compare(files.count, 3); // the root folder and the two files
    }

    function test_space_selects_and_a_priority_applies_to_the_selection() {
        const view = testCase.filesView();
        const files = App.details.files;

        view.moveCursor(1);
        keyClick(Qt.Key_Space);
        view.moveCursor(5); // LICENSE.txt
        keyClick(Qt.Key_Space);
        compare(files.selectedCount, 2);

        // `x` skips both, wherever the cursor is (the demo applies it locally).
        keyClick(Qt.Key_X);
        compare(files.priority(1), 0);
        compare(files.priority(6), 0);
        compare(files.priority(2), 1); // not selected, untouched

        // `v` selects from the last toggled row to the cursor.
        view.moveCursor(-2);
        keyClick(Qt.Key_V);
        compare(files.selectedCount, 4);

        keyClick(Qt.Key_Escape);
        compare(files.selectedCount, 0);
    }

    // The click rules: ctrl+click toggles a row and moves the anchor there; shift+click makes
    // the selection exactly anchor..row and leaves the anchor; ctrl+shift+click adds that
    // range. A folder clicked with ctrl is selected, not folded.
    function test_ctrl_and_shift_click_select_files() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);

        mouseClick(findChild(view, "fileRow_1"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        compare(files.selectedCount, 1);
        verify(files.isSelected(1));
        compare(view.cursorIndex, 1);

        mouseClick(findChild(view, "fileRow_4"), 120, 4, Qt.LeftButton, Qt.ShiftModifier);
        compare(files.selectedCount, 4); // rows 1 to 4
        compare(view.cursorIndex, 4);

        // A ctrl+click on a selected row lets it go; on a folder it selects, not folds.
        mouseClick(findChild(view, "fileRow_2"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        verify(!files.isSelected(2));
        verify(files.isFolder(3));
        const expanded = findChild(view, "fileRow_3").expanded;
        mouseClick(findChild(view, "fileRow_3"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        verify(!files.isSelected(3));
        compare(findChild(view, "fileRow_3").expanded, expanded);
        compare(files.selectedCount, 2); // 1 and 4

        // The anchor is the last ctrl+click, row 3: shift replaces the selection with 3..5.
        mouseClick(findChild(view, "fileRow_5"), 120, 4, Qt.LeftButton, Qt.ShiftModifier);
        compare(files.selectedCount, 3);
        verify(files.isSelected(3) && files.isSelected(4) && files.isSelected(5));
        verify(!files.isSelected(1), "shift+click kept a row outside the range");
        compare(view.cursorIndex, 5);

        // ctrl+shift adds the anchor's range (still from row 3) to what is selected.
        mouseClick(findChild(view, "fileRow_1"), 120, 4, Qt.LeftButton,
                   Qt.ControlModifier | Qt.ShiftModifier);
        compare(files.selectedCount, 5); // 1 to 5
        compare(view.cursorIndex, 1);

        keyClick(Qt.Key_Escape);
        compare(files.selectedCount, 0);
    }

    // A plain click lets the selection go and puts the anchor on its row, so a shift+click
    // after it selects from there (it used to select the clicked row alone).
    function test_a_plain_click_clears_the_selection_and_sets_the_anchor() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);

        mouseClick(findChild(view, "fileRow_1"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        mouseClick(findChild(view, "fileRow_2"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        compare(files.selectedCount, 2);

        mouseClick(findChild(view, "fileRow_4"), 120, 4);
        compare(files.selectedCount, 0);
        compare(view.cursorIndex, 4);

        mouseClick(findChild(view, "fileRow_6"), 120, 4, Qt.LeftButton, Qt.ShiftModifier);
        compare(files.selectedCount, 3); // 4 to 6
        verify(files.isSelected(4) && files.isSelected(5) && files.isSelected(6));

        // On a folder a plain click lets the selection go too, and leaves it open (#51 D5).
        verify(findChild(view, "fileRow_3").expanded);
        mouseClick(findChild(view, "fileRow_3"), 120, 4);
        compare(files.selectedCount, 0);
        compare(view.cursorIndex, 3);
        verify(files.isExpanded(3), "a plain click folded a folder");
    }

    // #51 D5, as the add sheet's tree: the arrow folds, a double click folds, a plain click
    // only moves the cursor; a double click on a file still opens it.
    function test_a_folder_folds_on_its_arrow_or_a_double_click() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);
        verify(files.isFolder(3) && files.isExpanded(3));

        mouseClick(findChild(view, "fileRow_1"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        mouseClick(findChild(view, "fileRow_2"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        const arrow = findChild(view, "fileFold_3");
        verify(arrow !== null && arrow.enabled, "no fold arrow on the folder");
        compare(arrow.cursorShape, Qt.PointingHandCursor);
        verify(arrow.width > findChild(view, "fileRow_3").height / 2, "the arrow is hard to hit");
        mouseClick(arrow);
        verify(!files.isExpanded(3), "the arrow did not fold");
        compare(view.cursorIndex, 3);
        compare(files.selectedCount, 2, "folding let the selection go");
        mouseClick(findChild(view, "fileFold_3"));
        verify(files.isExpanded(3), "the arrow did not open it again");
        compare(findChild(view, "fileFold_1").enabled, false, "a file has a fold arrow");

        mouseDoubleClickSequence(findChild(view, "fileRow_3"), 120, 4);
        verify(!files.isExpanded(3), "a double click did not fold");
        mouseDoubleClickSequence(findChild(view, "fileRow_3"), 120, 4);
        verify(files.isExpanded(3), "a double click did not open it again");

        fileSpy.target = view;
        fileSpy.clear();
        mouseDoubleClickSequence(findChild(view, "fileRow_6"), 120, 4);
        compare(fileSpy.count, 1);
        compare(fileSpy.signalArguments[0][0], files.pathAt(6));
    }

    // The click rules: a click on the empty space under the last file lets the selection go.
    function test_a_click_on_the_empty_space_clears_the_files_selection() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);
        mouseClick(findChild(view, "fileRow_1"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        mouseClick(findChild(view, "fileRow_2"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        compare(files.selectedCount, 2);
        elsewhere.forceActiveFocus();
        const empty = findChild(view, "filesEmptySpace");
        verify(empty.height > 0);
        mouseClick(empty);
        compare(files.selectedCount, 0);
        compare(view.filesAnchorPath, "");
        verify(view.activeFocus, "the click left the keys elsewhere");
    }

    // #51 D8: a middle click toggles a row in the selection, as ctrl+click does.
    function test_a_middle_click_toggles_a_file() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);

        mouseClick(findChild(view, "fileRow_1"), 120, 4, Qt.MiddleButton);
        mouseClick(findChild(view, "fileRow_4"), 120, 4, Qt.MiddleButton);
        compare(files.selectedCount, 2);
        verify(files.isSelected(1) && files.isSelected(4));
        compare(view.cursorIndex, 4);
        compare(view.filesAnchorPath, files.pathAt(4));
        // A folder is selected, not folded.
        mouseClick(findChild(view, "fileRow_3"), 120, 4, Qt.MiddleButton);
        verify(files.isSelected(3) && files.isExpanded(3));
        mouseClick(findChild(view, "fileRow_4"), 120, 4, Qt.MiddleButton);
        verify(!files.isSelected(4));
        compare(files.selectedCount, 2);
    }

    // With no anchor yet, shift+click starts from the cursor; `v` still adds to the selection.
    function test_shift_click_without_an_anchor_starts_at_the_cursor() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);

        view.moveCursor(2);
        mouseClick(findChild(view, "fileRow_4"), 120, 4, Qt.LeftButton, Qt.ShiftModifier);
        compare(files.selectedCount, 3); // 2 to 4
        compare(view.filesAnchorPath, files.pathAt(2));

        view.moveCursor(2); // row 6
        keyClick(Qt.Key_V);
        compare(files.selectedCount, 5); // `v` adds 2..6 to what was there
    }

    // The anchor is a file, not a row number: a sort, a fold or a filter that rebuilds the
    // rows keeps it on its file, and another torrent drops it.
    function test_the_anchor_follows_its_file() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);

        mouseClick(findChild(view, "fileRow_6"), 120, 4); // LICENSE.txt
        const license = files.pathAt(6);
        view.sortFilesBy("size");
        if (files.rowOfPath(license) === 6)
            view.sortFilesBy("size"); // the other way round, then
        verify(files.rowOfPath(license) !== 6, "the sort did not move the file");
        compare(view.filesAnchorRow(), files.rowOfPath(license));

        App.selectTorrent(testCase.hashOf(testCase.archName));
        compare(view.filesAnchorPath, "");
    }

    // A right click on a row outside the selection makes that row the menu's only target;
    // on a row inside it, the selection stays and the menu acts on all of it.
    function test_a_right_click_retargets_the_file_menu() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);

        mouseClick(findChild(view, "fileRow_1"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        mouseClick(findChild(view, "fileRow_2"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        compare(files.selectedCount, 2);

        mouseClick(findChild(view, "fileRow_6"), 120, 4, Qt.RightButton);
        verify(view.menuOpen, "a right click did not open the file menu");
        compare(files.selectedCount, 0);
        compare(view.cursorIndex, 6);
        keyClick("p"); // the priority menu, for LICENSE.txt alone
        keyClick("m");
        compare(files.priority(6), 7);
        compare(files.priority(1), 6, "the old selection changed priority");
        compare(files.priority(2), 1, "the old selection changed priority");

        mouseClick(findChild(view, "fileRow_1"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        mouseClick(findChild(view, "fileRow_2"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        mouseClick(findChild(view, "fileRow_2"), 120, 4, Qt.RightButton);
        verify(view.menuOpen);
        compare(files.selectedCount, 2);
        compare(view.cursorIndex, 2);
        keyClick(Qt.Key_Escape);
        verify(!view.menuOpen);
    }

    // The menus sit on the window, so one opened low in a short pane is not cut by it.
    function test_the_file_menu_is_hosted_on_the_window() {
        const view = testCase.filesView();
        view.height = 260;
        waitForRendering(view);
        const host = findChild(view, "detailsMenuHost");
        verify(host !== null);
        compare(host.parent, view.Window.window.contentItem);
        view.openFileMenu(-1, -1);
        verify(view.menuOpen);
        keyClick(Qt.Key_Escape);
    }

    // #42 (docs/27 D2/D3): `x` skips the cursor row, and on a skipped one ticks it back at
    // the priority it had.
    function test_x_skips_then_keeps_at_the_old_priority() {
        const view = testCase.filesView();
        const files = App.details.files;
        view.moveCursor(1); // bbb_sunflower_2160p, high
        compare(files.priority(1), 6);
        keyClick(Qt.Key_X);
        compare(files.priority(1), 0);
        keyClick(Qt.Key_X);
        compare(files.priority(1), 6);
        // A file skipped from the start comes back at normal.
        view.moveCursor(3); // bbb.en.srt
        keyClick(Qt.Key_X);
        compare(files.priority(4), 1);
    }

    // #42 (docs/27 D1): one click per row in the tick column, and the head for every file.
    function test_the_tick_column_and_its_head() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);

        const tick = findChild(view, "fileTick_1");
        verify(tick, "a tick on the 2160p row");
        compare(tick.wanted, 2);
        mouseClick(tick);
        compare(files.priority(1), 0);
        compare(view.cursorIndex, 1);
        mouseClick(findChild(view, "fileTick_1"));
        compare(files.priority(1), 6);

        // A skipped folder ticks all of it.
        const subs = findChild(view, "fileTick_3");
        compare(subs.wanted, 0);
        mouseClick(subs);
        compare(files.priority(3), 1);

        const head = findChild(view, "filesHeadTick");
        verify(head, "the header's tick");
        compare(head.wanted, 2);
        mouseClick(head);
        compare(files.headWanted, 0);
        mouseClick(head);
        compare(files.headWanted, 2);
        compare(files.priority(1), 6); // high came back, not normal
    }

    function test_p_opens_the_priority_menu_and_its_letters_apply() {
        const view = testCase.filesView();
        const files = App.details.files;
        view.moveCursor(2); // bbb_sunflower_1080p, normal

        keyClick("p");
        verify(view.editing, "`p` did not open the priority menu");
        keyClick("m");
        verify(!view.editing);
        compare(files.priority(2), 7);
    }

    function test_s_sorts_and_the_cursor_follows_its_row() {
        const view = testCase.filesView();
        const files = App.details.files;
        view.moveCursor(6); // LICENSE.txt
        compare(files.pathAt(view.cursorIndex), "Big Buck Bunny 4K/LICENSE.txt");

        keyClick("s");
        keyClick("s"); // size, largest first
        compare(files.sortKey, "size");
        verify(files.sortDescending);
        compare(files.pathAt(view.cursorIndex), "Big Buck Bunny 4K/LICENSE.txt");
        compare(files.pathAt(1), "Big Buck Bunny 4K/bbb_sunflower_2160p_60fps.mp4");

        // The same key again reverses.
        keyClick("s");
        keyClick("s");
        verify(!files.sortDescending);
    }

    function test_r_renames_a_file_inline() {
        const view = testCase.filesView();
        const files = App.details.files;
        view.moveCursor(4); // subs/bbb.en.srt
        renameSpy.target = App;
        renameSpy.clear();

        keyClick("r");
        verify(view.editing, "`r` did not open the editor");
        // The name is selected up to its extension, so typing replaces "bbb.en".
        keyClick("e");
        keyClick("n");
        keyClick(Qt.Key_Return);
        compare(renameSpy.count, 1);
        compare(renameSpy.signalArguments[0][2], "Big Buck Bunny 4K/subs/en.srt");
        verify(!view.editing);
        compare(files.nameAt(4), "en.srt");
    }

    // Actions walk: a folder's rename opened with the cursor at the end of the selected
    // name, so a name wider than the editor showed "ublic Domain Sampler". The editor opens
    // with the cursor at the start, a file's stem or a folder's whole name selected.
    function test_r_opens_the_editor_at_the_start_of_the_name() {
        const view = testCase.filesView();
        const files = App.details.files;
        view.moveCursor(-files.count); // the torrent's top folder
        verify(files.isFolder(view.cursorIndex));
        keyClick("r");
        verify(view.editing);
        let editor = findChild(view, "fileRenameEditor");
        verify(editor);
        compare(editor.selectedText, files.nameAt(view.cursorIndex));
        compare(editor.cursorPosition, 0);
        keyClick(Qt.Key_Escape);
        verify(!view.editing);

        view.moveCursor(4); // subs/bbb.en.srt
        keyClick("r");
        editor = findChild(view, "fileRenameEditor");
        compare(editor.selectedText, "bbb.en");
        compare(editor.cursorPosition, 0);
        keyClick(Qt.Key_Escape);
    }

    // Review VIEW-7: a sync that re-sorts the rows under an open rename (sorted by progress,
    // say) must not move the rename or the cursor onto another file.
    function test_a_resort_under_a_rename_keeps_the_file() {
        const view = testCase.filesView();
        const files = App.details.files;
        // A file that the name sort moves.
        let from = -1;
        const before = [];
        for (let r = 0; r < files.count; ++r)
            before.push(files.pathAt(r));
        files.sortBy("name");
        for (let r = 0; r < before.length && from < 0; ++r) {
            if (!files.isFolder(files.rowOfPath(before[r])) && files.rowOfPath(before[r]) !== r)
                from = r;
        }
        files.sortKey = "";
        verify(from >= 0);
        const path = before[from];
        compare(files.pathAt(from), path);
        view.moveCursor(-files.count);
        view.moveCursor(from);
        compare(files.pathAt(view.cursorIndex), path);
        renameSpy.target = App;
        renameSpy.clear();

        keyClick("r");
        verify(view.editing);
        keyClick("L"); // replaces the selected stem
        // What a sync does when the sort key's numbers move: the rows are rebuilt in a new
        // order, without the pane asking for it.
        files.sortBy("name");
        verify(files.rowOfPath(path) !== from, "the re-sort did not move the row");
        compare(files.pathAt(view.cursorIndex), path, "the cursor stayed on the old index");
        verify(view.editing, "the rename closed");
        compare(findChild(view, "filesTab").renamingRow, files.rowOfPath(path),
                "the editor stayed on the old index");
        wait(0); // the editor takes the keyboard back once the reset has settled
        keyClick("x");
        keyClick(Qt.Key_Return);
        compare(renameSpy.count, 1);
        compare(renameSpy.signalArguments[0][1], path, "the rename went to another file");
        compare(renameSpy.signalArguments[0][3], false, "a file was renamed as a folder");
        const dir = path.substring(0, path.lastIndexOf("/") + 1);
        const ext = path.substring(path.lastIndexOf("."));
        compare(renameSpy.signalArguments[0][2], dir + "Lx" + ext);
    }

    function test_H_and_L_fold_and_open_every_folder() {
        const view = testCase.filesView();
        const files = App.details.files;
        view.foldAll(true);
        compare(files.count, 1);
        view.foldAll(false);
        compare(files.count, 7);
    }

    function test_o_and_O_ask_for_the_folder_and_the_file() {
        const view = testCase.filesView();
        view.moveCursor(1);
        fileSpy.target = view;
        fileSpy.clear();
        view.fileAction("openFile");
        compare(fileSpy.count, 1);
        compare(fileSpy.signalArguments[0][0], "Big Buck Bunny 4K/bbb_sunflower_2160p_60fps.mp4");

        // A folder has nothing to open with an application.
        view.moveCursor(-1);
        view.fileAction("openFile");
        compare(fileSpy.count, 1);
    }

    function test_y_Y_n_copy_the_path_the_local_path_and_the_name() {
        const view = testCase.filesView();
        view.moveCursor(6);
        copySpy.target = view;
        copySpy.clear();
        view.copyCurrent();
        view.copyLocalPath();
        view.copyName();
        compare(copySpy.count, 3);
        compare(copySpy.signalArguments[0][0], "Big Buck Bunny 4K/LICENSE.txt");
        verify(copySpy.signalArguments[1][0].endsWith("/Big Buck Bunny 4K/LICENSE.txt"));
        verify(copySpy.signalArguments[1][0].startsWith("/"));
        compare(copySpy.signalArguments[2][0], "LICENSE.txt");
    }

    // docs/11 J8: the tags give way before the added date does.
    function test_the_head_keeps_the_added_date_whole() {
        const view = testCase.select(testCase.bbbName);
        view.width = 220;
        const labels = findChild(view, "headLabels");
        const added = findChild(view, "headAdded");
        verify(labels !== null && added !== null);
        verify(added.text.startsWith(" · added"));
        verify(!added.truncated, "the date was cut");
        verify(labels.truncated, "the tags did not give way");
        verify(added.x + added.width <= view.width);
    }

    // Review RUN-6: narrower still, the labels go with their ` · `, and nothing draws past
    // the head, even where the head has no room at all.
    function test_the_head_drops_the_labels_and_their_separator_when_they_cannot_show() {
        const view = testCase.select(testCase.bbbName);
        const labels = findChild(view, "headLabels");
        const added = findChild(view, "headAdded");
        view.width = Theme.space(32) + added.implicitWidth + Theme.space(10);
        verify(!labels.visible, "a sliver of the labels is still drawn");
        verify(!added.text.startsWith(" · "), "the separator stayed without its labels");
        verify(added.x + added.width <= view.width);

        view.width = Theme.space(20);
        verify(added.width <= 0 || added.x + added.width <= view.width, "the date drew past the pane");
    }

    // Review VER-2: the pane is created with the torrent but no width yet (the first
    // layout), then opens. The category and tags must show, not a bare ` · added …`.
    function test_the_head_shows_the_labels_when_the_pane_widens_after_the_torrent() {
        testCase.select(testCase.bbbName);
        const row = App.torrentRow(testCase.hashOf(testCase.bbbName));
        const view = createTemporaryObject(detailsComponent, testCase, {"width": 0, "torrent": row});
        wait(0);
        view.width = 450;
        wait(0);
        const labels = findChild(view, "headLabels");
        const added = findChild(view, "headAdded");
        verify(labels.text.length > 0, "the demo row has no category or tags");
        verify(labels.visible, "the labels are hidden at 450 px");
        verify(labels.width > 0, "the labels are laid out with no width");
        verify(!labels.truncated, "the labels are cut with room to spare");
        verify(added.text.startsWith(" · added"));
        verify(added.x >= labels.width, "the date overlaps the labels");
    }

    // Review VER-2: a fresh pane at a normal width shows the labels straight away.
    function test_a_fresh_head_shows_the_labels() {
        const view = testCase.select(testCase.bbbName);
        wait(0);
        const labels = findChild(view, "headLabels");
        verify(labels.visible);
        verify(labels.width > 0, "the labels are laid out with no width");
    }

    TextMetrics {
        id: sizeSample

        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        text: "999.9 MB"
    }

    // Review RUN-6: the Size column holds a whole size in the font on screen, whatever the
    // spacing scale, so the unit is never cut.
    function test_the_size_column_holds_a_whole_size() {
        const view = testCase.filesView();
        wait(0);
        const header = findChild(view, "fileHeader_size");
        verify(header.width >= Math.ceil(sizeSample.advanceWidth),
               header.width + " < " + sizeSample.advanceWidth);
    }

    // docs/11 J2: every header starts where its column's text does.
    function test_file_headers_line_up_with_the_rows() {
        const view = testCase.filesView();
        wait(0);
        const header = findChild(view, "fileHeader_name");
        const name = findChild(view, "fileName_1");
        verify(header !== null && name !== null);
        // A file row at depth 1 is indented one step; the header lines up with depth 0,
        // where the folder's glyph starts. Compare against the row's own left edge.
        const headerX = header.mapToItem(view, 0, 0).x;
        const rowX = name.mapToItem(view, 0, 0).x - Theme.space(14);
        compare(Math.round(headerX), Math.round(rowX));
    }

    function columnHeaders(item, out) {
        for (const child of item.children) {
            if (child.visible && child.fitWidth !== undefined && child.accessibleName !== undefined)
                out.push(child);
            testCase.columnHeaders(child, out);
        }
        return out;
    }

    function test_column_captions_never_touch_data() {
        return [{
                tag: "files narrow",
                tab: 1,
                width: 340
            }, {
                tag: "swarm narrow",
                tab: 2,
                width: 340
            }, {
                tag: "trackers narrow",
                tab: 3,
                width: 340
            }, {
                tag: "files wide",
                tab: 1,
                width: 760
            }, {
                tag: "swarm wide",
                tab: 2,
                width: 760
            }, {
                tag: "trackers wide",
                tab: 3,
                width: 760
            }];
    }

    // #171: a skin's caption voice (Console's upper case, tracked .18 em) is wider than
    // Omarchy's. A fixed column is at least its caption's whole width, so no caption is cut,
    // and no two captions touch: "Seeds" ran into "Peers" ("SEEDSPEERS") in Console.
    function test_column_captions_never_touch(data) {
        const view = testCase.select(data.tab === 1 ? testCase.bbbName : testCase.archName);
        view.width = data.width;
        view.activateTab(data.tab);
        waitForRendering(view);
        const heads = testCase.columnHeaders(view, []).filter(head => head.text.length > 0);
        verify(heads.length >= 3, "only " + heads.length + " headers");
        const byRow = new Map();
        for (const head of heads) {
            const caption = findChild(head, "caption");
            if (!head.Layout.fillWidth) {
                verify(head.width >= head.fitWidth, head.text + ": " + head.width + " < "
                       + head.fitWidth);
                verify(caption.width >= caption.implicitWidth - 0.5, head.text + " is cut");
            }
            const left = caption.mapToItem(view, 0, 0).x;
            const row = byRow.get(head.parent) ?? [];
            row.push({
                         text: head.text,
                         left: left,
                         right: left + caption.width
                     });
            byRow.set(head.parent, row);
        }
        for (const row of byRow.values()) {
            row.sort((a, b) => a.left - b.left);
            for (let i = 1; i < row.length; ++i)
                verify(row[i].left - row[i - 1].right >= Theme.space(4), row[i - 1].text + " touches "
                       + row[i].text + " (" + row[i - 1].right + " → " + row[i].left + ")");
        }
    }

    // A click away from an open rename commits it, as a file manager does: the pane is never
    // left with the editor open and every key swallowed.
    function test_a_click_on_another_row_commits_the_rename() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);
        view.moveCursor(4); // subs/bbb.en.srt
        renameSpy.target = App;
        renameSpy.clear();

        keyClick("r");
        keyClick("e");
        keyClick("n");
        mouseClick(findChild(view, "fileRow_1"), 120, 4);
        compare(renameSpy.count, 1);
        compare(renameSpy.signalArguments[0][2], "Big Buck Bunny 4K/subs/en.srt");
        verify(!view.editing, "the editor stayed open");
        compare(view.cursorIndex, 1);
        verify(view.activeFocus, "the pane did not take the keys");
        keyClick(Qt.Key_J);
        compare(view.cursorIndex, 2, "the pane still swallows its keys");
    }

    // The keyboard going elsewhere (a click on the table) commits too, and stays there; `esc`
    // still cancels.
    function test_the_focus_leaving_commits_the_rename_and_esc_cancels() {
        const view = testCase.filesView();
        waitForRendering(view);
        view.moveCursor(4);
        renameSpy.target = App;
        renameSpy.clear();

        keyClick("r");
        keyClick(Qt.Key_Escape);
        verify(!view.editing);
        compare(renameSpy.count, 0, "esc renamed the file");

        keyClick("r");
        keyClick("x");
        elsewhere.forceActiveFocus();
        compare(renameSpy.count, 1);
        compare(renameSpy.signalArguments[0][2], "Big Buck Bunny 4K/subs/x.srt");
        verify(!view.editing);
        verify(elsewhere.activeFocus, "the commit took the keyboard back");

        // The pane given the keyboard back while a rename is open (a click on a tab): it
        // commits, and the pane hears its keys again.
        view.forceActiveFocus();
        view.moveCursor(1);
        keyClick("r");
        view.forceActiveFocus();
        verify(!view.editing, "the rename stayed open without the keyboard");
        compare(renameSpy.count, 1, "an unchanged name was sent");
        keyClick(Qt.Key_K);
        compare(view.cursorIndex, 4, "the pane still swallows its keys");
    }

    // A click on a column's caption sorts and gives the pane the keyboard, as a row does.
    function test_a_header_click_sorts_and_takes_the_keys() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);
        elsewhere.forceActiveFocus();
        mouseClick(findChild(view, "fileHeader_size"));
        compare(files.sortKey, "size");
        verify(view.activeFocus, "a header click left the keys where they were");
    }

    // #51 D14: a header's third click goes back to the torrent's own order, and the cursor
    // keeps its file the whole way round.
    function test_a_third_header_click_returns_to_torrent_order() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);
        mouseClick(findChild(view, "fileRow_6"), 120, 4); // LICENSE.txt
        const license = files.pathAt(6);

        mouseClick(findChild(view, "fileHeader_size"));
        compare(files.sortKey, "size");
        compare(files.sortDescending, true);
        mouseClick(findChild(view, "fileHeader_size"));
        compare(files.sortKey, "size");
        compare(files.sortDescending, false);
        mouseClick(findChild(view, "fileHeader_size"));
        compare(files.sortKey, "");
        compare(files.pathAt(6), license, "torrent order is not the order it started in");
        compare(view.cursorIndex, 6);

        // Name starts a to z, so its cycle runs a to z, z to a, torrent order.
        mouseClick(findChild(view, "fileHeader_name"));
        compare(files.sortDescending, false);
        mouseClick(findChild(view, "fileHeader_name"));
        compare(files.sortDescending, true);
        mouseClick(findChild(view, "fileHeader_name"));
        compare(files.sortKey, "");
        compare(files.pathAt(view.cursorIndex), license);
    }

    // The filter's `esc` cap is a button: it clears the filter and closes the field.
    function test_the_filter_esc_cap_clears_it() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);
        keyClick("/");
        keyClick("s");
        keyClick("r");
        keyClick("t");
        compare(files.filterText, "srt");
        const field = findChild(view, "filesFilter");
        const button = findChild(field, "fieldTrailingButton");
        verify(button.enabled, "the esc cap is not clickable");
        mouseClick(button);
        compare(files.filterText, "");
        verify(!field.visible, "the field stayed open");
        verify(view.activeFocus);
    }

    // The row ticks say what a click does, as the header's does for every file.
    function test_the_row_ticks_have_a_tip() {
        const view = testCase.filesView();
        waitForRendering(view);
        compare(findChild(view, "fileTick_1").tip, "skip this file · x");
        compare(findChild(view, "fileTick_3").tip, "tick this folder · x");
        verify(findChild(view, "fileTick_1").tooltip !== null);
    }

    // A name the head had to cut shows whole on hover.
    function test_a_cut_torrent_name_shows_whole_on_hover() {
        const view = testCase.select(testCase.bbbName);
        view.width = 160;
        waitForRendering(view);
        const name = findChild(view, "headName");
        verify(name.truncated, "the name fits; the case needs a narrower pane");
        mouseMove(name, 10, 5);
        const tip = findChild(view, "headTip");
        tryVerify(() => tip.shown, 2000, "no tip on the cut name");
        compare(tip.text, testCase.bbbName);
        mouseMove(view, 100, 400);
        verify(!tip.shown);
    }

    SignalSpy {
        id: trackerSpy

        signalName: "trackerAction"
    }

    SignalSpy {
        id: peerSpy

        signalName: "peerAction"
    }

    // Trackers: a right click moves the cursor and opens the tab's menu, whose rows are the
    // tab's keys.
    function test_a_right_click_opens_the_trackers_menu() {
        const view = testCase.select(testCase.archName);
        view.activateTab(view.tabTrackers);
        view.forceActiveFocus();
        waitForRendering(view);
        trackerSpy.target = view;
        trackerSpy.clear();
        copySpy.target = view;
        copySpy.clear();
        const trackers = App.details.trackers;

        mouseClick(findChild(view, "trackerRow_1"), 120, 4, Qt.RightButton);
        compare(view.trackersCursor, 1);
        verify(view.menuOpen, "no menu on a tracker");
        keyClick("d");
        verify(!view.menuOpen);
        compare(trackerSpy.count, 1);
        compare(trackerSpy.signalArguments[0][0], "remove");
        compare(trackerSpy.signalArguments[0][1], [trackers.urlAt(1)]);

        mouseClick(findChild(view, "trackerRow_2"), 120, 4, Qt.RightButton);
        keyClick("y");
        compare(copySpy.count, 1);
        compare(copySpy.signalArguments[0][0], trackers.urlAt(2));

        // DHT is a row, not a tracker: nothing to remove.
        mouseClick(findChild(view, "trackerRow_3"), 120, 4, Qt.RightButton);
        verify(view.menuOpen);
        keyClick("d");
        verify(view.menuOpen, "remove ran on DHT");
        keyClick("a");
        compare(trackerSpy.count, 2);
        compare(trackerSpy.signalArguments[1][0], "add");
    }

    function test_a_right_click_opens_the_peers_menu() {
        const view = testCase.select(testCase.archName);
        view.activateTab(view.tabSwarm);
        view.forceActiveFocus();
        waitForRendering(view);
        peerSpy.target = view;
        peerSpy.clear();
        copySpy.target = view;
        copySpy.clear();
        const peers = App.details.peers;

        mouseClick(findChild(view, "peerRow_2"), 120, 4, Qt.RightButton);
        compare(view.peersCursor, 2);
        verify(view.menuOpen, "no menu on a peer");
        keyClick("b");
        compare(peerSpy.count, 1);
        compare(peerSpy.signalArguments[0][0], "ban");
        compare(peerSpy.signalArguments[0][1], [peers.addressAt(2)]);

        mouseClick(findChild(view, "peerRow_0"), 120, 4, Qt.RightButton);
        keyClick("y");
        compare(copySpy.count, 1);
        compare(copySpy.signalArguments[0][0], peers.addressAt(0));
    }

    function test_a_right_click_opens_the_http_sources_menu() {
        const view = testCase.select(testCase.archName);
        view.activateTab(view.tabWebSeeds);
        view.forceActiveFocus();
        waitForRendering(view);
        webSeedSpy.target = view;
        webSeedSpy.clear();

        mouseClick(findChild(view, "webSeedRow_1"), 120, 4, Qt.RightButton);
        compare(view.webSeedsCursor, 1);
        verify(view.menuOpen, "no menu on an HTTP source");
        keyClick("d");
        compare(webSeedSpy.count, 1);
        compare(webSeedSpy.signalArguments[0][0], "remove");
        compare(webSeedSpy.signalArguments[0][1], [App.details.webSeeds.urlAt(1)]);
    }

    // ---- #51 D15: Trackers, Peers and HTTP sources select several ---------------------

    function listView(tab) {
        const view = testCase.select(testCase.archName);
        view.activateTab(tab);
        view.forceActiveFocus();
        waitForRendering(view);
        return view;
    }

    function menuItem(view, menuName, action) {
        const menu = findChild(findChild(view, "detailsMenuHost"), menuName);
        return menu.items.find(item => item.action === action);
    }

    // The click rules, as the table has them: a plain click selects that row alone, ctrl
    // toggles, shift draws anchor..row, ctrl+shift adds it, a middle click toggles, a click
    // on the empty space clears. DHT, PeX and LSD never join.
    function test_trackers_select_by_the_click_rules() {
        const view = testCase.listView(3);
        const trackers = App.details.trackers;
        const selection = view.trackersSelection;
        compare(trackers.urlAt(3), "** [DHT] **");
        const url = row => trackers.urlAt(row);

        mouseClick(findChild(view, "trackerRow_0"), 120, 4);
        compare(selection.keys, [url(0)]);
        compare(view.trackersCursor, 0);
        mouseClick(findChild(view, "trackerRow_1"), 120, 4);
        compare(selection.keys, [url(1)], "a plain click kept the last row");

        mouseClick(findChild(view, "trackerRow_0"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        compare(selection.count, 2);
        verify(findChild(view, "trackerRow_0").selected && findChild(view, "trackerRow_1").selected);

        // The anchor is row 0 (the last ctrl+click): shift draws 0..4, without DHT and PeX.
        mouseClick(findChild(view, "trackerRow_4"), 120, 4, Qt.LeftButton, Qt.ShiftModifier);
        compare(selection.targets(view.trackersCursor), [url(0), url(1), url(2)]);
        compare(view.trackersCursor, 4);
        verify(!findChild(view, "trackerRow_3").selected, "DHT took a selection fill");

        // shift again re-draws from the same anchor: 0..1.
        mouseClick(findChild(view, "trackerRow_1"), 120, 4, Qt.LeftButton, Qt.ShiftModifier);
        compare(selection.targets(0), [url(0), url(1)]);
        // ctrl+shift adds 0..2.
        mouseClick(findChild(view, "trackerRow_2"), 120, 4, Qt.LeftButton,
                   Qt.ControlModifier | Qt.ShiftModifier);
        compare(selection.count, 3);

        mouseClick(findChild(view, "trackerRow_3"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        compare(selection.count, 3, "ctrl+click selected DHT");
        mouseClick(findChild(view, "trackerRow_2"), 120, 4, Qt.MiddleButton);
        compare(selection.targets(0), [url(0), url(1)], "a middle click did not toggle");
        compare(view.trackersCursor, 2);

        const empty = findChild(view, "trackersEmptySpace");
        verify(empty.height > 0);
        mouseClick(empty);
        compare(selection.count, 0, "a click on the empty space kept the selection");
    }

    // The Files tab's keys: space, v, ctrl+a, esc; `d` removes every selected tracker in
    // one action, `y` copies their URLs a line each, and the menu names the count.
    function test_tracker_keys_select_and_d_removes_them_all() {
        const view = testCase.listView(3);
        const trackers = App.details.trackers;
        const selection = view.trackersSelection;
        trackerSpy.target = view;
        trackerSpy.clear();
        copySpy.target = view;
        copySpy.clear();

        keyClick(Qt.Key_Space);
        compare(selection.keys, [trackers.urlAt(0)]);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_V);
        compare(selection.count, 3);

        keyClick(Qt.Key_D);
        compare(trackerSpy.count, 1);
        compare(trackerSpy.signalArguments[0][0], "remove");
        compare(trackerSpy.signalArguments[0][1], [trackers.urlAt(0), trackers.urlAt(1),
                                                   trackers.urlAt(2)]);
        keyClick(Qt.Key_Y);
        compare(copySpy.signalArguments[0][0], [trackers.urlAt(0), trackers.urlAt(1),
                                                trackers.urlAt(2)].join("\n"));

        // The menu, on a selected row, keeps the selection and says how many.
        mouseClick(findChild(view, "trackerRow_1"), 120, 4, Qt.RightButton);
        verify(view.menuOpen);
        compare(selection.count, 3);
        compare(testCase.menuItem(view, "trackerMenu", "remove").text, "remove 3 trackers");
        compare(testCase.menuItem(view, "trackerMenu", "copy").text, "copy 3 URLs");
        keyClick(Qt.Key_Escape);
        verify(!view.menuOpen);
        compare(selection.count, 3, "esc on the menu cleared the selection");

        keyClick(Qt.Key_Escape);
        compare(selection.count, 0);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(selection.count, 3, "ctrl+a took DHT or PeX, or missed a tracker");

        // A right click outside the selection makes that row the only target.
        mouseClick(findChild(view, "trackerRow_4"), 120, 4, Qt.RightButton);
        compare(selection.count, 0);
        compare(view.trackersCursor, 4);
        compare(testCase.menuItem(view, "trackerMenu", "remove").disabled, true);
        keyClick(Qt.Key_Escape);
        mouseClick(findChild(view, "trackerRow_2"), 120, 4, Qt.RightButton);
        compare(selection.keys, [trackers.urlAt(2)]);
        compare(testCase.menuItem(view, "trackerMenu", "remove").text, "remove");
        keyClick("d");
        compare(trackerSpy.signalArguments[1][1], [trackers.urlAt(2)]);

        // `d` on DHT with nothing selected sends nothing.
        keyClick(Qt.Key_Escape);
        view.trackersCursor = 3;
        trackerSpy.clear();
        keyClick(Qt.Key_D);
        compare(trackerSpy.count, 0, "remove ran on DHT");

        // Another torrent lets the selection go.
        keyClick(Qt.Key_A, Qt.ControlModifier);
        verify(selection.count > 0);
        App.selectTorrent(testCase.hashOf(testCase.bbbName));
        compare(selection.count, 0);
    }

    // Peers: kept by address, banned all at once, the menu naming how many.
    function test_peers_select_several_and_ban_them() {
        const view = testCase.listView(2);
        const peers = App.details.peers;
        const selection = view.peersSelection;
        peerSpy.target = view;
        peerSpy.clear();
        copySpy.target = view;
        copySpy.clear();
        verify(peers.count >= 3);

        mouseClick(findChild(view, "peerRow_0"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        mouseClick(findChild(view, "peerRow_2"), 120, 4, Qt.MiddleButton);
        compare(selection.keys, [peers.addressAt(0), peers.addressAt(2)]);
        verify(findChild(view, "peerRow_2").selected);

        mouseClick(findChild(view, "peerRow_2"), 120, 4, Qt.RightButton);
        compare(testCase.menuItem(view, "peerMenu", "ban").text, "ban 2 peers…");
        compare(testCase.menuItem(view, "peerMenu", "copy").text, "copy 2 addresses");
        keyClick("b");
        compare(peerSpy.count, 1);
        compare(peerSpy.signalArguments[0][0], "ban");
        compare(peerSpy.signalArguments[0][1], [peers.addressAt(0), peers.addressAt(2)]);

        keyClick(Qt.Key_Y);
        compare(copySpy.signalArguments[0][0], peers.addressAt(0) + "\n" + peers.addressAt(2));

        // Outside the selection, a right click retargets.
        mouseClick(findChild(view, "peerRow_1"), 120, 4, Qt.RightButton);
        compare(selection.keys, [peers.addressAt(1)]);
        compare(testCase.menuItem(view, "peerMenu", "ban").text, "ban…");
        keyClick(Qt.Key_Escape);

        keyClick(Qt.Key_Escape);
        compare(selection.count, 0);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(selection.count, peers.count);
        mouseClick(findChild(view, "peersEmptySpace"));
        compare(selection.count, 0);
    }

    // HTTP sources: the same rules; `d` removes the selection.
    function test_http_sources_select_several_and_remove_them() {
        const view = testCase.listView(5);
        const seeds = App.details.webSeeds;
        const selection = view.webSeedsSelection;
        webSeedSpy.target = view;
        webSeedSpy.clear();

        mouseClick(findChild(view, "webSeedRow_2"), 120, 4);
        mouseClick(findChild(view, "webSeedRow_0"), 120, 4, Qt.LeftButton, Qt.ShiftModifier);
        compare(selection.count, 3);
        mouseClick(findChild(view, "webSeedRow_1"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        keyClick(Qt.Key_D);
        compare(webSeedSpy.signalArguments[0][0], "remove");
        compare(webSeedSpy.signalArguments[0][1], [seeds.urlAt(0), seeds.urlAt(2)]);

        mouseClick(findChild(view, "webSeedRow_0"), 120, 4, Qt.RightButton);
        compare(testCase.menuItem(view, "webSeedMenu", "remove").text, "remove 2 sources");
        keyClick(Qt.Key_Escape);
        keyClick(Qt.Key_Escape);
        compare(selection.count, 0);
    }

    // Somewhere outside the pane that can hold the keyboard: the table, in the window.
    Item {
        id: elsewhere

        height: 10
        width: 10
    }

    SignalSpy {
        id: renameSpy

        signalName: "torrentFileRenameRequested"
    }

    SignalSpy {
        id: fileSpy

        signalName: "openFileRequested"
    }

    SignalSpy {
        id: focusSpy

        signalName: "focusTable"
    }

    SignalSpy {
        id: valueSpy

        signalName: "valueCopyRequested"
    }

    SignalSpy {
        id: copySpy

        signalName: "copyRequested"
    }

    SignalSpy {
        id: noteSpy

        signalName: "noteRequested"
    }

    SignalSpy {
        id: openSpy

        signalName: "openFolderRequested"
    }

    SignalSpy {
        id: webSeedSpy

        signalName: "webSeedAction"
    }

    // ---- #10: the HTTP sources tab --------------------------------------------------------

    function tabTexts(view) {
        return findChild(view, "detailsTabs").model.map(tab => tab.text);
    }

    // The demo's Arch ISO lists three mirrors as web seeds; Big Buck Bunny has none.
    function test_the_http_sources_tab_shows_only_with_web_seeds() {
        const bbb = testCase.select(testCase.bbbName);
        compare(testCase.tabTexts(bbb), ["story", "files", "swarm", "trackers", "facts"]);
        compare(bbb.hasWebSeeds, false);
        bbb.activateTab(bbb.tabWebSeeds);
        compare(bbb.currentTab, bbb.tabStory);

        const arch = testCase.select(testCase.archName);
        compare(testCase.tabTexts(arch), ["story", "files", "swarm", "trackers", "facts", "web seeds"]);
        compare(findChild(arch, "detailsTabs").model[5].key, "6");
        arch.activateTab(arch.tabWebSeeds);
        compare(arch.currentTab, arch.tabWebSeeds);
        compare(arch.rowCount, 3);
        verify(findChild(arch, "webSeedsTab").visible);
    }

    // `https://` is dropped from what the rows print; a plain http mirror keeps its scheme.
    function test_the_rows_print_the_url_without_https() {
        const view = testCase.select(testCase.archName);
        view.activateTab(view.tabWebSeeds);
        waitForRendering(view, 500);
        compare(findChild(view, "webSeedUrl_0").text, "mirror.example.net/archlinux/iso/2026.09.01/");
        compare(findChild(view, "webSeedUrl_2").text,
                "http://ftp.example.ac.uk/pub/archlinux/iso/2026.09.01/");
    }

    // `6` is the tab while there are web seeds; without them it goes on to the window (where
    // it does nothing, S1's note), so the pane leaves it unaccepted.
    function test_six_opens_web_seeds_only_when_there_are_any() {
        const bbb = testCase.select(testCase.bbbName);
        bbb.forceActiveFocus();
        keyClick(Qt.Key_6);
        compare(bbb.currentTab, bbb.tabStory);

        const arch = testCase.select(testCase.archName);
        arch.forceActiveFocus();
        keyClick(Qt.Key_6);
        compare(arch.currentTab, arch.tabWebSeeds);
        keyClick(Qt.Key_J);
        compare(arch.cursorIndex, 1);
    }

    function test_http_sources_keys_add_remove_and_copy() {
        const view = testCase.select(testCase.archName);
        webSeedSpy.target = view;
        webSeedSpy.clear();
        copySpy.target = view;
        copySpy.clear();
        view.activateTab(view.tabWebSeeds);
        view.forceActiveFocus();
        keyClick(Qt.Key_J);

        keyClick(Qt.Key_Y);
        compare(copySpy.count, 1);
        compare(copySpy.signalArguments[0][0],
                "https://geo.mirror.example.org/archlinux/iso/2026.09.01/");
        keyClick(Qt.Key_D);
        compare(webSeedSpy.count, 1);
        compare(webSeedSpy.signalArguments[0][0], "remove");
        compare(webSeedSpy.signalArguments[0][1],
                ["https://geo.mirror.example.org/archlinux/iso/2026.09.01/"]);
        keyClick(Qt.Key_A);
        compare(webSeedSpy.count, 2);
        compare(webSeedSpy.signalArguments[1][0], "add");

        const hints = findChild(view, "detailsHints");
        compare(hints.model.map(hint => hint.key + " " + hint.label), ["a add", "d remove",
                                                                          "y copy", "space mark",
                                                                          "ctrl+a mark all"]);
        view.activateTab(view.tabStory);
    }

    // Another torrent without web seeds while their tab is on show: Trackers takes over, and
    // a torrent with them keeps the tab while its list is fetched.
    function test_a_torrent_without_web_seeds_falls_back_to_trackers() {
        const view = testCase.select(testCase.archName);
        view.activateTab(view.tabWebSeeds);
        compare(view.currentTab, view.tabWebSeeds);

        App.selectTorrent(testCase.hashOf(testCase.bbbName));
        compare(view.hasWebSeeds, false);
        compare(view.currentTab, view.tabTrackers);
        compare(testCase.tabTexts(view).length, 5);

        App.selectTorrent(testCase.hashOf(testCase.archName));
        compare(view.hasWebSeeds, true);
        view.activateTab(view.tabWebSeeds);
        compare(view.currentTab, view.tabWebSeeds);
    }

    // #77: each tab's rows are list items named by what identifies them: a tracker's URL (or
    // DHT's word, not selectable), a peer's address, a web seed's URL, a file's name.
    function test_the_tabs_rows_publish_list_items_named_by_what_they_are() {
        let view = testCase.listView(3);
        const trackers = App.details.trackers;
        let row = findChild(view, "trackerRow_0");
        compare(row.Accessible.role, Accessible.ListItem);
        compare(row.Accessible.name, trackers.urlAt(0));
        verify(row.Accessible.selectable);
        compare(trackers.urlAt(3), "** [DHT] **");
        row = findChild(view, "trackerRow_3");
        compare(row.Accessible.name, findChild(view, "trackerHost_3").text);
        verify(!row.Accessible.selectable, "DHT can be selected");

        view = testCase.listView(2);
        row = findChild(view, "peerRow_0");
        compare(row.Accessible.role, Accessible.ListItem);
        compare(row.Accessible.name, App.details.peers.addressAt(0));

        view = testCase.listView(5);
        row = findChild(view, "webSeedRow_0");
        compare(row.Accessible.role, Accessible.ListItem);
        compare(row.Accessible.name, App.details.webSeeds.urlAt(0));

        view = testCase.filesView();
        waitForRendering(view);
        row = findChild(view, "fileRow_3");
        compare(row.Accessible.role, Accessible.ListItem);
        compare(row.Accessible.name, "subs/");
        compare(row.Accessible.name, findChild(view, "fileName_3").text);
    }

    // #77: a screen reader's press on a tab's row is a plain click: the cursor goes there
    // and the selection becomes that row (the Files tab lets it go, as its click does).
    function test_the_accessible_press_on_a_tabs_row_is_a_plain_click() {
        let view = testCase.listView(2);
        const peers = App.details.peers;
        mouseClick(findChild(view, "peerRow_0"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        mouseClick(findChild(view, "peerRow_2"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        compare(view.peersSelection.count, 2);
        findChild(view, "peerRow_1").Accessible.pressAction();
        compare(view.peersSelection.keys, [peers.addressAt(1)]);

        view = testCase.listView(3);
        findChild(view, "trackerRow_1").Accessible.pressAction();
        compare(view.trackersSelection.keys, [App.details.trackers.urlAt(1)]);
        compare(view.trackersCursor, 1);

        view = testCase.filesView();
        waitForRendering(view);
        const files = App.details.files;
        mouseClick(findChild(view, "fileRow_1"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        mouseClick(findChild(view, "fileRow_2"), 120, 4, Qt.LeftButton, Qt.ControlModifier);
        compare(files.selectedCount, 2);
        findChild(view, "fileRow_4").Accessible.pressAction();
        compare(files.selectedCount, 0);
        compare(view.cursorIndex, 4);
    }

    // #77: a folder's arrow is a button named for what its press does, and the press folds;
    // a file's arrow is out of the tree.
    function test_the_accessible_press_on_a_fold_arrow_folds() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);
        verify(files.isFolder(3) && files.isExpanded(3));
        const arrow = findChild(view, "fileFold_3");
        compare(arrow.Accessible.role, Accessible.Button);
        compare(arrow.Accessible.name, "fold");
        verify(!arrow.Accessible.ignored);
        arrow.Accessible.pressAction();
        verify(!files.isExpanded(3), "the press did not fold");
        compare(findChild(view, "fileFold_3").Accessible.name, "unfold");
        findChild(view, "fileFold_3").Accessible.pressAction();
        verify(files.isExpanded(3), "the press did not open it again");
        verify(findChild(view, "fileFold_1").Accessible.ignored, "a file has a fold button");
    }

    // #77: the summary line's "N skipped" is a check box that hides them, and the line is a
    // button that opens the filter.
    function test_the_summary_line_publishes_its_switch_and_the_filter() {
        const view = testCase.filesView();
        const files = App.details.files;
        wait(0);
        const skipped = findChild(view, "filesSkippedArea");
        compare(skipped.Accessible.role, Accessible.CheckBox);
        compare(skipped.Accessible.name, "hide skipped files");
        compare(skipped.Accessible.description, "2 skipped");
        verify(!skipped.Accessible.checked);
        skipped.Accessible.toggleAction();
        verify(files.hideSkipped, "the toggle did not hide them");
        verify(skipped.Accessible.checked);
        skipped.Accessible.pressAction();
        verify(!files.hideSkipped, "the press did not show them again");

        const filter = findChild(view, "filesFilterArea");
        compare(filter.Accessible.role, Accessible.Button);
        compare(filter.Accessible.name, "filter files");
        filter.Accessible.pressAction();
        verify(view.filterEditing, "the press did not open the filter");
        keyClick(Qt.Key_Escape);
    }

    // #77: a row's tick is a "download" check box, mixed on a folder with some skipped; its
    // toggle is the click: it skips the file.
    function test_a_files_tick_is_a_check_box() {
        const view = testCase.filesView();
        const files = App.details.files;
        waitForRendering(view);
        view.filesCursor = files.rowOfPath("Big Buck Bunny 4K/LICENSE.txt");
        const at = view.filesCursor;
        const tick = findChild(view, "fileTick_" + at);
        compare(tick.Accessible.role, Accessible.CheckBox);
        compare(tick.Accessible.name, "download");
        verify(tick.Accessible.checked);
        verify(!tick.Accessible.checkStateMixed);
        tick.Accessible.toggleAction();
        verify(!findChild(view, "fileTick_" + at).Accessible.checked, "the toggle did not skip it");
        findChild(view, "fileTick_" + at).Accessible.pressAction();
        verify(findChild(view, "fileTick_" + at).Accessible.checked, "the press did not tick it");
    }
}
