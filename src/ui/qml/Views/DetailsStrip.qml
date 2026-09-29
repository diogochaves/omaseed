import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The folded details pane (docs/17 M10.1, the board's `.dstrip`): one line where the pane
// was, under the divider SplitLayout draws. ▴, the cursor torrent's name, its progress,
// speeds and ETA, and the way back at the right. Everything comes from the table's own row
// (`App.torrentRow`), so a folded pane costs no details polls. A click on ▴ or a
// double-click anywhere asks to unfold; dragging the divider above it unfolds too
// (SplitLayout's side of it).
//
//     DetailsStrip { torrent: App.torrentRow(hash); onUnfoldRequested: split.toggleFold() }
Item {
    id: root

    /// `App.torrentRow()`'s map for the cursor torrent, or null / {} when there is none.
    property var torrent: null
    /// Narrow: the hint drops "or double-click".
    readonly property bool compact: root.width < Theme.space(760)
    readonly property bool hasTorrent: root.torrent !== null && root.torrent !== undefined
                                       && root.torrent.hash !== undefined
    /// "87.2 % · ↓ 940 KB/s · ↑ 220 KB/s · 7m 05s", the ETA only when there is one.
    readonly property string statsText: {
        if (!root.hasTorrent)
            return "";
        const parts = [root.torrent.progressText, Glyphs.arrowDown + " " + root.torrent.dlspeedText,
                       Glyphs.arrowUp + " " + root.torrent.upspeedText];
        if (root.torrent.etaText !== undefined && root.torrent.etaText !== "—")
            parts.push(root.torrent.etaText);
        return parts.join(" · ");
    }

    signal unfoldRequested

    implicitHeight: Math.round(Theme.font.baseSize * 2.2)

    // One area for the whole line: a double-click anywhere unfolds, a click only on ▴ (and a
    // little around it). The ▴ is a Label in the row, so the hand shows over it alone.
    MouseArea {
        id: stripMouse

        /// The pointer is on ▴, give or take `Theme.space(6)`.
        readonly property bool overChevron: {
            const at = chevron.mapToItem(stripMouse, 0, 0);
            return stripMouse.containsMouse && stripMouse.mouseX >= at.x - Theme.space(6)
                    && stripMouse.mouseX <= at.x + chevron.width + Theme.space(6);
        }

        anchors.fill: parent
        cursorShape: stripMouse.overChevron ? Qt.PointingHandCursor : Qt.ArrowCursor
        hoverEnabled: true
        objectName: "stripMouse"

        onClicked: {
            if (stripMouse.overChevron)
                root.unfoldRequested();
        }
        onDoubleClicked: root.unfoldRequested()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space(14)
        anchors.rightMargin: Theme.space(14)
        spacing: Theme.space(10)

        Label {
            id: chevron

            color: Theme.accent
            objectName: "stripChevron"
            size: "small"
            text: Glyphs.chevronUp
        }

        Label {
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            Layout.minimumWidth: 0
            bright: root.hasTorrent
            dim: !root.hasTorrent
            elide: Text.ElideRight
            font.weight: Font.Medium
            objectName: "stripName"
            size: "small"
            text: root.hasTorrent ? root.torrent.name : qsTr("no torrent selected")
        }

        Label {
            dim: true
            objectName: "stripStats"
            size: "small"
            text: root.statsText
            visible: root.statsText.length > 0
        }

        Item {
            Layout.fillWidth: true
        }

        Row {
            spacing: Theme.space(4)

            Label {
                bright: true
                size: "small"
                text: "i"
            }

            Label {
                dim: true
                objectName: "stripHint"
                size: "small"
                text: root.compact ? qsTr("details") : qsTr("or double-click · details")
            }
        }
    }
}
