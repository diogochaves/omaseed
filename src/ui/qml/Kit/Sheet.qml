pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The centred sheet of spec v2 §3 (`.sheet`, Fig. 5, 7, 8, 9, 10 and the `?` sheet of
// Fig. 26): an opaque panel with a 1 px accent edge over a 70 % scrim, a head of
// icon-square / title / dim subtitle / `esc`, the caller's rows in the middle, equal-width
// buttons along the foot (`.acts`, `grid-auto-flow:column; grid-auto-columns:1fr`) and an
// optional hint line above a hairline (`.ft`).
//
// Usage — the sheet is an overlay, so the caller anchors it over the whole window:
//
//     Sheet {
//         anchors.fill: parent
//         glyph: Glyphs.folder; title: qsTr("Set location"); sheetWidth: Theme.space(420)
//         actions: [{ text: qsTr("cancel") }, { text: qsTr("move"), primary: true }]
//         FormRow { Layout.fillWidth: true; label: "…"; Field { Layout.fillWidth: true } }
//         onActionTriggered: text => …
//     }
//
// `inline: true` drops the scrim and the centring and draws the panel alone at its own
// implicit size — what the gallery shows, so a Sheet can be screenshot next to the other kit
// components instead of over a window.
//
// Keyboard (the focus trap of plan §4 T4.1):
//   esc              close() + rejected()
//   enter            the action flagged `primary`
//   shift+enter      the action flagged `secondary` (Fig. 5's `add stopped`)
//   tab / shift+tab  cycle the focusable rows and the action buttons, never leaving the sheet
// `open()` remembers what had the focus and gives it to the first focusable thing in the
// body; when the body has nothing focusable it goes to the `primary` action's button (so
// `enter` runs the primary and not the first button, which is usually `cancel`), else to the
// first button, else to the body's first child, so a sheet of plain content still owns the
// keyboard. `close()` gives the focus back.
Item {
    id: root

    /// The glyph in the head's bordered square (`.hd .ic`). Empty hides the square.
    property string glyph: ""
    /// The head's title (`.hd .t b`) and the dim line under it (`.hd .t .m`).
    property string title: ""
    property string subtitle: ""
    /// The `esc` chip at the right of the head (`.hd .x`).
    property string escText: qsTr("esc")
    /// The panel's maximum width; it shrinks when the window is narrower. Fig. 5 is 600,
    /// Fig. 7/8 420, Fig. 26 760; the spec's own `.sheet` default is 520.
    property int sheetWidth: Theme.space(520)
    /// `[{text, primary, secondary, danger, key}]`, drawn as equal-width buttons.
    property var actions: []
    /// `[{key, label}]` for the footer hint line, plus one dim string pushed to the right.
    property var hints: []
    property string footerRight: ""
    /// Draw the panel alone, with no scrim and no centring (the gallery).
    property bool inline: false

    /// The caller's rows. They are a `ColumnLayout`'s children: use `Layout.fillWidth`.
    default property alias content: bodyColumn.data

    /// True between `open()` and `close()`.
    readonly property bool opened: root._opened
    readonly property alias panel: panel
    readonly property alias actionsRow: actionsRow
    readonly property alias body: bodyColumn

    /// The action the caller's text names, for a test or a caller that wants the payload.
    signal actionTriggered(string text)
    /// The `primary` action ran (`enter`, or its button).
    signal accepted
    /// `esc`, a click on the scrim, or an action flagged `cancel`.
    signal rejected

    property bool _opened: false
    property Item _previousFocus: null
    /// Everything `focusables()` has ever recognised (see its comment).
    property var _known: []

    readonly property int _margin: Theme.space(16)

    /// Show the sheet, remembering what had the focus.
    function open(): void {
    if (root._opened)
    return;
    root._previousFocus = root.Window.activeFocusItem;
    root._opened = true;
    root.focusFirst();
}

    /// Hide it and give the focus back to whatever had it before `open()`.
    function close(): void {
                          if (!root._opened)
                          return;
                          root._opened = false;
                          const previous = root._previousFocus;
                          root._previousFocus = null;
                          if (previous)
                          previous.forceActiveFocus();
                      }

    /// Everything inside the sheet that the keyboard can reach, in tab order: the body's own
    /// focusable rows first, then the action buttons. A Kit `Field` is bordered rather than
    /// tab-focusable, so anything exposing `focusInput()` counts too.
    ///
    /// Collecting has one side effect, and it is the whole focus trap: Qt consumes `tab` in
    /// the focused item itself when that item has `activeFocusOnTab`, and moves the focus
    /// along the *window's* chain — out of the sheet — before any parent sees the key. So the
    /// sheet takes `activeFocusOnTab` off what it finds and cycles the list itself; `_known`
    /// keeps recognising those items afterwards.
    function focusables(): var {
    return root.bodyFocusables().concat(root.actionButtons());
}

    /// Only the caller's rows, in tab order. `focusFirst()` needs to tell "the body has
    /// something to type in" from "this sheet is a question with buttons and nothing else".
    function bodyFocusables(): var {
    const out = [];
    root._collect(bodyColumn, out);
    for (let i = 0; i < out.length; ++i)
    root._claim(out[i]);
    return out;
}

        /// The foot's buttons, in the order `actions` lists them. The row also holds the
        /// `Repeater` that made them, so its `children` cannot be used directly.
        function actionButtons(): var {
                                      const out = [];
                                      for (let i = 0; i < actionsRow.children.length; ++i) {
                                          const child = actionsRow.children[i];
                                          if (child.enabled && root._isFocusable(child))
                                          out.push(child);
                                      }
                                      for (let i = 0; i < out.length; ++i)
                                      root._claim(out[i]);
                                      return out;
                                  }

        /// The button of the action flagged `primary`, or null when there is none.
        function primaryButton(): var {
                                     const buttons = root.actionButtons();
                                     for (let i = 0; i < buttons.length; ++i) {
                                         if (buttons[i].primary === true)
                                         return buttons[i];
                                     }
                                     return null;
                                 }

        function _isFocusable(item: var): bool {
            return item.activeFocusOnTab === true || typeof item.focusInput === "function"
                    || root._known.indexOf(item) >= 0;
        }

        function _claim(item: var): void {
        if (root._known.indexOf(item) < 0)
        root._known.push(item);
        if (item.activeFocusOnTab === true)
        item.activeFocusOnTab = false;
    }

        /// Which of `focusables()` holds the keyboard right now, or -1. A Kit `Field` counts as
        /// focused when its inner `TextInput` has the caret, hence the walk up the parents.
        function focusedIndex(): int {
            const list = root.focusables();
            let item = root.Window.activeFocusItem;
            while (item) {
                const at = list.indexOf(item);
                if (at >= 0)
                return at;
                item = item.parent;
            }
                return -1;
            }

                /// Put the keyboard inside the sheet (see the class comment for the fallbacks).
                function focusFirst(): void {
                                           const rows = root.bodyFocusables();
                                           if (rows.length > 0) {
                                               root._focus(rows[0]);
                                               return;
                                           }
                                           // A sheet whose body is plain content (Fig. 10, a
                                           // confirm) has only its buttons. `enter` must run
                                           // the primary one, so that is what the keyboard
                                           // lands on — not `cancel`, which is drawn first.
                                           const primary = root.primaryButton();
                                           if (primary) {
                                               root._focus(primary);
                                               return;
                                           }
                                           const buttons = root.actionButtons();
                                           if (buttons.length > 0) {
                                               root._focus(buttons[0]);
                                               return;
                                           }
                                           for (let i = 0; i < bodyColumn.children.length; ++i) {
                                               if (bodyColumn.children[i].visible) {
                                                   bodyColumn.children[i].forceActiveFocus();
                                                   return;
                                               }
                                           }
                                           panel.forceActiveFocus();
                                       }

                /// Run the `primary` action, as `enter` does. Callers wire a field's `accepted` to this,
                /// because a focused `TextInput` swallows the key before the sheet sees it.
                function triggerPrimary(): void {
                const action = root._actionFlagged("primary");
                if (action)
                root._trigger(action);
            }

                /// Run the `secondary` action, as `shift+enter` does.
                function triggerSecondary(): void {
                                                 const action = root._actionFlagged("secondary");
                                                 if (action)
                                                 root._trigger(action);
                                             }

                function _actionFlagged(flag: string): var {
                for (let i = 0; i < root.actions.length; ++i) {
                    if (root.actions[i][flag] === true)
                    return root.actions[i];
                }
                    return null;
                }

                    function _trigger(action: var): void {
                    root.actionTriggered(action.text);
                    if (action.primary === true)
                    root.accepted();
                    else if (action.cancel === true)
                    root.rejected();
                }

                    function _collect(item: var, out: var): void {
                    for (let i = 0; i < item.children.length; ++i) {
                        const child = item.children[i];
                        if (!child.visible || !child.enabled)
                        continue;
                        if (root._isFocusable(child)) {
                            out.push(child);
                            continue;
                        }
                            root._collect(child, out);
                        }
                        }

                            function _focus(item: var): void {
                            if (typeof item.focusInput === "function")
                            item.focusInput();
                            else
                            item.forceActiveFocus();
                        }

                            function _cycleFocus(delta: int): void {
                            const list = root.focusables();
                            if (list.length === 0)
                            return;
                            const at = root.focusedIndex();
                            const next = at < 0 ? (delta > 0 ? 0 : list.length - 1) : (at + delta
                            + list.length) % list.length;
                            root._focus(list[next]);
                        }

                            function _onKey(event: var): void {
                            if (event.key === Qt.Key_Escape) {
                                root.close();
                                root.rejected();
                                event.accepted = true;
                                return;
                            }
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    const action = (event.modifiers & Qt.ShiftModifier)
                                    ? root._actionFlagged("secondary") : root._actionFlagged(
                                                                             "primary");
                                    if (action) {
                                        root._trigger(action);
                                        event.accepted = true;
                                    }
                                        return;
                                    }
                                        if (event.key === Qt.Key_Tab || event.key
                                        === Qt.Key_Backtab) {
                                            const backwards = event.key === Qt.Key_Backtab || (
                                            event.modifiers & Qt.ShiftModifier);
                                            root._cycleFocus(backwards ? -1 : 1);
                                            event.accepted = true;
                                        }
                                        }

                                            implicitHeight: panel.implicitHeight
                                            implicitWidth: root.sheetWidth
                                            visible: root.inline || root._opened

                                            Keys.onPressed: event => root._onKey(event)

                                            Scrim {
                                                anchors.fill: parent
                                                visible: !root.inline

                                                onDismissed: {
                                                    root.close();
                                                    root.rejected();
                                                }
                                            }

                                            Rectangle {
                                                id: panel

                                                readonly property int padding: Theme.space(16)

                                                border.color: Theme.accent
                                                border.width: 1
                                                color: Theme.background
                                                height: root.inline ? implicitHeight : Math.min(
                                                                          implicitHeight, Math.max(0,
                                                                                                   root.height
                                                                                                   - 2 * root._margin))
                                                implicitHeight: layout.implicitHeight + 2
                                                                * panel.padding
                                                radius: Theme.cornerRadius
                                                width: Math.min(root.sheetWidth, root.inline
                                                                ? root.sheetWidth : Math.max(0,
                                                                                             root.width
                                                                                             - 2 * root._margin))
                                                x: root.inline ? 0 : Math.round((root.width
                                                                                 - width) / 2)
                                                y: root.inline ? 0 : Math.round((root.height
                                                                                 - height) / 2)

                                                ColumnLayout {
                                                    id: layout

                                                    anchors.fill: parent
                                                    anchors.margins: panel.padding
                                                    spacing: Theme.space(12)

                                                    // head (`.sheet .hd`) ---------------------------------------------------
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: Theme.space(10)
                                                        visible: root.glyph.length > 0
                                                                 || root.title.length > 0

                                                        Rectangle {
                                                            Layout.alignment: Qt.AlignTop
                                                            Layout.preferredHeight: Theme.space(30)
                                                            Layout.preferredWidth: Theme.space(30)
                                                            border.color: Theme.fill(
                                                                              Theme.borderIdle)
                                                            border.width: 1
                                                            color: "transparent"
                                                            radius: Theme.cornerRadius
                                                            visible: root.glyph.length > 0

                                                            Text {
                                                                anchors.centerIn: parent
                                                                color: Theme.foreground
                                                                font.family: Theme.fontFamily
                                                                font.pixelSize: Theme.font.subtitle
                                                                text: root.glyph
                                                            }
                                                        }

                                                        ColumnLayout {
                                                            Layout.fillWidth: true
                                                            spacing: Theme.space(1)

                                                            Label {
                                                                Layout.fillWidth: true
                                                                bright: true
                                                                elide: Text.ElideRight
                                                                font.weight: Font.DemiBold
                                                                text: root.title
                                                            }

                                                            Label {
                                                                Layout.fillWidth: true
                                                                dim: true
                                                                elide: Text.ElideRight
                                                                size: "small"
                                                                text: root.subtitle
                                                                visible: root.subtitle.length > 0
                                                            }
                                                        }

                                                        Label {
                                                            Layout.alignment: Qt.AlignTop
                                                            dim: true
                                                            size: "caption"
                                                            text: root.escText
                                                            visible: root.escText.length > 0
                                                        }
                                                    }

                                                    // the caller's rows ------------------------------------------------------
                                                    Flickable {
                                                        Layout.fillHeight: true
                                                        Layout.fillWidth: true
                                                        Layout.maximumHeight:
                                                            bodyColumn.implicitHeight
                                                        Layout.preferredHeight:
                                                            bodyColumn.implicitHeight
                                                        boundsBehavior: Flickable.StopAtBounds
                                                        clip: true
                                                        contentHeight: bodyColumn.implicitHeight
                                                        contentWidth: width
                                                        visible: bodyColumn.children.length > 0

                                                        ColumnLayout {
                                                            id: bodyColumn

                                                            spacing: Theme.space(12)
                                                            width: parent.width
                                                        }
                                                    }

                                                    // the buttons (`.sheet .acts`) -------------------------------------------
                                                    RowLayout {
                                                        id: actionsRow

                                                        Layout.fillWidth: true
                                                        Layout.topMargin: Theme.space(4)
                                                        spacing: Theme.space(8)
                                                        visible: root.actions.length > 0

                                                        Repeater {
                                                            model: root.actions

                                                            Button {
                                                                id: actionButton

                                                                required property var modelData

                                                                Layout.fillWidth: true
                                                                Layout.preferredWidth: 1
                                                                danger: actionButton.modelData.danger
                                                                        === true
                                                                primary: actionButton.modelData.primary
                                                                         === true
                                                                text: actionButton.modelData.text

                                                                onClicked: root._trigger(
                                                                               actionButton.modelData)
                                                            }
                                                        }
                                                    }

                                                    // the hint line (`.sheet .ft`) -------------------------------------------
                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: Theme.space(8)
                                                        visible: root.hints.length > 0
                                                                 || root.footerRight.length > 0

                                                        Hairline {
                                                            Layout.fillWidth: true
                                                        }

                                                        KeyHints {
                                                            Layout.fillWidth: true
                                                            model: root.hints
                                                            pinned: root.footerRight.length > 0 ? {
                                                                                                      "key": "",
                                                                                                      "label": root.footerRight
                                                                                                  } : null
                                                            size: "caption"
                                                        }
                                                    }
                                                }
                                            }
                                        }
