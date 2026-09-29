import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// docs/17 M10.1: the folded details pane's one line.
TestCase {
    id: testCase

    height: 200
    name: "DetailsStrip"
    visible: true
    when: windowShown
    width: 1000

    Component {
        id: stripComponent

        DetailsStrip {
            width: 900
        }
    }

    readonly property var row: ({
                                    "hash": "abc",
                                    "name": "Cosmos Laundromat 2K (CC-BY)",
                                    "progressText": "87.2 %",
                                    "dlspeedText": "940 KB/s",
                                    "upspeedText": "220 KB/s",
                                    "etaText": "7m 05s"
                                })

    function test_it_reads_the_row() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "torrent": testCase.row
                                            });
        compare(findChild(strip, "stripName").text, "Cosmos Laundromat 2K (CC-BY)");
        compare(findChild(strip, "stripStats").text, "87.2 % · ↓ 940 KB/s · ↑ 220 KB/s · 7m 05s");
        compare(strip.implicitHeight, Math.round(Theme.font.baseSize * 2.2));
    }

    function test_no_eta_leaves_it_out() {
        const row = Object.assign({}, testCase.row, {
                                      "etaText": "—"
                                  });
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "torrent": row
                                            });
        compare(findChild(strip, "stripStats").text, "87.2 % · ↓ 940 KB/s · ↑ 220 KB/s");
    }

    function test_no_torrent_says_so() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "torrent": ({})
                                            });
        compare(findChild(strip, "stripName").text, "no torrent selected");
        verify(!findChild(strip, "stripStats").visible);
    }

    function test_the_hint_shortens_when_narrow() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "torrent": testCase.row
                                            });
        compare(findChild(strip, "stripHint").text, "or double-click · details");
        strip.width = Theme.space(500);
        compare(findChild(strip, "stripHint").text, "details");
    }

    function test_a_click_on_the_chevron_or_a_double_click_asks_to_unfold() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "torrent": testCase.row
                                            });
        waitForRendering(strip);
        let asked = 0;
        strip.unfoldRequested.connect(() => asked += 1);
        const chevron = findChild(strip, "stripChevron");
        mouseClick(chevron, chevron.width / 2, chevron.height / 2);
        compare(asked, 1);
        mouseDoubleClickSequence(strip, strip.width / 2, strip.height / 2);
        compare(asked, 2);
        // A single click elsewhere on the line is not a request.
        const name = findChild(strip, "stripName");
        mouseClick(name, name.width / 2, name.height / 2);
        compare(asked, 2);
    }
}
