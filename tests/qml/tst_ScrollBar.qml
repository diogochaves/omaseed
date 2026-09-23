import QtQuick
import QtTest
import Omaseed.Kit

// `mouseClick` does not deliver in the offscreen runner (see tst_Sheet.qml), so the drag and
// the track press are checked through `scrollThumbTo`, the one function both handlers call.
TestCase {
    id: testCase

    name: "ScrollBar"
    height: 300
    width: 400
    when: windowShown

    Component {
        id: flickComponent

        Flickable {
            contentHeight: 100
            contentWidth: 1000
            height: 100
            width: 400
        }
    }

    Component {
        id: barComponent

        ScrollBar {
            width: 400
        }
    }

    function makeBar(contentWidth) {
        const flick = createTemporaryObject(flickComponent, testCase, {
                                                "contentWidth": contentWidth
                                            });
        const bar = createTemporaryObject(barComponent, testCase, {
                                              "flickable": flick,
                                              "y": 120
                                          });
        return {
            "flick": flick,
            "bar": bar
        };
    }

    function test_hidden_while_the_content_fits() {
        const t = makeBar(400);
        compare(t.bar.needed, false);
    }

    function test_shown_while_the_content_overflows() {
        const t = makeBar(1000);
        compare(t.bar.needed, true);
        fuzzyCompare(t.bar.thumbLength, 400 * 0.4, 0.5);
    }

    function test_thumb_follows_the_content() {
        const t = makeBar(1000);
        compare(t.bar.thumbStart, 0);
        t.flick.contentX = 600; // the far end
        fuzzyCompare(t.bar.thumbStart, t.bar.length - t.bar.thumbLength, 0.5);
    }

    function test_moving_the_thumb_halfway_scrolls_halfway() {
        const t = makeBar(1000);
        const thumb = findChild(t.bar, "thumb");
        t.bar.scrollThumbTo((t.bar.length - t.bar.thumbLength) / 2);
        fuzzyCompare(t.flick.contentX, 300, 1);
        fuzzyCompare(thumb.x, (t.bar.length - t.bar.thumbLength) / 2, 1);
    }

    function test_moving_the_thumb_is_clamped_to_the_ends() {
        const t = makeBar(1000);
        t.bar.scrollThumbTo(t.bar.length * 2);
        compare(t.flick.contentX, 600);
        t.bar.scrollThumbTo(-50);
        compare(t.flick.contentX, 0);
    }

    function test_vertical_reads_the_height() {
        const flick = createTemporaryObject(flickComponent, testCase, {
                                                "contentWidth": 100,
                                                "contentHeight": 400,
                                                "width": 100
                                            });
        const bar = createTemporaryObject(barComponent, testCase, {
                                              "flickable": flick,
                                              "vertical": true,
                                              "height": 100,
                                              "width": 8
                                          });
        compare(bar.needed, true);
        fuzzyCompare(bar.thumbLength, Math.max(Theme.space(24), 25), 0.5);
    }
}
