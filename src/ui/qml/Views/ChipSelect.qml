pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// A keyboard-drivable row of Kit `Chip`s (spec v2 §3 Fig. 5 `.sheet .chips`: the add sheet's
// Category, Tags and Layout rows, plus the trailing dim `…` chip that opens the matching
// palette). The sheet's focus trap only cycles items that are tab-focusable or expose
// `focusInput()`, so this is an `activeFocusOnTab` Item of its own: `tab` lands on the row,
// `h`/`l` or the arrows move the cursor inside it, `space` picks. `enter` is deliberately
// left alone so the sheet's primary action still fires from anywhere inside it.
//
//     ChipSelect { options: ["linux", "movies"]; current: "linux"; onChanged: … }
//     ChipSelect { multiple: true; options: App.tags; selected: ["keep"] }
Item {
    id: root

    /// The chip texts, in order.
    property var options: []
    /// Multi-select (Tags) rather than single-select (Category, Layout).
    property bool multiple: false
    /// Single-select: the chosen option, "" for none.
    property string current: ""
    /// Single-select: picking the current chip again clears it (Category: "no category" is
    /// a real answer). Off for a row where one of the options is always the answer (the
    /// Limits sheet's share-limit action), so re-picking the current chip keeps it.
    property bool allowNone: true
    /// Multi-select: the chosen options, in the order they were picked.
    property var selected: []
    /// Show the dim `…` chip after the options (the palette hand-off).
    property bool more: false
    /// The `…` chip's text.
    property string moreText: "…"
    /// Which chip the keyboard is on.
    property int cursorIndex: 0

    /// `current`/`selected` changed through this row.
    signal changed
    /// The `…` chip was picked.
    signal moreRequested

    readonly property int _count: root.options.length + (root.more ? 1 : 0)

    /// True when `option` is picked, in either mode.
    function isPicked(option: string): bool {
        return root.multiple ? root.selected.indexOf(option) >= 0 : root.current === option;
    }

    /// Pick (or, in multi-select, unpick) one option by name.
    function pick(option: string): void {
    if (!root.multiple) {
        root.current = root.current === option && root.allowNone ? "" : option;
        root.changed();
        return;
    }
        const next = root.selected.slice();
        const at = next.indexOf(option);
        if (at >= 0)
        next.splice(at, 1);
        else
        next.push(option);
        root.selected = next;
        root.changed();
    }

        /// `space` on the chip under the cursor.
        function activateCursor(): void {
                                       if (root.more && root.cursorIndex === root.options.length) {
                                           root.moreRequested();
                                           return;
                                       }
                                       if (root.cursorIndex >= 0 && root.cursorIndex
                                           < root.options.length)
                                       root.pick(root.options[root.cursorIndex]);
                                   }

        function moveCursor(delta: int): void {
        if (root._count === 0)
        return;
        root.cursorIndex = (root.cursorIndex + delta + root._count) % root._count;
    }

        activeFocusOnTab: true
        implicitHeight: flow.implicitHeight
        implicitWidth: flow.implicitWidth

        Keys.onPressed: event => {
            switch (event.key) {
            case Qt.Key_Left:
            case Qt.Key_H:
                root.moveCursor(-1);
                break;
            case Qt.Key_Right:
            case Qt.Key_L:
                root.moveCursor(1);
                break;
            case Qt.Key_Space:
                root.activateCursor();
                break;
            default:
                return;
            }
            event.accepted = true;
        }

        Flow {
            id: flow

            spacing: Theme.space(6)
            width: root.width

            Repeater {
                model: root.options

                Chip {
                    id: optionChip

                    required property int index
                    required property string modelData

                    hasCursor: root.activeFocus && root.cursorIndex === optionChip.index
                    checkable: true
                    selected: root.isPicked(optionChip.modelData)
                    text: optionChip.modelData

                    onClicked: {
                        root.cursorIndex = optionChip.index;
                        root.pick(optionChip.modelData);
                    }
                }
            }

            // `.chip.dim` — the row's hand-off to the matching palette.
            Chip {
                // #78: the `…` is no name; it opens the rest of the list.
                Accessible.name: qsTr("more")
                hasCursor: root.activeFocus && root.cursorIndex === root.options.length
                text: root.moreText
                visible: root.more

                onClicked: {
                    root.cursorIndex = root.options.length;
                    root.moreRequested();
                }
            }
        }
    }
