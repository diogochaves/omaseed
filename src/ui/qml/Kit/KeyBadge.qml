import QtQuick
import Omaseed.Kit

// A key taught on demand (board 46 `.kb`, DECISIONS C24 H10): while alt is held alone the
// window's controls show their keys in these, the accent's ground with the window's ground
// for the key. Put one inside the control it names: by default it hangs over the control's
// top-right corner, outside any layout, so the control keeps its size (the action bar folds
// its buttons by their widths). With `corner` false it sets no place of its own and the
// parent anchors it (a rail row's right end).
//
// Silent for a screen reader: the control's description already says "shortcut KEY" (#78).
// Its form is `Theme.keyBadge*` tokens (frame plan, "form in tokens").
//
//     Button { …; KeyBadge { key: "a"; shown: root.keysShown } }
Rectangle {
    id: root

    /// The key as the `?` sheet writes it ("a", "shift+m", "ctrl+.").
    property string key: ""
    /// The window's `keysShown`: alt is held.
    property bool shown: false
    /// Hang over the parent's top-right corner; false, the parent places it.
    property bool corner: true

    readonly property alias label: keyText

    Accessible.ignored: true
    anchors.right: root.corner && root.parent ? root.parent.right : undefined
    anchors.rightMargin: root.corner ? -Theme.keyBadgeOverhangX : 0
    anchors.top: root.corner && root.parent ? root.parent.top : undefined
    anchors.topMargin: root.corner ? -Theme.keyBadgeOverhangY : 0
    color: Theme.accent
    implicitHeight: Theme.keyBadgeHeight
    implicitWidth: Math.max(Theme.keyBadgeHeight, Math.ceil(keyText.implicitWidth) + 2
                            * Theme.keyBadgePadding)

    objectName: "keyBadge"
    radius: Theme.keyBadgeRadius
    visible: root.shown && root.key.length > 0
    z: 3

    Text {
        id: keyText

        anchors.centerIn: parent
        color: Theme.background
        font.family: Theme.fontFamily
        font.pixelSize: Theme.keyBadgeFontSize
        font.weight: Theme.keyBadgeWeight
        text: root.key
    }
}
