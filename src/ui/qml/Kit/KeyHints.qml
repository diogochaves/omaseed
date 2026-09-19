import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// Unboxed key hints row (spec v2 §1 Fig. 1 footer `.hints`, and Fig. 22 empty-state teaching
// hints). `model` is a list of `{key, label}` objects, keys in `foreground`/`font.small`,
// labels dim. Keys are printed exactly as given — never upper-cased, because case is what
// separates `p` from `P` and `d` from `D` in the keymap. `pinned` is one more `{key, label}` pair pushed to the right edge
// (the `?` help hint) — matching `.hints .help { margin-left:auto }`. The caller must give
// this component a width (it is a `RowLayout`, so the pinned item's push only works with a
// bounded width).
RowLayout {
    id: root

    property var model: []
    property var pinned: null

    readonly property bool hasPinned: root.pinned !== null

    spacing: Theme.space(14)

    Row {
        spacing: Theme.space(14)

        Repeater {
            model: root.model

            Row {
                id: hintRow

                required property var modelData

                spacing: Theme.space(6)

                Text {
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: hintRow.modelData.key
                }

                Text {
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: hintRow.modelData.label
                }
            }
        }
    }

    Item {
        Layout.fillWidth: true
    }

    Row {
        spacing: Theme.space(6)
        visible: root.hasPinned

        Text {
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: root.pinned ? root.pinned.key : ""
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: root.pinned ? root.pinned.label : ""
        }
    }
}
