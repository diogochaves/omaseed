import QtQuick
import QtTest
import Omaseed
import Omaseed.Views

// Board 64 (DECISIONS C47 I5, I7): `Views/FilesPaneHead.qml`, the file pane's kinds and count,
// on the demo `.torrent` loaded into App.addFiles (four videos, three subtitles, a README).
TestCase {
    id: testCase

    height: 300
    name: "FilesPaneHead"
    visible: true
    when: windowShown
    width: 800

    Component {
        id: headComponent

        FilesPaneHead {
            countText: App.addFiles.summaryText
            files: App.addFiles.tree
            width: 700
        }
    }

    Component {
        id: parsedSpy

        SignalSpy {
            signalName: "addableParsed"
            target: App
        }
    }

    Component {
        id: tickedSpy

        SignalSpy {
            signalName: "kindTicked"
        }
    }

    function initTestCase() {
        App.loadDemo(16);
        const spy = createTemporaryObject(parsedSpy, testCase);
        // A `.torrent` is read off the GUI thread: its files come with addableParsed.
        App.parseAddable(App.demoTorrentPath());
        spy.wait(5000);
        const token = spy.signalArguments[0][0].token;
        App.loadAddFiles(token);
        compare(App.addFiles.fileCount, 8);
    }

    function cleanup() {
        App.addFiles.tree.clearFilters();
        App.addFiles.setAllWanted(true);
    }

    function make() {
        const head = createTemporaryObject(headComponent, testCase);
        waitForRendering(head);
        return head;
    }

    function test_the_chips_in_order_with_their_words() {
        const head = testCase.make();
        compare(head.kinds.map(entry => entry.kind), ["video", "subtitles", "extras"]);
        compare(findChild(head, "filesKindText_video").text, "4 videos");
        compare(findChild(head, "filesKindText_subtitles").text, "3 subtitles");
        compare(findChild(head, "filesKindText_extras").text, "1 extra", "the singular");
        const video = findChild(head, "filesKindChip_video");
        const subtitles = findChild(head, "filesKindChip_subtitles");
        const extras = findChild(head, "filesKindChip_extras");
        verify(video.x < subtitles.x && subtitles.x < extras.x, "biggest first, extras last");
    }

    function test_the_words_of_every_kind() {
        const head = testCase.make();
        compare(head.kindWords("video", 1), "1 video");
        compare(head.kindWords("video", 9), "9 videos");
        compare(head.kindWords("audio", 2), "2 audio files");
        compare(head.kindWords("discImages", 1), "1 disc image");
        compare(head.kindWords("other", 3), "3 other files");
    }

    function test_the_biggest_kind_says_its_size() {
        const head = testCase.make();
        const size = findChild(head, "filesKindSize_video");
        verify(size.visible);
        compare(size.text, App.sizeText(head.kinds[0].bytes));
        verify(!findChild(head, "filesKindSize_subtitles").visible);
        verify(!findChild(head, "filesKindSize_extras").visible);
    }

    function test_a_box_asks_and_moves_only_with_the_model() {
        const head = testCase.make();
        const spy = createTemporaryObject(tickedSpy, testCase, {
                                              "target": head
                                          });
        const box = findChild(head, "filesKindBox_subtitles");
        compare(box.wanted, 2);
        mouseClick(box);
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], "subtitles");
        compare(box.wanted, 2, "a box never changes by itself (C28)");
        App.addFiles.toggleKindWanted("subtitles");
        compare(findChild(head, "filesKindBox_subtitles").wanted, 0);
        App.addFiles.toggleWanted(App.addFiles.rowOfPath("Big Buck Bunny/subs/bbb.en.srt"));
        compare(findChild(head, "filesKindBox_subtitles").wanted, 1);
    }

    function test_a_word_filters_and_a_second_shows_all() {
        const head = testCase.make();
        const files = App.addFiles.tree;
        mouseClick(findChild(head, "filesKindWord_video"));
        compare(files.kindFilter, "video");
        compare(files.matchCount, 4);
        mouseClick(findChild(head, "filesKindWord_video"));
        compare(files.kindFilter, "");
    }

    function test_showing_n_of_m_and_show_all_clears_all_three() {
        const head = testCase.make();
        const files = App.addFiles.tree;
        const count = findChild(head, "filesPaneCount");
        const showAll = findChild(head, "filesShowAll");
        compare(count.text, App.addFiles.summaryText);
        verify(!showAll.visible);

        files.kindFilter = "subtitles";
        compare(count.text, "showing 3 of 8");
        verify(showAll.visible);
        files.filterText = "bbb";
        files.hideSkipped = true;
        compare(count.text, "showing 2 of 8");

        mouseClick(showAll);
        compare(files.kindFilter, "");
        compare(files.filterText, "");
        compare(files.hideSkipped, false);
        compare(count.text, App.addFiles.summaryText);
        verify(!showAll.visible);
    }

    function test_without_boxes_the_word_alone() {
        const head = createTemporaryObject(headComponent, testCase, {
                                               "boxes": false
                                           });
        verify(!findChild(head, "filesKindBox_video").visible);
        verify(findChild(head, "filesKindWord_video").visible);
    }

    // Docked details (~400 px): six kinds flow several to a line, not one per line.
    function test_six_kinds_flow_in_a_narrow_head() {
        const head = createTemporaryObject(headComponent, testCase, {
                                               "width": 400,
                                               "countText":
                                               "113 files · 4.2 GB · 12 skipped, 3 still on disk"
                                           });
        const six = [["video", 33, 4e9], ["audio", 3, 3e7], ["images", 16, 2e7], ["subtitles", 54,
                                                                                  1e6], ["extras", 5,
                                                                                         1e4], ["other",
                                                                                                2, 1e3]];
        head.kinds = six.map(([kind, count, bytes]) => ({
            "kind": kind,
            "count": count,
            "files": [],
            "bytes": bytes,
            "wanted": 2,
            "word": head.kindWords(kind, count)
        }));
        waitForRendering(head);
        const count = findChild(head, "filesPaneCount");
        const lines = new Set();
        for (const [kind] of six) {
            const chip = findChild(head, "filesKindChip_" + kind);
            const at = chip.mapToItem(head, 0, 0);
            lines.add(at.y);
            verify(at.x + chip.width <= (at.y === 0 ? count.mapToItem(head, 0, 0).x : head.width),
                   kind + " runs past its line");
        }
        verify(lines.size <= 3, "six chips on " + lines.size + " lines");
        verify(head.height < 3 * 2 * findChild(head, "filesKindChip_video").height,
               "the head is as tall as its lines");
    }

    function test_a_box_says_its_kind_once() {
        const head = testCase.make();
        head.kinds = [
                    {
                        "kind": "video",
                        "count": 9,
                        "files": [],
                        "bytes": 1e9,
                        "wanted": 2,
                        "word": "videos"
                    }
                ];
        waitForRendering(head);
        const box = findChild(head, "filesKindBox_video");
        compare(box.Accessible.name, "download every video");
        compare(box.tip, "skip every video");
    }
}
