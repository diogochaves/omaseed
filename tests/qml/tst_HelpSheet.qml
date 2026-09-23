import QtQuick
import QtTest
import Omaseed.Views

// `Views/HelpSheet.qml` on a short window (batch-2 pre-walk P1): the list is capped at 60 % of
// the window, so the rows below the fold need a scrollbar that shows, and keys that reach them
// without a mouse.
TestCase {
    id: testCase

    name: "HelpSheet"

    height: 500
    visible: true
    when: windowShown
    width: 1100

    Component {
        id: sheetComponent

        HelpSheet {
            anchors.left: parent.left
            anchors.right: parent.right
        }
    }

    /// Depth-first search by objectName; QML's TestCase has no findChild of its own.
    function childNamed(item, name) {
        if (item.objectName === name)
            return item;
        for (let i = 0; i < item.children.length; ++i) {
            const found = testCase.childNamed(item.children[i], name);
            if (found)
                return found;
        }
        return null;
    }

    function init() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        verify(sheet);
        sheet.forceActiveFocus();
        return sheet;
    }

    function test_scrollbarShowsWhenRowsOverflow() {
        const sheet = init();
        const bar = childNamed(sheet, "helpScrollBar");
        verify(bar);
        tryVerify(() => bar.needed);
        verify(bar.visible);
        verify(bar.flickable.height < bar.flickable.contentHeight);
    }

    function test_arrowsAndPageKeysScrollWithoutAFilter() {
        const sheet = init();
        const list = childNamed(sheet, "helpScrollBar").flickable;
        tryVerify(() => list.contentHeight > list.height);
        compare(list.contentY, 0);
        keyClick(Qt.Key_Down);
        verify(list.contentY > 0);
        const afterDown = list.contentY;
        keyClick(Qt.Key_PageDown);
        verify(list.contentY > afterDown);
        for (let i = 0; i < 20; ++i)
            keyClick(Qt.Key_PageDown);
        compare(list.contentY, list.contentHeight - list.height); // clamped at the end
        keyClick(Qt.Key_Up);
        verify(list.contentY < list.contentHeight - list.height);
    }

    function test_arrowsMoveTheCursorWhileFiltering() {
        const sheet = init();
        const list = childNamed(sheet, "helpScrollBar").flickable;
        childNamed(sheet, "helpFilter").focusInput();
        keyClick("s"); // a character, so the filter field receives text
        tryCompare(sheet, "query", "s");
        verify(sheet.matchedRows.length > 1);
        compare(sheet.cursor, -1);
        keyClick(Qt.Key_Down);
        verify(sheet.cursor >= 0);
        compare(list.contentY, 0);
    }
}
