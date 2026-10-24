import QtQuick
import Omaseed.Kit

// How a seam loads its face (the console skin's design note, "Seams"): the file the skin
// names for `seamName` (Theme.face), handed the seam itself as its required `seam`, and
// loaded again when the skin changes. A face that fails to load gives way to Omarchy's
// (Theme.defaultFace), so a seam always draws. Synchronous, so a seam is drawn the frame it
// is made, as the drawing it replaced was.
Loader {
    id: face

    /// The seam's name in a skin's `faces` ("Plate", "SelectionMark", "Meter", "Lamp").
    required property string seamName
    /// The seam that owns this loader: every face reads its API from it.
    required property Item seam

    readonly property url wanted: Theme.face(face.seamName)
    readonly property url fallback: Theme.defaultFace(face.seamName)

    function load(url: url): void {
        face.setSource(url, {
            "seam": face.seam
        });
    }

    onStatusChanged: {
        if (face.status === Loader.Error && face.source !== face.fallback)
            face.load(face.fallback);
    }
    onWantedChanged: face.load(face.wanted)
    Component.onCompleted: face.load(face.wanted)
}
