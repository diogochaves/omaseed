import QtQuick
import Omaseed.Kit

// One text cell of a details list (Files, Trackers, Peers): the list's small type, eliding,
// and the full text in a hover label when the cell had to cut it (docs/11 J1, as the table's
// names do since B1.8).
//
// Names that end in something meaningful elide in the middle (`Text.ElideMiddle`), so a file
// keeps its extension and a tracker its TLD: `bbb_sunf…1080p_30fps.mp4`. `tipText` is what
// the label shows, the cell's own text unless the row has a longer form (a tracker's full
// announce URL); `alwaysTip` shows it even when nothing was cut, for exactly that case.
Text {
    id: root

    /// The list's `Kit/Tooltip`; null turns the hover label off.
    property Tooltip tip: null
    property string tipText: root.text
    property bool alwaysTip: false

    readonly property bool _wantsTip: root.tip !== null && root.tipText.length > 0
                                      && (root.truncated || (root.alwaysTip
                                                             && root.tipText !== root.text))

    color: Theme.foreground
    elide: Text.ElideRight
    font.family: Theme.fontFamily
    font.pixelSize: Theme.font.small

    HoverHandler {
        id: hover

        onHoveredChanged: {
            if (!hover.hovered && root.tip !== null)
                root.tip.hide();
        }
        onPointChanged: {
            if (!hover.hovered || root.tip === null)
                return;
            if (!root._wantsTip) {
                root.tip.hide();
                return;
            }
            const at = root.mapToItem(root.tip, hover.point.position.x, hover.point.position.y);
            root.tip.showAt(at.x, at.y, root.tipText);
        }
    }
}
