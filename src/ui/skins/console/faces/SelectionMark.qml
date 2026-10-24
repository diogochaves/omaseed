pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// Console's SelectionMark (Kit/SelectionMark.qml; board 46 `.row.sel::after`, `.mi.cur`,
// `.card.sel`): a bracket at each corner, two arms of `Theme.selectionMarkArm` each,
// `Theme.litWidth` thick, in the seam's colour; on a cut plate (`cut`) the top right and
// bottom left ones fall in the cut and are left out. Omarchy's `rule` is not drawn here.
Item {
    id: face

    required property SelectionMark seam

    readonly property real arm: Math.min(Theme.selectionMarkArm, face.width / 2, face.height / 2)
    readonly property real weight: Theme.litWidth

    objectName: "selectionMarkFaceConsole"

    Repeater {
        model: 4

        Item {
            id: corner

            required property int index

            readonly property bool atRight: corner.index % 2 === 1
            readonly property bool atBottom: corner.index >= 2

            height: face.arm
            objectName: "selectionMarkCorner"
            visible: face.seam.cut <= 0 || corner.atRight === corner.atBottom
            width: face.arm
            x: corner.atRight ? face.width - face.arm : 0
            y: corner.atBottom ? face.height - face.arm : 0

            Rectangle {
                color: face.seam.color
                height: face.weight
                width: face.arm
                y: corner.atBottom ? face.arm - face.weight : 0
            }

            Rectangle {
                color: face.seam.color
                height: face.arm
                width: face.weight
                x: corner.atRight ? face.arm - face.weight : 0
            }
        }
    }
}
