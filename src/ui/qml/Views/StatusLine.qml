pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The status line (board 46 `statusLine()`, DECISIONS C24 H15, H10's footer half): one line
// across the content's foot, under the body, the rail beside it. Left to right: the server
// lane (its dot, its name, ▾ with two servers, a click to the server palette), ↓ and ↑, the
// alt-speed chip and the network's warning (#167 assumed 5), the facts (count · free · sync),
// the note slot (a copied, a failure with its "details", the note after A), and "? keys" at
// the right edge. It replaces the footer's key hints (supersedes v2's footer hints): keys are
// taught on demand now, in menus, tooltips, the palette and `?`.
//
// Properties only: MainView feeds it the words and the state and answers its signals. Its
// form (height, insets, gaps, the dot, the steps) is `Theme.statusLine*` tokens, so a later
// skin can set them (frame plan, "form in tokens").
//
// Narrow (`level`, from its own width against `Theme.statusLineSteps`): the lane keeps the
// name alone and the alt-speed limits go short (1), the facts' words shorten (2, the caller
// reads `level`), the speeds print short and "? keys" is "?" (3), and the least-read facts go,
// the alt-speed glyph while off and the word "details" too (4). The lane and the note elide,
// and the facts are cut, before anything is pushed out.
Item {
    id: root

    // ---- the server lane ---------------------------------------------------------------
    /// The lane's words: "NAME · qBittorrent 5.2.3", or the name with C12's state words while
    /// it is down (MainView.serverLaneText(), where engines' K11 adds the engine).
    property string serverText: ""
    /// The lane from step 1: the name alone; "" keeps `serverText` (a state worth its words).
    property string serverShortText: ""
    /// docs/33 D3, DECISIONS C24 H15: with two servers the lane takes a ▾, a hover fill and a
    /// click (`serverClicked`); one server is named, with no ▾ and no click (D19).
    property bool serverSwitchable: false
    /// No server is configured: the dot reads as absent (spec `.dot.off`).
    property bool serverConfigured: true
    /// The API answers (GlobalState.connected, or a demo).
    property bool connected: true
    /// The first sync has not landed yet: the dot is yellow.
    property bool syncing: false
    /// docs/35 N3: the server is gone and omaseed retries on its own: yellow, not red.
    property bool retrying: false

    // ---- qBittorrent's own network (docs/08 §3, docs/16 §5) -----------------------------
    /// "connected", "firewalled" or "disconnected" (`server_state.connection_status`).
    property string connectionStatus: "connected"
    /// qBittorrent's listening port: "firewalled" reads "port 26999 not reachable".
    property int listenPort: 0
    /// Anonymous mode is on while private torrents are in the library.
    property bool anonymousWarning: false
    /// What is wrong with the network, "firewalled", "disconnected" or "": only read while
    /// the API itself answers, after the first sync.
    readonly property bool _networkRead: root.connected && !root.syncing && root.serverConfigured
    readonly property string networkWarning: root._networkRead && (root.connectionStatus === "firewalled"
                                                                   || root.connectionStatus === "disconnected")
                                             ? root.connectionStatus : ""
    /// The warning in words, after the speeds: the network's state and anonymous mode, joined.
    readonly property string networkText: {
        const parts = [];
        if (root.networkWarning === "firewalled")
            parts.push(root.listenPort > 0 ? qsTr("port %1 not reachable").arg(root.listenPort)
                                           : qsTr("firewalled"));
        else if (root.networkWarning === "disconnected")
            parts.push(qsTr("disconnected"));
        if (root.anonymousWarning && root._networkRead)
            parts.push(qsTr("anonymous mode"));
        return parts.join(" · ");
    }
    /// What hovering the warning explains: what it means and where to change it.
    readonly property string networkAdvice: {
        const lines = [];
        if (root.networkWarning === "firewalled")
            lines.push(qsTr("Peers cannot reach port %1. Forward it on your router, or turn on UPnP in Settings › Connection (ctrl+shift+,).").arg(
                           root.listenPort > 0 ? root.listenPort : "?"));
        if (root.anonymousWarning && root._networkRead)
            lines.push(qsTr("Anonymous mode hides which client you use, and most private trackers refuse clients they cannot name. Settings › Discovery and privacy."));
        return lines.join("\n");
    }
    /// The dot's colour: none dim, first sync yellow, lost red (yellow while retried), the
    /// network's warning in its colour, else green.
    readonly property color dotColor: !root.serverConfigured ? Theme.dimForeground
                                    : root.syncing ? Theme.yellow
                                    : !root.connected ? (root.retrying ? Theme.yellow : Theme.red)
                                    : root.networkWarning === "disconnected" ? Theme.red
                                    : root.networkWarning === "firewalled" || root.anonymousWarning ? Theme.yellow
                                    : Theme.green
    /// The lane's tooltip: the whole line, the warning and its advice, and the key.
    readonly property string serverTipText: (root.networkText.length > 0 ? root.serverText + " · " + root.networkText
                                                                         : root.serverText)
                                            + (root.networkAdvice.length > 0 ? "\n" + root.networkAdvice : "")
                                            + (root.serverSwitchable ? "\n" + qsTr("switch server · ctrl+s") : "")

    // ---- the speeds ----------------------------------------------------------------------
    property string downSpeed: "—"
    property string upSpeed: "—"
    /// Short forms ("4.21M"), from step 3.
    property string downSpeedShort: root.downSpeed
    property string upSpeedShort: root.upSpeed
    /// #24: what ↑ adds for a screen reader and its tooltip, e.g. "18.4 GB today".
    property string upNote: ""
    /// #24: the ↑ figure opens the seed console (the rail's "seed" does the same).
    property bool upOpensSeed: false

    // ---- alt speeds (docs/31, #167 assumed 5) ------------------------------------------
    property bool altSpeedActive: false
    property string altDownLimit: "∞"
    property string altUpLimit: "∞"
    property string altDownLimitShort: root.altDownLimit
    property string altUpLimitShort: root.altUpLimit
    /// "burst", "turtle" or "alt" (mixed, the same, or not known).
    property string altKind: "alt"
    /// When the schedule ends the set ("07:00"); "" for no end.
    property string altUntil: ""
    /// The chip's tooltip; "" for today's ("alt-speed on · A").
    property string altTooltip: ""
    readonly property bool altNamed: root.altKind === "burst" || root.altKind === "turtle"

    // ---- the facts -----------------------------------------------------------------------
    /// `[{value, label, spoken}]`, value in the foreground and label dim, joined with `·`.
    property var facts: []
    /// #79: the facts as a screen reader says them.
    property string factsSpoken: ""

    // ---- the note slot (docs/DECISIONS.md M16, docs/35 N8, docs/31 F1, docs/32 N1) ------
    property string noteText: ""
    /// The mark before the note: "▲" "✓" green, "▼" yellow, "✕" red; "" for none.
    property string noteMark: ""
    /// A failure qBittorrent's log told of: yellow.
    property bool noteWarn: false
    /// The note after A, or a failure: the foreground, until the next key.
    property bool noteSticky: false
    /// The note is a failure with details: a "details" link after it.
    property bool noteHasDetails: false
    /// The band with those details is open.
    property bool noteDetailsOpen: false
    /// docs/32 N1: "· i details" after the note, while the pane that holds the block is hidden.
    property bool noteHintShown: false

    /// docs/32 D12: the log's face shows whether it follows new lines.
    property bool logFollowShown: false
    property bool logFollowing: true

    // ---- the steps -----------------------------------------------------------------------
    /// The step the line is on, from its own width (see the top).
    readonly property int level: {
        const steps = Theme.statusLineSteps;
        for (let i = 0; i < steps.length; ++i) {
            if (root.width >= steps[i])
                return i;
        }
        return steps.length;
    }

    /// The parts tests and the walks read.
    readonly property alias serverLane: lane
    readonly property alias altChip: altChip
    readonly property alias networkLabel: networkLabel
    readonly property alias tip: tip

    signal serverClicked
    signal upClicked
    signal altSpeedClicked
    signal keysClicked
    signal detailsToggled
    signal noteHintClicked

    /// The alt-speed chip's words at `step` (docs/31 H1–H4): off, the glyph alone; burst and
    /// turtle lead with their word, which keeps only itself from step 2; mixed or the same
    /// prints the limits; from step 3 the glyph alone.
    function altText(step: int): string {
        if (step >= 3 || !root.altSpeedActive)
            return "";
        const limits = step >= 1 ? qsTr("↓%1 ↑%2").arg(root.altDownLimitShort).arg(root.altUpLimitShort)
                                 : qsTr("↓%1 ↑%2").arg(root.altDownLimit).arg(root.altUpLimit);
        if (!root.altNamed)
            return limits;
        return step >= 2 ? root.altKind : root.altKind + " " + limits;
    }

    /// What follows the words, a little fainter: "· until 07:00" (docs/31 D5).
    function altSuffix(step: int): string {
        if (step >= 3 || !root.altSpeedActive || !root.altNamed || root.altUntil.length === 0)
            return "";
        return qsTr("· until %1").arg(root.altUntil);
    }

    implicitHeight: Theme.statusLineHeight
    implicitWidth: Theme.space(900)

    Hairline {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.leftMargin: Theme.statusLineInsetStart
        anchors.rightMargin: Theme.statusLineInsetEnd
        spacing: Theme.statusLineGap

        /// What every part but the facts, the note and the spacer needs, with the spacing
        /// between all the parts shown: the facts' room is what is left.
        readonly property real othersWidth: {
            let used = 0;
            let shown = 0;
            for (const child of row.children) {
                if (!child.visible)
                    continue;
                shown += 1;
                if (child === factsBox || child === noteText || child === spacer)
                    continue;
                // Rounded up, as the layout rounds each part onto whole pixels.
                used += Math.ceil(child === lane ? lane.minimumWidth : child === networkLabel ? networkLabel.minimumWidth
                                                                                              : child.implicitWidth);
            }
            return used + Math.max(0, shown - 1) * row.spacing + 1;
        }

        // The server lane: the dot, the name, ▾ with two servers.
        Item {
            id: lane

            readonly property string shown: root.level >= 1 && root.serverShortText.length > 0
                                            ? root.serverShortText : root.serverText
            /// Its width with nothing elided, from the parts' implicit widths (the name's own
            /// width follows the lane's, so the row's implicit width cannot size it).
            // Rounded up: a layout puts parts on whole pixels, and a text a fraction short of
            // its width elides.
            readonly property real naturalWidth: Math.ceil(dot.width + laneRow.spacing + laneName.implicitWidth
                                                           + (chevron.visible ? laneRow.spacing + chevron.implicitWidth : 0))
            /// The least it takes: the dot, a letter or two and the ▾.
            readonly property real minimumWidth: Math.min(lane.naturalWidth, Theme.statusLineDotSize
                                                          + Theme.statusLineLaneGap + Theme.space(24))

            // #77, #79: with two servers the lane is the "switch server" button, its words the
            // description; with one it is the server's words, a static text.
            Accessible.description: root.serverSwitchable ? lane.shown : ""
            Accessible.name: root.serverSwitchable ? qsTr("switch server") : lane.shown
            Accessible.role: root.serverSwitchable ? Accessible.Button : Accessible.StaticText
            Accessible.onPressAction: if (root.enabled && root.serverSwitchable)
                                          root.serverClicked()
            Layout.alignment: Qt.AlignVCenter
            Layout.fillHeight: true
            // A layout gives an item that does not fill its preferred width and no less: the
            // lane, the warning and the note fill up to their own width, so they are what
            // gives way (elides) when the line is short.
            Layout.fillWidth: true
            Layout.maximumWidth: lane.naturalWidth
            Layout.minimumWidth: lane.minimumWidth
            Layout.preferredWidth: lane.naturalWidth
            objectName: "statusServer"

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: -Theme.statusLineHoverInset
                anchors.rightMargin: -Theme.statusLineHoverInset
                anchors.topMargin: Theme.statusLineHoverInset
                anchors.bottomMargin: Theme.statusLineHoverInset
                color: Theme.fill(Theme.fillHover)
                radius: Theme.cornerRadius
                visible: root.serverSwitchable && laneMouse.containsMouse
            }

            Row {
                id: laneRow

                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.statusLineLaneGap

                Rectangle {
                    id: dot

                    anchors.verticalCenter: parent.verticalCenter
                    color: root.dotColor
                    height: Theme.statusLineDotSize
                    objectName: "statusDot"
                    radius: width / 2
                    width: Theme.statusLineDotSize
                }

                Text {
                    id: laneName

                    // The lane says it (#79).
                    Accessible.ignored: true
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.foreground
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.statusLineFontSize
                    font.weight: Theme.statusLineNameWeight
                    objectName: "statusServerName"
                    text: lane.shown
                    width: Math.min(Math.ceil(laneName.implicitWidth), lane.width - dot.width - laneRow.spacing
                                    - (chevron.visible ? chevron.implicitWidth + laneRow.spacing : 0))
                }

                Text {
                    id: chevron

                    Accessible.ignored: true
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.statusLineFontSize
                    objectName: "statusServerChevron"
                    text: Glyphs.chevronDown
                    visible: root.serverSwitchable
                }
            }

            MouseArea {
                id: laneMouse

                anchors.fill: parent
                cursorShape: root.serverSwitchable ? Qt.PointingHandCursor : Qt.ArrowCursor
                hoverEnabled: true
                objectName: "statusServerMouse"

                onClicked: if (root.serverSwitchable)
                               root.serverClicked()
                onContainsMouseChanged: laneMouse.containsMouse ? tip.showFor(lane, root.serverTipText, true)
                                                                : tip.hide()
            }
        }

        Text {
            id: downText

            // #79: "download", its speed the description, on every step.
            Accessible.description: root.downSpeed
            Accessible.name: qsTr("download")
            Accessible.role: Accessible.StaticText
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: downText.implicitWidth
            color: Theme.stateDownloading
            font.family: Theme.fontFamily
            font.pixelSize: Theme.statusLineFontSize
            objectName: "statusDown"
            text: root.level >= 3 ? Glyphs.arrowDown + root.downSpeedShort : Glyphs.arrowDown + " " + root.downSpeed
        }

        Text {
            id: upText

            // #77: with `upOpensSeed`, ↑ is a button to the seed console, its speed and the
            // day's total the description; otherwise the "upload" figure (#79).
            Accessible.description: [root.upSpeed, root.upNote].filter(part => part.length > 0).join(" · ")
            Accessible.name: root.upOpensSeed ? qsTr("seed console") : qsTr("upload")
            Accessible.role: root.upOpensSeed ? Accessible.Button : Accessible.StaticText
            Accessible.onPressAction: if (root.enabled && root.upOpensSeed)
                                          root.upClicked()
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: upText.implicitWidth
            color: upMouse.containsMouse && root.upOpensSeed ? Theme.accent : Theme.stateSeeding
            font.family: Theme.fontFamily
            font.pixelSize: Theme.statusLineFontSize
            objectName: "statusUp"
            text: root.level >= 3 ? Glyphs.arrowUp + root.upSpeedShort : Glyphs.arrowUp + " " + root.upSpeed

            MouseArea {
                id: upMouse

                anchors.fill: parent
                anchors.margins: -Theme.space(3)
                cursorShape: root.upOpensSeed ? Qt.PointingHandCursor : Qt.ArrowCursor
                hoverEnabled: true

                onClicked: if (root.upOpensSeed)
                               root.upClicked()
                onContainsMouseChanged: upMouse.containsMouse && root.upOpensSeed
                                        ? tip.showFor(upText, [qsTr("seed console · `"), root.upNote].filter(
                                                          part => part.length > 0).join("\n"), true)
                                        : tip.hide()
            }
        }

        // Alt-speed: its glyph alone while off, named by its tooltip; on, the set's words in
        // the warning role (burst green), so the state reads from across the room. A click
        // is `A` (docs/31, #167 assumed 5).
        Item {
            id: altChip

            readonly property string text: root.altText(root.level)
            readonly property string suffix: root.altSuffix(root.level)
            readonly property string glyph: root.altKind === "burst" ? Glyphs.burst : Glyphs.altSpeed
            readonly property bool warn: root.altSpeedActive
            readonly property color tone: !root.altSpeedActive ? Theme.dimForeground
                                                               : root.altKind === "burst" ? Theme.green : Theme.yellow
            readonly property string tooltip: root.altTooltip.length > 0 ? root.altTooltip
                                              : root.altSpeedActive ? qsTr("alt-speed on · A")
                                                                    : qsTr("alt-speed · A")

            // #78: named by the set it switches, never by the limits or the time it prints;
            // the tooltip is the description.
            Accessible.description: altChip.tooltip
            Accessible.name: root.altKind === "burst" ? qsTr("burst")
                                                      : root.altKind === "turtle" ? qsTr("turtle")
                                                                                  : qsTr("alt-speed")
            Accessible.role: Accessible.Button
            Accessible.onPressAction: if (root.enabled)
                                          root.altSpeedClicked()
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: altRow.implicitWidth
            implicitHeight: altRow.implicitHeight
            implicitWidth: altRow.implicitWidth
            objectName: "statusAlt"
            // The narrowest step keeps it only while it throttles; A still toggles it.
            visible: root.level < 4 || root.altSpeedActive

            Row {
                id: altRow

                spacing: Theme.space(4)

                Glyph {
                    anchors.verticalCenter: parent.verticalCenter
                    color: altMouse.containsMouse ? Theme.accent : altChip.tone
                    pixelSize: Theme.statusLineFontSize
                    text: altChip.glyph
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: altChip.tone
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.statusLineFontSize
                    objectName: "statusAltText"
                    text: altChip.text
                    visible: altChip.text.length > 0
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.statusLineFontSize
                    text: altChip.suffix
                    visible: altChip.suffix.length > 0
                }
            }

            MouseArea {
                id: altMouse

                anchors.fill: parent
                anchors.margins: -Theme.space(3)
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: root.altSpeedClicked()
                onContainsMouseChanged: altMouse.containsMouse ? tip.showFor(altChip, altChip.tooltip, true)
                                                               : tip.hide()
            }
        }

        Text {
            id: networkLabel

            /// The least it takes before it elides.
            readonly property real minimumWidth: Math.min(networkLabel.implicitWidth, Theme.space(60))

            // #79: a warning that comes on its own, the tooltip's advice its description.
            Accessible.description: root.networkAdvice
            Accessible.name: root.networkText
            Accessible.role: Accessible.AlertMessage
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.maximumWidth: Math.ceil(networkLabel.implicitWidth)
            Layout.minimumWidth: networkLabel.minimumWidth
            color: root.networkWarning === "disconnected" ? Theme.red : Theme.yellow
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.statusLineFontSize
            objectName: "statusNetwork"
            text: root.networkText
            visible: root.networkText.length > 0

            HoverHandler {
                onHoveredChanged: hovered ? tip.showFor(networkLabel, root.networkAdvice, true) : tip.hide()
            }
        }

        // The facts keep their width while the line has it; the last resort on a line too
        // short for its fixed parts, they are cut at the right rather than push "? keys" out.
        Item {
            id: factsBox

            // #79: the facts as one label in whole words ("16 torrents, 7 downloading, 1.80 TB
            // free"); the texts it is drawn from publish nothing themselves.
            Accessible.name: root.factsSpoken
            Accessible.role: Accessible.StaticText
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.maximumWidth: facts.implicitWidth
            Layout.minimumWidth: Math.min(facts.implicitWidth, Math.max(0, row.width - row.othersWidth))
            Layout.preferredWidth: facts.implicitWidth
            clip: true
            implicitHeight: facts.implicitHeight
            implicitWidth: facts.implicitWidth
            objectName: "statusFacts"
            visible: root.facts.length > 0

            Row {
                id: facts

                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.statusLineGap

                Repeater {
                    model: root.facts

                    Row {
                        id: factRow

                        required property var modelData

                        spacing: Theme.statusLineGap

                        Text {
                            Accessible.ignored: true
                            color: Theme.dimForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.statusLineFontSize
                            text: "·"
                        }

                        Row {
                            spacing: Theme.space(4)

                            Text {
                                Accessible.ignored: true
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.statusLineFontSize
                                text: factRow.modelData.value
                                visible: factRow.modelData.value.length > 0
                            }

                            Text {
                                Accessible.ignored: true
                                color: Theme.dimForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.statusLineFontSize
                                text: factRow.modelData.label
                                visible: factRow.modelData.label.length > 0
                            }
                        }
                    }
                }
            }
        }

        // The note sits in the line, after the facts (docs/DECISIONS.md M16). docs/31 F1: the
        // note after A leads with the set's mark, ▲ for burst in green and ▼ for turtle in
        // yellow, and reads in the foreground.
        Text {
            Layout.alignment: Qt.AlignVCenter
            color: root.noteMark === "▲" || root.noteMark === "✓" ? Theme.green
                                                                   : root.noteMark === "✕" ? Theme.red : Theme.yellow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.statusLineFontSize
            objectName: "footerNoteMark"
            text: root.noteMark
            visible: root.noteText.length > 0 && root.noteMark.length > 0
        }

        Text {
            id: noteText

            // #79: the note comes on its own ("copied", a failure, the set after A): an alert
            // by its words. Its ▲ ▼ mark stays out; the words name the set.
            Accessible.name: noteText.text
            Accessible.role: Accessible.AlertMessage
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            // Whole pixels: a text a fraction short of its width elides.
            Layout.maximumWidth: Math.ceil(noteText.implicitWidth)
            Layout.minimumWidth: 0
            Layout.preferredWidth: Math.ceil(noteText.implicitWidth)
            // docs/32 N1: a failure the log told of is yellow, as its block is.
            color: root.noteWarn ? Theme.yellow : root.noteSticky ? Theme.foreground : Theme.accent
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.statusLineFontSize
            objectName: "footerNote"
            text: root.noteText
            visible: root.noteText.length > 0
        }

        // docs/35 N8: the sentence's "details" open the band above the line.
        Text {
            id: detailsLink

            // #77: a "details" button; its press is the click.
            Accessible.name: qsTr("details")
            Accessible.role: Accessible.Button
            Accessible.onPressAction: if (root.enabled)
                                          root.detailsToggled()
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: detailsLink.implicitWidth
            color: detailsMouse.containsMouse ? Theme.accent : Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.statusLineFontSize
            objectName: "footerFailureDetails"
            // The narrowest step keeps its chevron; the name stays "details".
            text: (root.level >= 4 ? "" : qsTr("details") + " ")
                  + (root.noteDetailsOpen ? Glyphs.chevronDown : Glyphs.chevronRight)
            visible: root.noteText.length > 0 && root.noteHasDetails

            MouseArea {
                id: detailsMouse

                anchors.fill: parent
                anchors.margins: -Theme.space(4)
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: root.detailsToggled()
            }
        }

        // docs/32 N1: "· i details", while the pane that holds the block is hidden.
        Text {
            id: noteHint

            // #77: a "show details" button with its key; its press is the click.
            Accessible.description: qsTr("shortcut %1").arg("i")
            Accessible.name: qsTr("show details")
            Accessible.role: Accessible.Button
            Accessible.onPressAction: if (root.enabled && noteHint.visible)
                                          root.noteHintClicked()
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: noteHint.implicitWidth
            color: noteHintMouse.containsMouse ? Theme.accent : Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.statusLineFontSize
            objectName: "footerNoteHint"
            text: "· i " + qsTr("details")
            visible: root.noteText.length > 0 && root.noteHintShown

            MouseArea {
                id: noteHintMouse

                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: root.noteHintClicked()
            }
        }

        Item {
            id: spacer

            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }

        // docs/32 D12 (L1, L2): whether the log follows new lines, on its face only.
        Text {
            // #79: the log's state by its word, without the ● or ○.
            Accessible.name: root.logFollowing ? qsTr("following") : qsTr("paused")
            Accessible.role: Accessible.StaticText
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: implicitWidth
            color: root.logFollowing ? Theme.green : Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.statusLineFontSize
            objectName: "logFollowState"
            text: root.logFollowing ? qsTr("● following") : qsTr("○ paused")
            visible: root.logFollowShown
        }

        // "? keys": the `?` sheet, where every key is (C24 H10: keys on demand).
        Text {
            id: keys

            Accessible.description: qsTr("shortcut %1").arg("?")
            Accessible.name: qsTr("keys")
            Accessible.role: Accessible.Button
            Accessible.onPressAction: if (root.enabled)
                                          root.keysClicked()
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: keys.implicitWidth
            color: keysMouse.containsMouse ? Theme.accent : Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.statusLineFontSize
            objectName: "statusKeys"
            text: root.level >= 3 ? "?" : qsTr("? keys")

            MouseArea {
                id: keysMouse

                anchors.fill: parent
                anchors.margins: -Theme.space(4)
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: root.keysClicked()
                onContainsMouseChanged: keysMouse.containsMouse ? tip.showFor(keys, qsTr("keys and help · ?"), true)
                                                                : tip.hide()
            }
        }
    }

    // The line's one tooltip, over the window rather than the line (KIT-8): clamped inside a
    // line one row tall, it would have no room and cover what it spells out.
    Tooltip {
        id: tip

        objectName: "statusTip"
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 1000
    }
}
