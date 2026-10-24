pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The action bar (board 46 `statusLine()` while torrents are selected, DECISIONS C24 H8;
// supersedes docs/17's floating bar): while one or more torrents are selected the status
// line becomes this bar, docked in its place across the content's foot, so the mouse has
// what `enter` gives the keyboard. Its contents are docs/17 M9's (Diogo took every
// recommendation, 2026-09-29):
//
//   M9.2  the host shows it while one or more torrents are selected (never for the cursor)
//   M9.3  count ✕ · the ledger | start · stop | recheck · reannounce | category · tags ·
//         set location · limits | open folder · ⋯ | delete (red), then C18 D6's mouse hint.
//         ⋯ is the full actions menu. The ✕ right after the count clears the selection, as
//         `esc` does (#51 D2).
//   M9.4  mouse only: no button takes the focus (tab never lands here, a click leaves the
//         keyboard on the table) and each tooltip names the key that does the same
//   M9.5  never wraps: when it is wider than `availableWidth`, buttons fold into ⋯ in
//         `foldOrder`; the count, start, stop, ⋯ and delete always stay, and last of all the
//         count drops its word
//
// The status line's note (a "copied", a failure) takes the hint's place while the bar shows,
// so a key pressed on a selection still says what it did.
//
// It runs nothing itself: `triggered(action)` names the action (the actions menu's names,
// plus "start" and "stop"), and ⋯ asks the host for the menu with `moreRequested(button,
// folded)`, `folded` being the actions it no longer shows, for the menu to list first. Its
// form is `Theme.actionBar*` tokens (frame plan, "form in tokens").
//
//     SelectionBar { count: table.selectedCount; anchors.fill: statusLine
//                    availableWidth: width - Theme.actionBarInsetStart - Theme.actionBarInsetEnd }
//
// Hand-formatted to qmlformat's own style (typed functions, KIT-19).
Rectangle {
    id: root

    /// How many torrents are selected; the host hides the bar at 0.
    property int count: 0
    /// The widest its contents may be, insets aside; buttons fold into ⋯ past it.
    property real availableWidth: 0
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
    /// DECISIONS C24 H10: alt is held: each button shows its key over its corner (board 46
    /// `.ab .kb`), outside the row, so no fold moves.
    property bool keysShown: false

    /// A button ran: "start", "stop", "recheck", "reannounce", "category", "tags",
    /// "location", "limits", "openFolder" or "delete".
    signal triggered(string action)
    /// ⋯ ran: open the actions menu over `button`, with `folded` first.
    signal moreRequested(Item button, var folded)
    /// #24 T1: the summary was clicked: the seed console, on the selection.
    signal summaryClicked
    /// The ✕ after the count: clear the selection (#51 D2).
    signal clearRequested

    /// Every button, keyed by the actions menu's action names; `key` is what the tooltip
    /// teaches (the same keys as Fig. 11's menu).
    readonly property var buttons: ({
                                        "start": {
                                            "glyph": Glyphs.start,
                                            "text": qsTr("start"),
                                            "key": "p"
                                        },
                                        "stop": {
                                            "glyph": Glyphs.stop,
                                            "text": qsTr("stop"),
                                            "key": "p"
                                        },
                                        "recheck": {
                                            "glyph": Glyphs.recheck,
                                            "text": qsTr("recheck"),
                                            "key": "c"
                                        },
                                        "reannounce": {
                                            "glyph": Glyphs.reannounce,
                                            "text": qsTr("reannounce"),
                                            "key": "R"
                                        },
                                        "category": {
                                            "glyph": Glyphs.category,
                                            "text": qsTr("category"),
                                            "key": "C"
                                        },
                                        "tags": {
                                            "glyph": Glyphs.tag,
                                            "text": qsTr("tags"),
                                            "key": "t"
                                        },
                                        "location": {
                                            "glyph": Glyphs.location,
                                            "text": qsTr("move…"),
                                            "key": "m"
                                        },
                                        "limits": {
                                            "glyph": Glyphs.limits,
                                            "text": qsTr("limits"),
                                            "key": "L"
                                        },
                                        "openFolder": {
                                            "glyph": Glyphs.openFolder,
                                            "text": qsTr("open folder"),
                                            "key": "o"
                                        },
                                        "more": {
                                            "glyph": Glyphs.more,
                                            "text": qsTr("all actions"),
                                            "key": "enter"
                                        },
                                        "delete": {
                                            "glyph": Glyphs.trash,
                                            "text": qsTr("delete"),
                                            "key": "d"
                                        }
                                    })
    /// M9.3's groups, a hairline between each two.
    readonly property var groups: [["start", "stop"], ["recheck", "reannounce"], ["category", "tags",
            "location", "limits"], ["openFolder", "more"], ["delete"]]
    /// M9.5: the first to go when the bar does not fit, one at a time.
    readonly property var foldOrder: ["openFolder", "limits", "location", "reannounce", "recheck",
        "tags", "category"]

    /// How many of `foldOrder` are folded, and whether the count keeps its word, at
    /// `availableWidth`.
    readonly property var fit: {
        for (let folds = 0; folds <= root.foldOrder.length; ++folds) {
            if (root.widthOf(root.itemsFor(folds), true) <= root.availableWidth)
                return {
                    "folds": folds,
                    "word": true
                };
        }
        return {
            "folds": root.foldOrder.length,
            "word": false
        };
    }
    /// #24 T1: the summary's own width, and whether it fits beside every button.
    // From its texts, not the row: a row hidden with the summary lays nothing out, and a
    // width that went with it would bring the summary back (a binding loop).
    readonly property real _summaryWidth: summarySize.implicitWidth + givenWord.implicitWidth
                                          + givenValue.implicitWidth + ratioWord.implicitWidth
                                          + ratioValue.implicitWidth + 2 * Theme.space(12)
                                          + 2 * Theme.space(5) + Theme.space(4) + Theme.space(8)
    readonly property bool showSummary: root.summary.sizeText !== undefined && root.fit.folds === 0
                                        && root.fit.word
                                        && root.widthOf(root.items, true) + root._summaryWidth
                                        <= root.availableWidth
    /// What follows the buttons: the note, else the hint.
    readonly property string tail: root.note.length > 0 ? root.note : root.hint
    readonly property real _hintWidth: hintLabel.implicitWidth + Theme.space(16)
    readonly property bool showHint: root.tail.length > 0 && root.fit.folds === 0 && root.fit.word
                                     && root.widthOf(root.items, true) + (root.showSummary ? root._summaryWidth : 0)
                                     + root._hintWidth <= root.availableWidth
    /// The actions ⋯ now holds besides the menu's own, in `foldOrder`.
    readonly property var folded: root.foldOrder.slice(0, root.fit.folds)
    /// What the bar draws after the count: action names, and "|" for a hairline.
    readonly property var items: root.itemsFor(root.fit.folds)

    readonly property int _spacing: Theme.actionBarGap
    readonly property int _ruleWidth: 1 + Theme.actionBarRuleRoom

    /// The buttons and hairlines left once `folds` of `foldOrder` have gone: empty groups
    /// leave no hairline behind.
    function itemsFor(folds: int): var {
        const gone = root.foldOrder.slice(0, folds);
        const out = [];
        for (const group of root.groups) {
            const kept = group.filter(action => gone.indexOf(action) < 0);
            if (kept.length === 0)
                continue;
            if (out.length > 0)
                out.push("|");
            for (const action of kept)
                out.push(action);
        }
        return out;
    }

    /// The count's width, with or without its word. Worked out from the parts, not read from
    /// the row: the row hides the word by `fit`, which is itself worked out from this.
    function countWidth(word: bool): real {
        return countRow.leftPadding + countNumber.implicitWidth + countRow.rightPadding
                + (word ? countRow.spacing + countWord.implicitWidth : 0) + countRow.spacing
                + clearButton.width;
    }

    /// The contents' width drawing `items`, with or without the count's word: what
    /// `availableWidth` bounds.
    function widthOf(items: var, word: bool): real {
        let width = root.countWidth(word) + root._spacing + root._ruleWidth;
        for (const item of items)
            width += root._spacing + (item === "|" ? root._ruleWidth : Theme.actionBarButtonSize);
        return width;
    }

    /// The contents drawn now, insets aside.
    readonly property real contentWidth: root.widthOf(root.items, root.fit.word)
                                         + (root.showSummary ? root._summaryWidth : 0)
                                         + (root.showHint ? root._hintWidth : 0)

    /// docs/DECISIONS.md C30 F1: every word on the bar is on its ground; its buttons rest on it.
    readonly property Ink ink: Ink {
        animated: false
        ground: Theme.actionBarGround
    }

    border.width: 0
    color: Theme.actionBarGround.fill
    implicitHeight: Theme.actionBarHeight
    implicitWidth: Theme.actionBarInsetStart + root.contentWidth + Theme.actionBarInsetEnd

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

    Row {
        id: content

        anchors.left: parent.left
        anchors.leftMargin: Theme.actionBarInsetStart
        anchors.verticalCenter: parent.verticalCenter
        spacing: root._spacing

        // `.sbar .n`: the count, bright and tabular, and a dim word after it.
        Item {
            height: Theme.actionBarButtonSize
            width: root.countWidth(root.fit.word)

            Row {
                id: countRow

                anchors.verticalCenter: parent.verticalCenter
                leftPadding: 0
                rightPadding: Theme.space(8)
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

                // #51 D2: quiet beside the count, dim until the pointer is on it. Like every
                // button here it never takes the focus (M9.4).
                IconButton {
                    id: clearButton

                    // #78: the action is the name, the key the description.
                    Accessible.description: qsTr("shortcut %1").arg("esc")
                    Accessible.name: qsTr("clear selection")
                    accent: clearButton.hot
                    activeFocusOnTab: false
                    anchors.verticalCenter: parent.verticalCenter
                    block: Theme.actionBarGround
                    glyph: Glyphs.close
                    height: Theme.font.body + Theme.space(4)
                    objectName: "bar_clear"
                    tooltip: qsTr("clear selection · esc")
                    tooltipAbove: true
                    width: clearButton.height

                    onClicked: root.clearRequested()

                    KeyBadge {
                        key: "esc"
                        objectName: "barKey_clear"
                        shown: root.keysShown
                    }
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

        Item {
            height: Theme.actionBarButtonSize
            width: root._ruleWidth

            Hairline {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.actionBarRuleInset
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Theme.actionBarRuleInset
                vertical: true
            }
        }

        Repeater {
            model: root.items

            Item {
                id: slot

                required property string modelData

                readonly property bool rule: slot.modelData === "|"
                readonly property var button: slot.rule ? null : root.buttons[slot.modelData]

                height: Theme.actionBarButtonSize
                width: slot.rule ? root._ruleWidth : Theme.actionBarButtonSize

                Hairline {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.actionBarRuleInset
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: Theme.actionBarRuleInset
                    vertical: true
                    visible: slot.rule
                }

                IconButton {
                    id: iconButton

                    // #78: named by its action ("delete"), the key its description; a rule
                    // between groups is no button.
                    Accessible.description: slot.button ? qsTr("shortcut %1").arg(slot.button.key)
                                                        : ""
                    Accessible.ignored: slot.rule
                    Accessible.name: slot.button ? slot.button.text : ""
                    activeFocusOnTab: false
                    anchors.fill: parent
                    block: Theme.actionBarGround
                    danger: slot.modelData === "delete"
                    glyph: slot.button ? slot.button.glyph : ""
                    objectName: "bar_" + slot.modelData
                    tooltip: slot.button ? qsTr("%1 · %2").arg(slot.button.text).arg(slot.button.key)
                                         : ""
                    tooltipAbove: true
                    visible: !slot.rule

                    onClicked: {
                        if (slot.modelData === "more")
                            root.moreRequested(iconButton, root.folded);
                        else
                            root.triggered(slot.modelData);
                    }
                }

                // ⋯'s "enter" would hang over open folder's badge; board 46's "all actions"
                // has none either, its tooltip says it.
                KeyBadge {
                    key: slot.button && slot.modelData !== "more" ? slot.button.key : ""
                    objectName: "barKey_" + slot.modelData
                    shown: root.keysShown && !slot.rule
                }
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
                anchors.rightMargin: Theme.space(4)
                anchors.verticalCenter: parent.verticalCenter
                color: !hintLabel.isNote ? root.ink.dimForeground : root.noteWarn ? root.ink.yellow
                                                                                   : root.ink.accent
                objectName: hintLabel.isNote ? "bar_note" : "bar_hint"
                size: hintLabel.isNote ? "small" : "caption"
                text: root.tail
            }
        }
    }
}
