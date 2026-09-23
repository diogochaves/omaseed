import QtQuick
import QtTest
import Omaseed.Kit

// N3. `Kit/ScrollHint.qml`: the three looks the "Scroll hint" setting picks, each shown only
// on the side that has more, the counted bands' words and insets, and the page a band's
// click scrolls (through `page()`, the function the click calls).
TestCase {
    id: testCase

    height: 300
    name: "ScrollHint"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: flickComponent

        Flickable {
            contentHeight: 1000
            contentWidth: 200
            height: 200
            width: 200
        }
    }

    Component {
        id: hintComponent

        ScrollHint {
            height: 200
            width: 200
        }
    }

    function make(style, above, below) {
        const flick = createTemporaryObject(flickComponent, testCase);
        const hint = createTemporaryObject(hintComponent, testCase, {
                                               "flickable": flick,
                                               "style": style,
                                               "countAbove": above,
                                               "countBelow": below
                                           });
        return {
            "flick": flick,
            "hint": hint
        };
    }

    function test_chevrons_count_each_side_that_has_more() {
        const t = make("chevrons", 0, 7);
        compare(findChild(t.hint, "moreAbove").visible, false);
        compare(findChild(t.hint, "moreBelow").visible, true);
        verify(findChild(t.hint, "moreBelowText").text.indexOf("7 below") >= 0);
        compare(t.hint.topInset, 0);
        compare(t.hint.bottomInset, t.hint.bandHeight);

        t.hint.countAbove = 3;
        compare(findChild(t.hint, "moreAbove").visible, true);
        verify(findChild(t.hint, "moreAboveText").text.indexOf("3 above") >= 0);
        compare(t.hint.topInset, t.hint.bandHeight);
        compare(t.hint.clearTop, t.hint.bandHeight);
        compare(findChild(t.hint, "fadeAbove").visible, false);
        compare(findChild(t.hint, "scrollbar").visible, false);
    }

    function test_fade_draws_only_the_side_that_has_more_and_covers_nothing() {
        const t = make("fade", 2, 0);
        compare(findChild(t.hint, "fadeAbove").visible, true);
        compare(findChild(t.hint, "fadeBelow").visible, false);
        compare(findChild(t.hint, "moreAbove").visible, false);
        compare(t.hint.topInset, 0);
        compare(t.hint.bottomInset, 0);
        // A cursor row keeps out of the fade, where it would read dimmed.
        compare(t.hint.clearTop, t.hint.fadeHeight);
        compare(t.hint.clearBottom, 0);
    }

    function test_the_scrollbar_shows_when_revealed_and_the_list_overflows() {
        const t = make("scrollbar", 0, 5);
        compare(findChild(t.hint, "scrollbar").visible, false, "shown without hover or focus");
        t.hint.revealed = true;
        compare(findChild(t.hint, "scrollbar").visible, true);
        t.flick.contentHeight = 100;
        compare(findChild(t.hint, "scrollbar").visible, false, "shown for a list that fits");
    }

    function test_an_unknown_style_draws_nothing() {
        const t = make("none", 4, 4);
        t.hint.revealed = true;
        for (const name of ["moreAbove", "moreBelow", "fadeAbove", "fadeBelow", "scrollbar"])
            compare(findChild(t.hint, name).visible, false, name);
    }

    function test_page_scrolls_most_of_a_page_and_stops_at_the_ends() {
        const t = make("chevrons", 0, 5);
        t.hint.page(0.8);
        compare(t.flick.contentY, 160);
        t.hint.page(100);
        compare(t.flick.contentY, 800);
        t.hint.page(-100);
        compare(t.flick.contentY, 0);
    }
}
