pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The action bar (board 51 D17, DECISIONS C32; supersedes C24 H8's status line turned action
// bar, and brings back docs/17's floating bar, rebuilt): while one or more torrents are
// selected it floats over the list, bottom middle, so the mouse has what `enter` gives the
// keyboard. The status line under the list stays the server's (D17.3 a). Board 51 draws it:
//
//   3 selected  │ stop  recheck │ move…  category ›  tags › │ limits…  more › │ delete… │ esc
//   81 KB · ratio 2.1
//
// Its buttons are the torrent menu's own rows (`groups`, ActionsMenu's: run, place, transfer,
// reach, delete), in the menu's groups with a rule between two, each with the menu's icon and
// word (D17.1 a). `p`'s row follows the selection (`startable`, `stoppable`): "stop" when any
// is running, "start" when any is stopped, both for a mix (H8). "category ›" and "tags ›" ask
// the host for their lists, which open upward over the button; "more ›" asks for the menu's
// other rows (reannounce, rename, transfer ›, queue ›, open folder, copy ›), so nothing in the
// menu is missing from the bar. The count and the selection's ledger lead (#24 T1, as board
// 51 draws it: the size and the ratio; what was given is in its description and in the seed
// console a click on it opens); "esc" clears the selection (#51 D2).
//
// Mouse only (docs/17 M9.4): no button takes the focus, a click leaves the keyboard on the
// list. It never wraps (M9.5): as the list narrows, the ledger goes first, then the words
// (D17.2 a: the icons stay, each word in its tooltip), then the count's "selected", and last
// limits, tags, category and recheck fold into more one by one (H8's narrow order), leaving
// stop, move…, more and delete. C18 D6's mouse hint, shown the first three times only, stays
// while it fits: the ledger, then the words give way to it, and it goes before the icons do. While alt is held each button shows
// its key over its corner (C24 H10).
//
// It runs nothing itself: `triggered(action)` names a row's action (the menu's names, with
// "start" and "stop" for `p`), `menuRequested(action, button, folded)` asks for category's,
// tags' or more's list over `button`, `folded` being the rows it no longer shows, for more's
// list to lead with. Its form is `Theme.actionBar*` tokens.
//
//     SelectionBar { count: table.selectedCount; groups: actionsMenu.groups
//                    availableWidth: list.width - 2 * Theme.space(12) }
//
// Hand-formatted to qmlformat's own style (typed functions, KIT-19).
Item {
    id: root

    /// How many torrents are selected; the host hides the bar at 0.
    property int count: 0
    /// The widest the whole bar may be; it folds past it.
    property real availableWidth: 0
    /// Some of the selection is stopped (`start` is offered), some is running (`stop`).
    property bool startable: false
    property bool stoppable: true
    /// #24 T1: the selection's ledger, `{sizeText, givenText, ratioText, ratioGood}`, under
    /// the count while the bar has room for it.
    property var summary: ({})
    /// DECISIONS C18 D6 (board 45 M1): a quiet line after the buttons, the first 3 times the
    /// bar opens after a plain click ("ctrl+click add · shift+click range"). It goes first.
    property string hint: ""
    /// DECISIONS C24 H10: alt is held: each button shows its key over its corner.
    property bool keysShown: false
    /// The torrent menu's groups (ActionsMenu `groups`): `[{name, items}]`.
    property var groups: []
    /// The button whose list is open over it ("category", "tags", "more"): lit as the menu's
    /// open row is (board 51 `.ab2.open`).
    property string openAction: ""

    /// A button ran: "start", "stop", "recheck", "location", "limits" or "delete".
    signal triggered(string action)
    /// Category, tags or more: open its list over `button`; `folded` leads more's.
    signal menuRequested(string action, Item button, var folded)
    /// #24 T1: the ledger was clicked: the seed console, on the selection.
    signal summaryClicked
    /// "esc": clear the selection (#51 D2).
    signal clearRequested

    /// The menu's rows the bar carries (board 51 `ACT`), `p`'s as "stopStart".
    readonly property var barActions: ["stopStart", "recheck", "location", "category", "tags", "limits",
                                       "delete"]
    /// The buttons that open a list over themselves rather than run.
    readonly property var listActions: ["category", "tags", "more"]
    /// M9.5, H8's narrow order: the first to fold into more, one at a time.
    readonly property var foldOrder: ["limits", "tags", "category", "recheck"]

    /// A row of the menu as a button of the bar: its word (category's "…" gives way to the
    /// list's ›), its icon, its key.
    function _entry(item: var, action: string): var {
        const list = root.listActions.indexOf(action) >= 0;
        const word = list ? String(item.text).replace(/…$/, "") : String(item.text);
        return {
            "action": action,
            "text": word,
            "glyph": item.glyph ?? "",
            "key": item.key ?? "",
            "list": list,
            "danger": item.danger === true
        };
    }

    /// Every button the bar can show, in the menu's groups: `[[entry]]`. More ends the
    /// transfer group (board 51: "limits… more ›").
    readonly property var _all: {
        const out = [];
        let more = false;
        const moreEntry = root._entry({
                                          "text": qsTr("more"),
                                          "glyph": Glyphs.more
                                      }, "more");
        for (const group of root.groups) {
            const entries = [];
            if (group.name === "delete" && !more) {
                out.push([moreEntry]);
                more = true;
            }
            for (const item of group.items) {
                if (item.action === "stopStart") {
                    if (root.startable)
                        entries.push(root._entry({
                                                     "text": qsTr("start"),
                                                     "glyph": Glyphs.start,
                                                     "key": "p"
                                                 }, "start"));
                    if (root.stoppable || !root.startable)
                        entries.push(root._entry({
                                                     "text": qsTr("stop"),
                                                     "glyph": Glyphs.stop,
                                                     "key": "p"
                                                 }, "stop"));
                } else if (root.barActions.indexOf(item.action) >= 0) {
                    entries.push(root._entry(item, item.action));
                }
            }
            if (group.name === "transfer") {
                entries.push(moreEntry);
                more = true;
            }
            if (entries.length > 0)
                out.push(entries);
        }
        if (!more)
            out.push([moreEntry]);
        return out;
    }

    /// The groups once `folds` of `foldOrder` have gone; a group left empty goes.
    function groupsFor(folds: int): var {
        const gone = root.foldOrder.slice(0, folds);
        return root._all.map(entries => entries.filter(entry => gone.indexOf(entry.action) < 0)).filter(
                    entries => entries.length > 0);
    }

    /// A button's width: with its word, its icon, the word and the ›; without, a square.
    function buttonWidth(entry: var, words: bool): real {
        if (!words)
            return Theme.actionBarButtonSize;
        const text = Theme.controlUpper ? entry.text.toUpperCase() : entry.text;
        return Math.ceil(wordMetrics.advanceWidth(text) + (entry.glyph.length > 0 ? iconProbe.implicitWidth
                                                                                     + Theme.space(6) : 0)
                         + (entry.list ? chevronProbe.implicitWidth + Theme.space(6) : 0))
                + 2 * Theme.actionBarWordPadding;
    }

    readonly property real _ruleWidth: Theme.hairWidth + 2 * Theme.actionBarRuleInset

    /// The count's block: its number and word over the ledger, or the number alone.
    function countWidth(word: bool, ledger: bool): real {
        const line = countNumber.implicitWidth + (word ? countRow.spacing + countWord.implicitWidth : 0);
        return Math.ceil(Math.max(line, ledger ? root._ledgerWidth : 0)) + Theme.actionBarCountStart
                + Theme.actionBarCountEnd;
    }

    /// The whole bar's width at a step of `fit`: frame, count, groups, hint, esc.
    function widthOf(step: var): real {
        const gap = Theme.actionBarGap;
        let width = root.countWidth(step.word, step.ledger);
        for (const entries of root.groupsFor(step.folds)) {
            width += gap + root._ruleWidth;
            for (const entry of entries)
                width += gap + root.buttonWidth(entry, step.words);
        }
        if (step.hint)
            width += gap + root._hintWidth;
        width += gap + root._ruleWidth + gap + root._escWidth;
        return width + 2 * (Theme.actionBarPadding + Theme.edgeWidth);
    }

    readonly property bool _hasLedger: root.summary.sizeText !== undefined
    readonly property bool _hasHint: root.hint.length > 0

    /// What the bar draws at `availableWidth`: the ledger goes first, then the words, the
    /// count's word, then the folds one by one; the hint is kept while it fits beside them.
    readonly property var fit: {
        const steps = [];
        const step = (folds, words, word, ledger, hint) => ({
                                                                 "folds": folds,
                                                                 "words": words,
                                                                 "word": word,
                                                                 "ledger": ledger,
                                                                 "hint": hint
                                                             });
        // C18 D6's hint shows the first three times only, so it is kept while it fits at all:
        // the ledger gives way to it, then the words.
        if (root._hasHint) {
            steps.push(step(0, true, true, root._hasLedger, true));
            steps.push(step(0, true, true, false, true));
            steps.push(step(0, false, true, false, true));
        }
        if (root._hasLedger)
            steps.push(step(0, true, true, true, false));
        steps.push(step(0, true, true, false, false));
        steps.push(step(0, false, true, false, false));
        for (let folds = 0; folds <= root.foldOrder.length; ++folds)
            steps.push(step(folds, false, false, false, false));
        for (const candidate of steps) {
            if (root.widthOf(candidate) <= root.availableWidth)
                return candidate;
        }
        return steps[steps.length - 1];
    }
    readonly property bool showSummary: root.fit.ledger
    readonly property bool showHint: root.fit.hint
    readonly property bool showWords: root.fit.words
    /// The rows more now leads with, in `foldOrder`.
    readonly property var folded: root.foldOrder.slice(0, root.fit.folds)
    readonly property var _groups: root.groupsFor(root.fit.folds)
    /// The buttons drawn now, more aside, in order ("stop", "recheck", "location", …).
    readonly property var items: {
        const out = [];
        for (const entries of root._groups) {
            for (const entry of entries) {
                if (entry.action !== "more")
                    out.push(entry.action);
            }
        }
        return out;
    }
    /// The menu's rows the bar shows now (`p`'s as "stopStart"): what more's list leaves out.
    readonly property var menuActionsShown: {
        const out = [];
        for (const action of root.items) {
            const name = action === "start" || action === "stop" ? "stopStart" : action;
            if (out.indexOf(name) < 0)
                out.push(name);
        }
        return out;
    }
    /// The parts between the count and the hint: a rule before each group, then its buttons.
    readonly property var _parts: {
        const out = [];
        for (const entries of root._groups) {
            out.push({
                         "rule": true
                     });
            for (const entry of entries)
                out.push(entry);
        }
        return out;
    }

    // From its texts, not the row: a row hidden with the ledger lays nothing out, and a width
    // that went with it would bring the ledger back (a binding loop).
    readonly property real _ledgerWidth: summarySize.implicitWidth + dotOne.implicitWidth
                                         + ratioWord.implicitWidth + ratioValue.implicitWidth
                                         + 3 * summaryRow.spacing
    readonly property real _hintWidth: hintLabel.implicitWidth + 2 * Theme.space(6)
    readonly property real _escWidth: Math.ceil(wordMetrics.advanceWidth(Theme.controlUpper ? qsTr("esc").toUpperCase()
                                                                                            : qsTr("esc")))
                                      + 2 * Theme.actionBarWordPadding

    /// docs/DECISIONS.md C30 F1: every word on the bar is on its ground; its buttons rest on it.
    readonly property Ink ink: Ink {
        animated: false
        ground: Theme.actionBarGround
    }

    implicitHeight: Theme.actionBarHeight
    implicitWidth: root.widthOf(root.fit)

    FontMetrics {
        id: wordMetrics

        // A word button's own type: the skin's control voice (Kit/Button).
        font.family: Theme.labelFamily
        font.letterSpacing: Theme.actionBarFontSize * Theme.controlTrack
        font.pixelSize: Theme.actionBarFontSize
        font.weight: Theme.controlWeight
    }

    // The sizes of a button's icon and of the ›, as Kit/Button draws them.
    Glyph {
        id: iconProbe

        pixelSize: Theme.font.icon
        text: Glyphs.stop
        visible: false
    }

    Glyph {
        id: chevronProbe

        pixelSize: Theme.font.icon
        text: Glyphs.chevronRight
        visible: false
    }

    // Its ground and edge, the outcome strip's (they float together, C18 D1): Omarchy's
    // rectangle on the window's ground, Console's cut plate on the panel (#171).
    Plate {
        anchors.fill: parent
        base: Theme.platedBlocks ? Theme.background : "transparent"
        edgeColor: Theme.fill(Theme.borderIdle)
        edgeWidth: Theme.edgeWidth
        fill: Theme.actionBarGround.fill
        objectName: "actionBarPlate"
        radius: Theme.cornerRadius
    }

    // It floats over the rows: a click on its own ground selects nothing and goes no further.
    // a11y-ignore A2: the bar's ground, not a control
    MouseArea {
        acceptedButtons: Qt.AllButtons
        anchors.fill: parent
    }

    component Rule: Item {
        height: Theme.actionBarButtonSize
        width: root._ruleWidth

        Hairline {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            height: Theme.actionBarButtonSize - 2 * Theme.space(3)
            vertical: true
        }
    }

    component BarButton: Button {
        id: button

        /// The entry it draws (`_entry`'s shape).
        property var entry: ({})
        readonly property string key: button.entry.key ?? ""

        // #78: named by its word ("delete…"), its key the description; an icon alone too.
        Accessible.description: button.key.length > 0 ? qsTr("shortcut %1").arg(button.key) : ""
        Accessible.name: button.entry.text ?? ""
        activeFocusOnTab: false
        block: Theme.actionBarGround
        danger: button.entry.danger === true
        fontSize: Theme.actionBarFontSize
        glyph: button.entry.glyph ?? ""
        // Dim beside its word (board 51 `.ab2 .ic`); an icon alone is the word, in its colour.
        glyphDim: root.showWords
        height: Theme.actionBarButtonSize
        objectName: "bar_" + (button.entry.action ?? "")
        selected: root.openAction.length > 0 && root.openAction === button.entry.action
        sidePadding: root.showWords ? Theme.actionBarWordPadding : 0
        text: root.showWords ? (button.entry.text ?? "") : ""
        // D17.2 a: the word stays in the tooltip when the bar draws the icon alone.
        tooltip: button.key.length > 0 ? qsTr("%1 · %2").arg(button.entry.text).arg(button.key)
                                       : (button.entry.text ?? "")
        tooltipAbove: true
        trailingGlyph: root.showWords && button.entry.list === true ? Glyphs.chevronRight : ""
        width: root.buttonWidth(button.entry, root.showWords)

        onClicked: {
            if (button.entry.list === true)
                root.menuRequested(button.entry.action, button, root.folded);
            else
                root.triggered(button.entry.action);
        }

        KeyBadge {
            key: button.key
            objectName: "barKey_" + (button.entry.action ?? "")
            shown: root.keysShown
        }
    }

    Row {
        id: content

        anchors.left: parent.left
        anchors.leftMargin: Theme.actionBarPadding + Theme.edgeWidth
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.actionBarGap

        // board 51 `.abar .cnt`: the count, bright, over the ledger, dim and small.
        Item {
            id: countBox

            height: Theme.actionBarButtonSize
            width: root.countWidth(root.fit.word, root.showSummary)

            Column {
                anchors.left: parent.left
                anchors.leftMargin: Theme.actionBarCountStart
                anchors.verticalCenter: parent.verticalCenter

                Row {
                    id: countRow

                    spacing: Theme.space(4)

                    Label {
                        id: countNumber

                        // #79: "2 selected" in words, even when the bar is too narrow for it.
                        Accessible.name: qsTr("%1 selected").arg(root.count)
                        Accessible.role: Accessible.StaticText
                        bright: true
                        font.features: {
                            "tnum": 1
                        }
                        font.weight: Font.DemiBold
                        ink: root.ink
                        objectName: "selectionCount"
                        size: "small"
                        text: String(root.count)
                    }

                    Label {
                        id: countWord

                        bright: true
                        font.weight: Font.DemiBold
                        ink: root.ink
                        size: "small"
                        text: qsTr("selected")
                        visible: root.fit.word
                    }
                }

                // T1: the size and ratio of what is selected, given in its description; a
                // click opens the console on it.
                Item {
                    id: summaryBox

                    // #77: a button to the seed console on the selection, the figures its
                    // description; its press is the click.
                    Accessible.description: [root.summary.sizeText ?? "",
                                             qsTr("given %1").arg(root.summary.givenText ?? ""),
                                             qsTr("ratio %1").arg(root.summary.ratioText ?? "")].join(" · ")
                    Accessible.name: qsTr("seed console")
                    Accessible.role: Accessible.Button
                    Accessible.onPressAction: if (summaryBox.enabled)
                                                  root.summaryClicked()
                    height: summaryRow.implicitHeight
                    objectName: "bar_summary"
                    visible: root.showSummary
                    width: root._ledgerWidth

                    Row {
                        id: summaryRow

                        spacing: Theme.space(4)

                        Label {
                            id: summarySize

                            dim: true
                            ink: root.ink
                            size: "caption"
                            text: root.summary.sizeText ?? ""
                        }

                        Label {
                            id: dotOne

                            dim: true
                            ink: root.ink
                            size: "caption"
                            text: "·"
                        }

                        Label {
                            id: ratioWord

                            dim: true
                            ink: root.ink
                            size: "caption"
                            text: qsTr("ratio")
                        }

                        Label {
                            id: ratioValue

                            color: root.summary.ratioGood === true ? root.ink.stateSeeding
                                                                   : root.ink.dimForeground
                            size: "caption"
                            text: root.summary.ratioText ?? ""
                        }
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: root.summaryClicked()
                    }
                }
            }
        }

        Repeater {
            model: root._parts

            Item {
                id: part

                required property var modelData

                height: Theme.actionBarButtonSize
                width: part.modelData.rule === true ? root._ruleWidth : root.buttonWidth(part.modelData,
                                                                                          root.showWords)

                Rule {
                    visible: part.modelData.rule === true
                }

                BarButton {
                    entry: part.modelData
                    visible: part.modelData.rule !== true
                }
            }
        }

        // C18 D6: the mouse's ways to pick several, dim and small, after the last button.
        Item {
            height: Theme.actionBarButtonSize
            visible: root.showHint
            width: root._hintWidth

            Label {
                id: hintLabel

                // #77: read out with the bar, as the count is.
                Accessible.name: hintLabel.text
                Accessible.role: Accessible.StaticText
                anchors.centerIn: parent
                dim: true
                ink: root.ink
                objectName: "bar_hint"
                size: "caption"
                text: root.hint
            }
        }

        Rule {}

        Button {
            id: escButton

            // #51 D2: what `esc` does, by click; the word is its key.
            Accessible.name: qsTr("clear selection")
            activeFocusOnTab: false
            block: Theme.actionBarGround
            fontSize: Theme.actionBarFontSize
            height: Theme.actionBarButtonSize
            objectName: "bar_clear"
            sidePadding: Theme.actionBarWordPadding
            text: qsTr("esc")
            tooltip: qsTr("clear selection · esc")
            tooltipAbove: true
            width: root._escWidth

            onClicked: root.clearRequested()

            // Its word is its key: no badge (board 46 badges none).
            KeyBadge {
                key: ""
                objectName: "barKey_clear"
                shown: false
            }
        }
    }
}
