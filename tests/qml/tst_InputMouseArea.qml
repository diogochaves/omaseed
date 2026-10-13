import QtQuick
import QtTest
import Omaseed.Kit

// `Kit/InputMouseArea.qml`: a press anywhere in a text box puts the caret in its input, and a
// right-click opens cut / copy / paste / select all.
TestCase {
    id: testCase

    height: 400
    name: "InputMouseArea"
    visible: true
    when: windowShown
    width: 600

    // A box like the kit Field's: a short input centred in a taller, wider box.
    Component {
        id: boxComponent

        Rectangle {
            property alias area: area
            property alias input: input

            height: 60
            width: 300
            x: 100
            y: 100

            InputMouseArea {
                id: area

                anchors.fill: parent
                input: input
            }

            TextInput {
                id: input

                anchors.left: parent.left
                anchors.leftMargin: 40
                anchors.right: parent.right
                anchors.rightMargin: 40
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 14
                selectByMouse: true
                text: "hello world"
            }
        }
    }

    Component {
        id: probeComponent

        Item {
            activeFocusOnTab: true
        }
    }

    /// The open menu's row `action`.
    function menuRow(menu, action) {
        const index = menu.items.findIndex(item => item.action === action);
        verify(index >= 0, "no row " + action);
        return menu.rowsLayout.children[index];
    }

    function test_a_press_off_the_input_puts_the_caret_in_it() {
        const probe = createTemporaryObject(probeComponent, testCase);
        const box = createTemporaryObject(boxComponent, testCase);
        probe.forceActiveFocus();

        // The left padding, on the band above the line: the start.
        mouseClick(box, 5, 5);
        verify(box.input.activeFocus, "the press did not focus the input");
        compare(box.input.cursorPosition, 0);
        // The right padding, below the line: the end.
        mouseClick(box, box.width - 5, box.height - 5);
        compare(box.input.cursorPosition, box.input.text.length);
        compare(box.area.cursorShape, Qt.IBeamCursor);
    }

    function test_a_right_click_opens_the_edit_menu() {
        const box = createTemporaryObject(boxComponent, testCase);
        verify(box.area.menu === null);
        // Over the input itself: it takes the left button only, so this reaches the area.
        mouseClick(box.input, 10, box.input.height / 2, Qt.RightButton);
        const menu = box.area.menu;
        verify(menu !== null, "no menu");
        verify(menu.opened);
        compare(menu.items.map(item => item.action ?? "-"), ["cut", "copy", "paste", "-",
                                                             "selectAll"]);
        // Nothing selected: nothing to cut or copy.
        compare(menu.selectable(0), false);
        compare(menu.selectable(1), false);
        compare(menu.selectable(4), true);

        keyClick(Qt.Key_Escape);
        verify(box.area.menu === null, "the menu stayed after esc");
        verify(box.input.activeFocus, "the keyboard did not come back to the input");
    }

    function test_cut_and_paste_through_the_menu() {
        const box = createTemporaryObject(boxComponent, testCase);
        box.input.forceActiveFocus();
        box.input.select(0, 5); // "hello"

        mouseClick(box, 5, 5, Qt.RightButton);
        let menu = box.area.menu;
        verify(menu.selectable(0), "cut is off with a selection");
        verify(menu.selectable(1), "copy is off with a selection");
        mouseClick(menuRow(menu, "cut"));
        compare(box.input.text, " world");
        verify(box.area.menu === null);

        box.input.cursorPosition = box.input.text.length;
        mouseClick(box, box.width - 5, 5, Qt.RightButton);
        menu = box.area.menu;
        verify(menu.selectable(2), "paste is off with text on the clipboard");
        mouseClick(menuRow(menu, "paste"));
        compare(box.input.text, " worldhello");

        mouseClick(box, 5, 5, Qt.RightButton);
        mouseClick(menuRow(box.area.menu, "selectAll"));
        compare(box.input.selectedText, " worldhello");
    }

    function test_a_secret_cannot_be_cut_or_copied() {
        const box = createTemporaryObject(boxComponent, testCase);
        box.input.echoMode = TextInput.Password;
        box.input.forceActiveFocus();
        box.input.selectAll();
        mouseClick(box, 5, 5, Qt.RightButton);
        const menu = box.area.menu;
        compare(menu.selectable(0), false);
        compare(menu.selectable(1), false);
        keyClick(Qt.Key_Escape);
    }
}
