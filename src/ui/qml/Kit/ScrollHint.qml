import QtQuick
import Omaseed.Kit

// How a list says there is more above or below it (docs/08 §5, N3): one overlay laid over a
// scrolling list, in one of the three looks the Settings › Appearance "Scroll hint" row picks.
// The rail and every palette use it, so the setting reads the same everywhere.
//
//   "chevrons" (shown as "counted"): a band at each end that says how many rows are out of
//              view that way; a click on it scrolls most of a page towards them.
//   "fade":    the edge where there is more fades into `ground`.
//   "scrollbar": a 2 px track and thumb while `revealed` or the pointer is over the list.
//
// The caller counts the rows (`countAbove`, `countBelow`), since only it knows what a row is
// (the rail counts headings too). `topInset`/`bottomInset` say how much of the list a band
// covers (the rail hangs its sticky heading under the top one); `clearTop`/`clearBottom` say
// how far from each edge a cursor row should stay to be read, band or fade.
Item {
    id: root

    /// The list this hint describes.
    property Flickable flickable: null
    /// "chevrons", "fade" or "scrollbar"; anything else draws nothing.
    property string style: "chevrons"
    /// Rows wholly out of view above and below.
    property int countAbove: 0
    property int countBelow: 0
    /// Show the scrollbar even without the pointer over the list (the list has focus).
    property bool revealed: false
    /// What the bands are filled with and the fades fade into: the surface under the list.
    property color ground: Theme.background
    /// The height of a band.
    property real bandHeight: Math.round(Theme.railRowHeight * 0.8)
    /// How far a fade reaches into the list.
    property real fadeHeight: Theme.railRowHeight * 1.5

    readonly property bool overflows: root.flickable !== null
                                      && root.flickable.contentHeight > root.flickable.height + 0.5
    readonly property real topInset: moreAbove.visible ? moreAbove.height : 0
    readonly property real bottomInset: moreBelow.visible ? moreBelow.height : 0
    /// How far from each edge a cursor row should stay to be read: under a band or a fade
    /// it is hidden or dimmed.
    readonly property real clearTop: root.topInset > 0 ? root.topInset : fadeAbove.visible
                                                                         ? root.fadeHeight : 0
    readonly property real clearBottom: root.bottomInset > 0 ? root.bottomInset : fadeBelow.visible
                                                                                ? root.fadeHeight : 0
    /// The pointer is over the list (for the scrollbar look).
    readonly property alias hovered: hover.hovered

    /// Scroll `fraction` of a page: negative up, positive down, clamped to the content.
    function page(fraction: real): void {
        const f = root.flickable;
        if (f === null)
            return;
        const top = f.originY;
        const bottom = f.originY + Math.max(0, f.contentHeight - f.height);
        f.contentY = Math.max(top, Math.min(bottom, f.contentY + f.height * fraction));
    }

    HoverHandler {
        id: hover
    }

    // "chevrons" ------------------------------------------------------------------------
    Rectangle {
        id: moreAbove

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        color: root.ground
        height: root.bandHeight
        objectName: "moreAbove"
        visible: root.style === "chevrons" && root.countAbove > 0
        z: 1

        Label {
            anchors.centerIn: parent
            dim: true
            objectName: "moreAboveText"
            size: "caption"
            text: qsTr("%1 %2 above").arg(Glyphs.chevronUp).arg(root.countAbove)
        }

        Hairline {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor

            onClicked: root.page(-0.8)
        }
    }

    Rectangle {
        id: moreBelow

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        color: root.ground
        height: root.bandHeight
        objectName: "moreBelow"
        visible: root.style === "chevrons" && root.countBelow > 0
        z: 1

        Label {
            anchors.centerIn: parent
            dim: true
            objectName: "moreBelowText"
            size: "caption"
            text: qsTr("%1 %2 below").arg(Glyphs.chevronDown).arg(root.countBelow)
        }

        Hairline {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor

            onClicked: root.page(0.8)
        }
    }

    // "fade" ----------------------------------------------------------------------------
    Rectangle {
        id: fadeAbove

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.fadeHeight
        objectName: "fadeAbove"
        visible: root.style === "fade" && root.countAbove > 0

        gradient: Gradient {
            GradientStop {
                color: root.ground
                position: 0
            }
            GradientStop {
                color: "transparent"
                position: 1
            }
        }
    }

    Rectangle {
        id: fadeBelow

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.fadeHeight
        objectName: "fadeBelow"
        visible: root.style === "fade" && root.countBelow > 0

        gradient: Gradient {
            GradientStop {
                color: "transparent"
                position: 0
            }
            GradientStop {
                color: root.ground
                position: 1
            }
        }
    }

    // "scrollbar" -----------------------------------------------------------------------
    Item {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space(2)
        anchors.right: parent.right
        anchors.rightMargin: 1
        anchors.top: parent.top
        anchors.topMargin: Theme.space(2)
        objectName: "scrollbar"
        visible: root.style === "scrollbar" && root.overflows && (root.revealed || hover.hovered)
        width: 2

        Rectangle {
            anchors.fill: parent
            color: Theme.fill(Theme.hairlineAlpha)
        }

        Rectangle {
            color: Theme.fill(Theme.borderIdle)
            height: root.flickable === null ? 0 : Math.max(Theme.space(16), parent.height
                                                           * root.flickable.visibleArea.heightRatio)
            width: parent.width
            y: root.flickable === null ? 0 : Math.max(0, Math.min(parent.height - height,
                                                                  root.flickable.visibleArea.yPosition
                                                                  * parent.height))
        }
    }
}
