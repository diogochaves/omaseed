import QtQuick
import qs.Commons
import qs.Ui

// One row of the widget's settings page (docs/21): the setting's name and a line under
// it, the shell's own switch or chips on the right. The page owns the cursor and the
// values; a click asks it to change one.
CursorSurface {
  id: root

  // A row of Status.settingRows(): key, title, type, options, min, max.
  property var row: ({})
  property var value
  property string detail: ""
  // The last of mark and speed on (it cannot go off), or a row that means nothing as
  // things are (the speed's direction with the speed off).
  property bool locked: false
  property bool inactive: false
  property color dim: Qt.darker(foreground, 1.55)
  property color warn: Color.foreground
  property string fontFamily: Style.font.family

  signal stepped(int dir)
  signal chosen(var value)
  signal hovered()

  readonly property bool isSwitch: row.type === "bool"
  readonly property var chips: {
    if (row.type === "enum") return row.options.map(function(o) { return { value: o[0], label: o[1] } })
    if (row.type === "int") {
      var out = []
      for (var n = row.min; n <= row.max; n++) out.push({ value: String(n), label: String(n) })
      return out
    }
    return []
  }

  width: parent ? parent.width : implicitWidth
  implicitHeight: Math.max(Style.spacing.popupRowHeight, Math.max(text.implicitHeight, control.height) + Style.space(10))

  Column {
    id: text
    anchors.left: parent.left
    anchors.leftMargin: Style.space(8)
    anchors.right: control.left
    anchors.rightMargin: Style.space(10)
    anchors.verticalCenter: parent.verticalCenter
    spacing: Style.space(2)

    Text {
      width: parent.width
      textFormat: Text.PlainText
      text: root.row.title || ""
      color: root.foreground
      font.family: root.fontFamily
      font.pixelSize: Style.font.body
      elide: Text.ElideRight
    }

    Text {
      visible: root.detail !== ""
      width: parent.width
      textFormat: Text.PlainText
      text: root.detail
      color: root.locked ? root.warn : root.dim
      font.family: root.fontFamily
      font.pixelSize: Style.font.caption
      wrapMode: Text.WordWrap
    }
  }

  Item {
    id: control
    anchors.right: parent.right
    anchors.rightMargin: Style.space(8)
    anchors.verticalCenter: parent.verticalCenter
    width: root.isSwitch ? toggle.implicitWidth : group.implicitWidth
    height: root.isSwitch ? toggle.implicitHeight : group.implicitHeight
    opacity: root.locked || root.inactive ? 0.45 : 1

    ToggleSwitch {
      id: toggle
      visible: root.isSwitch
      anchors.centerIn: parent
      checked: root.value === true
      // The row owns the click and the cursor (as in the shell's Toggle).
      interactive: false
      foreground: root.foreground
    }

    ButtonGroup {
      id: group
      visible: !root.isSwitch
      anchors.centerIn: parent
      options: root.chips
      value: root.value === undefined || root.value === null ? "" : String(root.value)
      focusable: false
      fontFamily: root.fontFamily
      fontSize: Style.font.bodySmall
      foreground: root.foreground
      spacing: Style.space(4)
      onChanged: function(v) {
        if (root.inactive) return
        root.chosen(root.row.type === "int" ? Number(v) : v)
      }
      onHovered: function(index, isHovered) { if (isHovered) root.hovered() }
    }
  }

  MouseArea {
    // The switch and the text: the whole row flips it. The chips take their own clicks.
    anchors.fill: parent
    anchors.rightMargin: root.isSwitch ? 0 : control.width + Style.space(8)
    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor
    onEntered: root.hovered()
    onClicked: root.stepped(1)
  }
}
