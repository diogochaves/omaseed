pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The mouse for a one-line text box (the kit Field, the palette's query row), laid under its
// `TextInput` across the whole box. The input is one line tall and centred, so a press on the
// bands above and below it, the padding or the hint glyph went nowhere: here it puts the caret
// in the input, at the place nearest the press.
//
// A right-click anywhere in the box (the input takes only the left button, so it passes
// through) opens the kit Menu with cut, copy, paste and select all, enabled as usual: nothing
// to cut or copy without a selection or in a password, nothing to paste from an empty
// clipboard. A mouse-only user can paste a path into a field this way. The menu is made on the
// window's root item, over every sheet and palette, and goes when it closes; the keyboard comes
// back to the input.
//
// Hand-formatted; do NOT run `qmlformat` on it (typed functions, see Menu.qml).
MouseArea {
    id: root

    /// The input the box holds.
    property TextInput input: null

    /// The right-click menu while it is open, else null.
    readonly property Menu menu: root._menu

    property Menu _menu: null
    /// The selection when the menu opened: the input loses it with the keyboard.
    property int _selectionStart: 0
    property int _selectionEnd: 0

    /// The menu's rows for the input as it is now.
    function menuItems(): var {
        const input = root.input;
        const selected = input.selectionStart !== input.selectionEnd;
        const secret = input.echoMode !== TextInput.Normal;
        return [{
                "text": qsTr("cut"),
                "key": "ctrl+x",
                "action": "cut",
                "disabled": !selected || secret || input.readOnly
            }, {
                "text": qsTr("copy"),
                "key": "ctrl+c",
                "action": "copy",
                "disabled": !selected || secret
            }, {
                "text": qsTr("paste"),
                "key": "ctrl+v",
                "action": "paste",
                "disabled": !input.canPaste || input.readOnly
            }, {
                "separator": true
            }, {
                "text": qsTr("select all"),
                "key": "ctrl+a",
                "action": "selectAll",
                "disabled": input.text.length === 0
            }];
    }

    /// Open the menu at `x`, `y` in this item's coordinates.
    function openMenu(x: real, y: real): void {
        const host = root.Window.contentItem;
        if (root.input === null || host === null || root._menu !== null)
            return;
        root._selectionStart = root.input.selectionStart;
        root._selectionEnd = root.input.selectionEnd;
        const at = root.mapToItem(host, x, y);
        root._menu = menuComponent.createObject(host, {
                                                    "items": root.menuItems()
                                                });
        root._menu.openAt(Math.round(at.x), Math.round(at.y));
    }

    function _run(action: string): void {
        const input = root.input;
        // The menu gave the keyboard back already; the selection went with it.
        input.select(root._selectionStart, root._selectionEnd);
        if (action === "cut")
            input.cut();
        else if (action === "copy")
            input.copy();
        else if (action === "paste")
            input.paste();
        else if (action === "selectAll")
            input.selectAll();
    }

    function _dropMenu(): void {
        const menu = root._menu;
        root._menu = null;
        if (menu !== null)
            menu.destroy();
    }

    acceptedButtons: Qt.LeftButton | Qt.RightButton
    cursorShape: Qt.IBeamCursor
    enabled: root.input !== null && root.input.enabled

    onPressed: mouse => {
        const input = root.input;
        const at = root.mapToItem(input, mouse.x, mouse.y);
        const position = input.positionAt(at.x, at.y);
        const hadFocus = input.activeFocus;
        input.forceActiveFocus();
        if (mouse.button === Qt.RightButton) {
            // Inside the selection it stays, for cut and copy; elsewhere the caret moves there.
            const inside = input.selectionStart !== input.selectionEnd
                    && position >= input.selectionStart && position <= input.selectionEnd;
            if (!hadFocus || !inside)
                input.cursorPosition = position;
            root.openMenu(mouse.x, mouse.y);
            return;
        }
        input.cursorPosition = position;
    }
    Component.onDestruction: root._dropMenu()

    Component {
        id: menuComponent

        Menu {
            anchors.fill: parent
            dimmed: false
            menuWidth: Theme.space(200)
            objectName: "inputMenu"
            z: 1000

            onActivated: action => {
                root._run(action);
                root._dropMenu();
            }
            onRejected: {
                root.input.select(root._selectionStart, root._selectionEnd);
                root._dropMenu();
            }
        }
    }
}
