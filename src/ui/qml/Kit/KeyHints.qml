pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// Unboxed key hints row (spec v2 §1 Fig. 1 footer `.hints`, and Fig. 22 empty-state teaching
// hints). `model` is a list of `{key, label}` objects, keys in `foreground`/`font.small`,
// labels dim, both at `size`. Keys are printed exactly as given — never upper-cased, because case is what
// separates `p` from `P` and `d` from `D` in the keymap. `pinned` is one more `{key, label}` pair pushed to the right edge
// (the `?` help hint) — matching `.hints .help { margin-left:auto }`. The caller must give
// this component a width (it is a `RowLayout`, so the pinned item's push only works with a
// bounded width).
//
// Too narrow for every hint (docs/10 §3), the row shows as many as fit, dropping from the
// right; the pinned hint always keeps its place. A hint with `keep: true` is dropped last: the
// others go from the right first, so a line that ends in the key that matters (the add sheet's
// `enter add`, #8) loses the extras before it.
//
// A hint with `clickable: true` names a safe action the mouse may run too: it shows a pointing
// hand and turns its key and label to the accent under the pointer (as the console's
// "esc clear" and Field's clickable trailing do), and a click sends `hintClicked`. The row
// only reports it; the caller runs what that key runs.
RowLayout {
    id: root

    property var model: []
    property var pinned: null
    /// Which `Theme.font.*` token the row prints at. The window footer is `small` (spec
    /// `.hints`); a sheet's or a palette's own hint line is `caption` (spec `.sheet .ft`,
    /// `.palette .ft`).
    property string size: "small"

    /// A click on a `clickable` hint: its index in `model` and the hint itself.
    signal hintClicked(int index, var hint)

    readonly property bool hasPinned: root.pinned !== null
    readonly property int _pixelSize: Theme.font[root.size]
    /// Which of `model`'s hints are shown, by index: every `keep` hint that fits (left to
    /// right), then the others from the left while there is room.
    readonly property var _shown: {
        const hints = root.model; // re-evaluate when the hints change
        const count = hintRepeater.count;
        const shown = [];
        for (let i = 0; i < count; ++i)
            shown.push(false);
        let room = root.width - (root.hasPinned ? pinnedRow.implicitWidth + root.spacing : 0);
        // Each shown hint takes its width and one gap; the first gap is given back.
        room += hintsRow.spacing;
        const need = i => {
            const item = hintRepeater.itemAt(i);
            return item === null ? Infinity : item.implicitWidth + hintsRow.spacing;
        };
        const kept = i => hints !== null && hints[i] !== undefined && hints[i].keep === true;
        for (let i = 0; i < count; ++i) {
            if (kept(i) && need(i) <= room) {
                shown[i] = true;
                room -= need(i);
            }
        }
        for (let i = 0; i < count; ++i) {
            if (kept(i))
                continue;
            if (need(i) > room)
                break;
            shown[i] = true;
            room -= need(i);
        }
        return shown;
    }
    /// How many of `model`'s hints are shown.
    readonly property int shownCount: root._shown.filter(shown => shown).length

    spacing: Theme.space(14)

    Row {
        id: hintsRow

        spacing: Theme.space(14)

        Repeater {
            id: hintRepeater

            model: root.model

            Item {
                id: hintRow

                required property int index
                required property var modelData

                implicitHeight: hintPair.implicitHeight
                implicitWidth: hintPair.implicitWidth
                objectName: "keyHint_" + hintRow.modelData.key
                visible: root._shown[hintRow.index] === true

                Row {
                    id: hintPair

                    spacing: Theme.space(6)

                    Text {
                        color: hintArea.containsMouse ? Theme.accent : Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: root._pixelSize
                        text: hintRow.modelData.key
                    }

                    Text {
                        color: hintArea.containsMouse ? Theme.accent : Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: root._pixelSize
                        text: hintRow.modelData.label
                    }
                }

                MouseArea {
                    id: hintArea

                    Accessible.name: hintRow.modelData.key + " " + hintRow.modelData.label
                    Accessible.role: Accessible.Button
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    enabled: hintRow.modelData.clickable === true
                    hoverEnabled: true
                    objectName: "keyHintArea"

                    onClicked: root.hintClicked(hintRow.index, hintRow.modelData)
                }
            }
        }
    }

    Item {
        Layout.fillWidth: true
    }

    Row {
        id: pinnedRow

        spacing: Theme.space(6)
        visible: root.hasPinned

        Text {
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: root._pixelSize
            text: root.pinned ? root.pinned.key : ""
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: root._pixelSize
            text: root.pinned ? root.pinned.label : ""
        }
    }
}
