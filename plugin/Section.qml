import QtQuick
import qs.Commons
import qs.Ui

// A section of the popup that folds (contract §3): its header is a cursor target, a
// click or `space` folds it, and while folded it keeps one line, the fact worth
// knowing. The body is the section's own content, given as children.
Column {
  id: root

  property string title: ""
  property string summary: ""
  property bool folded: false
  property bool hasCursor: false
  property color foreground: Color.foreground
  property color dim: Qt.darker(foreground, 1.55)
  property string fontFamily: Style.font.family
  default property alias body: bodyColumn.data

  signal toggled()
  signal hovered()

  width: parent ? parent.width : implicitWidth
  spacing: Style.space(10)

  CursorSurface {
    id: header
    width: parent.width
    height: Math.max(Style.spacing.popupRowHeight, headerRow.implicitHeight)
    hasCursor: root.hasCursor
    foreground: root.foreground

    Row {
      id: headerRow
      anchors.left: parent.left
      anchors.leftMargin: Style.space(6)
      anchors.right: chevron.left
      anchors.rightMargin: Style.space(6)
      anchors.verticalCenter: parent.verticalCenter
      spacing: Style.space(8)

      PanelSectionHeader {
        id: titleText
        text: root.title.toUpperCase()
        foreground: root.foreground
        fontFamily: root.fontFamily
        font.letterSpacing: Style.space(1)
        anchors.verticalCenter: parent.verticalCenter
      }

      Text {
        visible: root.folded && root.summary !== ""
        width: Math.max(0, parent.width - titleText.width - parent.spacing)
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: root.summary
        color: root.dim
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        elide: Text.ElideRight
      }
    }

    Text {
      id: chevron
      anchors.right: parent.right
      anchors.rightMargin: Style.space(6)
      anchors.verticalCenter: parent.verticalCenter
      textFormat: Text.PlainText
      text: root.folded ? "▸" : "▾"
      color: root.dim
      font.family: root.fontFamily
      font.pixelSize: Style.font.caption
    }

    MouseArea {
      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor
      onEntered: root.hovered()
      onClicked: root.toggled()
    }
  }

  Column {
    id: bodyColumn
    visible: !root.folded
    width: parent.width
    spacing: Style.space(10)
  }
}
