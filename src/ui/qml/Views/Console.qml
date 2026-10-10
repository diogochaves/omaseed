import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The seed console (docs/22 §4, #24): the main window's second face, in the body's place
// while the hero, the rail and the footer stay. Everything it prints comes from
// `App.console` (core::SeedConsole), which works out each block for the rail's scope; this
// file only lays the blocks out, top to bottom, in one column that scrolls:
//
//   scope line · attention · totals · the tide · the year | keeping alive · ratio ladder ·
//   trackers · earning | holding · on air | lately
//
// Keys (routed by MainView's console area): `[` `]` change the tide's range, `j` `k` scroll,
// `g` `G` top and bottom, `esc` clears the scope. A click on a torrent shows it in the
// library; a click on a tracker scopes the console to it.
Flickable {
    id: root

    required property SeedConsole seed
    /// What the rail selects, in words: "every torrent", "category films", "tracker …".
    property string scopeName: qsTr("every torrent")

    signal showTorrent(string hash)
    signal scopeTracker(string host)
    signal clearScope
    signal attentionRequested(string host)

    readonly property real gutter: Theme.space(22)
    readonly property bool wide: root.width >= Theme.space(900)

    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: column.implicitHeight + Theme.space(16) + Theme.space(28)
    contentWidth: width

    function scrollBy(dy: real): void {
        root.contentY = Math.max(0, Math.min(root.contentHeight - root.height, root.contentY + dy));
    }

    ScrollBar {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.top: parent.top
        flickable: root
        parent: root
        vertical: true
        width: Theme.space(6)
        z: 2
    }

    Column {
        id: column

        spacing: Theme.space(22)
        width: root.width - 2 * root.gutter
        x: root.gutter
        y: Theme.space(16)

        // --- scope ---------------------------------------------------------------------------
        RowLayout {
            spacing: Theme.space(10)
            width: parent.width

            Label {
                Layout.alignment: Qt.AlignBaseline
                text: qsTr("scope")
                tracked: true
            }

            Text {
                Layout.alignment: Qt.AlignBaseline
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.body
                font.weight: Font.Medium
                text: root.seed.selectionScope ? qsTr("the selection") : root.scopeName
            }

            Label {
                Layout.alignment: Qt.AlignBaseline
                text: root.seed.scopeCount === 1 ? qsTr("· 1 torrent") : qsTr("· %1 torrents").arg(root.seed.scopeCount)
                tracked: true
            }

            Text {
                Layout.alignment: Qt.AlignBaseline
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.caption
                text: qsTr("esc  clear")
                visible: root.seed.scoped

                TapHandler {
                    onTapped: root.clearScope()
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                Layout.alignment: Qt.AlignBaseline
                text: root.seed.ledgerSince
                tracked: true
            }
        }

        // --- attention -----------------------------------------------------------------------
        Rectangle {
            color: Qt.alpha(Theme.stateError, 0.07)
            height: attentionRow.implicitHeight + Theme.space(14)
            visible: root.seed.attention.visible === true
            width: parent.width

            Rectangle {
                color: Theme.stateError
                height: parent.height
                width: Theme.space(2)
            }

            RowLayout {
                id: attentionRow

                anchors.left: parent.left
                anchors.leftMargin: Theme.space(12)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(12)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space(14)

                Text {
                    color: Theme.stateError
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    font.weight: Font.Medium
                    text: root.seed.attention.title ?? ""
                }

                Text {
                    Layout.fillWidth: true
                    color: Theme.foreground
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: root.seed.attention.detail ?? ""
                }

                Text {
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.caption
                    text: qsTr("show them")
                    visible: (root.seed.attention.errors ?? 0) > 0
                }
            }

            HoverHandler {
                cursorShape: (root.seed.attention.errors ?? 0) > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
            }

            TapHandler {
                enabled: (root.seed.attention.errors ?? 0) > 0
                onTapped: root.attentionRequested(root.seed.attention.host ?? "")
            }
        }

        // --- totals --------------------------------------------------------------------------
        Item {
            implicitHeight: totalsRow.implicitHeight
            width: parent.width

            Hairline {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
            }

            Hairline {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
            }

            Row {
                id: totalsRow

                readonly property real cell: width / 6
                width: parent.width

                Repeater {
                    model: [
                        { caption: qsTr("given"), value: root.seed.totals.givenText, note: root.seed.totals.todayText ? qsTr("%1 today").arg(root.seed.totals.todayText) : "", good: true },
                        { caption: qsTr("taken"), value: root.seed.totals.takenText, note: root.seed.totals.takenNote },
                        { caption: qsTr("ratio"), value: root.seed.totals.ratioText, note: root.seed.totals.ratioNote },
                        { caption: qsTr("buffer"), value: root.seed.totals.bufferText, note: qsTr("given − taken"), bad: root.seed.totals.bufferNegative === true },
                        { caption: qsTr("on offer"), value: root.seed.totals.offerText, note: root.seed.totals.offerNote },
                        { caption: qsTr("streak"), value: (root.seed.totals.streakText ?? "0") + " " + (root.seed.totals.streakUnit ?? qsTr("days")), note: root.seed.totals.streakNote ?? "" }
                    ]

                    delegate: Item {
                        id: totalCell

                        required property var modelData
                        required property int index

                        height: total.implicitHeight
                        width: totalsRow.cell

                        Hairline {
                            anchors.bottom: parent.bottom
                            anchors.top: parent.top
                            vertical: true
                            visible: totalCell.index > 0
                        }

                        ConsoleTotal {
                            id: total

                            readonly property var parts: String(totalCell.modelData.value ?? "").split(" ")

                            anchors.left: parent.left
                            anchors.leftMargin: totalCell.index === 0 ? -Theme.space(14) : 0
                            anchors.right: parent.right
                            caption: totalCell.modelData.caption
                            note: totalCell.modelData.note ?? ""
                            noteColor: totalCell.modelData.good ? Theme.stateSeeding : Theme.dimForeground
                            unit: total.parts.length > 1 ? total.parts.slice(1).join(" ") : ""
                            value: total.parts[0] ?? ""
                            valueColor: totalCell.modelData.bad ? Theme.stateError : Theme.brightForeground
                        }
                    }
                }
            }
        }

        // --- the tide ------------------------------------------------------------------------
        Column {
            spacing: Theme.space(10)
            width: parent.width

            ConsoleHead {
                summary: root.seed.tide.any ? qsTr("↑ %1 given · ↓ %2 taken in the last %3%4").arg(root.seed.tide.upText).arg(
                                                  root.seed.tide.downText).arg(root.seed.range).arg(
                                                  root.seed.tide.wholeServer ? qsTr(" · whole server: hours are not kept per torrent") : "")
                                            : qsTr("nothing yet in this range")
                title: qsTr("the tide")
                width: parent.width

                Repeater {
                    model: root.seed.ranges

                    delegate: Rectangle {
                        id: rangeChip

                        required property string modelData
                        readonly property bool on: rangeChip.modelData === root.seed.range

                        border.color: rangeChip.on ? "transparent" : Theme.fill(Theme.hairlineAlpha)
                        border.width: 1
                        color: rangeChip.on ? Theme.fill(Theme.fillSelected) : "transparent"
                        implicitHeight: chipText.implicitHeight + Theme.space(4)
                        implicitWidth: chipText.implicitWidth + Theme.space(16)

                        Text {
                            id: chipText

                            anchors.centerIn: parent
                            color: rangeChip.on ? Theme.brightForeground : Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.caption
                            text: rangeChip.modelData
                        }

                        TapHandler {
                            onTapped: root.seed.range = rangeChip.modelData
                        }
                    }
                }
            }

            Item {
                height: Theme.space(190)
                width: parent.width

                AreaChart {
                    id: tide

                    anchors.fill: parent
                    // The halo's pulse repaints the whole window every frame: only while the
                    // window is in use, not behind another one or hidden to the tray (#49).
                    animate: tide.Window.active
                    formatValue: v => App.sizeText(v)
                    gapCaption: (first, last) => {
                        const pts = root.seed.tide.points;
                        let bytes = 0;
                        for (let i = first; i <= last; ++i)
                            bytes += (pts[i].up ?? 0) * root.seed.tide.step;
                        const hours = Math.round((last - first + 1) * root.seed.tide.step / 3600);
                        return qsTr("omaseed closed · %1 over %2 h").arg(App.sizeText(bytes)).arg(hours);
                    }
                    labels: root.seed.tide.labels ?? []
                    points: root.seed.tide.points ?? []
                }

                Rectangle {
                    readonly property var point: tide.hoveredIndex >= 0 ? (root.seed.tide.points[tide.hoveredIndex] ?? null) : null

                    border.color: Theme.fill(Theme.hairlineAlpha)
                    border.width: 1
                    color: Theme.background
                    height: readout.implicitHeight + Theme.space(10)
                    visible: point !== null
                    width: readout.implicitWidth + Theme.space(18)
                    x: Math.max(0, Math.min(parent.width - width, tide.xAt(Math.max(0, tide.hoveredIndex)) + Theme.space(12)))
                    y: 0

                    Column {
                        id: readout

                        anchors.centerIn: parent
                        spacing: Theme.space(1)

                        Text {
                            color: Theme.dimForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.caption
                            text: {
                                const p = parent.parent.point;
                                return p ? p.whenText + (p.gap ? qsTr(" · omaseed closed, spread evenly") : "") : "";
                            }
                        }

                        Text {
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.small
                            text: {
                                const p = parent.parent.point;
                                if (!p)
                                    return "";
                                return p.up === undefined || p.up === null ? qsTr("before the ledger")
                                                                           : qsTr("↑ %1 · ↓ %2").arg(p.upText).arg(p.downText);
                            }
                        }

                        Text {
                            color: Theme.dimForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.caption
                            text: {
                                const p = parent.parent.point;
                                return p && p.volumeText ? qsTr("%1 given in this bucket").arg(p.volumeText) : "";
                            }
                            visible: text.length > 0
                        }
                    }
                }
            }
        }

        // --- the year | keeping alive ------------------------------------------------------
        GridLayout {
            columnSpacing: Theme.space(28)
            columns: root.wide ? 2 : 1
            rowSpacing: Theme.space(22)
            width: parent.width

            Column {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.preferredWidth: 1.45
                spacing: Theme.space(10)

                ConsoleHead {
                    summary: root.seed.year.summary ?? ""
                    title: qsTr("the year")
                    width: parent.width
                }

                DayGrid {
                    id: year

                    cellSize: Math.max(Theme.space(7), Math.min(Theme.space(13), (parent.width - Theme.space(30)) / 53 - Theme.space(3)))
                    days: root.seed.year.days ?? []
                }

                Flow {
                    spacing: Theme.space(22)
                    width: parent.width

                    Repeater {
                        model: year.hoveredIndex >= 0 ? [root.seed.year.days[year.hoveredIndex].text] : (root.seed.year.facts ?? [])

                        delegate: Text {
                            required property string modelData

                            color: Theme.dimForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.small
                            text: modelData.replace(/<b>/g, "<font color='" + Theme.brightForeground + "'>").replace(/<\/b>/g, "</font>")
                            textFormat: Text.StyledText
                        }
                    }
                }
            }

            Column {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: Theme.space(10)

                ConsoleHead {
                    summary: root.seed.alive.summary ?? ""
                    summaryColor: (root.seed.alive.rare ?? 0) > 0 ? Theme.yellow : Theme.dimForeground
                    title: qsTr("keeping alive")
                    width: parent.width
                }

                StackStrip {
                    segments: (root.seed.alive.buckets ?? []).map(b => ({
                                                                     value: b.count,
                                                                     hatched: b.kind === "unknown",
                                                                     color: b.kind === "only" ? Theme.yellow
                                                                          : b.kind === "two" ? Qt.alpha(Theme.yellow, 0.5)
                                                                          : b.kind === "few" ? Qt.alpha(Theme.stateSeeding, 0.45)
                                                                          : b.kind === "some" ? Qt.alpha(Theme.stateSeeding, 0.22)
                                                                          : Theme.fill(Theme.fillHover)
                                                                 }))
                    width: parent.width
                }

                RowLayout {
                    width: parent.width

                    Repeater {
                        model: root.seed.alive.buckets ?? []

                        delegate: Column {
                            required property var modelData

                            Layout.fillWidth: true

                            Text {
                                color: modelData.rare && modelData.count > 0 ? Theme.yellow : Theme.brightForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.body
                                font.weight: Font.Medium
                                text: modelData.count
                            }

                            Text {
                                color: modelData.rare && modelData.count > 0 ? Theme.yellow : Theme.dimForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.caption
                                text: modelData.label
                            }
                        }
                    }
                }

                Column {
                    width: parent.width

                    Repeater {
                        model: root.seed.alive.rows ?? []

                        delegate: ConsoleRow {
                            required property var modelData
                            required property int index

                            current: index === 0
                            name: modelData.name
                            note: modelData.seedsText
                            noteColor: modelData.rare ? Theme.yellow : Theme.dimForeground
                            trailing: modelData.waitingText
                            width: parent.width

                            onClicked: root.showTorrent(modelData.hash)
                        }
                    }

                    ConsoleRow {
                        clickable: false
                        name: qsTr("Every torrent here has more than five seeds.")
                        visible: (root.seed.alive.rows ?? []).length === 0
                        width: parent.width
                    }
                }
            }
        }

        // --- ratio ladder --------------------------------------------------------------------
        Column {
            spacing: Theme.space(10)
            width: parent.width

            ConsoleHead {
                summary: (root.seed.ladder.summary ?? "") + qsTr(" · dot size is torrent size")
                title: qsTr("ratio ladder")
                width: parent.width
            }

            Item {
                height: Theme.space(150)
                width: parent.width

                RatioLadder {
                    id: ladder

                    anchors.fill: parent
                    paidText: qsTr("paid back")
                    points: root.seed.ladderPoints
                }

                Rectangle {
                    readonly property var point: ladder.hoveredIndex >= 0 ? (root.seed.ladderPoints[ladder.hoveredIndex] ?? null) : null

                    border.color: Theme.fill(Theme.hairlineAlpha)
                    border.width: 1
                    color: Theme.background
                    height: ladderTip.implicitHeight + Theme.space(10)
                    visible: point !== null
                    width: ladderTip.implicitWidth + Theme.space(18)
                    x: Math.min(parent.width - width, Math.max(0, ladder._x(point ? point.ratio : 1) + Theme.space(10)))
                    y: 0

                    Text {
                        id: ladderTip

                        anchors.centerIn: parent
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: parent.point ? qsTr("%1 · ratio %2 · %3").arg(parent.point.name).arg(parent.point.ratioText).arg(parent.point.sizeText) : ""
                    }
                }
            }
        }

        // --- trackers ------------------------------------------------------------------------
        Column {
            spacing: Theme.space(6)
            width: parent.width

            ConsoleHead {
                summary: (root.seed.trackers.summary ?? "") + qsTr(" · a click scopes the console")
                title: qsTr("trackers")
                width: parent.width
            }

            ConsoleTrackers {
                rows: root.seed.trackers.rows ?? []
                width: parent.width

                onActivated: host => root.scopeTracker(host)
            }
        }

        // --- earning | holding ---------------------------------------------------------------
        GridLayout {
            columnSpacing: Theme.space(28)
            columns: root.wide ? 2 : 1
            rowSpacing: Theme.space(22)
            width: parent.width

            Column {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: Theme.space(6)

                ConsoleHead {
                    summary: qsTr("given in 30 days ÷ size")
                    title: qsTr("earning")
                    width: parent.width
                }

                Repeater {
                    model: root.seed.earning.rows ?? []

                    delegate: ConsoleRow {
                        required property var modelData

                        fraction: modelData.fraction
                        name: modelData.name
                        note: modelData.yieldText
                        noteColor: Theme.stateSeeding
                        noteWidth: Theme.space(130)
                        width: parent.width

                        onClicked: root.showTorrent(modelData.hash)
                    }
                }

                ConsoleRow {
                    clickable: false
                    name: root.seed.earning.empty ?? ""
                    visible: (root.seed.earning.rows ?? []).length === 0
                    width: parent.width
                }
            }

            Column {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: Theme.space(6)

                ConsoleHead {
                    summary: root.seed.holding.summary ?? ""
                    title: qsTr("holding")
                    width: parent.width
                }

                Repeater {
                    model: root.seed.holding.rows ?? []

                    delegate: ConsoleRow {
                        required property var modelData

                        name: modelData.name
                        note: modelData.sizeText
                        trailing: modelData.quietText
                        width: parent.width

                        onClicked: root.showTorrent(modelData.hash)
                    }
                }
            }
        }

        // --- on air | lately -----------------------------------------------------------------
        GridLayout {
            columnSpacing: Theme.space(28)
            columns: root.wide ? 2 : 1
            rowSpacing: Theme.space(22)
            width: parent.width

            Column {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.preferredWidth: 1.2
                spacing: Theme.space(6)

                ConsoleHead {
                    summary: (root.seed.onAir.rows ?? []).length > 0 ? qsTr("↑ %1 · %2").arg(root.seed.onAir.speedText).arg(root.seed.onAir.summary)
                                                                     : qsTr("quiet")
                    summaryColor: (root.seed.onAir.rows ?? []).length > 0 ? Theme.stateSeeding : Theme.dimForeground
                    title: qsTr("on air")
                    width: parent.width
                }

                Repeater {
                    model: root.seed.onAir.rows ?? []

                    delegate: ConsoleRow {
                        required property var modelData

                        barWidth: Theme.space(110)
                        fraction: modelData.fraction
                        name: modelData.name
                        note: modelData.speedText
                        noteColor: Theme.stateSeeding
                        trailing: modelData.peersText
                        trailingWidth: Theme.space(64)
                        width: parent.width

                        onClicked: root.showTorrent(modelData.hash)
                    }
                }

                ConsoleRow {
                    clickable: false
                    name: qsTr("Nothing is uploading in this scope right now.")
                    visible: (root.seed.onAir.rows ?? []).length === 0
                    width: parent.width
                }
            }

            Column {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: Theme.space(6)

                ConsoleHead {
                    summary: qsTr("milestones and finishes")
                    title: qsTr("lately")
                    width: parent.width
                }

                Repeater {
                    model: root.seed.lately

                    delegate: ConsoleRow {
                        required property var modelData

                        glyph: modelData.glyph
                        glyphColor: modelData.role === "down" ? Theme.stateDownloading : Theme.stateSeeding
                        name: modelData.name + "  ·  " + modelData.what
                        trailing: modelData.when
                        trailingWidth: Theme.space(70)
                        width: parent.width

                        onClicked: root.showTorrent(modelData.hash)
                    }
                }

                ConsoleRow {
                    clickable: false
                    name: qsTr("Milestones show up here as they happen.")
                    visible: root.seed.lately.length === 0
                    width: parent.width
                }
            }
        }
    }
}
