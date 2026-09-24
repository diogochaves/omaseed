import QtQuick
import QtTest
import Omaseed.Kit

// The hover label. The cases are about when it is up and where it lands, not how it looks:
// it waits for the pointer to rest, it refuses to show nothing, and it clamps itself inside
// the view rather than hanging off an edge.
TestCase {
    id: testCase

    name: "Tooltip"

    Component {
        id: host

        Item {
            property alias target: target
            property alias tip: tooltip

            height: 200
            width: 400

            Rectangle {
                id: target

                height: 30
                width: 30
                x: 100
                y: 40
            }

            Tooltip {
                id: tooltip

                delay: 1
            }
        }
    }

    Component {
        id: plainHost

        Item {
            property alias tip: plainTip

            height: 200
            width: 400

            Tooltip {
                id: plainTip
            }
        }
    }

    // Review Q8: one look, IconButton's: an accent edge and a 400 ms rest.
    function test_one_look_accent_edge_and_four_hundred_ms() {
        const item = createTemporaryObject(host, testCase);
        compare(item.tip.panelItem.border.color, Theme.accent);
        compare(item.tip.panelItem.border.width, 1);
        const plain = createTemporaryObject(plainHost, testCase);
        compare(plain.tip.delay, 400);
    }

    function test_show_for_centres_the_panel_under_the_item() {
        const item = createTemporaryObject(host, testCase);
        item.tip.showFor(item.target, "add torrent");
        tryCompare(item.tip, "shown", true);
        const panel = item.tip.panelItem;
        verify(panel.y >= item.target.y + item.target.height, "the panel is not under the item");
        fuzzyCompare(panel.x + panel.width / 2, item.target.x + item.target.width / 2, 1);
    }

    function test_show_for_flips_above_an_item_at_the_bottom_edge() {
        const item = createTemporaryObject(host, testCase);
        item.target.y = item.height - item.target.height;
        item.tip.showFor(item.target, "add torrent");
        tryCompare(item.tip, "shown", true);
        const panel = item.tip.panelItem;
        verify(panel.y + panel.height <= item.target.y, "the panel was not flipped above the item");
    }

    function test_show_for_nothing_hides() {
        const item = createTemporaryObject(host, testCase);
        item.tip.showFor(item.target, "");
        wait(20);
        compare(item.tip.shown, false);
    }

    function test_it_waits_for_the_delay_before_showing() {
        const item = createTemporaryObject(host, testCase);
        item.tip.delay = 10000; // long enough that the check below cannot race it
        item.tip.showAt(10, 10, "a name too long for its column");
        compare(item.tip.shown, false);
    }

    function test_it_shows_after_the_delay() {
        const item = createTemporaryObject(host, testCase);
        item.tip.showAt(10, 10, "a name too long for its column");
        tryCompare(item.tip, "shown", true);
        compare(item.tip.text, "a name too long for its column");
    }

    function test_empty_text_never_shows() {
        const item = createTemporaryObject(host, testCase);
        item.tip.showAt(10, 10, "");
        wait(20);
        compare(item.tip.shown, false);
    }

    function test_hide_takes_it_down() {
        const item = createTemporaryObject(host, testCase);
        item.tip.showAt(10, 10, "something");
        tryCompare(item.tip, "shown", true);
        item.tip.hide();
        compare(item.tip.shown, false);
    }

    function test_clearing_the_text_takes_it_down() {
        const item = createTemporaryObject(host, testCase);
        item.tip.showAt(10, 10, "something");
        tryCompare(item.tip, "shown", true);
        item.tip.text = "";
        compare(item.tip.shown, false);
    }

    function test_it_stays_inside_the_view_at_the_right_edge() {
        const item = createTemporaryObject(host, testCase);
        item.tip.showAt(item.width - 2, 10, "a name wide enough to overflow the right edge");
        tryCompare(item.tip, "shown", true);
        const panel = item.tip.panelItem;
        verify(panel.x >= 0, "panel hangs off the left");
        verify(panel.x + panel.width <= item.width, "panel hangs off the right");
    }

    function test_it_flips_above_the_pointer_at_the_bottom_edge() {
        const item = createTemporaryObject(host, testCase);
        item.tip.showAt(10, item.height - 2, "near the bottom");
        tryCompare(item.tip, "shown", true);
        const panel = item.tip.panelItem;
        verify(panel.y < item.height - 2, "panel was not flipped above the pointer");
        verify(panel.y >= 0, "panel hangs off the top");
    }
}
