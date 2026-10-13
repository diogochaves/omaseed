import QtQuick
import QtTest
import Omaseed.Views

// `Views/QbtListSheet.qml`: the one-per-line list editor of Settings' qBittorrent group. The
// text box is 220 px tall and the editor only as tall as its lines, so the rest of the box
// takes the clicks itself, and a scroll bar shows once the lines outgrow it.
TestCase {
    id: testCase

    height: 600
    name: "QbtListSheet"
    visible: true
    when: windowShown
    width: 900

    Component {
        id: sheetComponent

        QbtListSheet {
            anchors.fill: parent
        }
    }

    Component {
        id: probeComponent

        Item {
            activeFocusOnTab: true
        }
    }

    function test_a_click_on_the_empty_box_puts_the_caret_at_the_end() {
        const probe = createTemporaryObject(probeComponent, testCase);
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openWith("Trackers", "tracker URLs", ["udp://a.example:1337", "udp://b.example:80"]);
        sheet.editor.cursorPosition = 0;
        probe.forceActiveFocus();
        verify(!sheet.editor.activeFocus);

        // Under the two lines, well inside the box.
        const box = sheet.editor.parent.parent.parent; // editor > content > Flickable > box
        waitForRendering(box, 500);
        mouseClick(box, box.width / 2, box.height - 30);
        verify(sheet.editor.activeFocus, "a click under the text did not focus the editor");
        compare(sheet.editor.cursorPosition, sheet.editor.length);

        // The box's own padding does the same.
        sheet.editor.cursorPosition = 0;
        probe.forceActiveFocus();
        mouseClick(box, 2, 2);
        verify(sheet.editor.activeFocus, "a click on the padding did not focus the editor");
        compare(sheet.editor.cursorPosition, sheet.editor.length);
    }

    function test_a_scroll_bar_shows_once_the_lines_outgrow_the_box() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openWith("Trackers", "tracker URLs", ["udp://a.example:1337"]);
        const bar = findChild(sheet, "listScrollBar");
        verify(bar);
        compare(bar.visible, false);
        const many = [];
        for (let i = 0; i < 40; ++i)
            many.push("udp://tracker-" + i + ".example:1337");
        sheet.close();
        sheet.openWith("Trackers", "tracker URLs", many);
        tryVerify(() => bar.visible, 1000, "no scroll bar for forty lines");
        verify(sheet.editor.width + bar.width <= bar.flickable.width, "the text runs under the bar");
    }
}
