pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The rail on the log face (board 51 D5 a, DECISIONS C32; supersedes docs/32 L1's bar of level
// chips): the four levels ticked with their counts (`1`–`4`, as the chips were), the torrents
// the lines are about with how many name each (a click narrows the lines to one, again or
// `esc` lets it go, as `t` does), and since when (where qBittorrent's log starts, or the last
// hour). The rail's other groups are about torrents and stay away on the log.
//
// Its rows answer the pointer; the keys stay the log's (`1`–`4`, `t`, `esc`), so none is a
// stop of the rail's cursor. The groups fold as the rail's do, under the names `logLevels`,
// `logTorrents` and `logSince` (`groupToggled`).
Column {
    id: root

    required property LogFilterModel lines
    required property ServerLog log
    /// `log.aboutTorrents()`, re-read by the host as lines come: `[{hash, name, count}]`.
    property var torrents: []
    property bool keysShown: false
    property ThemeInk block: Theme.on.window
    property var collapsedGroups: []
    /// The time "the last hour" counts back from; the host's clock, a test's own.
    property var now: () => Date.now()

    signal groupToggled(string group)
    /// The hover label of a row whose name is cut, as the rail's others have.
    signal nameTip(RailRow row, bool hovering, string full)

    readonly property real headPadding: levelsGroup.headPadding
    /// "the last hour" is on: the lines' since is within the hour before it was picked.
    readonly property bool lastHour: root.lines.since > 0

    readonly property var levelRows: [
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

    /// A torrent row: its lines, or every line again when they are its already.
    function pickTorrent(hash: string): void {
        if (root.lines.torrentHash === hash)
            root.lines.clearTorrent();
        else
            root.lines.setTorrent(hash);
    }

    /// The since rows: from where the log starts (0), or the hour before now.
    function pickSince(lastHour: bool): void {
        root.lines.since = lastHour ? Math.floor(root.now() / 1000) - 3600 : 0;
    }

    spacing: Theme.space(14) - root.headPadding

    RailGroup {
        id: levelsGroup

        block: root.block
        collapsed: root.collapsedGroups.indexOf("logLevels") >= 0
        count: root.levelRows.length
        label: qsTr("levels")
        objectName: "railLogLevels"
        width: root.width

        onToggled: root.groupToggled("logLevels")

        Repeater {
            model: root.levelRows

            RailRow {
                id: levelRow

                required property int index
                required property var modelData

                block: root.block
                count: root.log.levelCounts[levelRow.index] ?? 0
                keyText: String(levelRow.index + 1)
                keysShown: root.keysShown
                objectName: "railLogLevel-" + levelRow.modelData.word
                text: levelRow.modelData.word
                tick: true
                tickColor: levelRow.modelData.tone
                ticked: (root.lines.levels & levelRow.modelData.level) !== 0
                // Board 51 `.lvw`: the warning and critical words in their colour.
                toneName: levelRow.index > 1
                visible: !levelsGroup.collapsed
                width: root.width

                onClicked: root.lines.toggleLevel(levelRow.modelData.level)
            }
        }
    }

    RailGroup {
        id: torrentsGroup

        block: root.block
        collapsed: root.collapsedGroups.indexOf("logTorrents") >= 0
        count: root.torrents.length
        label: qsTr("about a torrent")
        objectName: "railLogTorrents"
        visible: root.torrents.length > 0
        width: root.width

        onToggled: root.groupToggled("logTorrents")

        Repeater {
            model: root.torrents

            RailRow {
                id: torrentRow

                required property int index
                required property var modelData

                block: root.block
                count: torrentRow.modelData.count
                glyph: Glyphs.middot
                objectName: "railLogTorrent_" + torrentRow.index
                selected: root.lines.torrentHash === torrentRow.modelData.hash
                text: torrentRow.modelData.name
                visible: !torrentsGroup.collapsed
                width: root.width

                onClicked: root.pickTorrent(torrentRow.modelData.hash)
                onHoverChanged: hovering => root.nameTip(torrentRow, hovering, torrentRow.modelData.name)
            }
        }
    }

    RailGroup {
        id: sinceGroup

        block: root.block
        collapsed: root.collapsedGroups.indexOf("logSince") >= 0
        count: 2
        label: qsTr("since")
        objectName: "railLogSince"
        visible: root.log.total > 0
        width: root.width

        onToggled: root.groupToggled("logSince")

        RailRow {
            id: startRow

            block: root.block
            glyph: Glyphs.middot
            objectName: "railLogSinceStart"
            selected: !root.lastHour
            // D15: the log held starts where qBittorrent did, or is the last lines it keeps.
            text: root.log.fromStart ? qsTr("qBittorrent started, %1").arg(root.log.firstAtText)
                                     : qsTr("the oldest line kept, %1").arg(root.log.firstAtText)
            visible: !sinceGroup.collapsed
            width: root.width

            onClicked: root.pickSince(false)
            onHoverChanged: hovering => root.nameTip(startRow, hovering, startRow.text)
        }

        RailRow {
            block: root.block
            glyph: Glyphs.middot
            objectName: "railLogSinceHour"
            selected: root.lastHour
            text: qsTr("the last hour")
            visible: !sinceGroup.collapsed
            width: root.width

            onClicked: root.pickSince(true)
        }
    }
}
