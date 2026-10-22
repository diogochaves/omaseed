import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "Status.js" as Status

// The app's shared menu, drawn as it comes (docs/33 §11, W1 and W2; DECISIONS C13): the same
// rows the tray shows, from status.json's `menu`, so a row the app adds shows here without the
// widget learning it. A caption is an UPPERCASE tracked line, a separator a hairline, a radio
// row (a server) a ring that fills for the one the app is on, a check row a box, and each row
// says at its right what the app wrote (a server's figure, urgent for a failure) or the
// popup's own key for it.
//
// The panel owns the cursor (`cursorIndex`, an index into `rows`); hovering a row moves it.
Column {
  id: root

  property var rows: []
  property int cursorIndex: -1
  property bool readOnly: false
  property color foreground: Commons.Color.foreground
  property color dim: Qt.darker(foreground, 1.55)
  property color urgent: Commons.Color.urgent
  property color accent: Commons.Color.accent
  property string fontFamily: Style.font.family

  signal picked(var row)
  signal hoveredRow(int index)
  signal cursorItem(var item)

  width: parent ? parent.width : implicitWidth
  spacing: Style.space(2)

  Repeater {
    model: root.rows

    delegate: Item {
      id: entry

      required property var modelData
      required property int index

      readonly property string kind: String(modelData.kind || "action")
      readonly property bool live: modelData.enabled !== false && !root.readOnly
      readonly property string aside: String(modelData.detail || "") !== "" ? String(modelData.detail)
                                      : Status.menuKey(modelData.id)

      // A click and a screen reader's press both come here; a row that can't run takes neither.
      function pick() {
        if (entry.live) root.picked(entry.modelData)
      }

      width: root.width
      height: kind === "separator" ? Style.space(9)
              : (kind === "caption" ? captionText.implicitHeight + Style.space(8) : Style.spacing.popupRowHeight)

      // A hairline at 12 %.
      Rectangle {
        visible: entry.kind === "separator"
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 1
        color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.12)
      }

      Text {
        id: captionText
        visible: entry.kind === "caption"
        anchors.left: parent.left
        anchors.leftMargin: Style.space(8)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Style.space(2)
        textFormat: Text.PlainText
        text: String(entry.modelData.label || "").toUpperCase()
        color: root.dim
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        font.letterSpacing: Style.space(1)
        // As written, not in the capitals it is drawn in (#80).
        Accessible.role: Accessible.StaticText
        Accessible.name: String(entry.modelData.label || "")
      }

      CursorSurface {
        id: surface
        visible: entry.kind !== "separator" && entry.kind !== "caption"
        anchors.fill: parent
        hasCursor: visible && root.cursorIndex === entry.index
        foreground: root.foreground
        opacity: entry.live ? 1 : 0.45
        onHasCursorChanged: if (hasCursor) root.cursorItem(entry)

        // A screen reader (#80): a menu item named by its label, a server or a check with its
        // state, what the app wrote at its right (or the popup's key) as the description. Not
        // `accessibleName` (an Omarchy without PR #13212 would fail to load the widget);
        // `ignored`: with #13212 the kit leaves an unnamed CursorSurface out. The kit presses
        // no CursorSurface, so no guard.
        Accessible.role: Accessible.MenuItem
        Accessible.name: String(entry.modelData.label || "")
        Accessible.description: String(entry.modelData.detail || "") !== "" ? String(entry.modelData.detail)
                                : (entry.aside !== "" ? "shortcut " + entry.aside : "")
        Accessible.checkable: entry.kind === "radio" || entry.kind === "check"
        Accessible.checked: entry.modelData.checked === true
        Accessible.ignored: false
        // a11y-ignore A9: a CursorSurface; #13212's kit presses no row, so there is no second press
        Accessible.onPressAction: entry.pick()
        // a11y-ignore A9: as the press: the kit has no toggle for a row
        Accessible.onToggleAction: entry.pick()

        Item {
          id: lead
          anchors.left: parent.left
          anchors.leftMargin: Style.space(8)
          anchors.verticalCenter: parent.verticalCenter
          width: Style.font.body
          height: Style.font.body

          // A server: a ring, filled for the one the app is on.
          Rectangle {
            visible: entry.kind === "radio"
            anchors.centerIn: parent
            width: Math.round(parent.width * 0.62)
            height: width
            radius: width / 2
            color: entry.modelData.checked === true ? root.accent : "transparent"
            border.width: entry.modelData.checked === true ? 0 : 1
            border.color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.4)
          }

          // A check: the Add rows' box (ListRow's tick).
          Rectangle {
            visible: entry.kind === "check"
            anchors.fill: parent
            anchors.margins: Style.space(1)
            color: entry.modelData.checked === true ? root.accent : "transparent"
            border.width: entry.modelData.checked === true ? 0 : 1
            border.color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.4)
          }
        }

        Text {
          anchors.left: lead.right
          anchors.leftMargin: Style.space(10)
          anchors.right: rightText.left
          anchors.rightMargin: Style.space(10)
          anchors.verticalCenter: parent.verticalCenter
          textFormat: Text.PlainText
          text: String(entry.modelData.label || "")
          color: root.foreground
          font.family: root.fontFamily
          font.pixelSize: Style.font.body
          elide: Text.ElideRight
        }

        Text {
          id: rightText
          anchors.right: parent.right
          anchors.rightMargin: Style.space(8)
          anchors.verticalCenter: parent.verticalCenter
          width: Math.min(implicitWidth, parent.width * 0.5)
          horizontalAlignment: Text.AlignRight
          textFormat: Text.PlainText
          text: entry.aside
          color: entry.modelData.urgent === true ? root.urgent : root.dim
          font.family: root.fontFamily
          font.pixelSize: Style.font.bodySmall
          elide: Text.ElideLeft
        }

        MouseArea {
          anchors.fill: parent
          hoverEnabled: true
          enabled: entry.live
          cursorShape: Qt.PointingHandCursor
          onEntered: root.hoveredRow(entry.index)
          onClicked: entry.pick()
        }
      }
    }
  }
}
