import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A notice with a 2 px rule on its left (the app's banner, v2 Fig. 23): a line that says
// what is wrong, a dim line that says what to do, and at most one action.
Item {
  id: root

  property string title: ""
  // What a screen reader hears for the title, when the title changes faster than it should
  // be read (a retry counting down).
  property string spokenTitle: title
  property string detail: ""
  property string action: ""
  property color rule: Commons.Color.urgent
  property color foreground: Commons.Color.foreground
  property string fontFamily: Style.font.family

  signal actionClicked()

  implicitHeight: body.implicitHeight

  // A screen reader (#80): a notice appears by itself, so it is an alert, its title the name
  // and the line under it the description; its one action is a button inside it.
  Accessible.role: Accessible.AlertMessage
  Accessible.name: root.spokenTitle
  Accessible.description: root.detail

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
      id: actionButton
      visible: root.action !== ""
      text: root.action
      bordered: true
      foreground: root.rule
      fontFamily: root.fontFamily
      fontSize: Style.font.bodySmall
      verticalPadding: Style.space(3)
      onClicked: root.actionClicked()
      // Not `accessibleName` (an Omarchy without PR #13212 would fail to load the widget);
      // where the kit has it, the kit presses and this steps aside.
      Accessible.role: Accessible.Button
      Accessible.name: root.action
      Accessible.ignored: root.action === ""
      Accessible.onPressAction: if (!("accessibleName" in actionButton)) actionButton.clicked()
    }
  }
}
