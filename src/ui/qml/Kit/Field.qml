pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// Single-line filter/search field (spec v2 §1 Fig. 1 `.field.search`: a leading `/` hint, a
// placeholder, and a trailing live count). Bordered idle (4 %/40 %), hover/cursor (8 %/25 %),
// accent edge while the caret is in it (spec `.field.focus`; a field keeps it rather than the
// kit's outside FocusRing, review Q6: no field is ever primary, and the caret already says
// where the keys go). `Escape` clears the text and emits `cleared`;
// `Return` emits `accepted`. Typing updates `text` directly (it is a plain alias over the
// inner `TextInput`), so a caller that wants single-source-of-truth state should listen for
// `onTextChanged` and write back through a `Connections`/binding of its own — assigning
// `text` imperatively from outside while focused would fight the user's keystrokes.
Rectangle {
    id: root

    property alias text: input.text
    /// Where the caret is. A value written from outside leaves it at the end, which scrolls a
    /// long text to its tail; 0 shows it from the start (docs/16's long path fields).
    property alias cursorPosition: input.cursorPosition
    property string placeholder: ""
    property string hint: ""
    property string trailing: ""
    /// A key drawn as a cap after `trailing` (docs/12 F1: the Files filter's `esc`); "" none.
    property string trailingKey: ""
    /// The trailing text and cap act as a button (the add sheet's `browse`, manual box M6):
    /// a pointing hand and the accent under the mouse, and a click emits `trailingClicked`.
    /// The key it names still has to be handled by the caller.
    property bool trailingClickable: false
    property bool hasCursor: false
    /// `TextInput.Password` for a secret (spec §3 Fig. 9's `Key` field). Normal by default.
    property int echoMode: TextInput.Normal
    /// Whether `esc` clears the text first. A filter wants that (the escape ladder); a form
    /// field opened with a value to edit does not: there `esc` goes straight to the sheet,
    /// which cancels, rather than wiping the value and leaving the next keys to land in it.
    property bool escClears: true

    /// True while the caret is in the field: the caller (Main) reads it to know that the
    /// search box, not the table, owns the keyboard.
    readonly property alias editing: input.activeFocus

    signal accepted
    signal cleared
    /// A click on the trailing text or cap, when `trailingClickable`.
    signal trailingClicked
    /// One of `shortcuts` was pressed while typing here.
    signal shortcutPressed(int key, int modifiers)

    /// Keys the field hands on instead of typing: `[{key: Qt.Key_E, modifiers:
    /// Qt.ControlModifier}]`. Qt's Linux text bindings take some ctrl keys for editing (ctrl+e
    /// moves to the end of the line), so a key the caller means for itself never got past
    /// the TextInput (#11: the rename sheet's ctrl+e).
    property var shortcuts: []

    /// Puts the caret in the field. `forceActiveFocus()` on the Rectangle itself would stop
    /// at the root and never reach the inner TextInput.
    function focusInput(): void {
        input.forceActiveFocus();
    }

    /// Selects all of the text, so typing replaces it (the rename sheet's find on opening).
    function selectAll(): void {
        input.selectAll();
    }

    // `enter` (Return or keypad Enter) accepts. `shift+enter` is deliberately left alone:
    // inside a Sheet it is the secondary action (Fig. 5's `add stopped`), and a TextInput that
    // swallowed it would never let the sheet see it (T4.2).
    function _onEnter(event: var): void {
        if (event.modifiers & Qt.ShiftModifier) {
            event.accepted = false;
            return;
        }
        root.accepted();
    }

    readonly property bool hot: root.hasCursor || hoverHandler.hovered || input.activeFocus

    border.color: input.activeFocus ? Theme.accent : root.hot ? Theme.fill(Theme.borderHover) :
                                                                Theme.fill(Theme.borderIdle)
    border.width: 1
    color: root.hot ? Theme.fill(Theme.fillHover) : Theme.fill(Theme.fillIdle)
    height: Theme.controlHeight
    implicitWidth: Theme.space(220)
    radius: Theme.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: 120
        }
    }

    HoverHandler {
        id: hoverHandler
    }

    Text {
        id: hintText

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(10)
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.body
        text: root.hint
        visible: root.hint.length > 0
    }

    KeyCap {
        id: trailingCap

        anchors.right: parent.right
        anchors.rightMargin: Theme.space(8)
        anchors.verticalCenter: parent.verticalCenter
        objectName: "fieldTrailingKey"
        text: root.trailingKey
        visible: root.trailingKey.length > 0
    }

    Text {
        id: trailingText

        anchors.right: trailingCap.visible ? trailingCap.left : parent.right
        anchors.rightMargin: trailingCap.visible ? Theme.space(8) : Theme.space(10)
        anchors.verticalCenter: parent.verticalCenter
        color: trailingButton.containsMouse ? Theme.accent : Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.caption
        objectName: "fieldTrailingText"
        text: root.trailing
        visible: root.trailing.length > 0
    }

    // Over the trailing text and cap, from the text's left edge to the field's right one.
    MouseArea {
        id: trailingButton

        Accessible.name: root.trailing
        Accessible.role: Accessible.Button
        anchors.bottom: parent.bottom
        anchors.left: trailingText.visible ? trailingText.left : trailingCap.left
        anchors.leftMargin: -Theme.space(4)
        anchors.right: parent.right
        anchors.top: parent.top
        cursorShape: Qt.PointingHandCursor
        enabled: root.trailingClickable && (trailingText.visible || trailingCap.visible)
        hoverEnabled: true
        objectName: "fieldTrailingButton"

        onClicked: root.trailingClicked()
    }

    TextInput {
        id: input

        Accessible.name: root.placeholder
        Accessible.role: Accessible.EditableText
        anchors.left: hintText.visible ? hintText.right : parent.left
        anchors.leftMargin: hintText.visible ? Theme.space(6) : Theme.space(10)
        anchors.right: trailingText.visible ? trailingText.left : trailingCap.visible
                                                                  ? trailingCap.left : parent.right
        anchors.rightMargin: trailingText.visible || trailingCap.visible ? Theme.space(6)
                                                                        : Theme.space(10)
        anchors.verticalCenter: parent.verticalCenter
        clip: true
        color: Theme.brightForeground
        // Qt shows a delegate caret when the field gains the keyboard but can leave it drawn
        // when the keyboard moves on in the same frame: Speed limits opened on its alt-speed
        // pair focused Download first, and Download kept a caret over its `∞` (batch-2 P2).
        // Opacity rather than `visible`, which Qt itself toggles to blink.
        cursorDelegate: Rectangle {
            color: Theme.accent
            objectName: "fieldCaret"
            opacity: input.activeFocus ? 1 : 0
            width: 1
        }
        echoMode: root.echoMode
        passwordCharacter: "\u2022"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.body
        selectByMouse: true
        selectionColor: Theme.selection

        // The placeholder stays until something is typed: an empty focused field still
        // says what empty means (`∞` in the limit sheets). It dims one step further while
        // the caret is in, as most text fields do (batch-2 J6).
        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.body
            objectName: "fieldPlaceholder"
            opacity: input.activeFocus ? 0.55 : 1
            text: root.placeholder
            visible: input.text.length === 0
        }

        // Escape clears the field; with nothing left to clear it is passed on, so the
        // sheet, palette or view around the field still gets its turn (spec §7 Fig. 26's
        // escape ladder: "clear, then filter, then close").
        Keys.onEscapePressed: event => {
            if (!root.escClears || input.text.length === 0) {
                event.accepted = false;
                return;
            }
            input.text = "";
            root.cleared();
        }
        Keys.onPressed: event => {
            const mask = Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier | Qt.MetaModifier;
            for (const shortcut of root.shortcuts) {
                if (event.key === shortcut.key && (event.modifiers & mask) === shortcut.modifiers) {
                    root.shortcutPressed(event.key, shortcut.modifiers);
                    event.accepted = true;
                    return;
                }
            }
            event.accepted = false;
        }
        // `enter` accepts; see `_onEnter`.
        Keys.onReturnPressed: event => root._onEnter(event)
        // Keypad `enter` is its own key; without this it went past `accepted` to the Sheet's
        // primary action (KIT-7).
        Keys.onEnterPressed: event => root._onEnter(event)
    }
}
