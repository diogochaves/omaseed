import QtQuick
import qs.Commons
import qs.Ui

// One row of the popup: a cursor target with a name, a right-hand fact, and as needed a
// tick box (Add), a leading icon (Lately), a second line, a badge, and a 2 px progress
// line (In flight). The panel owns the cursor; hovering a row moves it here.
CursorSurface {
  id: root

  property string title: ""
  property string meta: ""
  property color metaColor: dim
  property string subline: ""
  property string badge: ""
  property color badgeColor: Color.accent
  // "" | "tick" | "check" | "mark"
  property string leading: ""
  property bool ticked: false
  property color leadingColor: foreground
  property real progress: -1
  property color progressColor: Color.accent
  property bool readOnly: false
  property color dim: Qt.darker(foreground, 1.55)
  property string fontFamily: Style.font.family

  signal clicked()
  signal hovered()

  width: parent ? parent.width : implicitWidth
  implicitHeight: Math.max(Style.spacing.popupRowHeight, body.implicitHeight + Style.space(8))
  opacity: readOnly ? 0.45 : 1

  Row {
    id: body
    anchors.left: parent.left
    anchors.leftMargin: Style.space(8)
    anchors.right: parent.right
    anchors.rightMargin: Style.space(8)
    anchors.verticalCenter: parent.verticalCenter
    spacing: Style.space(10)

    Item {
      id: lead
      visible: root.leading !== ""
      width: Style.font.body
      height: Style.font.body
      // Level with the name, whether or not a second line follows.
      y: Math.round((titleText.implicitHeight - height) / 2)

      Rectangle {
        visible: root.leading === "tick"
        anchors.fill: parent
        anchors.margins: Style.space(1)
        color: root.ticked ? root.leadingColor : "transparent"
        border.width: root.ticked ? 0 : 1
        border.color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.4)
      }

      LineIcon {
        visible: root.leading === "check" || root.leading === "mark"
        anchors.fill: parent
        name: root.leading
        color: root.leadingColor
      }
    }

    Column {
      width: parent.width - (lead.visible ? lead.width + parent.spacing : 0)
      spacing: Style.space(3)

      Item {
        width: parent.width
        height: Math.max(titleText.implicitHeight, metaText.implicitHeight, badgeBox.visible ? badgeBox.height : 0)

        Text {
          id: titleText
          anchors.left: parent.left
          anchors.right: badgeBox.visible ? badgeBox.left : (metaText.visible ? metaText.left : parent.right)
          anchors.rightMargin: Style.space(10)
          anchors.verticalCenter: parent.verticalCenter
          textFormat: Text.PlainText
          text: root.title
          color: root.foreground
          font.family: root.fontFamily
          font.pixelSize: Style.font.body
          elide: Text.ElideRight
        }

        Text {
          id: metaText
          visible: root.meta !== "" && !badgeBox.visible
          anchors.right: parent.right
          anchors.verticalCenter: parent.verticalCenter
          width: Math.min(implicitWidth, parent.width * 0.55)
          horizontalAlignment: Text.AlignRight
          textFormat: Text.PlainText
          text: root.meta
          color: root.metaColor
          font.family: root.fontFamily
          font.pixelSize: Style.font.bodySmall
          elide: Text.ElideLeft
        }

        Rectangle {
          id: badgeBox
          visible: root.badge !== ""
          anchors.right: parent.right
          anchors.verticalCenter: parent.verticalCenter
          width: badgeText.implicitWidth + Style.space(12)
          height: badgeText.implicitHeight + Style.space(4)
          color: "transparent"
          border.width: 1
          border.color: root.badgeColor
          radius: Style.cornerRadius

          Text {
            id: badgeText
            anchors.centerIn: parent
            textFormat: Text.PlainText
            text: root.badge
            color: root.badgeColor
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
          }
        }
      }

      Text {
        visible: root.subline !== ""
        width: parent.width
        textFormat: Text.PlainText
        text: root.subline
        color: root.dim
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        elide: Text.ElideRight
      }

      // 2 px on a 12 % track.
      Rectangle {
        visible: root.progress >= 0
        width: parent.width
        height: Style.space(2)
        color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.12)

        Rectangle {
          width: parent.width * Math.max(0, Math.min(1, root.progress))
          height: parent.height
          color: root.progressColor
        }
      }
    }
  }

  MouseArea {
    anchors.fill: parent
    hoverEnabled: true
    enabled: !root.readOnly
    cursorShape: Qt.PointingHandCursor
    onEntered: root.hovered()
    onClicked: root.clicked()
  }
}
