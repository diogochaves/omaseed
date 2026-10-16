pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The selection bar of docs/17 M9 (Diogo took every recommendation, 2026-09-29): a row of
// icon buttons that floats over the table while torrents are selected, so the mouse has what
// `enter` gives the keyboard.
//
//   M9.1  the host floats it over the table, centred, 12 u above the table's bottom edge
//   M9.2  the host shows it while one or more torrents are selected (never for the cursor)
//   M9.3  count · start · stop | recheck · reannounce | category · tags · set location ·
//         limits | open folder · ⋯ | delete (red). ⋯ is the full actions menu. A small ✕
//         right after the count clears the selection, as `esc` does (#51 D2).
//   M9.4  mouse only: no button takes the focus (tab never lands here, a click leaves the
//         keyboard on the table) and each tooltip names the key that does the same
//   M9.5  never wraps: when it is wider than `availableWidth`, buttons fold into ⋯ in
//         `foldOrder`; the count, start, stop, ⋯ and delete always stay, and last of all the
//         count drops its word
//
// It runs nothing itself: `triggered(action)` names the action (the actions menu's names,
// plus "start" and "stop"), and ⋯ asks the host for the menu with `moreRequested(button,
// folded)`, `folded` being the actions it no longer shows, for the menu to list first.
//
//     SelectionBar { count: table.selectedCount; availableWidth: table.width - Theme.space(16) }
//
// Hand-formatted to qmlformat's own style (typed functions, KIT-19).
Rectangle {
    id: root

    /// How many torrents are selected; the host hides the bar at 0.
    property int count: 0
    /// The widest the bar may be; buttons fold into ⋯ past it.
    property real availableWidth: 0
    /// #24 T1: the selection's ledger, `{sizeText, givenText, ratioText, ratioGood}`, drawn
    /// after the count while the bar has room for it (it goes before any button folds).
    property var summary: ({})

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
                                            "text": qsTr("set location"),
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
    readonly property real _summaryWidth: summaryRow.implicitWidth + Theme.space(8)
    readonly property bool showSummary: root.summary.sizeText !== undefined && root.fit.folds === 0
                                        && root.fit.word
                                        && root.widthOf(root.items, true) + root._summaryWidth
                                        <= root.availableWidth
    /// The actions ⋯ now holds besides the menu's own, in `foldOrder`.
    readonly property var folded: root.foldOrder.slice(0, root.fit.folds)
    /// What the bar draws after the count: action names, and "|" for a hairline.
    readonly property var items: root.itemsFor(root.fit.folds)

    readonly property int _padding: Theme.space(6)
    readonly property int _spacing: Theme.space(2)
    readonly property int _ruleWidth: 1 + 2 * Theme.space(4)

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

    /// The bar's width drawing `items`, with or without the count's word.
    function widthOf(items: var, word: bool): real {
        let width = 2 * root._padding + root.countWidth(word) + root._spacing + root._ruleWidth;
        for (const item of items)
            width += root._spacing + (item === "|" ? root._ruleWidth : Theme.controlHeight);
        return width;
    }

    border.color: Theme.fill(Theme.borderIdle)
    border.width: 1
    color: Theme.background
    implicitHeight: Theme.controlHeight + 2 * Theme.space(4)
    implicitWidth: root.widthOf(root.items, root.fit.word) + (root.showSummary ? root._summaryWidth : 0)
    radius: Theme.cornerRadius

    // The bar floats over the rows: a click on its own ground must not reach the row under
    // it (and change the selection it acts on). The wheel still scrolls the table.
    MouseArea {
        acceptedButtons: Qt.AllButtons
        anchors.fill: parent
    }

    Row {
        id: content

        anchors.left: parent.left
        anchors.leftMargin: root._padding
        anchors.verticalCenter: parent.verticalCenter
        spacing: root._spacing

        // `.sbar .n`: the count, bright and tabular, and a dim word after it.
        Item {
            height: Theme.controlHeight
            width: root.countWidth(root.fit.word)

            Row {
                id: countRow

                anchors.verticalCenter: parent.verticalCenter
                leftPadding: Theme.space(6)
                rightPadding: Theme.space(8)
                spacing: Theme.space(5)

                Label {
                    id: countNumber

                    anchors.baseline: countWord.baseline
                    bright: true
                    font.features: {
                        "tnum": 1
                    }
                    objectName: "selectionCount"
                    text: String(root.count)
                }

                Label {
                    id: countWord

                    dim: true
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
                    glyph: Glyphs.close
                    height: Theme.font.body + Theme.space(4)
                    objectName: "bar_clear"
                    tooltip: qsTr("clear selection · esc")
                    tooltipAbove: true
                    width: clearButton.height

                    onClicked: root.clearRequested()
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
            height: Theme.controlHeight
            objectName: "bar_summary"
            visible: root.showSummary
            width: root._summaryWidth

            Row {
                id: summaryRow

                anchors.verticalCenter: parent.verticalCenter
                rightPadding: Theme.space(4)
                spacing: Theme.space(12)

                Label {
                    dim: true
                    size: "small"
                    text: root.summary.sizeText ?? ""
                }

                Row {
                    spacing: Theme.space(5)

                    Label {
                        dim: true
                        size: "small"
                        text: qsTr("given")
                    }

                    Label {
                        bright: true
                        size: "small"
                        text: root.summary.givenText ?? ""
                    }
                }

                Row {
                    spacing: Theme.space(5)

                    Label {
                        dim: true
                        size: "small"
                        text: qsTr("ratio")
                    }

                    Label {
                        color: root.summary.ratioGood === true ? Theme.stateSeeding : Theme.brightForeground
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
            height: Theme.controlHeight
            width: root._ruleWidth

            Hairline {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space(6)
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Theme.space(6)
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

                height: Theme.controlHeight
                width: slot.rule ? root._ruleWidth : Theme.controlHeight

                Hairline {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.space(6)
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: Theme.space(6)
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
            }
        }
    }
}
