import QtQuick
import qs.Commons

// A number over a tracked caption, the app's hero stat in the shell's card wording:
// "↓ 1.18 MB/s" over "DOWNLOAD", "14.2 GB" over "GIVEN".
Column {
  id: root

  property string arrow: ""
  property color arrowColor: foreground
  property string value: ""
  property color valueColor: foreground
  property string caption: ""
  property color foreground: Color.foreground
  property color dim: Qt.darker(foreground, 1.55)
  property string fontFamily: Style.font.family
  property real valueSize: Style.font.heading

  spacing: Style.space(2)

  Row {
    spacing: Style.space(5)

    Text {
      visible: root.arrow !== ""
      anchors.baseline: valueText.baseline
      textFormat: Text.PlainText
      text: root.arrow
      color: root.arrowColor
      font.family: root.fontFamily
      font.pixelSize: Style.font.body
    }

    Text {
      id: valueText
      textFormat: Text.PlainText
      text: root.value
      color: root.valueColor
      font.family: root.fontFamily
      font.pixelSize: root.valueSize
    }
  }

  Text {
    textFormat: Text.PlainText
    text: root.caption.toUpperCase()
    color: root.dim
    font.family: root.fontFamily
    font.pixelSize: Style.font.caption
    font.letterSpacing: Style.space(1)
  }
}
