import QtQuick
import QtTest
import Omaseed.Views

// #24 T7: the seeding toast floats over the table's rows. A click on it shows its torrent and
// goes no further (the row under it must not be selected too), and the pointer resting on it
// holds its five seconds.
TestCase {
    id: testCase

    property Timer timerProbe: null

    height: 300
    name: "SeedToast"
    visible: true
    when: windowShown
    width: 600

    // Stands for the table row under the toast.
    MouseArea {
        id: under

        property int clicks: 0

        anchors.fill: parent

        onClicked: under.clicks = under.clicks + 1
    }

    SeedToast {
        id: toast

        anchors.centerIn: parent
        height: implicitHeight
        width: implicitWidth
    }

    SignalSpy {
        id: showSpy

        signalName: "showRequested"
        target: toast
    }

    function init() {
        mouseMove(testCase, 1, 1);
        toast.current = null;
        under.clicks = 0;
        showSpy.clear();
        testCase.timerProbe = findChild(toast, "seedToastTimer");
        verify(testCase.timerProbe);
    }

    function test_a_click_shows_the_torrent_and_stops_there() {
        toast.push("paidBack", "abc", "debian.iso paid back", "ratio 1.00");
        verify(toast.visible);
        mouseClick(toast, toast.width / 2, toast.height / 2);
        compare(showSpy.count, 1);
        compare(showSpy.signalArguments[0][0], "abc");
        compare(under.clicks, 0, "the click went through to the row under the toast");
        compare(toast.visible, false);
    }

    function test_the_pointer_on_it_holds_the_timer() {
        toast.push("paidBack", "abc", "debian.iso paid back", "");
        verify(testCase.timerProbe.running);
        mouseMove(toast, toast.width / 2, toast.height / 2);
        tryCompare(testCase.timerProbe, "running", false);
        mouseMove(testCase, 1, 1);
        tryCompare(testCase.timerProbe, "running", true);
        verify(toast.visible);
    }
}
