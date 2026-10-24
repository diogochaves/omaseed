pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The centred sheet (spec v2 §3 `.sheet`, in board 46's frame: `.sheet`, `.shh`, `.shb`, `.shf`,
// DECISIONS C24 H7): an opaque panel with a 1 px accent edge over the scrim; a head on a
// hairline (title, an optional trailer, the dim subtitle beside the title while it fits and
// under it otherwise, `esc`); the caller's rows; a foot on a hairline, a dim note at its left
// and the buttons at its right, each as wide as its words, the primary filled with the
// accent. Its form is `Theme.sheet*` tokens (frame plan, "form in tokens").
//
// Keys on demand (C24 H10, law 4): the foot paints no key hints; the `?` sheet's block "in a
// sheet" teaches them, and the note holds only words (C11 D4's "s next server" is gone).
//
// Usage — the sheet is an overlay, so the caller anchors it over the whole window:
//
//     Sheet {
//         anchors.fill: parent
//         title: qsTr("Set location"); sheetWidth: Theme.space(420)
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
//   esc              close() + rejected(); when `guarded`, the first `esc` only puts the
//                    keyboard on the `cancel` action, and `esc` there closes
//   enter            the action flagged `primary`
//   shift+enter      the action flagged `secondary` (Fig. 5's `add stopped`)
//   tab / shift+tab  cycle the focusable rows and the action buttons, never leaving the sheet
// `open()` remembers what had the focus and gives it to the first focusable thing in the
// body; when the body has nothing focusable it goes to the `primary` action's button (so
// `enter` runs the primary and not the first button, which is usually `cancel`), else to the
// first button, else to the body's first child, so a sheet of plain content still owns the
// keyboard. `close()` gives the focus back.
//
// The mouse: the head's `esc` is a button that does what the key does for this sheet
// (`pressEscape()`: a guarded sheet puts the keyboard on `cancel` first). A body taller than the
// room it has scrolls, and a scroll bar at its right edge says so.
//
// Do NOT run `qmlformat` on this file: with the project's 100-column wrap it re-indents the
// typed functions further on every pass (the defect MainView.qml and TorrentTable.qml note).
// It is hand-formatted to qmlformat's own style instead.
Item {
    id: root

    /// The glyph in the head's bordered square (spec v2 `.hd .ic`), drawn only where the
    /// skin draws one (`Theme.sheetHeadGlyph`; board 46 draws none). Empty hides the square.
    property string glyph: ""
    /// The head's title (`.hd .t b`) and the dim line under it (`.hd .t .m`).
    property string title: ""
    property string subtitle: ""
    /// What a screen reader calls the dialog: the title, or words of the caller's when the
    /// head has none (#79: Settings draws its titles in the pane).
    property string accessibleName: root.title
    /// The subtitle takes a second line when it needs one, as ConfirmDialog's does, instead
    /// of being cut at the panel's edge (#66: the `?` sheet's). Off, it stays one line and
    /// elides (a torrent's name in the add sheet).
    property bool subtitleWraps: false
    /// How many lines a wrapping subtitle may take before it elides (docs/34 D6 a: the import
    /// sheet's "from" line names a server, a date, a file and the counts).
    property int subtitleLines: 2
    /// The `esc` chip at the right of the head (`.hd .x`).
    property string escText: qsTr("esc")
    /// The panel's maximum width; it shrinks when the window is narrower. Fig. 5 is 600,
    /// Fig. 7/8 420, Fig. 26 760; the spec's own `.sheet` default is 520.
    property int sheetWidth: Theme.space(520)
    /// `[{text, primary, secondary, danger, disabled, cancel}]`, drawn right-aligned in the
    /// foot, each as wide as its words (board 46 `.shf .btn`; spec v2's equal widths made the
    /// mixed free sheet 880 wide, #169). A
    /// `disabled` one is dimmed, skipped by `tab`, and `enter` does not run it (#11: the
    /// rename sheet's button while names collide). A new list of the same length relabels the
    /// buttons in place, so the one holding the keyboard keeps it.
    property var actions: []
    /// A dim line at the foot's left, before the buttons (board 46's "waves of 3, smallest
    /// first"): words, never keys (C24 H10). Empty draws none.
    property string note: ""
    /// Draw the panel alone, with no scrim and no centring (the gallery).
    property bool inline: false
    /// While open, keys the sheet has no use for stop here instead of reaching the view
    /// behind the scrim (KIT-6); only `ctrl+q` goes on, since quitting is never modal.
    property bool modal: true
    /// For a sheet whose work is easy to lose (the add sheet, manual box M5): a click on the
    /// scrim does nothing, and `esc` goes to the `cancel` button before it closes anything.
    /// Only `cancel` (or `esc` on it) throws the sheet away.
    property bool guarded: false
    /// A sheet's own keys, asked before the kit's (`esc`, `enter`, `tab`): a function taking
    /// the key event, which sets `event.accepted` for a key it took. Null asks nothing.
    property var keyHandler: null
    /// Drawn right after the title on its line (docs/33 A1: the add sheet's server chip).
    /// Null draws nothing.
    property Component titleTrailer: null
    /// docs/35 N7: a band under the head, the panel's full width (its padding bled), between
    /// the head and the rows: the sheet's notice. Null draws nothing; a band of no height
    /// (a folded Notice) takes no room.
    property Component headBand: null

    /// The caller's rows. They are a `ColumnLayout`'s children: use `Layout.fillWidth`.
    default property alias content: bodyColumn.data

    /// True between `open()` and `close()`.
    readonly property bool opened: root._opened
    readonly property alias panel: panel
    readonly property alias actionsRow: actionsRow
    readonly property alias body: bodyColumn
    /// The head's `esc` button and the body's scroll bar, for the tests.
    readonly property alias escButton: escArea
    readonly property alias bodyScrollBar: bodyScrollBar
    /// How tall the caller's rows can be before the body starts to scroll: the window less
    /// the margins, padding, head, buttons and hint line. A row that should take what is left
    /// (the add sheet's file tree) sizes itself from this.
    readonly property real bodyRoom: {
        let chrome = 2 * root._margin + 2 * Theme.sheetEdge + 2 * Theme.sheetBodyPadding;
        if (headBox.visible)
            chrome += headBox.height + 1;
        if (headBandLoader.visible)
            chrome += headBandLoader.height;
        if (footBox.visible)
            chrome += footBox.height + 1;
        return Math.max(0, root.height - chrome);
    }

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

    readonly property int _margin: Theme.sheetMargin

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

    /// The button of the action flagged `primary`, or null when there is none. A disabled
    /// one counts too (it is still drawn); actionButtons() leaves it out.
    function primaryButton(): var {
        // Claims the buttons first: focusFirst() puts the keyboard on this one, and a button
        // still on the window's own tab chain then takes `tab` itself and leaves the sheet.
        root.actionButtons();
        for (let i = 0; i < actionsRow.children.length; ++i) {
            const child = actionsRow.children[i];
            if (child.primary === true)
                return child;
        }
        return null;
    }

    /// The button of the action flagged `cancel`, or null when there is none.
    function cancelButton(): var {
        const buttons = root.actionButtons();
        for (let i = 0; i < buttons.length; ++i) {
            if (buttons[i].modelData && buttons[i].modelData.cancel === true)
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
        // A sheet whose body is plain content (Fig. 10, a confirm) has only its buttons.
        // `enter` must run the primary one, so that is what the keyboard lands on — not
        // `cancel`, which is drawn first.
        const primary = root.primaryButton();
        if (primary && primary.enabled) {
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

    /// Do what `esc` does: the sheet's own key handler is asked first, then the kit's (close
    /// and reject; on a guarded sheet, the keyboard goes to `cancel` first). The head's `esc`
    /// runs this. A field's own `esc` (clearing a filter) is not part of it.
    function pressEscape(): void {
        const event = {
            "key": Qt.Key_Escape,
            "modifiers": Qt.NoModifier,
            "text": "",
            "accepted": false
        };
        root._onKey(event);
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
        if (action.disabled === true)
            return;
        root.actionTriggered(action.text);
        if (action.primary === true) {
            root.accepted();
        } else if (action.cancel === true) {
            // The same as `esc`: a `cancel` button closes the sheet itself. `close()` does
            // nothing when the caller already closed it from `actionTriggered`.
            root.close();
            root.rejected();
        }
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
        const next = at < 0 ? (delta > 0 ? 0 : list.length - 1)
                : (at + delta + list.length) % list.length;
        root._focus(list[next]);
    }

    /// The one key a modal overlay lets through: `ctrl+q`.
    function _passesThrough(event: var): bool {
        return (event.modifiers & Qt.ControlModifier) !== 0 && event.key === Qt.Key_Q;
    }

    function _onKey(event: var): void {
        if (root.keyHandler) {
            root.keyHandler(event);
            if (event.accepted)
                return;
        }
        if (event.key === Qt.Key_Escape) {
            // Guarded: the first `esc` only puts the keyboard on `cancel`, so one stray key
            // cannot throw the sheet's work away; `esc` (or `enter`) on it then closes.
            const cancel = root.guarded ? root.cancelButton() : null;
            if (cancel && root.focusables()[root.focusedIndex()] !== cancel) {
                root._focus(cancel);
                event.accepted = true;
                return;
            }
            root.close();
            root.rejected();
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            const action = (event.modifiers & Qt.ShiftModifier) ? root._actionFlagged("secondary")
                    : root._actionFlagged("primary");
            if (action) {
                root._trigger(action);
                event.accepted = true;
            }
            return;
        }
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            const backwards = event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier);
            root._cycleFocus(backwards ? -1 : 1);
            event.accepted = true;
        }
    }

    implicitHeight: panel.implicitHeight
    implicitWidth: root.sheetWidth
    visible: root.inline || root._opened

    Keys.onPressed: event => {
        root._onKey(event);
        if (root.modal && root._opened && !event.accepted)
            event.accepted = !root._passesThrough(event);
    }

    Scrim {
        anchors.fill: parent
        visible: !root.inline

        onDismissed: {
            if (root.guarded)
                return;
            root.close();
            root.rejected();
        }
    }

    Rectangle {
        id: panel

        /// The head's room for the title, its trailer and a subtitle beside them.
        readonly property real _titleRoom: titleRow.width
        /// The subtitle sits beside the title while the whole of it fits there (board 46's
        /// move sheet, "Move 3 torrents  81 KB on disk · on public"); a long one goes under
        /// the title, and one that wraps always does.
        readonly property bool subtitleBeside: root.subtitle.length > 0 && !root.subtitleWraps
                                               && Math.ceil(titleMetrics.advanceWidth)
                                               + (trailerLoader.visible ? trailerLoader.width + titleRow.spacing : 0)
                                               + Theme.sheetHeadGap + Math.ceil(subtitleMetrics.advanceWidth)
                                               <= panel._titleRoom

        Accessible.name: root.accessibleName
        Accessible.role: Accessible.Dialog
        border.color: Theme.accent
        border.width: Theme.sheetEdge
        color: Theme.background
        height: root.inline ? implicitHeight
                : Math.min(implicitHeight, Math.max(0, root.height - 2 * root._margin))
        implicitHeight: layout.implicitHeight + 2 * Theme.sheetEdge
        radius: Theme.sheetRadius
        width: Math.min(root.sheetWidth, root.inline ? root.sheetWidth
                : Math.max(0, root.width - 2 * root._margin))
        x: root.inline ? 0 : Math.round((root.width - width) / 2)
        y: root.inline ? 0 : Math.round((root.height - height) / 2)

        ColumnLayout {
            id: layout

            anchors.fill: parent
            anchors.margins: Theme.sheetEdge
            spacing: 0

            // head (board 46 `.shh`) ------------------------------------------------
            Item {
                id: headBox

                Layout.fillWidth: true
                Layout.preferredHeight: head.implicitHeight + 2 * Theme.sheetHeadPadding
                visible: (Theme.sheetHeadGlyph && root.glyph.length > 0) || root.title.length > 0

                RowLayout {
                    id: head

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.sheetInset
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.sheetInset
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.sheetHeadGap

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredHeight: Theme.space(30)
                        Layout.preferredWidth: Theme.space(30)
                        border.color: Theme.fill(Theme.borderIdle)
                        border.width: 1
                        color: "transparent"
                        objectName: "sheetGlyph"
                        radius: Theme.cornerRadius
                        visible: Theme.sheetHeadGlyph && root.glyph.length > 0

                        Glyph {
                            anchors.centerIn: parent
                            color: Theme.foreground
                            pixelSize: Theme.font.subtitle
                            text: root.glyph
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        // The head's whole width, whatever its children allow: a layout's
                        // maximum is otherwise its children's, and a title alone (its maximum
                        // its own words) left no room to measure a subtitle against.
                        Layout.maximumWidth: Number.POSITIVE_INFINITY
                        spacing: Theme.space(1)

                        RowLayout {
                            id: titleRow

                            Layout.fillWidth: true
                            Layout.maximumWidth: Number.POSITIVE_INFINITY
                            spacing: Theme.space(6)

                            Label {
                                id: titleLabel

                                // #79: the sheet's title is its heading; a plain Text
                                // publishes nothing to a screen reader otherwise.
                                Accessible.name: text
                                Accessible.role: Accessible.Heading
                                Layout.alignment: Qt.AlignLeft | Qt.AlignBaseline
                                Layout.fillWidth: root.titleTrailer === null && !panel.subtitleBeside
                                // The title's whole width, measured apart: an elided Text's
                                // own implicitWidth can follow its elision, and a title that
                                // grew (a move sheet's, read again as its plan changes)
                                // stayed cut.
                                Layout.maximumWidth: Math.ceil(titleMetrics.advanceWidth) + 1
                                Layout.minimumWidth: Math.min(Layout.maximumWidth, Theme.space(80))
                                bright: true
                                elide: Text.ElideRight
                                font.pixelSize: Theme.sheetTitleSize
                                font.weight: Theme.sheetTitleWeight
                                objectName: "sheetTitle"
                                text: root.title

                                TextMetrics {
                                    id: titleMetrics

                                    font: titleLabel.font
                                    text: titleLabel.text
                                }
                            }

                            Loader {
                                id: trailerLoader

                                active: root.titleTrailer !== null
                                objectName: "sheetTitleTrailer"
                                sourceComponent: root.titleTrailer
                                visible: active
                            }

                            Label {
                                id: subtitleBesideLabel

                                // #79: what the sheet acts on, whole even when it elides.
                                Accessible.name: text
                                Accessible.role: Accessible.StaticText
                                // On the title's baseline (board 46 `.shh`'s one line).
                                Layout.alignment: Qt.AlignLeft | Qt.AlignBaseline
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.sheetHeadGap - titleRow.spacing
                                dim: true
                                elide: Text.ElideRight
                                font.pixelSize: Theme.sheetSubtitleSize
                                objectName: panel.subtitleBeside ? "sheetSubtitle" : "sheetSubtitleSpare"
                                text: root.subtitle
                                visible: panel.subtitleBeside

                                TextMetrics {
                                    id: subtitleMetrics

                                    font: subtitleBesideLabel.font
                                    text: root.subtitle
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                visible: root.titleTrailer !== null && !panel.subtitleBeside
                            }
                        }

                        Label {
                            // #79: what the sheet acts on ("1 torrent · 1.31 GB moves with it"),
                            // whole even when it elides.
                            Accessible.name: text
                            Accessible.role: Accessible.StaticText
                            Layout.fillWidth: true
                            dim: true
                            elide: Text.ElideRight
                            font.pixelSize: Theme.sheetSubtitleSize
                            maximumLineCount: root.subtitleWraps ? root.subtitleLines : 1
                            objectName: panel.subtitleBeside ? "sheetSubtitleSpare" : "sheetSubtitle"
                            text: root.subtitle
                            visible: root.subtitle.length > 0 && !panel.subtitleBeside
                            wrapMode: root.subtitleWraps ? Text.WordWrap : Text.NoWrap
                        }
                    }

                    // It looks like a close control, so it is one: what `esc` does, by click
                    // (board 46 `.btn.ghost`).
                    Label {
                        id: escLabel

                        Layout.alignment: Qt.AlignTop
                        color: escArea.containsMouse ? Theme.accent : Theme.dimForeground
                        size: "small"
                        text: root.escText
                        visible: root.escText.length > 0

                        MouseArea {
                            id: escArea

                            // #78: named by what it does, not by its key, which is the
                            // description.
                            Accessible.description: qsTr("shortcut %1").arg(root.escText)
                            Accessible.name: qsTr("close")
                            Accessible.role: Accessible.Button
                            // A screen reader's press is the click (#75); disabled, it does
                            // nothing, as the pointer. A MouseArea's own `enabled` is its flag
                            // alone, so the label's says whether the sheet is disabled.
                            Accessible.onPressAction: if (escLabel.enabled)
                                                          root.pressEscape()
                            anchors.fill: parent
                            anchors.margins: -Theme.space(4)
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            objectName: "sheetEsc"

                            onClicked: root.pressEscape()
                        }
                    }
                }
            }

            Hairline {
                Layout.fillWidth: true
                objectName: "sheetHeadRule"
                visible: headBox.visible
            }

            // docs/35 N7: the sheet's notice, edge to edge under the head.
            Loader {
                id: headBandLoader

                readonly property Item band: headBandLoader.item as Item

                Layout.fillWidth: true
                Layout.preferredHeight: headBandLoader.band ? headBandLoader.band.height : 0
                active: root.headBand !== null
                objectName: "sheetHeadBand"
                sourceComponent: root.headBand
                visible: active && headBandLoader.band !== null && headBandLoader.band.height > 0
            }

            // the caller's rows (board 46 `.shb`) ------------------------------------
            // The Flickable clips, so it reaches `Theme.focusExtent` past the rows on every
            // side and insets them by as much: a row's FocusRing (review Q6) draws outside its
            // control, and a control flush with the edge would lose it. The rows stay where
            // they were.
            Flickable {
                id: bodyScroller

                Layout.bottomMargin: Theme.sheetBodyPadding - Theme.focusExtent
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.leftMargin: Theme.sheetInset - Theme.focusExtent
                Layout.maximumHeight: bodyColumn.implicitHeight + 2 * Theme.focusExtent
                Layout.preferredHeight: bodyColumn.implicitHeight + 2 * Theme.focusExtent
                Layout.rightMargin: Theme.sheetInset - Theme.focusExtent
                Layout.topMargin: Theme.sheetBodyPadding - Theme.focusExtent
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                contentHeight: bodyColumn.implicitHeight + 2 * Theme.focusExtent
                contentWidth: width
                objectName: "sheetBody"
                visible: bodyColumn.children.length > 0

                ColumnLayout {
                    id: bodyColumn

                    spacing: Theme.sheetRowGap
                    // Clear of the scroll bar while there is one.
                    width: bodyScroller.width - 2 * Theme.focusExtent
                           - (bodyScrollBar.needed ? bodyScrollBar.width + Theme.space(4) : 0)
                    x: Theme.focusExtent
                    y: Theme.focusExtent
                }

                // More body than room: the bar says so. Parented to the Flickable itself, not
                // its content, so it stays put while the rows scroll (the help sheet's).
                ScrollBar {
                    id: bodyScrollBar

                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.top: parent.top
                    flickable: bodyScroller
                    objectName: "sheetBodyScrollBar"
                    parent: bodyScroller
                    vertical: true
                }
            }

            Hairline {
                Layout.fillWidth: true
                objectName: "sheetFootRule"
                visible: footBox.visible
            }

            // the foot (board 46 `.shf`): the note, then the buttons -----------------
            Item {
                id: footBox

                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(actionsRow.implicitHeight, noteLabel.implicitHeight)
                                        + 2 * Theme.sheetFootPadding
                visible: root.actions.length > 0 || root.note.length > 0

                RowLayout {
                    id: footRow

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.sheetInset
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.sheetInset
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.sheetHeadGap

                    Label {
                        id: noteLabel

                        Accessible.name: text
                        Accessible.role: Accessible.StaticText
                        Layout.fillWidth: true
                        dim: true
                        elide: Text.ElideRight
                        font.pixelSize: Theme.sheetNoteSize
                        maximumLineCount: Theme.sheetNoteLines
                        objectName: "sheetNote"
                        text: root.note
                        // Kept in the row while empty: it is the room that pushes the buttons
                        // to the right. Buttons that need the whole foot leave it none.
                        visible: actionsRow.width < footRow.width
                        wrapMode: Text.WordWrap
                    }

                    // One line at the right while the buttons fit; on a window too narrow for
                    // them they wrap onto a second line rather than leave the panel (the mixed
                    // free sheet's four at the window's narrowest, #169).
                    Flow {
                        id: actionsRow

                        /// The buttons' widths and the gaps between them on one line.
                        readonly property real oneLine: {
                            let width = 0;
                            for (let i = 0; i < buttonRepeater.count; ++i) {
                                const button = buttonRepeater.itemAt(i);
                                if (button)
                                    width += button.width;
                            }
                            return width + Math.max(0, buttonRepeater.count - 1) * Theme.sheetButtonGap;
                        }

                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredHeight: actionsRow.implicitHeight
                        Layout.preferredWidth: Math.min(actionsRow.oneLine, footRow.width)
                        objectName: "sheetActions"
                        spacing: Theme.sheetButtonGap
                        visible: root.actions.length > 0

                        // One button per place, not per action: a sheet that relabels or dims
                        // its actions (the server sheet's `save` → `save anyway`, #68) keeps
                        // the same buttons, so the keyboard stays on the one it was on. Only a
                        // change in the number of actions builds the row again.
                        Repeater {
                            id: buttonRepeater

                            model: root.actions.length

                            Button {
                                id: actionButton

                                required property int index
                                readonly property var modelData: root.actions[actionButton.index] ?? ({})

                                // Spec §3 draws every sheet action as `.btn b`: a hairline
                                // outline at idle. §8's "idle buttons have no chrome" is about
                                // the hero's bare glyph buttons.
                                bordered: true
                                danger: actionButton.modelData.danger === true
                                enabled: actionButton.modelData.disabled !== true
                                filled: Theme.sheetPrimaryFilled
                                primary: actionButton.modelData.primary === true
                                sidePadding: Theme.sheetButtonPadding
                                text: actionButton.modelData.text
                                // As wide as its words (board 46 `.btn`), never under the
                                // least a target needs, nor over the foot.
                                width: Math.min(footRow.width, Math.max(Theme.sheetButtonMinWidth,
                                                                        actionButton.implicitWidth))

                                onClicked: root._trigger(actionButton.modelData)
                            }
                        }
                    }
                }
            }
        }

        // The panel's own ground (its padding, the head, the gaps around the rows and the
        // buttons, the note) takes the clicks nothing on it wanted. Without it they fell
        // through to the Scrim underneath, and a click inside the sheet closed it.
        MouseArea {
            // Last, at z -1: under the rows, and `children[0]` is still the layout.
            acceptedButtons: Qt.AllButtons
            anchors.fill: parent
            z: -1

            onWheel: wheel => wheel.accepted = true
        }
    }
}
