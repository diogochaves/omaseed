pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The action bar (board 46 `statusLine()` while torrents are selected, DECISIONS C24 H8;
// supersedes docs/17's floating bar): while one or more torrents are selected the status
// line becomes this bar, docked in its place across the content's foot, so the mouse has
// what `enter` gives the keyboard. Board 46 draws it, and the sheets-and-menus step (#170)
// built it so:
//
//   "3 selected" · the ledger (#24 T1) · stop · recheck · move… · category · tags · limits ·
//   delete (red) · C18 D6's mouse hint ......................... all actions · esc
//
// Words, not glyphs: each button is its word, its key in its tooltip and, while alt is held,
// in a badge over its corner (C24 H10). `p`'s word follows the selection (`startable`,
// `stoppable`): "stop" when any is running, "start" when any is stopped, both for a mix.
// "all actions" is the actions menu (as `enter`), "esc" clears the selection (#51 D2).
//
// Mouse only (docs/17 M9.4): no button takes the focus (tab never lands here, a click leaves
// the keyboard on the table). It never wraps (M9.5): the hint goes first, then the ledger,
// then the middle buttons fold into all actions from the right (limits, tags, category,
// recheck: board 46's narrow bar keeps stop, move… and delete), and last of all the count
// drops its word.
//
// The status line's note (a "copied", a failure) takes the hint's place while the bar shows,
// so a key pressed on a selection still says what it did.
//
// It runs nothing itself: `triggered(action)` names the action (the actions menu's names,
// plus "start" and "stop"), and all actions asks the host for the menu with
// `moreRequested(button, folded)`, `folded` being the actions it no longer shows, for the
// menu to list first. Its form is `Theme.actionBar*` tokens (frame plan, "form in tokens").
//
//     SelectionBar { count: table.selectedCount; anchors.fill: statusLine
//                    availableWidth: width - Theme.actionBarInsetStart - Theme.actionBarInsetEnd }
//
// Hand-formatted to qmlformat's own style (typed functions, KIT-19).
Rectangle {
    id: root

    /// How many torrents are selected; the host hides the bar at 0.
    property int count: 0
    /// The widest its contents may be, insets aside; buttons fold into all actions past it.
    property real availableWidth: 0
    /// Some of the selection is stopped (`start` is offered), some is running (`stop`).
    property bool startable: false
    property bool stoppable: true
    /// #24 T1: the selection's ledger, `{sizeText, givenText, ratioText, ratioGood}`, drawn
    /// after the count while the bar has room for it (it goes before any button folds).
    property var summary: ({})
    /// DECISIONS C18 D6 (board 45 M1): a quiet line after the buttons, the first 3 times the
    /// bar opens after a plain click ("ctrl+click add · shift+click range"). It goes first
    /// when the bar has no room, before the summary and any button.
    property string hint: ""
    /// The status line's note while the bar stands in for it ("copied", a failure); it takes
    /// the hint's place, and goes as the hint does when there is no room.
    property string note: ""
    /// The note is a failure qBittorrent's log told of: yellow, as in the status line.
    property bool noteWarn: false
    /// DECISIONS C24 H10: alt is held: each word shows its key over its corner (board 46
    /// `.ab .kb`), outside the row, so no fold moves.
    property bool keysShown: false

    /// A button ran: "start", "stop", "recheck", "category", "tags", "location", "limits" or
    /// "delete".
    signal triggered(string action)
    /// All actions ran: open the actions menu over `button`, with `folded` first.
    signal moreRequested(Item button, var folded)
    /// #24 T1: the summary was clicked: the seed console, on the selection.
    signal summaryClicked
    /// "esc": clear the selection (#51 D2).
    signal clearRequested

    /// Every button, keyed by the actions menu's action names; `key` is what the tooltip and
    /// the badge teach (the actions menu's keys).
    readonly property var buttons: ({
                                        "start": {
                                            "text": qsTr("start"),
                                            "key": "p"
                                        },
                                        "stop": {
                                            "text": qsTr("stop"),
                                            "key": "p"
                                        },
                                        "recheck": {
                                            "text": qsTr("recheck"),
                                            "key": "c"
                                        },
                                        "location": {
                                            "text": qsTr("move…"),
                                            "key": "m"
                                        },
                                        "category": {
                                            "text": qsTr("category"),
                                            "key": "C"
                                        },
                                        "tags": {
                                            "text": qsTr("tags"),
                                            "key": "t"
                                        },
                                        "limits": {
                                            "text": qsTr("limits"),
                                            "key": "L"
                                        },
                                        "delete": {
                                            "text": qsTr("delete"),
                                            "key": "d"
                                        },
                                        "more": {
                                            "text": qsTr("all actions"),
                                            "key": "enter"
                                        },
                                        "clear": {
                                            "text": qsTr("esc"),
                                            "key": "esc"
                                        }
                                    })
    /// The middle group in board 46's order, `p`'s word as the selection needs it.
    readonly property var middle: (root.startable ? ["start"] : []).concat(root.stoppable || !root.startable
                                                                            ? ["stop"] : [],
                                                                            ["recheck", "location",
                                                                             "category", "tags",
                                                                             "limits", "delete"])
    /// M9.5: the first to go when the bar does not fit, one at a time.
    readonly property var foldOrder: ["limits", "tags", "category", "recheck"]

    /// A word button's width: its word, measured, and the room either side.
    function wordWidth(action: string): real {
        const text = root.buttons[action].text;
        return Math.ceil(wordMetrics.advanceWidth(Theme.controlUpper ? text.toUpperCase() : text))
                + 2 * Theme.actionBarWordPadding;
    }

    /// The middle group once `folds` of `foldOrder` have gone.
    function itemsFor(folds: int): var {
        const gone = root.foldOrder.slice(0, folds);
        return root.middle.filter(action => gone.indexOf(action) < 0);
    }

    /// The count's width, with or without its word. Worked out from the parts, not read from
    /// the row: the row hides the word by `fit`, which is itself worked out from this.
    function countWidth(word: bool): real {
        return countNumber.implicitWidth + (word ? countRow.spacing + countWord.implicitWidth : 0)
                + countRow.rightPadding;
    }

    /// The contents' width with `folds` buttons folded, the count's word or not, the ledger
    /// and the hint or not: what `availableWidth` bounds.
    function widthOf(folds: int, word: bool, summary: bool, hint: bool): real {
        let width = root.countWidth(word);
        if (summary)
            width += root._spacing + root._summaryWidth;
        for (const action of root.itemsFor(folds))
            width += root._spacing + root.wordWidth(action);
        if (hint)
            width += root._spacing + root._hintWidth;
        return width + Theme.actionBarEndGap + root.wordWidth("more") + root._spacing + root.wordWidth("clear");
    }

    readonly property bool _hasSummary: root.summary.sizeText !== undefined
    /// What follows the buttons: the note, else the hint.
    readonly property string tail: root.note.length > 0 ? root.note : root.hint
    readonly property bool _hasTail: root.tail.length > 0

    /// How many of `foldOrder` are folded, whether the count keeps its word, and whether the
    /// ledger and the hint show, at `availableWidth`: the hint goes first, then the ledger,
    /// then the buttons one by one, then the count's word.
    readonly property var fit: {
        const steps = [];
        steps.push({
                       "folds": 0,
                       "word": true,
                       "summary": root._hasSummary,
                       "hint": root._hasTail
                   });
        if (root._hasTail)
            steps.push({
                           "folds": 0,
                           "word": true,
                           "summary": root._hasSummary,
                           "hint": false
                       });
        if (root._hasSummary)
            steps.push({
                           "folds": 0,
                           "word": true,
                           "summary": false,
                           "hint": false
                       });
        for (let folds = 1; folds <= root.foldOrder.length; ++folds)
            steps.push({
                           "folds": folds,
                           "word": true,
                           "summary": false,
                           "hint": false
                       });
        steps.push({
                       "folds": root.foldOrder.length,
                       "word": false,
                       "summary": false,
                       "hint": false
                   });
        for (const step of steps) {
            if (root.widthOf(step.folds, step.word, step.summary, step.hint) <= root.availableWidth)
                return step;
        }
        return steps[steps.length - 1];
    }
    readonly property bool showSummary: root.fit.summary
    readonly property bool showHint: root.fit.hint
    /// The actions all actions now holds besides the menu's own, in `foldOrder`.
    readonly property var folded: root.foldOrder.slice(0, root.fit.folds)
    /// The middle group drawn now.
    readonly property var items: root.itemsFor(root.fit.folds)

    // From its texts, not the row: a row hidden with the summary lays nothing out, and a
    // width that went with it would bring the summary back (a binding loop).
    readonly property real _summaryWidth: summarySize.implicitWidth + givenWord.implicitWidth
                                          + givenValue.implicitWidth + ratioWord.implicitWidth
                                          + ratioValue.implicitWidth + 2 * Theme.space(12)
                                          + 2 * Theme.space(5) + Theme.space(4)
    readonly property real _hintWidth: hintLabel.implicitWidth + Theme.space(8)
    readonly property int _spacing: Theme.actionBarGap

    /// The contents drawn now, insets aside.
    readonly property real contentWidth: root.widthOf(root.fit.folds, root.fit.word, root.fit.summary,
                                                      root.fit.hint)

    /// docs/DECISIONS.md C30 F1: every word on the bar is on its ground; its buttons rest on it.
    readonly property Ink ink: Ink {
        animated: false
        ground: Theme.actionBarGround
    }

    border.width: 0
    color: Theme.actionBarGround.fill
    implicitHeight: Theme.actionBarHeight
    implicitWidth: Theme.actionBarInsetStart + root.contentWidth + Theme.actionBarInsetEnd

    FontMetrics {
        id: wordMetrics

        // A word button's own type: the skin's control voice (Kit/Button).
        font.family: Theme.labelFamily
        font.letterSpacing: Theme.actionBarFontSize * Theme.controlTrack
        font.pixelSize: Theme.actionBarFontSize
        font.weight: Theme.controlWeight
    }

    // Its ground is no row: a click there selects nothing and goes no further.
    MouseArea {
        acceptedButtons: Qt.AllButtons
        anchors.fill: parent
    }

    // The status line's rule, kept: the bar is that line, acting.
    Hairline {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    component WordButton: Button {
        id: word

        /// The action it names, a key of `root.buttons`.
        property string action: ""
        readonly property var spec: root.buttons[word.action] ?? ({
                                                                     "text": "",
                                                                     "key": ""
                                                                 })
        /// Draw its key's badge while alt is held.
        property bool badged: true

        // #78: named by its action ("delete"), the key its description.
        Accessible.description: qsTr("shortcut %1").arg(word.spec.key)
        activeFocusOnTab: false
        block: Theme.actionBarGround
        fontSize: Theme.actionBarFontSize
        height: Theme.actionBarButtonSize
        objectName: "bar_" + word.action
        sidePadding: Theme.actionBarWordPadding
        text: word.spec.text
        tooltip: qsTr("%1 · %2").arg(word.spec.text).arg(word.spec.key)
        tooltipAbove: true
        width: root.wordWidth(word.action)

        KeyBadge {
            key: word.badged ? word.spec.key : ""
            objectName: "barKey_" + word.action
            shown: root.keysShown
        }
    }

    Row {
        id: content

        anchors.left: parent.left
        anchors.leftMargin: Theme.actionBarInsetStart
        anchors.verticalCenter: parent.verticalCenter
        spacing: root._spacing

        // board 46 `.status.sel b`: the count, bright and tabular, and its word.
        Item {
            height: Theme.actionBarButtonSize
            width: root.countWidth(root.fit.word)

            Row {
                id: countRow

                anchors.verticalCenter: parent.verticalCenter
                rightPadding: Theme.space(4)
                spacing: Theme.space(5)

                Label {
                    id: countNumber

                    // #79: "2 selected" in words, even when the bar is too narrow for "selected".
                    Accessible.name: qsTr("%1 selected").arg(root.count)
                    Accessible.role: Accessible.StaticText
                    anchors.baseline: countWord.baseline
                    bright: true
                    font.features: {
                        "tnum": 1
                    }
                    ink: root.ink
                    objectName: "selectionCount"
                    text: String(root.count)
                }

                Label {
                    id: countWord

                    dim: true
                    ink: root.ink
                    size: "small"
                    text: qsTr("selected")
                    visible: root.fit.word
                }
            }
        }

        // T1: size, given and ratio of what is selected; a click opens the console on it.
        Item {
            id: summaryBox

            // #77: a button to the seed console on the selection, the figures its
            // description; its press is the click, and does nothing while disabled.
            Accessible.description: [root.summary.sizeText ?? "",
                                     qsTr("given %1").arg(root.summary.givenText ?? ""),
                                     qsTr("ratio %1").arg(root.summary.ratioText ?? "")].join(" · ")
            Accessible.name: qsTr("seed console")
            Accessible.role: Accessible.Button
            Accessible.onPressAction: if (summaryBox.enabled)
                                          root.summaryClicked()
            height: Theme.actionBarButtonSize
            objectName: "bar_summary"
            visible: root.showSummary
            width: root._summaryWidth

            Row {
                id: summaryRow

                anchors.verticalCenter: parent.verticalCenter
                rightPadding: Theme.space(4)
                spacing: Theme.space(12)

                Label {
                    id: summarySize

                    dim: true
                    ink: root.ink
                    size: "small"
                    text: root.summary.sizeText ?? ""
                }

                Row {
                    spacing: Theme.space(5)

                    Label {
                        id: givenWord

                        dim: true
                        ink: root.ink
                        size: "small"
                        text: qsTr("given")
                    }

                    Label {
                        id: givenValue

                        bright: true
                        ink: root.ink
                        size: "small"
                        text: root.summary.givenText ?? ""
                    }
                }

                Row {
                    spacing: Theme.space(5)

                    Label {
                        id: ratioWord

                        dim: true
                        ink: root.ink
                        size: "small"
                        text: qsTr("ratio")
                    }

                    Label {
                        id: ratioValue

                        color: root.summary.ratioGood === true ? root.ink.stateSeeding
                                                               : root.ink.brightForeground
                        size: "small"
                        text: root.summary.ratioText ?? ""
                    }
                }
            }

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: root.summaryClicked()
            }
        }

        Repeater {
            model: root.items

            WordButton {
                required property string modelData

                action: modelData
                danger: modelData === "delete"

                onClicked: root.triggered(modelData)
            }
        }

        // C18 D6 (moved to the bar by C24 H8): the mouse's ways to pick several, dim and
        // small, after the last button; the status line's note in its place while one is up.
        Item {
            height: Theme.actionBarButtonSize
            visible: root.showHint
            width: root._hintWidth

            Label {
                id: hintLabel

                readonly property bool isNote: root.note.length > 0

                // #77: read out with the bar, as the count is; a note comes on its own (#79).
                Accessible.name: hintLabel.text
                Accessible.role: hintLabel.isNote ? Accessible.AlertMessage : Accessible.StaticText
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                color: !hintLabel.isNote ? root.ink.dimForeground : root.noteWarn ? root.ink.yellow
                                                                                   : root.ink.accent
                objectName: hintLabel.isNote ? "bar_note" : "bar_hint"
                size: hintLabel.isNote ? "small" : "caption"
                text: root.tail
            }
        }
    }

    // board 46: "all actions" and "esc" at the right end.
    Row {
        id: end

        anchors.right: parent.right
        anchors.rightMargin: Theme.actionBarInsetEnd
        anchors.verticalCenter: parent.verticalCenter
        spacing: root._spacing

        WordButton {
            id: moreButton

            // Its key is its tooltip's (`enter` opens the same menu); board 46 badges none.
            Accessible.name: qsTr("all actions")
            action: "more"
            badged: false

            onClicked: root.moreRequested(moreButton, root.folded)
        }

        WordButton {
            // #51 D2: what `esc` does, by click; the word is its key.
            Accessible.name: qsTr("clear selection")
            action: "clear"
            badged: false
            tooltip: qsTr("clear selection · esc")

            onClicked: root.clearRequested()
        }
    }
}
