import QtQuick
import QtTest
import Omaseed.Kit

// `Kit/WheelScroll.qml`: a notch moves `rowsPerNotch` rows, notches add up instead of
// restarting the glide (Qt's Flickable moved 165 px for twenty quick notches), and the list
// stops at its ends. `mouseWheel` does not deliver in the offscreen runner (see
// tst_ScrollBar.qml), so the notches go through `scrollBy`, the one function `onWheel` calls.
TestCase {
    id: testCase

    height: 300
    name: "WheelScroll"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: listComponent

        Item {
            readonly property alias flick: flick
            readonly property alias wheel: wheel

            height: 200
            width: 200

            Flickable {
                id: flick

                anchors.fill: parent
                contentHeight: 2000
                contentWidth: width
            }

            WheelScroll {
                id: wheel

                anchors.fill: flick
                duration: 0
                flickable: flick
                rowHeight: 20
            }
        }
    }

    function make() {
        return createTemporaryObject(listComponent, testCase);
    }

    function test_a_notch_is_three_rows() {
        const t = make();
        compare(t.wheel.step, 60);
        t.wheel.scrollBy(t.wheel.step);
        compare(t.flick.contentY, 60);
        t.wheel.scrollBy(-t.wheel.step);
        compare(t.flick.contentY, 0);
    }

    function test_quick_notches_add_up() {
        const t = make();
        for (let i = 0; i < 20; ++i)
            t.wheel.scrollBy(t.wheel.step);
        compare(t.flick.contentY, 1200);
    }

    function test_notches_during_a_glide_add_to_where_it_is_going() {
        const t = make();
        t.wheel.duration = 2000;
        t.wheel.scrollBy(t.wheel.step);
        t.wheel.scrollBy(t.wheel.step);
        t.wheel.scrollBy(t.wheel.step);
        compare(t.wheel.target, 180);
        verify(t.flick.contentY < 180, "the glide jumped instead of gliding");
        tryCompare(t.flick, "contentY", 180, 5000);
    }

    function test_the_list_stops_at_its_ends() {
        const t = make();
        t.wheel.scrollBy(-500);
        compare(t.flick.contentY, 0);
        t.wheel.scrollBy(100000);
        compare(t.flick.contentY, t.flick.contentHeight - t.flick.height);
    }

    function test_a_list_that_fits_does_not_move() {
        const t = make();
        t.flick.contentHeight = 100;
        t.wheel.scrollBy(t.wheel.step);
        compare(t.flick.contentY, 0);
    }

    // It takes no button: a click on a row under it still reaches the row.
    function test_it_takes_no_buttons() {
        const t = make();
        compare(t.wheel.acceptedButtons, Qt.NoButton);
        verify(!t.wheel.hoverEnabled, "it would take the rows' hover (M11)");
    }
}
