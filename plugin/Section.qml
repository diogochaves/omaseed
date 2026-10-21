import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "Status.js" as Status

// A section of the popup that folds (contract §3): its header is a cursor target, a
// click or `space` folds it, and while folded it keeps one line, the fact worth
// knowing. The body is the section's own content, given as children.
Column {
  id: root

  property string title: ""
  property string summary: ""
  property bool folded: false
  property bool hasCursor: false
  property color foreground: Commons.Color.foreground
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

    // A screen reader (#80): a heading that folds is a button; its description says whether
    // it is folded, with the line it keeps then (Qt Quick has no `Accessible.expanded`). Not
    // `accessibleName` (an Omarchy without PR #13212 would fail to load the widget);
    // `ignored`: with #13212 the kit leaves an unnamed CursorSurface out. The kit presses no
    // CursorSurface, so no guard.
    Accessible.role: Accessible.Button
    Accessible.name: root.title
    Accessible.description: root.folded ? (root.summary !== "" ? "folded: " + Status.spoken(root.summary) : "folded") : "unfolded"
    Accessible.ignored: false
    // a11y-ignore A9: a CursorSurface; #13212's kit presses no row, so there is no second press
    Accessible.onPressAction: root.toggled()

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
