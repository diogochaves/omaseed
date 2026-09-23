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
// right; the pinned hint always keeps its place.
RowLayout {
    id: root

    property var model: []
    property var pinned: null
    /// Which `Theme.font.*` token the row prints at. The window footer is `small` (spec
    /// `.hints`); a sheet's or a palette's own hint line is `caption` (spec `.sheet .ft`,
    /// `.palette .ft`).
    property string size: "small"

    readonly property bool hasPinned: root.pinned !== null
    readonly property int _pixelSize: Theme.font[root.size]
    /// How many of `model`'s hints are shown.
    readonly property int shownCount: {
        const hints = root.model; // re-evaluate when the hints change
        const room = root.width - (root.hasPinned ? pinnedRow.implicitWidth + root.spacing : 0);
        let used = 0;
        for (let i = 0; i < hintRepeater.count; ++i) {
            const item = hintRepeater.itemAt(i);
            if (item === null)
                return i;
            const need = item.implicitWidth + (i > 0 ? hintsRow.spacing : 0);
            if (used + need > room)
                return i;
            used += need;
        }
        return hints === null ? 0 : hintRepeater.count;
    }

    spacing: Theme.space(14)

    Row {
        id: hintsRow

        spacing: Theme.space(14)

        Repeater {
            id: hintRepeater

            model: root.model

            Row {
                id: hintRow

                required property int index
                required property var modelData

                spacing: Theme.space(6)
                visible: hintRow.index < root.shownCount

                Text {
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: root._pixelSize
                    text: hintRow.modelData.key
                }

                Text {
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: root._pixelSize
                    text: hintRow.modelData.label
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
