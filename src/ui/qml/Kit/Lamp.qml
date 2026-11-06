import QtQuick
import Omaseed.Kit

// A seam (the console skin's design note, "Seams"): a state's lamp (a server's, the status
// line's), `size` across in `color`. Omarchy's face is today's dot; Console's is board 46's
// square lamp (`.dot`, `.lamp`).
Item {
    id: root

    property color color: Theme.green
    property int size: Theme.space(7)

    /// The face drawn, for tests.
    readonly property Item face: loader.item as Item

    implicitHeight: root.size
    implicitWidth: root.size

    FaceLoader {
        id: loader

        anchors.fill: parent
        seam: root
        seamName: "Lamp"
    }
}
