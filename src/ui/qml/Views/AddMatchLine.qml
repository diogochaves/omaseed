pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The add sheet's files match (docs/25 §3, #27): the `.torrent` being added has the same
// files, path for path and size for size, as a torrent already in the library, usually the
// same release from another tracker. Saved where those files are, qBittorrent checks them
// and seeds both swarms from one copy. Before `save there`: where the files are and whose
// they are (X1), or the copies to pick from (X3). After it: what will happen, and `undo` (X2).
//
// Hand-formatted (qmlformat rewrites typed function bodies).
//
//     AddMatchLine { matches: App's filesMatchFound list; onSaveRequested: index => … }
RowLayout {
    id: root

    /// `App.filesMatchFound`'s maps.
    property var matches: []
    /// The copy `save there` uses (X3: `j k`, or a click on its row).
    property int index: 0
    /// `save there` was pressed: the line says so and offers `undo`.
    property bool saved: false

    signal saveRequested(int index)
    signal undoRequested

    readonly property var current: root.matches.length > 0
                                   ? root.matches[Math.max(0, Math.min(root.index, root.matches.length - 1))]
                                   : null
    readonly property alias button: actionButton

    /// "seeding from tracker.a.example", or the state when it is not seeding.
    function whoText(match) {
        if (!match)
            return "";
        if (match.seeding)
            return match.tracker.length > 0 ? qsTr("seeding from %1").arg(match.tracker)
                                            : qsTr("seeding");
        return match.tracker.length > 0 ? qsTr("%1 · %2").arg(match.stateText).arg(match.tracker)
                                        : match.stateText;
    }

    readonly property string headText: {
        const match = root.current;
        if (!match)
            return "";
        if (root.saved)
            return match.fileCount === 1
                    ? qsTr("<b>Saving beside %1.</b> qBittorrent will check the file and start seeding.").arg(
                          match.name)
                    : qsTr("<b>Saving beside %1.</b> qBittorrent will check the %2 files and start seeding.").arg(
                          match.name).arg(match.fileCount);
        if (root.matches.length > 1)
            return qsTr("<b>These files are already on disk, %1 times.</b> Pick the copy to share:").arg(
                        root.matches.length);
        if (match.fileCount === 1)
            return qsTr("<b>This file is already on disk.</b> The same file, same size, as <b>%1</b>, %2.").arg(
                        match.name).arg(root.whoText(match));
        return qsTr("<b>These files are already on disk.</b> Same %1 files and sizes as <b>%2</b>, %3.").arg(
                    match.fileCount).arg(match.name).arg(root.whoText(match));
    }
    /// #79: the head's words for a screen reader, without the bold's markup.
    readonly property string headSpoken: root.headText.replace(/<[^>]*>/g, "")
    readonly property string subText: {
        if (!root.current)
            return "";
        if (root.saved)
            return qsTr("undo puts Save to back as it was");
        if (root.matches.length > 1)
            return "";
        return qsTr("%1 · nothing to download if it's saved there").arg(root.current.savePath);
    }

    spacing: Theme.space(8)
    visible: root.matches.length > 0

    Rectangle {
        Layout.fillHeight: true
        Layout.preferredWidth: 2
        color: Theme.accent
    }

    Label {
        Layout.alignment: Qt.AlignTop
        color: Theme.accent
        size: "small"
        text: root.saved ? Glyphs.check : Glyphs.diamond
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space(2)

        Label {
            Accessible.name: root.headSpoken
            Accessible.role: Accessible.StaticText
            objectName: "addMatchHead"
            Layout.fillWidth: true
            size: "small"
            text: root.headText
            textFormat: Text.StyledText
            wrapMode: Text.WordWrap
        }

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            dim: true
            elide: Text.ElideMiddle
            size: "caption"
            text: root.subText
            visible: text.length > 0
        }

        // X3: one row per copy, the chosen one marked.
        Repeater {
            model: !root.saved && root.matches.length > 1 ? root.matches : []

            delegate: Item {
                id: copyRow

                required property var modelData
                required property int index

                /// Make this the copy `save there` uses: a click, or a screen reader's press.
                function pick(): void {
                    root.index = copyRow.index;
                }

                // #77: a list item for each copy, named by where it is, selected while it is
                // the one `save there` uses.
                Accessible.description: root.whoText(copyRow.modelData) + " · " + copyRow.modelData.ratioText
                Accessible.name: copyRow.modelData.savePath
                Accessible.role: Accessible.ListItem
                Accessible.selectable: true
                Accessible.selected: copyRow.index === root.index
                Accessible.onPressAction: if (copyRow.enabled)
                                              copyRow.pick()
                Layout.fillWidth: true
                implicitHeight: copyLabel.implicitHeight
                objectName: "matchCopy_" + copyRow.index

                Label {
                    id: copyLabel

                    anchors.left: parent.left
                    anchors.right: parent.right
                    color: copyRow.index === root.index ? Theme.foreground : Theme.dimForeground
                    elide: Text.ElideMiddle
                    size: "caption"
                    text: (copyRow.index === root.index ? "› " : "  ") + copyRow.modelData.savePath
                          + " · " + root.whoText(copyRow.modelData) + " · " + copyRow.modelData.ratioText
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor

                    onClicked: copyRow.pick()
                }
            }
        }
    }

    Button {
        id: actionButton

        Layout.alignment: Qt.AlignVCenter
        bordered: true
        primary: true
        text: root.saved ? qsTr("undo") : qsTr("save there")

        onClicked: {
            if (root.saved)
                root.undoRequested();
            else
                root.saveRequested(root.index);
        }
        // X3: `j k` (or the arrows) pick the copy while the button has the keyboard.
        Keys.onPressed: event => {
            if (root.saved || root.matches.length < 2)
                return;
            if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
                root.index = Math.min(root.matches.length - 1, root.index + 1);
                event.accepted = true;
            } else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
                root.index = Math.max(0, root.index - 1);
                event.accepted = true;
            }
        }
    }
}
