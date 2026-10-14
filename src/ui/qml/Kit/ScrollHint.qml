import QtQuick
import Omaseed.Kit

// How a list says there is more above or below it (docs/08 §5, N3): one overlay laid over a
// scrolling list, in one of the three looks the Settings › Appearance "Scroll hint" row picks.
// The rail and every palette use it, so the setting reads the same everywhere.
//
//   "chevrons" (shown as "counted"): a band at each end that says how many rows are out of
//              view that way; a click on it scrolls most of a page towards them.
//   "fade":    the edge where there is more fades into `ground`.
//   "scrollbar": a 2 px track and thumb while `revealed`, while the pointer is over the list
//              (`revealOnHover`), and for a moment after the list moves. The pointer on its
//              strip at the right edge widens it; drag the thumb, or press the track to jump
//              the thumb there, as with `Kit/ScrollBar`.
//
// The caller counts the rows (`countAbove`, `countBelow`), since only it knows what a row is
// (the rail counts headings too). `topInset`/`bottomInset` say how much of the list a band
// covers (the rail hangs its sticky heading under the top one); `clearTop`/`clearBottom` say
// how far from each edge a cursor row should stay to be read, band or fade. A row the caller
// counts out of view can still straddle a band's inner edge; `topCover`/`bottomCover` stretch
// that band over the rest of it (#8: a sliver of it peeked under the band otherwise).
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
    /// Show the scrollbar while the pointer is anywhere over the list. The table turns this
    /// off: there the scrollbar shows only while the list moves or the pointer is on it.
    property bool revealOnHover: true
    /// How long the scrollbar stays after the list stops moving, in ms.
    property int lingerMs: 900
    /// What the bands are filled with and the fades fade into: the surface under the list.
    property color ground: Theme.background
    /// The height of a band.
    property real bandHeight: Math.round(Theme.railRowHeight * 0.8)
    /// How far a fade reaches into the list.
    property real fadeHeight: Theme.railRowHeight * 1.5
    /// How much further than `bandHeight` each band reaches, to hide all of a row that
    /// straddles its inner edge. The insets below leave it out: the cursor keeps clear of
    /// the band itself, and scrolling to it brings the covered row out whole.
    property real topCover: 0
    property real bottomCover: 0

    readonly property bool overflows: root.flickable !== null
                                      && root.flickable.contentHeight > root.flickable.height + 0.5
    readonly property real topInset: moreAbove.visible ? root.bandHeight : 0
    readonly property real bottomInset: moreBelow.visible ? root.bandHeight : 0
    /// How far from each edge a cursor row should stay to be read: under a band or a fade
    /// it is hidden or dimmed.
    readonly property real clearTop: root.topInset > 0 ? root.topInset : fadeAbove.visible
                                                                         ? root.fadeHeight : 0
    readonly property real clearBottom: root.bottomInset > 0 ? root.bottomInset : fadeBelow.visible
                                                                                ? root.fadeHeight : 0
    /// The pointer is over the list (for the scrollbar look).
    readonly property alias hovered: hover.hovered
    /// The scrollbar is wanted: revealed, hovered, just scrolled, or held.
    readonly property bool scrollbarShown: root.style === "scrollbar" && root.overflows
                                           && (root.revealed || (root.revealOnHover && hover.hovered)
                                               || linger.running || grip.containsMouse
                                               || grip.pressed)
    /// The scrollbar's track length, along which `scrollThumbTo()` measures.
    readonly property real trackLength: track.height

    /// Scroll so the scrollbar's thumb starts at `start` (along the track, from its top):
    /// what a drag on the thumb and a press on the track call.
    function scrollThumbTo(start: real): void {
        const f = root.flickable;
        if (f === null)
            return;
        const travel = track.height - thumb.height;
        const fraction = travel <= 0 ? 0 : Math.max(0, Math.min(1, start / travel));
        f.contentY = f.originY + fraction * Math.max(0, f.contentHeight - f.height);
    }

    /// Scroll `fraction` of a page: negative up, positive down, clamped to the content.
    function page(fraction: real): void {
        const f = root.flickable;
        if (f === null)
            return;
        const top = f.originY;
        const bottom = f.originY + Math.max(0, f.contentHeight - f.height);
        f.contentY = Math.max(top, Math.min(bottom, f.contentY + f.height * fraction));
    }

    // On the list, not on this overlay. An item that holds a HoverHandler takes the hover
    // events of the items under it (`blocking: false` does not change that), and the hint
    // lies over the whole list: its rows never heard the pointer come or go, and a row a
    // click had marked as under the pointer stayed hovered (docs/DECISIONS.md M11). The list
    // is the rows' ancestor, so its handler hears the pointer alongside them.
    HoverHandler {
        id: hover

        parent: root.flickable !== null ? root.flickable : root
    }

    // "chevrons" ------------------------------------------------------------------------
    Rectangle {
        id: moreAbove

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        color: root.ground
        height: root.bandHeight + Math.max(0, root.topCover)
        objectName: "moreAbove"
        visible: root.style === "chevrons" && root.countAbove > 0
        z: 1

        // In the band's own height, so a cover only lengthens the ground under it.
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            dim: true
            objectName: "moreAboveText"
            y: Math.round((root.bandHeight - height) / 2)
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
        height: root.bandHeight + Math.max(0, root.bottomCover)
        objectName: "moreBelow"
        visible: root.style === "chevrons" && root.countBelow > 0
        z: 1

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            dim: true
            objectName: "moreBelowText"
            y: parent.height - root.bandHeight + Math.round((root.bandHeight - height) / 2)
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
    // The list moved (the wheel, a flick, the keys, a drag here): show the bar a moment.
    Connections {
        function onContentYChanged(): void {
            if (root.style === "scrollbar" && root.overflows)
                linger.restart();
        }

        target: root.flickable
    }

    Timer {
        id: linger

        interval: root.lingerMs
    }

    Item {
        id: track

        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space(2)
        anchors.right: parent.right
        anchors.rightMargin: 1
        anchors.top: parent.top
        anchors.topMargin: Theme.space(2)
        objectName: "scrollbar"
        opacity: root.scrollbarShown ? 1 : 0
        // Shown at once, gone once it has faded.
        visible: root.style === "scrollbar" && root.overflows
                 && (root.scrollbarShown || track.opacity > 0)
        // Wider while the pointer is on it, so it reads as something to take hold of.
        width: grip.containsMouse || grip.pressed ? Theme.space(4) : 2

        Behavior on opacity {
            NumberAnimation {
                duration: 160
            }
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.fill(Theme.hairlineAlpha)
        }

        // The thumb, with `Kit/ScrollBar`'s maths (KIT-12): read from the geometry, since
        // `visibleArea` only catches up on the next viewport move, and moved over the travel
        // the thumb leaves, so it reaches the bottom exactly when the list does even while its
        // minimum length is in play.
        Rectangle {
            id: thumb

            readonly property real range: root.flickable === null ? 0
                    : root.flickable.contentHeight - root.flickable.height
            readonly property real ratio: root.flickable === null || root.flickable.contentHeight <= 0
                    ? 1 : Math.min(1, root.flickable.height / root.flickable.contentHeight)

            color: Theme.fill(Theme.borderIdle)
            height: Math.min(parent.height, Math.max(Theme.space(16), parent.height * thumb.ratio))
            objectName: "scrollThumb"
            width: parent.width
            y: thumb.range <= 0 ? 0 : Math.max(0, Math.min(parent.height - height,
                    (root.flickable.contentY - root.flickable.originY) / thumb.range
                    * (parent.height - height)))
        }
    }

    // The strip along the right edge that takes hold of the bar. It hears the pointer there
    // (to show and widen the bar), but takes a press only while the bar is shown, so a click
    // at a row's right edge still reaches the row when there is no bar to grab.
    MouseArea {
        id: grip

        property real grab: 0

        acceptedButtons: root.scrollbarShown ? Qt.LeftButton : Qt.NoButton
        anchors.bottom: track.bottom
        anchors.right: parent.right
        anchors.top: track.top
        enabled: root.style === "scrollbar" && root.overflows
        hoverEnabled: true
        objectName: "scrollGrip"
        preventStealing: true
        width: Theme.space(10)
        z: 2

        onPositionChanged: mouse => {
            if (grip.pressed)
                root.scrollThumbTo(mouse.y - grip.grab);
        }
        onPressed: mouse => {
            const onThumb = mouse.y >= thumb.y && mouse.y < thumb.y + thumb.height;
            // On the thumb: keep the point that was grabbed under the pointer. On the track:
            // centre the thumb on the pointer, then drag from there.
            grip.grab = onThumb ? mouse.y - thumb.y : thumb.height / 2;
            if (!onThumb)
                root.scrollThumbTo(mouse.y - grip.grab);
        }
    }
}
