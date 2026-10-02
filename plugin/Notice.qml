import QtQuick
import qs.Commons
import qs.Ui

// A notice with a 2 px rule on its left (the app's banner, v2 Fig. 23): a line that says
// what is wrong, a dim line that says what to do, and at most one action.
Item {
  id: root

  property string title: ""
  property string detail: ""
  property string action: ""
  property color rule: Color.urgent
  property color foreground: Color.foreground
  property string fontFamily: Style.font.family

  signal actionClicked()

  implicitHeight: body.implicitHeight

  Rectangle {
    width: Style.space(2)
    height: parent.height
    color: root.rule
  }

  Column {
    id: body
    anchors.left: parent.left
    anchors.leftMargin: Style.space(12)
    anchors.right: parent.right
    spacing: Style.space(4)

    Text {
      width: parent.width
      textFormat: Text.PlainText
      text: root.title
      color: root.foreground
      font.family: root.fontFamily
      font.pixelSize: Style.font.body
      wrapMode: Text.WordWrap
    }

    Text {
      visible: root.detail !== ""
      width: parent.width
      textFormat: Text.PlainText
      text: root.detail
      color: Qt.darker(root.foreground, 1.55)
      font.family: root.fontFamily
      font.pixelSize: Style.font.bodySmall
      wrapMode: Text.WordWrap
    }

    Button {
      visible: root.action !== ""
      text: root.action
      bordered: true
      foreground: root.rule
      fontFamily: root.fontFamily
      fontSize: Style.font.bodySmall
      verticalPadding: Style.space(3)
      onClicked: root.actionClicked()
    }
  }
}
