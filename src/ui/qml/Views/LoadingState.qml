pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The first-sync loading state (spec v2 §7 Fig. 25): four skeleton rules 2 px tall in
// `Theme.fill(Theme.fillHover)`, widths 100/80/90/70 % of the component, `Theme.space(8)`
// gaps, then the note `<n> torrents · <received> received` (small dim). `animate` (default
// true) pulses the rules' opacity between 0.6 and 1 over 1200 ms; tests set it false to keep
// the rendered state still.
//
// The pulse only runs while the state is actually on screen. An infinite animation in Qt
// Quick keeps the render thread awake whether or not its item is visible, and this component
// is a plain child of the main view (alive, `visible: false`, for the whole session once the
// first sync has landed): left running it cost ~3 % of a core, forever, on an idle window.
Item {
    id: root

    property bool animate: true
    property string receivedText: "0 B"
    property int torrentCount: 0

    readonly property string note: qsTr("%1 torrents · %2 received").arg(root.torrentCount).arg(
                                       root.receivedText)
    readonly property var _widths: [1, 0.8, 0.9, 0.7]

    implicitHeight: column.implicitHeight
    implicitWidth: Theme.space(360)

    Column {
        id: column

        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.space(8)

        Repeater {
            model: root._widths

            Rectangle {
                id: rule

                required property real modelData

                /// The pulse, exposed so a test can assert it stops when the state hides.
                readonly property alias pulse: pulse

                // Named so tst_States.qml can pick the four skeleton rules out of the
                // Column's children without depending on QML's object-toString format.
                objectName: "skeletonRule"

                color: Theme.fill(Theme.fillHover)
                height: Theme.space(2)
                opacity: 1
                width: parent.width * rule.modelData

                SequentialAnimation on opacity {
                    id: pulse

                    running: root.animate && root.visible
                    loops: Animation.Infinite

                    NumberAnimation {
                        duration: 1200
                        from: 1
                        to: 0.6
                    }
                    NumberAnimation {
                        duration: 1200
                        from: 0.6
                        to: 1
                    }
                }
            }
        }

        // #79: the note counts up while the first sync lands, so it is the description of a
        // label named for what is happening, not a name read out again at every step.
        Text {
            Accessible.description: root.note
            Accessible.name: qsTr("first sync")
            Accessible.role: Accessible.StaticText
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "loadingNote"
            text: root.note
            topPadding: Theme.space(4)
        }
    }
}
