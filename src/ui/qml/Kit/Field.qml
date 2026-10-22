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
//
// The mouse: a press anywhere in the box puts the caret in the text, nearest the press (the
// input itself is one line tall), and a right-click opens cut / copy / paste / select all
// (`Kit/InputMouseArea`).
//
// A `numeric` field steps with the wheel, but only while the caret is in it (#51 D16): one
// notch is `step`, clamped to `minimum`..`maximum`, written into the text as typing would be,
// so the caller applies it however it applies a typed value (on `enter`, when the caret
// leaves, or live). Without the caret the wheel is not taken, and scrolls the sheet or pane
// around the field: scrolling past a value never changes it. An empty field steps from
// `emptyValue`, else from its placeholder when that is a number; with neither (`∞`), the
// first notch up gives `minimum` + `step` and a notch down leaves it empty.
//
// An `opener` (board 45 D5, DECISIONS C18: the Move sheet's "places ▾") is a control in the
// field, before `trailing`, in the accent: a click emits `openerClicked`, and its arrow follows
// `openerExpanded` (▾ closed, ▴ open). A screen reader reads the field as text, then the
// opener as a button of its own, collapsed or expanded (#91).
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
    /// What a screen reader calls the clickable trailing when its text is no name for it (a
    /// count that clears the filter: "clear the filter"); the text then goes in its
    /// description. "" names it by `trailing` (#78).
    property string trailingName: ""
    /// The opener's word, drawn with ▾ or ▴ after it; "" none.
    property string opener: ""
    /// The list the opener shows is open: ▴, and "expanded" to a screen reader.
    property bool openerExpanded: false
    /// What a screen reader adds to the opener's name ("9 places"); "" nothing.
    property string openerDescription: ""
    /// What a screen reader calls the field when the placeholder is no name for it (a syntax
    /// lesson, a glyph like `∞`); the placeholder then goes in its description. "" names it
    /// by `placeholder` (#78).
    property string accessibleName: ""
    /// #91: named by the caption of the FormRow it sits straight in, having no
    /// `accessibleName` of its own (FormRow then keeps its caption out of the tree).
    readonly property bool namedByRow: root.accessibleName.length === 0 && root._rowCaption.length > 0
    readonly property string _rowCaption: (root.parent as FormRow)?.label ?? ""
    /// What a screen reader calls the field: its own name, its row's caption, or at last
    /// its placeholder.
    readonly property string spokenName: root.accessibleName.length > 0 ? root.accessibleName :
                                         root._rowCaption.length > 0 ? root._rowCaption : root.placeholder
    property bool hasCursor: false
    /// `TextInput.Password` for a secret (spec §3 Fig. 9's `Key` field). Normal by default.
    property int echoMode: TextInput.Normal
    /// Whether `esc` clears the text first. A filter wants that (the escape ladder); a form
    /// field opened with a value to edit does not: there `esc` goes straight to the sheet,
    /// which cancels, rather than wiping the value and leaving the next keys to land in it.
    property bool escClears: true
    /// The wheel steps the number while the caret is in the field (see above).
    property bool numeric: false
    property real step: 1
    property real minimum: 0
    property real maximum: Number.MAX_SAFE_INTEGER
    /// The number an empty field stands for when the wheel steps from it; NaN: none.
    property real emptyValue: NaN
    /// The wheel steps the number now: numeric, with the caret in. `Kit/WheelScroll` over a
    /// pane leaves the wheel to a field that takes it.
    readonly property bool takesWheel: root.numeric && input.activeFocus

    /// True while the caret is in the field: the caller (Main) reads it to know that the
    /// search box, not the table, owns the keyboard.
    readonly property alias editing: input.activeFocus

    signal accepted
    signal cleared
    /// A click on the trailing text or cap, when `trailingClickable`.
    signal trailingClicked
    /// A click on the opener (or a screen reader's press of it).
    signal openerClicked
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

    /// Step the number by `notches` steps (negative steps down), as the wheel does; nothing on
    /// a field that is not `numeric` or whose text is not a number.
    function stepBy(notches: int): void {
        if (!root.numeric || notches === 0)
            return;
        const typed = input.text.trim();
        const places = text => {
            const at = String(text).indexOf(".");
            return at < 0 ? 0 : String(text).length - at - 1;
        };
        let base = typed.length > 0 ? Number(typed) : NaN;
        let decimals = Math.max(places(root.step), typed.length > 0 ? places(typed) : 0);
        if (typed.length > 0 && !Number.isFinite(base))
            return; // not a number: what was typed stays for the caller to refuse
        if (typed.length === 0) {
            base = Number.isFinite(root.emptyValue) ? root.emptyValue : root.placeholder.trim().length > 0
                                                                        ? Number(root.placeholder) : NaN;
            if (!Number.isFinite(base)) {
                if (notches < 0)
                    return;
                base = root.minimum;
            }
        }
        const value = Math.min(root.maximum, Math.max(root.minimum, base + notches * root.step));
        input.text = String(Number(value.toFixed(decimals)));
    }

    /// The wheel's eighths of a degree not yet a whole notch (a touchpad sends small ones).
    property real _wheelRest: 0

    function _wheel(angle: real): void {
        root._wheelRest += angle;
        const notches = root._wheelRest > 0 ? Math.floor(root._wheelRest / 120) : Math.ceil(root._wheelRest / 120);
        root._wheelRest -= notches * 120;
        root.stepBy(notches);
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

    // Under everything that takes a click: the input, and the trailing button over it.
    InputMouseArea {
        anchors.fill: parent
        input: input
        objectName: "fieldMouse"
    }

    // #91: the input comes first among the parts a screen reader reads (Qt publishes children
    // in declaration order): the field, then its opener and its trailing button.
    TextInput {
        id: input

        // Qt withholds a password field's name (#91): there the name is the description too.
        Accessible.description: root.echoMode === TextInput.Password ? root.spokenName : root.spokenName
                                                                      !== root.placeholder ? root.placeholder : ""
        Accessible.name: root.spokenName
        Accessible.editable: true
        // #91: an editable text even with an opener. As a combobox, Qt published no text
        // interface, so a screen reader could not read the path; the opener is its own
        // button, expanded or not, right after the field.
        Accessible.role: Accessible.EditableText
        objectName: "fieldInput"
        anchors.left: hintText.visible ? hintText.right : parent.left
        anchors.leftMargin: hintText.visible ? Theme.space(6) : Theme.space(10)
        anchors.right: openerText.visible ? openerText.left : trailingText.visible
                                                             ? trailingText.left
                                                             : trailingCap.visible
                                                               ? trailingCap.left : parent.right
        anchors.rightMargin: openerText.visible || trailingText.visible || trailingCap.visible
                             ? Theme.space(6) : Theme.space(10)
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

    Text {
        id: openerText

        anchors.right: trailingText.visible ? trailingText.left : trailingCap.visible
                                                                  ? trailingCap.left : parent.right
        anchors.rightMargin: trailingText.visible || trailingCap.visible ? Theme.space(12)
                                                                        : Theme.space(10)
        anchors.verticalCenter: parent.verticalCenter
        color: openerButton.containsMouse ? Theme.brightForeground : Theme.accent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.caption
        objectName: "fieldOpenerText"
        text: root.opener + " " + (root.openerExpanded ? Glyphs.chevronUp : Glyphs.chevronDown)
        visible: root.opener.length > 0
    }

    // Over the opener's text, a little wider so it is easy to hit.
    MouseArea {
        id: openerButton

        Accessible.description: root.openerDescription
        Accessible.expandable: true
        Accessible.expanded: root.openerExpanded
        Accessible.ignored: !openerText.visible
        Accessible.name: root.opener
        Accessible.role: Accessible.Button
        Accessible.onPressAction: if (root.enabled && openerButton.enabled)
                                      root.openerClicked()
        anchors.bottom: parent.bottom
        anchors.left: openerText.left
        anchors.leftMargin: -Theme.space(4)
        anchors.right: openerText.right
        anchors.rightMargin: -Theme.space(4)
        anchors.top: parent.top
        cursorShape: Qt.PointingHandCursor
        enabled: openerText.visible
        hoverEnabled: true
        objectName: "fieldOpenerButton"

        onClicked: root.openerClicked()
    }

    // Over the trailing text and cap, from the text's left edge to the field's right one.
    MouseArea {
        id: trailingButton

        readonly property bool actionNamed: trailingButton.enabled && root.trailingName.length > 0

        // #78: the action is the name, its key the description ("shortcut ctrl+o"); a trailing
        // that only shows something (a count) is a label, and none at all is nothing. #91:
        // `trailingName` names it only while it is a button: a count that can't clear
        // anything is read as the count.
        Accessible.description: {
            const parts = [];
            if (trailingButton.actionNamed && root.trailing.length > 0)
                parts.push(root.trailing);
            if (root.trailingKey.length > 0)
                parts.push(qsTr("shortcut %1").arg(root.trailingKey));
            return parts.join(" · ");
        }
        Accessible.ignored: !trailingText.visible && !trailingCap.visible
        Accessible.name: trailingButton.actionNamed ? root.trailingName : root.trailing
        Accessible.role: trailingButton.enabled ? Accessible.Button : Accessible.StaticText
        // A screen reader's press is the click (#75): only on a clickable trailing, and not
        // while the field is disabled. A MouseArea's own `enabled` is its flag alone, so the
        // field's says whether the field, or something above it, is disabled.
        Accessible.onPressAction: if (root.enabled && trailingButton.enabled)
                                      root.trailingClicked()
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

    // The wheel, over the whole box: taken only while the caret is in a numeric field, else
    // passed on to the scroller around it. Above the input, but it takes no buttons and sets
    // no cursor, so clicks, drags and the I-beam are the input's as before.
    MouseArea {
        acceptedButtons: Qt.NoButton
        anchors.fill: parent
        objectName: "fieldWheel"

        onWheel: wheel => {
            if (!root.takesWheel) {
                wheel.accepted = false;
                return;
            }
            root._wheel(wheel.angleDelta.y);
        }
    }
}
