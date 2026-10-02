import QtQuick
import qs.Commons

// One direction's speed in the bar. Horizontal: the arrow, then the value right-aligned
// in a slot four characters wide (Format.barField's five characters). Vertical: the
// arrow on a line of its own, then the value, both centred, the way omagauge stacks.
// With nothing to report the value is empty, but the slot keeps its size.
Item {
  id: root

  property bool vertical: false
  property string arrow: "↓"
  property string value: ""
  property color ink: "white"
  property string fontFamily: Style.font.family
  property int valueSlot: 0
  property int arrowSlot: 0
  property bool animate: true

  implicitWidth: vertical ? Math.max(valueSlot, arrowSlot) : arrowSlot + valueSlot
  implicitHeight: vertical ? arrowText.implicitHeight + valueText.implicitHeight : Math.max(arrowText.implicitHeight, valueText.implicitHeight)

  Text {
    id: arrowText
    x: root.vertical ? Math.round((root.width - width) / 2) : 0
    y: root.vertical ? 0 : Math.round((root.height - height) / 2)
    text: root.arrow
    color: root.ink
    opacity: root.value === "" ? 0.45 : 1
    font.family: root.fontFamily
    font.pixelSize: root.vertical ? Style.font.caption : Style.font.body
    renderType: Text.NativeRendering
    Behavior on color { enabled: root.animate; ColorAnimation { duration: 160 } }
  }

  Text {
    id: valueText
    x: root.vertical ? Math.round((root.width - width) / 2) : root.arrowSlot
    y: root.vertical ? arrowText.implicitHeight : Math.round((root.height - height) / 2)
    width: root.vertical ? implicitWidth : root.valueSlot
    horizontalAlignment: root.vertical ? Text.AlignHCenter : Text.AlignRight
    text: root.value
    color: root.ink
    font.family: root.fontFamily
    font.pixelSize: Style.font.body
    font.features: { "tnum": 1 }
    renderType: Text.NativeRendering
    Behavior on color { enabled: root.animate; ColorAnimation { duration: 160 } }
  }
}
