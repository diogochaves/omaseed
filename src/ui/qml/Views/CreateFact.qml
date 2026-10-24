pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// One fact of the private create sheet (board 49 `.chk`, DECISIONS C27 CR6): a mark in its
// colour, the fact bright, a dim line under it, and an accent link at the right ("change",
// "type one"). Kinds: "ok" green ✓, "info" dim i, "warn" yellow !, "bad" red ✕.
//
//     CreateFact { kind: "ok"; title: qsTr("Private flag on"); detail: qsTr("no DHT…") }
RowLayout {
    id: root

    property string kind: "ok"
    property string title: ""
    property string detail: ""
    /// The link's words; "" draws none.
    property string link: ""

    signal linkClicked

    spacing: Theme.createFactGap

    Label {
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: Theme.createFactMark
        color: root.kind === "ok" ? Theme.green : root.kind === "warn" ? Theme.yellow : root.kind
                                                                         === "bad" ? Theme.red :
                                                                                     Theme.dimForeground
        horizontalAlignment: Text.AlignHCenter
        objectName: "createFactMark"
        size: "small"
        text: root.kind === "ok" ? Glyphs.factOk : root.kind === "warn" ? Glyphs.factWarn :
                                                                          root.kind === "bad"
                                                                          ? Glyphs.close :
                                                                            Glyphs.factInfo
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        Label {
            Accessible.name: root.detail.length > 0 ? root.title + ". " + root.detail : root.title
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            bright: true
            objectName: "createFactTitle"
            size: "small"
            text: root.title
            wrapMode: Text.Wrap
        }

        Label {
            Accessible.ignored: true
            Layout.fillWidth: true
            dim: true
            objectName: "createFactDetail"
            size: "caption"
            text: root.detail
            visible: text.length > 0
            wrapMode: Text.Wrap
        }
    }

    Label {
        Accessible.name: root.link
        Accessible.role: Accessible.Button
        Accessible.onPressAction: root.linkClicked()
        Layout.alignment: Qt.AlignTop
        color: Theme.accent
        objectName: "createFactLink"
        size: "caption"
        text: root.link
        visible: text.length > 0

        MouseArea {
            anchors.fill: parent
            anchors.margins: -Theme.createLinkReach
            cursorShape: Qt.PointingHandCursor

            onClicked: root.linkClicked()
        }
    }
}
