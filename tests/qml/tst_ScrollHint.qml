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

    // #8: a cover lengthens a band's ground over a straddling row; its words stay in the band
    // itself, and the insets the cursor keeps clear of do not grow.
    function test_a_cover_lengthens_the_band_but_not_its_inset() {
        const t = make("chevrons", 3, 4);
        const above = findChild(t.hint, "moreAbove");
        const below = findChild(t.hint, "moreBelow");
        t.hint.topCover = 9;
        t.hint.bottomCover = 5;
        compare(above.height, t.hint.bandHeight + 9);
        compare(below.height, t.hint.bandHeight + 5);
        compare(below.y, t.hint.height - t.hint.bandHeight - 5);
        compare(t.hint.topInset, t.hint.bandHeight);
        compare(t.hint.bottomInset, t.hint.bandHeight);
        const aboveText = findChild(t.hint, "moreAboveText");
        verify(aboveText.y + aboveText.height <= t.hint.bandHeight + 0.5);
        const belowText = findChild(t.hint, "moreBelowText");
        verify(belowText.y >= below.height - t.hint.bandHeight - 0.5);
        t.hint.topCover = -4; // never shorter than the band
        compare(above.height, t.hint.bandHeight);
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

    // KIT-12: with the thumb at its minimum length (a long list), it moves over the travel it
    // leaves, so it reaches the bottom exactly when the list does and not before.
    function test_the_thumb_reaches_the_bottom_only_at_the_end() {
        const t = make("scrollbar", 0, 5);
        t.flick.contentHeight = 20000;
        t.hint.revealed = true;
        const track = findChild(t.hint, "scrollbar");
        const thumb = findChild(t.hint, "scrollThumb");
        compare(thumb.height, Theme.space(16), "the minimum length is not in play");
        compare(thumb.y, 0);

        const range = t.flick.contentHeight - t.flick.height;
        t.flick.contentY = range * 0.97;
        verify(thumb.y + thumb.height < track.height - 0.5, "the thumb hit the bottom at 97 %");
        t.flick.contentY = range / 2;
        fuzzyCompare(thumb.y, (track.height - thumb.height) / 2, 0.5);
        t.flick.contentY = range;
        fuzzyCompare(thumb.y + thumb.height, track.height, 0.5);
    }

    // The list moving shows the bar a moment, then it goes; it never shows for a list that
    // fits or in another look.
    function test_the_scrollbar_shows_while_the_list_moves_then_goes() {
        const t = make("scrollbar", 0, 5);
        t.hint.lingerMs = 200;
        const track = findChild(t.hint, "scrollbar");
        compare(t.hint.scrollbarShown, false);
        t.flick.contentY = 100;
        compare(t.hint.scrollbarShown, true, "not shown while the list moves");
        tryCompare(track, "visible", true, 1000);
        tryCompare(t.hint, "scrollbarShown", false, 2000);
        tryCompare(track, "visible", false, 2000);

        const other = make("chevrons", 0, 5);
        other.flick.contentY = 100;
        compare(other.hint.scrollbarShown, false, "shown in the chevrons look");
    }

    // The table's bar (revealOnHover off) shows only while the list moves or the pointer is on
    // the bar itself, not whenever the pointer is over the rows.
    function test_without_reveal_on_hover_the_pointer_over_the_list_shows_nothing() {
        const t = createTemporaryObject(rowsComponent, testCase);
        t.hint.revealOnHover = false;
        waitForRendering(t);
        mouseMove(t, t.width - 5, t.height - 5);
        mouseMove(t.first, 20, t.first.height / 2);
        tryVerify(() => t.hint.hovered, 1000, "the hint does not know the pointer is over its list");
        tryCompare(t.hint, "scrollbarShown", false, 2000);
        mouseMove(testCase, testCase.width - 1, testCase.height - 1);
    }

    // A drag on the thumb and a press on the track both go through `scrollThumbTo`
    // (`mousePress` does not deliver in the offscreen runner, see tst_ScrollBar.qml).
    function test_scroll_thumb_to_moves_the_list_over_the_thumbs_travel() {
        const t = make("scrollbar", 0, 5);
        t.hint.revealed = true;
        const thumb = findChild(t.hint, "scrollThumb");
        const travel = t.hint.trackLength - thumb.height;
        const range = t.flick.contentHeight - t.flick.height;
        t.hint.scrollThumbTo(travel / 2);
        fuzzyCompare(t.flick.contentY, range / 2, 0.5);
        fuzzyCompare(thumb.y, travel / 2, 0.5);
        t.hint.scrollThumbTo(travel * 10);
        compare(t.flick.contentY, range);
        t.hint.scrollThumbTo(-50);
        compare(t.flick.contentY, 0);
    }

    // The grip takes a press only while the bar is shown, so a click at a row's right edge
    // reaches the row when there is no bar to grab.
    function test_the_grip_takes_presses_only_while_the_bar_is_shown() {
        const t = make("scrollbar", 0, 5);
        const grip = findChild(t.hint, "scrollGrip");
        compare(grip.acceptedButtons, Qt.NoButton);
        t.hint.revealed = true;
        compare(grip.acceptedButtons, Qt.LeftButton);
        const fits = make("chevrons", 0, 5);
        compare(findChild(fits.hint, "scrollGrip").enabled, false, "a grip in the chevrons look");
    }

    function test_an_unknown_style_draws_nothing() {
        const t = make("none", 4, 4);
        t.hint.revealed = true;
        for (const name of ["moreAbove", "moreBelow", "fadeAbove", "fadeBelow", "scrollbar"])
            compare(findChild(t.hint, name).visible, false, name);
    }

    // Two rail rows in a list under a hint, as the rail and every palette lay them out.
    Component {
        id: rowsComponent

        Item {
            id: rowsHost

            readonly property alias first: first
            readonly property alias second: second
            readonly property alias hint: rowsHint

            height: 200
            width: 200

            Flickable {
                id: rowsFlick

                anchors.fill: parent
                contentHeight: 1000
                contentWidth: width

                Column {
                    width: rowsFlick.width

                    RailRow {
                        id: first

                        text: "first"
                        width: parent.width
                    }

                    RailRow {
                        id: second

                        text: "second"
                        width: parent.width
                    }
                }
            }

            ScrollHint {
                id: rowsHint

                anchors.fill: parent
                countBelow: 5
                flickable: rowsFlick
                style: "scrollbar"
                z: 3
            }
        }
    }

    // M11: the hint lies over the list, and its HoverHandler made it take every hover
    // event, so a row under it never heard the pointer come or go. A click still reached
    // the row and set its MouseArea's `containsMouse`, which then never cleared: each
    // clicked rail row kept the hover fill.
    function test_rows_under_the_hint_still_hear_the_pointer() {
        const t = createTemporaryObject(rowsComponent, testCase);
        // Offscreen, hover is only delivered once the window has drawn and the pointer has
        // come into it: without these two lines no row hovers even with the hint hidden.
        waitForRendering(t);
        mouseMove(t, t.width - 5, t.height - 5);
        mouseMove(t.first, 20, t.first.height / 2);
        tryVerify(() => t.first.hovered, 1000, "the row under the pointer is not hovered");
        verify(t.hint.hovered, "the hint does not know the pointer is over its list");

        mouseClick(t.first, 20, t.first.height / 2);
        mouseMove(t.second, 20, t.second.height / 2);
        tryVerify(() => t.second.hovered, 1000, "the next row is not hovered");
        verify(!t.first.hovered, "the clicked row kept its hover");

        mouseMove(t, 20, t.height - 10);
        tryVerify(() => !t.second.hovered, 1000, "the row kept its hover after the pointer left");
        verify(t.hint.hovered, "the pointer is still over the list");
        mouseMove(testCase, testCase.width - 1, testCase.height - 1);
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
