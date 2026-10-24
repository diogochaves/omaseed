pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// qBittorrent's log, the window's third face (docs/32 §6, D9–D15, #56). It takes the body's
// whole width, since the rail and the details are about torrents (L1): a bar with the level
// chips, a torrent chip when the lines are narrowed to one torrent, the `/` filter and where
// the log starts; then the lines, oldest at the top and newest at the bottom.
//
// It follows new lines while the view is at the bottom; scrolling up pauses, and new lines are
// counted in a pill over the bottom edge instead of pulling the view down (D12). `G` goes to the
// newest line and follows again, `f` turns following on or off.
//
// Its keys are its own (Keymap's "log" area): `1`–`4` levels, `/` filter, `j` `k` `g` `G`,
// `w` `W` the next or previous warning, `f`, `space` `v` `ctrl+a` marks, `y` copy, `enter` the
// line's torrent in the library, `t` only that torrent's lines, `esc` clears the marks, then the
// torrent, then the filter, then goes back. What it doesn't take (`~`, `?`, `:`) reaches the
// window.
FocusScope {
    id: root

    required property LogFilterModel lines
    required property ServerLog log
    /// D15: the folder qBittorrent writes its log file in, on its own disk; "" when it writes
    /// none. Named, never read.
    property string logFilePath: ""
    /// The server answers no more: the lines stay, nothing new comes (Fig. 23).
    property bool serverLost: false

    /// The cursor's line, -1 with no line.
    property int cursor: -1
    /// The range anchor of `v` and shift+click, a log id.
    property int anchorId: -1
    /// D12: new lines pull the view down.
    property bool following: true
    /// New lines shown since following paused.
    property int newLines: 0

    readonly property alias list: list
    readonly property alias filterField: filterField
    readonly property bool filterEditing: filterField.editing
    /// The cursor line's message, for the command palette's second line.
    readonly property string cursorMessage: {
        root.lines.count; // re-read as lines come and go
        return root.lines.messageAt(root.cursor);
    }

    /// `enter`: show the torrent in the library, General open.
    signal showTorrent(string hash)
    /// `esc` with nothing left to clear: back to where the window was.
    signal leaveRequested
    /// `y`: put `text` on the clipboard and say `note`.
    signal copyRequested(string text, string note)
    /// A short word for the footer: "this line names no torrent".
    signal noteRequested(string text)
    /// Board 45 B2 (C18 D7): "move back…" on one of omaseed's move lines.
    signal moveBackRequested(string id)

    readonly property real timeWidth: Theme.space(118)
    readonly property real levelWidth: Theme.space(84)

    /// The bar's right-hand words: "N of M lines" while words or a torrent narrow them, else
    /// where the log starts (D15).
    readonly property string sinceText: {
        if (!root.log.loaded && root.log.total === 0)
            return root.serverLost ? qsTr("no lines read") : qsTr("reading qBittorrent's log…");
        const total = root.lines.levelTotal;
        if (root.lines.narrowed)
            return total === 1 ? qsTr("%1 of 1 line").arg(root.lines.count) : qsTr("%1 of %2 lines").arg(root.lines.count).arg(total);
        const count = total === 1 ? qsTr("1 line") : qsTr("%1 lines").arg(total);
        if (root.log.total === 0)
            return count;
        return root.log.fromStart ? qsTr("since qBittorrent started · %1 · %2").arg(root.log.firstAtText).arg(count)
                                  : qsTr("the last %1 it keeps · since %2").arg(count).arg(root.log.firstAtText);
    }

    /// D15, over the first line: where qBittorrent's memory of its log begins, and where the
    /// rest is.
    readonly property string startText: {
        const start = root.log.fromStart ? qsTr("qBittorrent's log starts when it started, %1: it keeps it in memory.").arg(root.log.firstAtText)
                                         : qsTr("qBittorrent keeps its last 20 000 lines in memory; these start %1.").arg(root.log.firstAtText);
        const file = root.logFilePath.length > 0 ? qsTr("Older lines are in its log file on the server, in %1.").arg(root.logFilePath)
                                                 : qsTr("It writes no log file (Settings › qBittorrent › Advanced).");
        return start + " " + file;
    }

    /// `/`: the caret in the filter.
    function focusFilter(): void {
        filterField.focusInput();
    }

    /// Back to the lines, with the keys.
    function focusLines(): void {
        list.forceActiveFocus();
    }

    function moveCursor(row: int): void {
        if (root.lines.count === 0) {
            root.cursor = -1;
            return;
        }
        root.cursor = Math.max(0, Math.min(root.lines.count - 1, row));
        // The first line shows with what is above it: where the log starts (D15).
        if (root.cursor === 0)
            list.positionViewAtBeginning();
        else
            list.positionViewAtIndex(root.cursor, ListView.Contain);
        root._settleFollow();
    }

    /// `G`, and `f` turning following on: the newest line, and follow.
    function followNewest(): void {
        root.following = true;
        root.newLines = 0;
        if (root.lines.count > 0)
            root.cursor = root.lines.count - 1;
        root._toEnd();
    }

    /// `f`.
    function toggleFollow(): void {
        if (root.following)
            root.following = false;
        else
            root.followNewest();
    }

    /// `w` (`step` 1) and `W` (-1).
    function nextProblem(step: int): void {
        const row = root.lines.nextProblem(root.cursor, step);
        if (row < 0) {
            root.noteRequested(step > 0 ? qsTr("no warning below") : qsTr("no warning above"));
            return;
        }
        root.moveCursor(row);
    }

    /// `y`: the marked lines, or the cursor's.
    function copyLines(): void {
        const count = root.lines.copyCount(root.cursor);
        if (count === 0) {
            root.noteRequested(qsTr("nothing to copy"));
            return;
        }
        root.copyRequested(root.lines.copyText(root.cursor), count === 1 ? qsTr("copied 1 line") : qsTr("copied %1 lines").arg(count));
    }

    /// `enter`: the cursor line's torrent in the library.
    function showLineTorrent(): void {
        const hash = root.lines.torrentAt(root.cursor);
        if (hash.length === 0) {
            root.noteRequested(qsTr("this line names no torrent in the library"));
            return;
        }
        root.showTorrent(hash);
    }

    /// `t`: only the lines of the cursor line's torrent.
    function narrowToLineTorrent(): void {
        const hash = root.lines.torrentAt(root.cursor);
        if (hash.length === 0) {
            root.noteRequested(qsTr("this line names no torrent in the library"));
            return;
        }
        root.showTorrentLines(hash);
    }

    /// D13: from a torrent's block, or `t`: its lines, the newest at the bottom.
    function showTorrentLines(hash: string): void {
        root.lines.clearMarks();
        if (root.lines.setTorrent(hash))
            root.followNewest();
    }

    function toggleLevel(level: int): void {
        root.lines.toggleLevel(level);
    }

    /// `esc`: the marks, then the torrent, then the words; then back.
    function stepBack(): void {
        if (root.lines.markedCount > 0) {
            root.lines.clearMarks();
            return;
        }
        if (root.lines.torrentHash.length > 0) {
            root.lines.clearTorrent();
            return;
        }
        if (root.lines.text.length > 0) {
            filterField.text = "";
            return;
        }
        root.leaveRequested();
    }

    /// `space`: the cursor's line in or out, and the anchor there.
    function toggleMark(): void {
        if (root.cursor < 0)
            return;
        root.lines.toggleMark(root.cursor);
        root.anchorId = root.lines.idAt(root.cursor);
    }

    /// `v`: anchor..cursor.
    function markRange(): void {
        if (root.cursor < 0)
            return;
        const anchor = root.lines.rowOfId(root.anchorId);
        root.lines.markRange(anchor >= 0 ? anchor : root.cursor, root.cursor);
    }

    /// A click, by the click rules every list follows (docs/DECISIONS.md).
    function clickRow(row: int, modifiers: int): void {
        root.focusLines();
        const ctrl = (modifiers & Qt.ControlModifier) !== 0;
        const shift = (modifiers & Qt.ShiftModifier) !== 0;
        if (shift) {
            const anchor = root.lines.rowOfId(root.anchorId);
            root.lines.markRange(anchor >= 0 ? anchor : row, row, ctrl);
        } else if (ctrl) {
            root.lines.toggleMark(row);
            root.anchorId = root.lines.idAt(row);
        } else {
            root.lines.clearMarks();
            root.anchorId = root.lines.idAt(row);
        }
        root.cursor = row;
    }

    function _toEnd(): void {
        root._autoScroll = true;
        list.positionViewAtEnd();
        root._autoScroll = false;
    }

    property bool _autoScroll: false

    /// Following is "the view is at the bottom": scrolled there, it follows again; scrolled
    /// up, it pauses. Asked after what the person did (a key, a drag, the scrollbar), never on
    /// every move of the view: the list moves by itself as it lays out rows of new heights.
    function _settleFollow(): void {
        if (root._autoScroll || list.count === 0)
            return;
        const atEnd = list.atYEnd || list.contentHeight <= list.height;
        if (atEnd && !root.following) {
            root.following = true;
            root.newLines = 0;
        } else if (!atEnd && root.following) {
            root.following = false;
        }
    }

    Connections {
        target: root.lines

        function onLinesArrived(count: int): void {
            const last = root.lines.count - 1;
            if (root.following) {
                // The cursor at the newest line, or nowhere yet, rides along.
                if (root.cursor < 0 || root.cursor >= last - count)
                    root.cursor = last;
                Qt.callLater(root._toEnd);
            } else {
                root.newLines += count;
            }
        }

        function onCountChanged(): void {
            if (root.cursor >= root.lines.count)
                root.cursor = root.lines.count - 1;
            if (root.cursor < 0 && root.lines.count > 0)
                root.cursor = root.lines.count - 1;
        }

        function onModelReset(): void {
            root.newLines = 0;
            root.cursor = root.lines.count - 1;
            if (root.following)
                Qt.callLater(root._toEnd);
        }
    }

    Connections {
        target: root.log

        // A restart, another server, the face reopened: a new log, followed from its end.
        function onRestarted(): void {
            root.anchorId = -1;
            root.newLines = 0;
            root.following = true;
        }
    }

    // A filter change can leave the view anywhere: the cursor's line stays in view.
    Connections {
        target: root.lines

        function onLevelsChanged(): void {
            root._afterFilter();
        }

        function onTextChanged(): void {
            root._afterFilter();
        }

        function onTorrentChanged(): void {
            root._afterFilter();
        }
    }

    function _afterFilter(): void {
        root.newLines = 0;
        if (root.following) {
            root.cursor = root.lines.count - 1;
            Qt.callLater(root._toEnd);
        } else if (root.cursor >= 0) {
            Qt.callLater(() => list.positionViewAtIndex(root.cursor, ListView.Contain));
        }
    }

    // --- the bar --------------------------------------------------------------------------
    Item {
        id: bar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        implicitHeight: barFlow.implicitHeight + Theme.space(20)
        height: implicitHeight

        Flow {
            id: barFlow

            anchors.left: parent.left
            anchors.leftMargin: Theme.space(16)
            anchors.right: sinceLabel.left
            anchors.rightMargin: Theme.space(16)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space(8)

            Repeater {
                model: [
                    {
                        "level": ServerLog.Normal,
                        "word": qsTr("normal"),
                        "tone": Theme.foreground
                    },
                    {
                        "level": ServerLog.Info,
                        "word": qsTr("info"),
                        "tone": Theme.accent
                    },
                    {
                        "level": ServerLog.Warning,
                        "word": qsTr("warning"),
                        "tone": Theme.yellow
                    },
                    {
                        "level": ServerLog.Critical,
                        "word": qsTr("critical"),
                        "tone": Theme.red
                    }
                ]

                Chip {
                    id: levelChip

                    required property int index
                    required property var modelData

                    count: String(root.log.levelCounts[levelChip.index] ?? 0)
                    keyText: String(levelChip.index + 1)
                    objectName: "levelChip" + (levelChip.index + 1)
                    checkable: true
                    selected: (root.lines.levels & levelChip.modelData.level) !== 0
                    text: levelChip.modelData.word
                    toggle: true
                    tone: levelChip.modelData.tone

                    onClicked: root.toggleLevel(levelChip.modelData.level)
                }
            }

            // D13 (L2): the torrent the lines are narrowed to; a click, or `esc`, clears it.
            Chip {
                // Named without its ✕; what a press does is the description.
                Accessible.description: tooltip
                Accessible.name: qsTr("torrent: %1").arg(root.lines.torrentName)
                accent: true
                objectName: "torrentChip"
                text: qsTr("torrent: %1").arg(root.lines.torrentName) + "  " + Glyphs.close
                tooltip: qsTr("only the lines that name it · esc clears")
                visible: root.lines.torrentHash.length > 0

                onClicked: root.lines.clearTorrent()
            }

            Field {
                id: filterField

                hint: "/"
                objectName: "logFilter"
                placeholder: qsTr("filter lines")
                width: Theme.space(240)

                onAccepted: root.focusLines()
                onCleared: root.focusLines()
                onTextChanged: root.lines.text = filterField.text
            }
        }

        // #79: what the log holds ("since qBittorrent started · 18:14 · 28 lines").
        Text {
            id: sinceLabel

            Accessible.name: text
            Accessible.role: Accessible.StaticText
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(16)
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.dimForeground
            elide: Text.ElideLeft
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "logSince"
            text: root.sinceText
            width: Math.min(implicitWidth, root.width * 0.45)
        }

        Hairline {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
        }
    }

    // --- the column heads ------------------------------------------------------------------
    Item {
        id: heads

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: bar.bottom
        height: Theme.space(26)

        Repeater {
            model: [qsTr("time"), qsTr("level"), qsTr("message")]

            Label {
                required property int index
                required property string modelData

                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space(4)
                size: "caption"
                text: modelData
                tracked: true
                // Over its column, as LogRow lays the line out. Placed here rather than in the
                // model: Theme.space() follows the base size (#156), and a model made of it
                // would be rebuilt, its heads made again, at every change.
                x: Theme.space(16) + (index > 0 ? root.timeWidth + Theme.space(14) : 0) +
                   (index > 1 ? root.levelWidth + Theme.space(14) : 0)
            }
        }
    }

    // --- the lines -------------------------------------------------------------------------
    ListView {
        id: list

        // #77: the log's lines, for a screen reader; each is a list item.
        Accessible.name: qsTr("log")
        Accessible.role: Accessible.List
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heads.bottom
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        focus: true
        model: root.lines
        objectName: "logList"
        // Fig. 23: read-only, dimmed, as the table is.
        opacity: root.serverLost ? 0.45 : 1.0
        reuseItems: true

        // a11y-ignore A1: named by `message`, a required property the model fills (#77)
        delegate: LogRow {
            current: index === root.cursor
            levelWidth: root.levelWidth
            timeWidth: root.timeWidth
            width: list.width

            onClicked: (row, modifiers) => root.clickRow(row, modifiers)
            onMoveBackRequested: id => root.moveBackRequested(id)
        }
        // #79: where the log starts, said before its lines.
        header: Text {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            bottomPadding: Theme.space(6)
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            leftPadding: Theme.space(16)
            objectName: "logStart"
            rightPadding: Theme.space(16)
            text: root.startText
            topPadding: Theme.space(4)
            visible: root.log.total > 0
            width: list.width
            wrapMode: Text.Wrap
        }

        // Rows are as tall as their wrapped message, known only once drawn: while following,
        // the view keeps to the bottom as they settle.
        onContentHeightChanged: {
            if (root.following && !root._autoScroll)
                Qt.callLater(root._toEnd);
        }
        onMovementEnded: root._settleFollow()
        onHeightChanged: {
            if (root.following)
                Qt.callLater(root._toEnd);
        }

        Keys.onPressed: event => root._key(event)

        // #79: the empty log's line; "esc clears" is a key hint, so the description.
        Text {
            Accessible.description: root.log.total === 0 ? "" : qsTr("shortcut %1").arg("esc")
            Accessible.name: root.log.total === 0 ? text : qsTr("no line matches")
            Accessible.role: Accessible.StaticText
            anchors.centerIn: parent
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.body
            objectName: "logEmpty"
            text: root.log.total === 0 ? (root.log.loaded ? qsTr("qBittorrent's log is empty") : "")
                                       : qsTr("no line matches · esc clears")
            visible: list.count === 0 && text.length > 0
        }
    }

    ScrollBar {
        id: scrollBar

        anchors.bottom: list.bottom
        anchors.right: list.right
        anchors.top: list.top
        flickable: list
        vertical: true
        width: Theme.space(4)

        onActiveChanged: {
            if (!scrollBar.active)
                root._settleFollow();
        }
    }

    // A wheel notch moves three rows, and quick notches add up (Kit/WheelScroll).
    WheelScroll {
        id: wheelScroll

        anchors.fill: list
        flickable: list
        z: 2
    }

    // D12: a notch up pauses at once; notches down that reach the bottom follow again. Judged
    // by where the glide is going, which it knows before it gets there.
    Connections {
        target: wheelScroll

        function onWheel(wheel: var): void {
            if (wheel.angleDelta.y > 0) {
                root.following = false;
                return;
            }
            const bottom = list.originY + list.contentHeight - list.height;
            if (wheelScroll.target >= bottom - 1 && !root.following) {
                root.following = true;
                root.newLines = 0;
            }
        }
    }

    // D12 (L2): new lines below while paused; a click is `G`.
    Rectangle {
        id: newerPill

        // #77: a button that follows the newest lines, how many its description; its press
        // is the click.
        Accessible.description: newerText.text
        Accessible.name: qsTr("follow new lines")
        Accessible.role: Accessible.Button
        Accessible.onPressAction: if (newerPill.enabled)
                                      root.followNewest()
        anchors.bottom: list.bottom
        anchors.bottomMargin: Theme.space(10)
        anchors.horizontalCenter: list.horizontalCenter
        border.color: Theme.accent
        border.width: Theme.edgeWidth
        color: Theme.background
        height: newerRow.implicitHeight + Theme.space(8)
        objectName: "newerPill"
        visible: !root.following && root.newLines > 0
        width: newerRow.implicitWidth + Theme.space(24)
        z: 3

        Row {
            id: newerRow

            anchors.centerIn: parent
            spacing: Theme.space(8)

            Text {
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: "G"
            }

            Text {
                id: newerText

                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                objectName: "newerText"
                text: root.newLines === 1 ? qsTr("1 new line below · follow is paused")
                                          : qsTr("%1 new lines below · follow is paused").arg(root.newLines)
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor

            onClicked: root.followNewest()
        }
    }

    function _key(event: var): void {
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0;
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0;
        if (ctrl) {
            if (event.key === Qt.Key_A) {
                root.lines.markAll();
                event.accepted = true;
            }
            return; // the window's ctrl keys
        }
        const page = Math.max(1, Math.floor(list.height / Theme.space(22)) - 1);
        switch (event.key) {
        case Qt.Key_J:
        case Qt.Key_Down:
            root.moveCursor(root.cursor + 1);
            break;
        case Qt.Key_K:
        case Qt.Key_Up:
            root.moveCursor(root.cursor - 1);
            break;
        case Qt.Key_PageDown:
            root.moveCursor(root.cursor + page);
            break;
        case Qt.Key_PageUp:
            root.moveCursor(root.cursor - page);
            break;
        case Qt.Key_Home:
            root.moveCursor(0);
            break;
        case Qt.Key_End:
            root.followNewest();
            break;
        case Qt.Key_G:
            if (shift)
                root.followNewest();
            else
                root.moveCursor(0);
            break;
        case Qt.Key_W:
            root.nextProblem(shift ? -1 : 1);
            break;
        case Qt.Key_F:
            root.toggleFollow();
            break;
        case Qt.Key_Space:
            root.toggleMark();
            break;
        case Qt.Key_V:
            root.markRange();
            break;
        case Qt.Key_Y:
            root.copyLines();
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.showLineTorrent();
            break;
        case Qt.Key_T:
            root.narrowToLineTorrent();
            break;
        case Qt.Key_Slash:
            root.focusFilter();
            break;
        case Qt.Key_Escape:
            root.stepBack();
            break;
        case Qt.Key_1:
            root.toggleLevel(ServerLog.Normal);
            break;
        case Qt.Key_2:
            root.toggleLevel(ServerLog.Info);
            break;
        case Qt.Key_3:
            root.toggleLevel(ServerLog.Warning);
            break;
        case Qt.Key_4:
            root.toggleLevel(ServerLog.Critical);
            break;
        default:
            return;
        }
        event.accepted = true;
    }
}
