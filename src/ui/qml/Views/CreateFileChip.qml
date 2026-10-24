pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The private torrent's saved file, in its band (board 49 `fileChip()`, DECISIONS C27 CR14): its
// name and "SIZE · drag to the upload form", a 1 px green edge, and a drag that carries the
// file as text/uri-list, so it drops straight into a browser's upload form. Its form is
// `Theme.createChip*` tokens.
//
//     CreateFileChip { path: "/home/me/Downloads/x [tracker].torrent"; sizeText: "125 KB" }
Rectangle {
    id: root

    /// The saved .torrent, on this machine.
    property string path: ""
    property string name: ""
    property string sizeText: ""

    readonly property url fileUrl: "file://" + encodeURI(root.path)

    Accessible.description: qsTr("drag it to the site's upload form")
    Accessible.name: root.name
    Accessible.role: Accessible.Graphic
    Drag.active: drag.active
    Drag.dragType: Drag.Automatic
    Drag.mimeData: {
        "text/uri-list": root.fileUrl.toString(),
        "text/plain": root.path
    }
    Drag.supportedActions: Qt.CopyAction
    border.color: Theme.green
    border.width: Theme.createChipEdge
    color: Theme.background
    implicitHeight: row.implicitHeight + 2 * Theme.createChipPaddingY
    implicitWidth: row.implicitWidth + 2 * Theme.createChipPaddingX
    objectName: "createFileChip"
    radius: Theme.createChipRadius

    RowLayout {
        id: row

        anchors.left: parent.left
        anchors.leftMargin: Theme.createChipPaddingX
        anchors.right: parent.right
        anchors.rightMargin: Theme.createChipPaddingX
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.createChipGap

        Label {
            Accessible.ignored: true
            dim: true
            size: "small"
            text: Glyphs.dragHandle
        }

        Label {
            Accessible.ignored: true
            Layout.fillWidth: true
            bright: true
            elide: Text.ElideMiddle
            objectName: "createFileChipName"
            size: "small"
            text: root.name
        }

        Label {
            Accessible.ignored: true
            dim: true
            size: "caption"
            text: qsTr("%1 · drag to the upload form").arg(root.sizeText)
        }
    }

    DragHandler {
        id: drag

        cursorShape: Qt.ClosedHandCursor
        target: null
    }

    HoverHandler {
        cursorShape: Qt.OpenHandCursor
    }
}
