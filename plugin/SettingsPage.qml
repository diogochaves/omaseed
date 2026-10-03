import QtQuick
import qs.Commons
import qs.Ui
import "Status.js" as Status

// The widget's own settings (docs/21, Q11 revised 2026-10-03): the manifest's nine in
// three groups, then a row for omaseed's own Settings. The popup owns the cursor, the
// keys and saving; this draws the rows and reports clicks.
Column {
  id: root

  property var cfg: Status.barOptions({})
  // Index into Status.settingRows(); rows.length is the foot row (omaseed settings).
  property int cursorIndex: 0
  property string note: ""
  property color foreground: Color.foreground
  property color dim: Qt.darker(foreground, 1.55)
  property color warn: Color.foreground
  property string fontFamily: Style.font.family

  readonly property var rows: Status.settingRows()
  readonly property var groups: {
    var out = []
    for (var i = 0; i < rows.length; i++)
      if (out.indexOf(rows[i].group) < 0) out.push(rows[i].group)
    return out
  }

  signal stepped(int index, int dir)
  signal chosen(int index, var value)
  signal hoveredRow(int index)
  signal appSettings()
  // The row under the cursor, for the popup to scroll into view.
  signal cursorItem(var item)

  width: parent ? parent.width : implicitWidth
  spacing: Style.space(12)

  Repeater {
    model: root.groups

    delegate: Column {
      id: groupBlock
      required property string modelData
      width: root.width
      spacing: Style.space(4)

      PanelSectionHeader {
        x: Style.space(6)
        text: groupBlock.modelData.toUpperCase()
        foreground: root.foreground
        fontFamily: root.fontFamily
        font.letterSpacing: Style.space(1)
      }

      Repeater {
        model: root.rows.length

        delegate: SettingRow {
          id: settingRow
          required property int index
          readonly property var rowData: root.rows[index]
          visible: rowData.group === groupBlock.modelData
          height: visible ? implicitHeight : 0
          row: rowData
          value: root.cfg[rowData.key]
          detail: Status.settingDetail(rowData, root.cfg)
          locked: Status.settingLocked(rowData, root.cfg)
          inactive: Status.settingInactive(rowData, root.cfg)
          hasCursor: root.cursorIndex === index
          foreground: root.foreground
          dim: root.dim
          warn: root.warn
          fontFamily: root.fontFamily
          onStepped: function(dir) { root.stepped(index, dir) }
          onChosen: function(v) { root.chosen(index, v) }
          onHovered: root.hoveredRow(index)
          onHasCursorChanged: if (hasCursor) root.cursorItem(settingRow)
        }
      }
    }
  }

  Text {
    width: parent.width
    height: Style.font.caption * 1.5
    leftPadding: Style.space(8)
    textFormat: Text.PlainText
    text: root.note
    color: root.dim
    font.family: root.fontFamily
    font.pixelSize: Style.font.caption
    elide: Text.ElideRight
  }

  PanelSeparator { width: parent.width; foreground: root.foreground }

  CursorSurface {
    id: appRow
    width: parent.width
    height: Math.max(Style.spacing.popupRowHeight, appRowContent.implicitHeight + Style.space(8))
    hasCursor: root.cursorIndex === root.rows.length
    foreground: root.foreground
    onHasCursorChanged: if (hasCursor) root.cursorItem(appRow)

    Row {
      id: appRowContent
      anchors.left: parent.left
      anchors.leftMargin: Style.space(8)
      anchors.right: parent.right
      anchors.rightMargin: Style.space(8)
      anchors.verticalCenter: parent.verticalCenter
      spacing: Style.space(10)

      LineIcon {
        anchors.verticalCenter: parent.verticalCenter
        name: "gear"
        size: Style.font.body
        color: root.dim
      }

      Text {
        id: appRowTitle
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: "omaseed settings"
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.body
      }

      Text {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, parent.width - appRowTitle.width - Style.font.body - parent.spacing * 2)
        horizontalAlignment: Text.AlignRight
        textFormat: Text.PlainText
        text: "servers and more ↗"
        color: root.dim
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        elide: Text.ElideLeft
      }
    }

    MouseArea {
      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor
      onEntered: root.hoveredRow(root.rows.length)
      onClicked: root.appSettings()
    }
  }
}
