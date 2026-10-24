import QtQuick
import Omaseed.Kit

// Console's Lamp (Kit/Lamp.qml; board 46 `.dot`, `.ri .lamp`): a square lamp.
Rectangle {
    id: face

    required property Lamp seam

    color: face.seam.color
    objectName: "lampFaceConsole"
}
