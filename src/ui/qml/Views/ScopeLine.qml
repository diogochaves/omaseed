pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// docs/34 D1 a, D2 a (#70): the line at the top of a Backup pane that says what the pane acts
// on and what it leaves to the other one. The Kit's info banner (`Banner` kind "info": an
// accent rule at the left, the `i` glyph in accent) on the idle fill, wrapped to the pane's
// width, its text ending in an accent link with its key in dim before it ("t qBittorrent ›
// Backup ›", "s switch server"). A click on the link sends `linkActivated`; the key itself
// belongs to the pane.
//
//     ScopeLine { text: qsTr("omaseed's own: …"); linkKey: "t"; linkText: qsTr("…"); onLinkActivated: … }
Rectangle {
    id: root

    /// What the pane holds, in plain text.
    property string text: ""
    /// The link's key, printed dim before it; empty for none.
    property string linkKey: ""
    /// The link's words; empty draws no link.
    property string linkText: ""

    /// The link was clicked.
    signal linkActivated

    /// Plain text made safe for StyledText.
    function _escaped(text: string): string {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    color: Theme.fill(Theme.fillIdle)
    implicitHeight: layout.implicitHeight + 2 * Theme.space(7)
    radius: Theme.cornerRadius

    // The banner's rule.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        color: Theme.accent
        width: 1
    }

    RowLayout {
        id: layout

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(12)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(12)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(8)

        Label {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: Theme.space(10)
            color: Theme.accent
            size: "small"
            text: Glyphs.about
        }

        Label {
            id: line

            Layout.fillWidth: true
            lineHeight: 1.25
            objectName: "scopeText"
            size: "small"
            text: {
                const body = root._escaped(root.text);
                if (root.linkText.length === 0)
                    return body;
                const key = root.linkKey.length > 0
                        ? "<font color='%1'>%2</font> ".arg(Theme.dimForeground).arg(root._escaped(root.linkKey))
                        : "";
                return body + " " + key + "<a href='scope'><font color='%1'>%2</font></a>".arg(Theme.accent).arg(
                            root._escaped(root.linkText));
            }
            textFormat: Text.StyledText
            wrapMode: Text.WordWrap

            onLinkActivated: root.linkActivated()

            HoverHandler {
                cursorShape: line.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
            }
        }
    }
}
