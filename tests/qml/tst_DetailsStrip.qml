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

    // Seen on Diogo's desk: after a name too long for the line (elided), a short one stayed cut
    // ("tiny-notes.t…") until the cursor moved again.
    function test_a_short_name_after_an_elided_one_shows_whole() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "torrent": Object.assign({}, testCase.row, {
                                                                             "name": "A deliberately long torrent name, ".repeat(6)
                                                                         })
                                            });
        waitForRendering(strip, 500);
        const name = findChild(strip, "stripName");
        verify(name.truncated);
        strip.torrent = Object.assign({}, testCase.row, {
                                          "name": "tiny-notes.txt"
                                      });
        waitForRendering(strip, 500);
        verify(!name.truncated, "width " + name.width + " of " + name.implicitWidth);
    }

    // #77: the line is a "show details" button; a screen reader's press asks to unfold,
    // once; disabled, it does nothing.
    function test_the_line_is_a_button_that_unfolds() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "torrent": testCase.row
                                            });
        const area = findChild(strip, "stripMouse");
        compare(area.Accessible.role, Accessible.Button);
        compare(area.Accessible.name, "show details");
        let asked = 0;
        strip.unfoldRequested.connect(() => asked++);
        area.Accessible.pressAction();
        compare(asked, 1);
        strip.enabled = false;
        area.Accessible.pressAction();
        compare(asked, 1, "a disabled strip asked to unfold");
    }
}
