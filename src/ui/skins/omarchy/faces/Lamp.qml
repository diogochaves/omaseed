import QtQuick
import Omaseed.Kit

// Omarchy's Lamp (Kit/Lamp.qml): the round dot the status line and the server rows drew.
Rectangle {
    id: face

    required property Lamp seam

    color: face.seam.color
    objectName: "lampFaceOmarchy"
    radius: width / 2
}
