pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// One line of the move sheet's preflight (board 42 S1, DECISIONS C15 M5–M9): its sign in the
// line's colour (green fine, yellow know it, red left out, dim a fact), the words, a dim note
// under them, and at the right a link: "show" when the line names torrents, or the caller's
// (`linkText`, "list ▸").
RowLayout {
    id: root

    /// {kind, glyph, text, note, hashes} from App.move.plan().
    property var line: ({})
    property color tone: Theme.dimForeground
    /// The link's words; "" with torrents named draws "show".
    property string linkText: ""

    readonly property var hashes: root.line.hashes ?? []
    readonly property string shownLink: root.linkText.length > 0 ? root.linkText :
                                                                   root.hashes.length > 0 ? qsTr(
                                                                                                "show") : ""

    /// "show": the table lists the line's torrents.
    signal showRequested(var hashes)
    /// The caller's link.
    signal linkActivated

    spacing: Theme.space(8)

    Label {
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: Theme.space(12)
        color: root.tone
        horizontalAlignment: Text.AlignHCenter
        objectName: "moveCheckGlyph"
        size: "small"
        text: root.line.glyph ?? ""
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space(1)

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            objectName: "moveCheckText"
            size: "small"
            text: root.line.text ?? ""
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            dim: true
            size: "caption"
            text: root.line.note ?? ""
            visible: text.length > 0
            wrapMode: Text.Wrap
        }
    }

    Text {
        id: link

        Accessible.name: link.text
        Accessible.role: Accessible.Link
        Accessible.onPressAction: link.activate()
        Layout.alignment: Qt.AlignTop
        color: linkMouse.containsMouse ? Theme.brightForeground : Theme.accent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        objectName: "moveCheckLink"
        text: root.shownLink
        visible: text.length > 0

        function activate(): void {
        if (root.linkText.length > 0)
        root.linkActivated();
        else
        root.showRequested(root.hashes);
    }

        MouseArea {
            id: linkMouse

            anchors.fill: parent
            anchors.margins: -Theme.space(4)
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            onClicked: link.activate()
        }
    }
}
