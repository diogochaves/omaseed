import QtQuick
import qs.Commons

// One direction's speed in the bar. Horizontal: the arrow, then the value in a slot as
// wide as the widest four characters it can hold (Format.barField's five characters), so
// the field keeps its width whatever the number; the slack falls after the value, where
// it reads as the gap to the next field. Vertical: the
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
  // Horizontal only: centre the figures' height, not the line box, which keeps room for
  // descenders and so sits the digits about 1 px above the bar's middle. Omarchy 4.5's own
  // labels do this (WidgetButton's `centerFigures`), so the speeds line up with them (#111).
  property bool centerFigures: false
  readonly property real figureOffset: centerFigures && !vertical
    ? Math.round((valueMetrics.descent - valueMetrics.ascent + valueMetrics.tightBoundingRect("0").height) / 2) : 0

  // The arrow is placed by its ink, not its advance: a monospace font centres "↓" in a
  // whole cell, which would leave half a cell between the arrow and its number.
  readonly property real arrowInk: Math.ceil(arrowMetrics.tightBoundingRect.width)
  readonly property real arrowGap: Style.space(2)

  implicitWidth: vertical ? Math.max(valueSlot, arrowSlot) : arrowInk + arrowGap + valueSlot

  FontMetrics {
    id: valueMetrics
    font: valueText.font
  }

  TextMetrics {
    id: arrowMetrics
    font: arrowText.font
    text: root.arrow
  }
  implicitHeight: vertical ? arrowText.implicitHeight + valueText.implicitHeight : Math.max(arrowText.implicitHeight, valueText.implicitHeight)

  Text {
    id: arrowText
    x: root.vertical ? Math.round((root.width - width) / 2) : -arrowMetrics.tightBoundingRect.x
    y: root.vertical ? 0 : Math.round((root.height - height) / 2) + root.figureOffset
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
    x: root.vertical ? Math.round((root.width - width) / 2) : root.arrowInk + root.arrowGap
    y: root.vertical ? arrowText.implicitHeight : Math.round((root.height - height) / 2) + root.figureOffset
    width: root.vertical ? implicitWidth : root.valueSlot
    horizontalAlignment: root.vertical ? Text.AlignHCenter : Text.AlignLeft
    text: root.value
    color: root.ink
    font.family: root.fontFamily
    font.pixelSize: Style.font.body
    font.features: { "tnum": 1 }
    renderType: Text.NativeRendering
    Behavior on color { enabled: root.animate; ColorAnimation { duration: 160 } }
  }
}
