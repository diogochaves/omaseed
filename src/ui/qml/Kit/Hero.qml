pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The hero row (spec v2 §1 Fig. 1 `.hero`): the mark, app name with a
// tracked server caption and status dot beneath, two headline speed `Stat`s, a search `Field`
// with a `/` hint and live count, four glyph `IconButton`s, and a hairline rule underneath.
// Only `Theme` tokens; the caller owns state and reacts to the signals.
//
// Narrow windows (docs/10 §2, the ladder): nothing is ever cut off. As the hero narrows it
// takes steps (`level`): the server line drops its version and the alt-speed chip goes short
// (1), the speed captions go and the gear and `?` move into ⋯ (2, `compact`), the filter
// folds into a `/` button that opens over the speeds and the chip keeps only its glyph (3),
// and the name and server line give way to the dot (4). The mark, the dot, both speeds, `+`
// and ⋯ never leave, and if something still does not fit the server line elides first.
// On step 4 the speeds print short (`↓4.21M ↑812K`) and the filter chips leave the row: the
// `/` button carries them, lit with their count, and `filtersClicked` asks the caller for the
// menu that lists them (docs/15 Q1 A).
Item {
    id: root

    property string appName: "omaseed"
    property bool connected: true
    /// No server is configured at all. The dot then reads as absent — the spec's `.dot.off`,
    /// the dim foreground — instead of the green `connected: true` would otherwise draw.
    property bool serverConfigured: true
    /// The first sync has not landed yet (spec §7 Fig. 25): the mark stays accent, the dot
    /// goes yellow and the caller writes "… · first sync" into `serverLine`.
    property bool syncing: false
    /// docs/35 N3: the server is gone and omaseed retries on its own; the dot goes yellow,
    /// not red (red is for a server that stopped and needs you).
    property bool retrying: false
    /// docs/33 D3: with two servers or more the server line takes a ▾, a hover fill and a
    /// click (`serverClicked`), the lone dot on the narrowest step too. One server: as v2.
    property bool serverSwitchable: false
    /// " ▾" after the server line while it switches.
    readonly property string _switchMark: root.serverSwitchable ? " " + Glyphs.chevronDown : ""
    /// qBittorrent's own view of its network: "connected", "firewalled" or "disconnected"
    /// (`server_state.connection_status`). Silent while connected; the other two are named in
    /// the server line in their colour (docs/08 §3). Only read while the API itself answers.
    property string connectionStatus: "connected"
    /// What the server line adds after itself: "firewalled", "disconnected" or nothing.
    readonly property string networkWarning: (root.connected && !root.syncing
                                              && root.serverConfigured
                                              && (root.connectionStatus === "firewalled"
                                                  || root.connectionStatus === "disconnected"))
                                             ? root.connectionStatus : ""
    /// qBittorrent's listening port (docs/16 §5): "firewalled" then reads "port 26999 not
    /// reachable". 0 while it is not known, and the plain word stays.
    property int listenPort: 0
    /// docs/16 §5: anonymous mode is on while private torrents are in the library. The server
    /// line says so in the warning colour; most private trackers refuse such a client.
    property bool anonymousWarning: false
    /// What the server line adds after itself, in words: the network's state and the
    /// anonymous-mode warning, joined; "" when there is nothing to say.
    readonly property string networkText: {
        const parts = [];
        if (root.networkWarning === "firewalled")
            parts.push(root.listenPort > 0 ? qsTr("port %1 not reachable").arg(root.listenPort)
                                           : qsTr("firewalled"));
        else if (root.networkWarning === "disconnected")
            parts.push(qsTr("disconnected"));
        if (root.anonymousWarning && root.connected && !root.syncing && root.serverConfigured)
            parts.push(qsTr("anonymous mode"));
        return parts.join(" · ");
    }
    /// What hovering the warning explains: what it means and where to change it.
    readonly property string networkAdvice: {
        const lines = [];
        if (root.networkWarning === "firewalled")
            lines.push(qsTr("Peers cannot reach port %1. Forward it on your router, or turn on UPnP in Settings › Connection (ctrl+shift+,).").arg(
                           root.listenPort > 0 ? root.listenPort : "?"));
        if (root.anonymousWarning && root.connected && !root.syncing && root.serverConfigured)
            lines.push(qsTr("Anonymous mode hides which client you use, and most private trackers refuse clients they cannot name. Settings › Discovery and privacy."));
        return lines.join("\n");
    }
    /// The server is on its alternative speed limits: the glyph turns accent (`.btn.icon.on`).
    property bool altSpeedActive: false
    /// The limits in force while alt-speed is on ("10 KB/s", "∞"); the chip prints them.
    property string altDownLimit: "∞"
    property string altUpLimit: "∞"
    /// The same limits in short form ("1.00M"), for the chip from step 1.
    property string altDownLimitShort: root.altDownLimit
    property string altUpLimitShort: root.altUpLimit
    /// docs/31 (DECISIONS.md C4): which way the alternative set goes, "burst", "turtle" or
    /// "alt" (mixed, the same, or not known). Burst and turtle put their word before the
    /// limits; burst is green with the needle up. "alt" is today's chip.
    property string altKind: "alt"
    /// When the schedule ends the alternative limits ("07:00", "Mon 07:00"); "" for no end.
    property string altUntil: ""
    /// The chip's tooltip; "" for today's ("alt-speed on · A").
    property string altTooltip: ""
    /// Burst or turtle: the chip leads with its word.
    readonly property bool altNamed: root.altKind === "burst" || root.altKind === "turtle"
    property string downSpeed: "—"
    /// The speeds in their short form ("4.21M"), printed on step 4 (docs/15 Q1 A).
    property string downSpeedShort: root.downSpeed
    property string upSpeedShort: root.upSpeed
    // Exposed read-only so `tst_Hero.qml` can drive the inner Field directly instead of
    // simulating keystrokes.
    property alias searchField: searchField
    /// The connection dot (spec `.dot`), exposed so a test can read the colour it resolved to.
    readonly property alias statusDot: statusDot
    /// The download figure, so a test can see which side `speedsAlignment` put the pair on.
    readonly property alias downStat: downStat
    /// The network word after the server line, for tests.
    readonly property alias networkLabel: networkLabel
    /// The step-4 filter button, so the caller can hang the filters' menu under it.
    readonly property alias filterButton: filterButton
    /// The ⋯ button, so the caller can hang its menu under it.
    readonly property alias moreButton: moreButton
    /// The alt-speed chip.
    readonly property alias altChip: altChip
    /// The `+`, so a test can see how it is drawn.
    readonly property alias addButton: addButton
    /// The whole-server-line tooltip. It lives on the window's content item, not in the hero.
    readonly property alias serverTip: serverTip
    property int searchCount: 0
    /// What the field's trailing count prints. Fig. 1 shows the bare count, Fig. 24 shows
    /// "0 of 142"; the caller decides, and the plain count is the default.
    property string searchCountText: String(root.searchCount)
    property string searchText: ""
    property string serverLine: ""
    /// The server line without the version, printed from step 1; the whole line stays in the
    /// tooltip. Empty means "the same as `serverLine`".
    property string serverLineShort: ""
    property string upSpeed: "—"

    /// docs/10 §2: the step the hero is on, from its own width (the window's). Thresholds are
    /// base-12 pixels through `Theme.space`, so they scale with the base size: 1500, 1150,
    /// 900 and 700 px at base 14.
    /// With filter chips up (`filterChips`, KIT-5) the step is also the first one whose row,
    /// measured part by part (`_rowWidth`), still holds the chips: whole for steps 0–2, folded
    /// to glyph and ✕ for 3–4, which is how they are drawn there. The chips use the slack the
    /// thresholds leave before anything folds (review VER-6: subtracting them from the
    /// window's width and then asking for the no-chip thresholds folded three steps early).
    /// Step 4 is where the ladder ends and the chips stop counting: there they go into the
    /// `/` button (`filtersInButton`, docs/15 Q1 A), so the row is as wide as with none.
    readonly property int level: {
        let step = 4;
        if (root.width >= Theme.space(1286))
            step = 0;
        else if (root.width >= Theme.space(986))
            step = 1;
        else if (root.width >= Theme.space(771))
            step = 2;
        else if (root.width >= Theme.space(600))
            step = 3;
        if (root.filterChips.length === 0)
            return step;
        const room = root.width - Theme.space(32);
        while (step < 4 && root._rowWidth(step) + root._chipsWidth(step >= 3) > room)
            ++step;
        return step;
    }

    /// The row's width at `step` without the chips and with nothing elided, from hidden
    /// copies of the parts that change with the step (`ladderMeasure`), so it does not read
    /// the drawn row, whose parts follow `level`.
    function _rowWidth(step: int): real {
        const buttons = step >= 2 ? 2 : 4;
        const identityWidth = step >= 4 ? loneDot.implicitWidth
                                        : Math.max(appNameText.implicitWidth,
                                                   Theme.space(7) + Theme.space(6)
                                                   + (step >= 1 && root.serverLineShort.length > 0
                                                      ? measureServerShort.implicitWidth
                                                      : measureServerFull.implicitWidth)
                                                   + (networkLabel.visible
                                                      ? Theme.space(6) + networkLabel.implicitWidth
                                                      : 0));
        const altWidth = step >= 3 ? measureAltGlyph.implicitWidth
                                   : step >= 2 ? measureAltWord.implicitWidth
                                               : step >= 1 ? measureAltShort.implicitWidth
                                                           : measureAltFull.implicitWidth;
        const searchWidth = step >= 3 ? addButton.implicitWidth : Theme.space([220, 180, 140][step]);
        // mark, identity (or the dot), two spacers, two speeds, search, alt-speed, buttons:
        // nine items and eight spacings; the chips bring their own (`_chipsWidth`).
        return Theme.space(24) + identityWidth + Theme.space(6) + measureDown.implicitWidth
                + measureUp.implicitWidth + searchWidth + altWidth
                + buttons * addButton.implicitWidth + (buttons - 1) * Theme.space(2)
                + 8 * heroRow.spacing;
    }
    /// The filter chips fold to their glyph and ✕ from step 3, as the alt-speed chip does.
    readonly property bool chipsFolded: root.level >= 3

    /// The room the filter chips take in the row, spacing included, drawn whole or folded.
    /// Measured on hidden copies (`chipMeasure`), so it does not depend on `level`.
    function _chipsWidth(folded: bool): real {
        let width = 0;
        for (let i = 0; i < chipMeasure.count; ++i) {
            // Each copy holds the whole chip (child 0) and the folded one (child 1).
            const measured = chipMeasure.itemAt(i);
            if (measured)
                width += measured.children[folded ? 1 : 0].implicitWidth + heroRow.spacing;
        }
        return width;
    }
    /// Step 4 with filter chips up: the chips leave the row and the `/` button carries them
    /// (docs/15 Q1 A). A name filter alone stays in the field the button opens.
    readonly property bool filtersInButton: root.level >= 4 && root.filterChips.length > 0
    /// Every filter that is up: the chips, and the name filter when it holds text.
    readonly property int filterCount: root.filterChips.length
                                       + (searchField.text.length > 0 ? 1 : 0)
    /// From step 2 the gear and `?` are not in the hero; the caller puts them in ⋯.
    readonly property bool compact: root.level >= 2
    /// From step 3 the filter is a button; this is true while it is open over the speeds.
    property bool searchOpen: false
    readonly property bool searchFolded: root.level >= 3 && !root.searchOpen
    /// The alt-speed chip's words for the current step: "" leaves the glyph alone.
    readonly property string altChipText: root._altText(root.level)
    /// What follows them, a little fainter: "· until 07:00" (docs/31 D5).
    readonly property string altChipSuffix: root._altSuffix(root.level)

    /// The chip's words at `step` (docs/31 H1–H4). Off, the glyph alone at every step; the
    /// tooltip names it (manual box M14). Burst and turtle lead with their word, which keeps
    /// only itself from step 2; mixed or the same is today's chip, limits only.
    function _altText(step: int): string {
        if (step >= 3 || !root.altSpeedActive)
            return "";
        const limits = step >= 1 ? qsTr("↓%1 ↑%2").arg(root.altDownLimitShort).arg(root.altUpLimitShort)
                                 : qsTr("↓%1 ↑%2").arg(root.altDownLimit).arg(root.altUpLimit);
        if (!root.altNamed)
            return limits;
        return step >= 2 ? root.altKind : root.altKind + " " + limits;
    }

    function _altSuffix(step: int): string {
        if (step >= 3 || !root.altSpeedActive || !root.altNamed || root.altUntil.length === 0)
            return "";
        return qsTr("· until %1").arg(root.altUntil);
    }

    /// `/`: focus the filter, opening it first when it is folded into its button. While the
    /// button carries the filters (step 4), it asks for their menu instead.
    function openSearch(): void {
        if (root.filtersInButton) {
            root.filtersClicked();
            return;
        }
        root.searchOpen = true;
        searchField.focusInput();
    }

    /// Filters to show as removable chips before the search field, `[{key, glyph, text}]`:
    /// the caller fills it while the rail that would show them is hidden (docs/08 §5).
    property var filterChips: []

    /// "left" (beside the server name, the spec's placement) or "right" (against the search
    /// field). Anything else reads as "left".
    property string speedsAlignment: "left"
    /// #24 T2: the last 60 s of upload, drawn faintly behind the ↑ number; empty draws nothing.
    property var upSpark: []
    /// The same for download, behind the ↓ number.
    property var downSpark: []
    /// #24 T2: what the ↑ caption adds after "upload", e.g. "18.4 GB today".
    property string upNote: ""
    /// #24: the window's two faces, `[{key, label, hint}]`, drawn as tabs after the speeds
    /// (docs/22 D1 a); empty draws none.
    property var views: []
    property string currentView: ""

    signal addClicked
    /// #24: a view tab, or the ↑ number (which opens the seed console).
    signal viewSelected(string key)
    signal upClicked
    signal altSpeedClicked
    signal helpClicked
    signal moreClicked
    /// The filter button on step 4, or `/` there: the caller opens the filters' menu.
    signal filtersClicked
    /// The ✕ of one of `filterChips`.
    signal filterChipCleared(string key)
    signal searchChanged(string text)
    /// `enter` in the search field: the filter is already live, so this only says "done".
    signal searchAccepted(string text)
    signal searchCleared
    signal settingsClicked
    /// docs/33 D3: the server line (or the lone dot) was clicked; the caller opens the
    /// server palette under it.
    signal serverClicked

    height: implicitHeight
    implicitHeight: heroRow.implicitHeight + Theme.space(24) + 1
    implicitWidth: Theme.space(600)
    width: implicitWidth

    RowLayout {
        id: heroRow

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.top: parent.top
        anchors.topMargin: Theme.space(12)
        spacing: Theme.space(14)

        Glyph {
            id: mark

            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: Theme.space(24)
            Layout.preferredWidth: Theme.space(24)
            color: (root.connected || root.syncing || !root.serverConfigured) ? Theme.accent
                                                                              : Theme.red
            pixelSize: Theme.space(24)
            text: Glyphs.mark

            HoverHandler {
                id: markHover
            }
        }

        // Step 4: the name and server line are gone; the dot stays beside the mark, in the
        // same colour, and the words move into the tooltip.
        Rectangle {
            id: loneDot

            Layout.alignment: Qt.AlignVCenter
            color: statusDot.color
            implicitHeight: Theme.space(7)
            implicitWidth: Theme.space(7)
            objectName: "loneDot"
            radius: width / 2
            visible: root.level >= 4 && !root.searchOpen

            HoverHandler {
                id: dotHover
            }

            // docs/33 D3: at the narrowest step the dot takes the server line's click.
            MouseArea {
                anchors.fill: parent
                anchors.margins: -Theme.space(4)
                cursorShape: Qt.PointingHandCursor
                enabled: root.serverSwitchable
                objectName: "loneDotMouse"

                onClicked: root.serverClicked()
            }
        }

        Column {
            id: identity

            /// The width it would take with nothing elided, from the texts' own implicit
            /// widths: the server label's `width` follows the column, so the column's
            /// `implicitWidth` cannot be what sizes it.
            readonly property real naturalWidth: Math.max(appNameText.implicitWidth,
                                                          statusDot.width + Theme.space(6)
                                                          + serverLabel.implicitWidth
                                                          + (networkLabel.visible
                                                             ? Theme.space(6)
                                                               + networkLabel.implicitWidth
                                                             : 0))

            // Allowed to narrow below its natural width, never above it: the server line is
            // the first thing in the hero to elide.
            /// What the rest of the row leaves it: every other visible item at its own width,
            /// plus the spacing between them all.
            readonly property real room: {
                let used = 0;
                let shown = 0;
                for (const child of heroRow.children) {
                    if (!child.visible)
                        continue;
                    // The layout leaves the Repeater out, so no spacing sits beside it (KIT-9).
                    // The two spacers stay in, even at no width, and do take a spacing.
                    if (child === chipRepeater)
                        continue;
                    shown += 1;
                    if (child === identity)
                        continue;
                    // Rounded up, as the layout rounds each item onto whole pixels.
                    used += child === searchField ? Theme.space([220, 180, 140, 140, 140][root.level])
                                                  : Math.ceil(child.implicitWidth);
                }
                return heroRow.width - used - Math.max(0, shown - 1) * heroRow.spacing
                        - Theme.space(6); // the download figure's left margin
            }

            /// Narrower than it would like: the server line is eliding.
            readonly property bool squeezed: identity.width + 0.5 < identity.naturalWidth

            // Sized here rather than with `fillWidth`: a filling item competes with the
            // spacers even when there is no room to spare, and would give up width they keep.
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: identity.naturalWidth
            Layout.preferredWidth: Math.max(Theme.space(48), Math.min(identity.naturalWidth,
                                                                      identity.room))
            Layout.minimumWidth: Theme.space(48)
            clip: true
            spacing: Theme.space(1)
            visible: root.level < 3 || (root.level === 3 && !root.searchOpen)

            HoverHandler {
                id: identityHover
            }


            Text {
                id: appNameText

                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.subtitle
                font.weight: Font.DemiBold
                text: root.appName
            }

            Row {
                id: serverRow

                spacing: Theme.space(6)

                Rectangle {
                    id: statusDot

                    anchors.verticalCenter: parent.verticalCenter
                    color: !root.serverConfigured ? Theme.dimForeground : root.syncing ? Theme.yellow : !root.connected ? (root.retrying ? Theme.yellow : Theme.red) : root.networkWarning === "disconnected" ? Theme.red : root.networkWarning === "firewalled" || root.anonymousWarning ? Theme.yellow : Theme.green
                    height: Theme.space(7)
                    radius: width / 2
                    width: Theme.space(7)
                }

                Label {
                    id: serverLabel

                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    objectName: "serverLine"
                    text: (root.level >= 1 && root.serverLineShort.length > 0 ? root.serverLineShort
                                                                               : root.serverLine)
                          + root._switchMark
                    tracked: true
                    // Squeezed with a warning to show, the warning wins over the server's name.
                    visible: !(identity.squeezed && networkLabel.visible)
                    width: Math.max(0, Math.min(serverLabel.implicitWidth, identity.width - statusDot.width
                                    - Theme.space(6) - (networkLabel.visible
                                                        ? networkLabel.implicitWidth + Theme.space(
                                                              6) : 0)))

                    // docs/33 D3: the line that names the server is the one that changes it:
                    // a hover fill over the dot and the words, and a click opens the server
                    // palette under it. Children of the label, so the row does not lay them out.
                    Rectangle {
                        color: serverMouse.containsMouse ? Theme.fill(Theme.fillHover) : "transparent"
                        height: serverLabel.height + Theme.space(4)
                        visible: root.serverSwitchable
                        width: serverMouse.width + Theme.space(4)
                        x: serverMouse.x - Theme.space(2)
                        y: -Theme.space(2)
                        z: -1
                    }

                    MouseArea {
                        id: serverMouse

                        Accessible.ignored: !root.serverSwitchable
                        Accessible.name: qsTr("switch server")
                        Accessible.role: Accessible.Button
                        // A screen reader's press is the click (#75): only while the server
                        // can switch, and not while disabled. A MouseArea's own `enabled` is
                        // its flag alone, so the label's says whether the hero is disabled.
                        Accessible.onPressAction: if (serverLabel.enabled && serverMouse.enabled)
                                                      root.serverClicked()
                        cursorShape: Qt.PointingHandCursor
                        enabled: root.serverSwitchable
                        height: serverLabel.height
                        hoverEnabled: true
                        objectName: "serverLineMouse"
                        width: serverLabel.width + statusDot.width + Theme.space(6)
                        x: -(statusDot.width + Theme.space(6))

                        onClicked: root.serverClicked()
                    }
                }

                Label {
                    id: networkLabel

                    anchors.verticalCenter: parent.verticalCenter
                    color: root.networkWarning === "disconnected" ? Theme.red : Theme.yellow
                    elide: Text.ElideRight
                    text: "· " + root.networkText
                    tracked: true
                    visible: root.networkText.length > 0

                    HoverHandler {
                        id: networkHover
                    }
                    width: Math.max(0, Math.min(networkLabel.implicitWidth, identity.width
                                                - statusDot.width - Theme.space(6)))
                }
            }
        }

        // The speed pair sits beside the server name (the spec's own placement) or against
        // the search field, per `speedsAlignment`. Exactly one of the two spacers takes the
        // slack, so the pair moves without the row being declared twice.
        Item {
            id: spacerLeft

            Layout.fillWidth: root.speedsAlignment === "right"
            objectName: "speedsSpacerLeft"
        }

        // T2: each trend sits under its number, a hairline at low alpha. A wrapper, so the
        // line is not one of the Stat column's own rows. `downStat` is the wrapper (tests read
        // its place and its value).
        Item {
            id: downStat

            readonly property string value: downInner.value

            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: Theme.space(6)
            implicitHeight: downInner.implicitHeight
            implicitWidth: downInner.implicitWidth
            visible: !(root.level >= 3 && root.searchOpen)

            Sparkline {
                color: Theme.stateDownloading
                fillAlpha: 0.08
                // Behind the number only: the caption under it stays clean.
                height: Theme.space(20)
                lineAlpha: 0.55
                values: root.downSpark
                visible: root.downSpark.length > 1
                width: downStat.width + Theme.space(12)
                x: -Theme.space(4)
                y: Theme.space(1)
            }

            Stat {
                id: downInner

                glyph: Glyphs.arrowDown
                glyphColor: Theme.stateDownloading
                label: root.level >= 2 ? "" : qsTr("download")
                value: root.level >= 4 ? root.downSpeedShort : root.downSpeed
            }
        }

        Item {
            id: upBox

            Layout.alignment: Qt.AlignVCenter
            implicitHeight: upStat.implicitHeight
            implicitWidth: upStat.implicitWidth
            visible: !(root.level >= 3 && root.searchOpen)

            Sparkline {
                fillAlpha: 0.08
                // Behind the number only: the caption under it stays clean.
                height: Theme.space(20)
                lineAlpha: 0.55
                values: root.upSpark
                visible: root.upSpark.length > 1
                width: upBox.width + Theme.space(12)
                x: -Theme.space(4)
                y: Theme.space(1)
            }

            Stat {
                id: upStat

                glyph: Glyphs.arrowUp
                glyphColor: Theme.stateSeeding
                label: root.level >= 2 ? "" : root.upNote.length > 0 ? qsTr("upload · %1").arg(root.upNote)
                                                                   : qsTr("upload")
                value: root.level >= 4 ? root.upSpeedShort : root.upSpeed
            }

            HoverHandler {
                cursorShape: root.views.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
            }

            TapHandler {
                enabled: root.views.length > 0
                onTapped: root.upClicked()
            }
        }

        // #24: library | seed, tabs in the Details tabs' idiom: the current one bright with
        // an accent rule under it.
        Row {
            id: viewTabs

            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: Theme.space(8)
            objectName: "viewTabs"
            spacing: Theme.space(2)
            visible: root.views.length > 0 && !(root.level >= 3 && root.searchOpen)

            Repeater {
                model: root.views

                delegate: Item {
                    id: viewTab

                    required property var modelData
                    readonly property bool current: viewTab.modelData.key === root.currentView

                    implicitHeight: Theme.space(34)
                    implicitWidth: tabRow.implicitWidth + Theme.space(20)

                    Row {
                        id: tabRow

                        anchors.centerIn: parent
                        spacing: Theme.space(6)

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: viewTab.current ? Theme.brightForeground : Theme.dimForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.body
                            text: viewTab.modelData.label
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.dimForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.caption
                            opacity: 0.7
                            text: viewTab.modelData.hint ?? ""
                            visible: text.length > 0 && root.level < 2
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        color: Theme.accent
                        height: Theme.space(2)
                        visible: viewTab.current
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: root.viewSelected(viewTab.modelData.key)
                    }
                }
            }
        }

        Item {
            id: spacerRight

            Layout.fillWidth: root.speedsAlignment !== "right"
            objectName: "speedsSpacerRight"
        }

        Repeater {
            id: chipRepeater

            model: root.filterChips

            Chip {
                id: filterChip

                required property var modelData

                Accessible.name: filterChip.modelData.text
                Layout.alignment: Qt.AlignVCenter
                glyph: filterChip.modelData.glyph
                objectName: "filterChip_" + filterChip.modelData.key
                text: root.chipsFolded ? Glyphs.close : filterChip.modelData.text + "  " + Glyphs.close
                // Folded to its ✕, the chip still says what it clears.
                tooltip: root.chipsFolded ? filterChip.modelData.text : ""
                visible: !root.filtersInButton

                onClicked: root.filterChipCleared(filterChip.modelData.key)
            }
        }

        // Step 3: the filter folded into a button; lit while a filter is set.
        IconButton {
            id: searchButton

            accent: searchField.text.length > 0
            glyph: Glyphs.search
            objectName: "searchButton"
            tooltip: searchField.text.length > 0 ? qsTr("filter: %1 · /").arg(searchField.text)
                                                 : qsTr("filter · /")
            visible: root.searchFolded && !root.filtersInButton

            onClicked: root.openSearch()
        }

        // Step 4 with filters up (docs/15 Q1 A): the search glyph and how many filters are
        // up, lit in the accent, drawn as a chip. It is never wider than a few digits.
        Rectangle {
            id: filterButton

            Accessible.name: qsTr("%n filter(s) · /", "", root.filterCount)
            Accessible.role: Accessible.Button
            // A screen reader's press is the click (#75); disabled, it does nothing, as the
            // pointer.
            Accessible.onPressAction: if (filterButton.enabled)
                                          root.filtersClicked()
            Layout.alignment: Qt.AlignVCenter
            activeFocusOnTab: true
            border.color: Theme.accent
            border.width: 1
            color: filterMouse.containsMouse ? Theme.fill(Theme.fillHover) : Theme.fill(
                                                   Theme.fillIdle)
            implicitHeight: Theme.space(22)
            implicitWidth: filterContent.implicitWidth + Theme.space(16)
            objectName: "filterButton"
            radius: Theme.cornerRadius
            visible: root.filtersInButton

            Row {
                id: filterContent

                anchors.centerIn: parent
                spacing: Theme.space(5)

                Glyph {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.accent
                    height: Theme.font.small
                    pixelSize: Theme.font.small
                    text: Glyphs.search
                    width: Theme.font.small
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    objectName: "filterCount"
                    text: String(root.filterCount)
                }
            }

            MouseArea {
                id: filterMouse

                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: root.filtersClicked()
            }

            // A button like its IconButton neighbours: reached by Tab, ringed while it holds
            // the focus (review Q6), and named in the kit's one tooltip (review Q8).
            FocusRing {}

            Tooltip {
                id: filterTip

                readonly property bool wanted: filterButton.visible && (filterMouse.containsMouse
                                                                        || filterButton.activeFocus)

                objectName: "filterButtonTip"
                parent: filterButton.Window.window ? filterButton.Window.window.contentItem : filterButton
                z: 1000

                onWantedChanged: filterTip.wanted ? filterTip.showFor(filterButton, filterButton.Accessible.name, false)
                                                  : filterTip.hide()
            }

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                        || event.key === Qt.Key_Space) {
                    root.filtersClicked();
                    event.accepted = true;
                }
            }
        }

        Field {
            id: searchField

            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: root.level >= 3
            Layout.preferredWidth: Theme.space([220, 180, 140, 140, 140][root.level])
            visible: !root.searchFolded
            hint: Glyphs.search
            placeholder: root.level >= 2 ? qsTr("filter") : qsTr("filter torrents")
            text: root.searchText
            trailing: root.searchCountText
            // While it filters, the count and an `esc` cap clear it with a click, as `esc`
            // does; the cap only where the field has room for it beside the count.
            trailingClickable: searchField.text.length > 0
            trailingKey: searchField.text.length > 0 && root.level < 2 ? "esc" : ""

            onAccepted: {
                root.searchChanged(searchField.text);
                root.searchAccepted(searchField.text);
            }
            onCleared: root.searchCleared()
            onEditingChanged: {
                if (!searchField.editing)
                    root.searchOpen = false;
            }
            onTextChanged: root.searchChanged(searchField.text)
            onTrailingClicked: {
                searchField.text = "";
                root.searchCleared();
            }
        }

        // Alt-speed is its glyph alone while off, named by its tooltip (manual box M14), and
        // while it throttles prints the limits in the warning role, so the state reads from
        // across the room (docs/08 §3; Fig. 1 had the `⏷` glyph, which read as a dropdown).
        // `A` still toggles it.
        // docs/31: burst is green with its needle up to the right; turtle and plain
        // alt-speed keep the yellow and today's needle. Off, the needle already points the way
        // the set goes.
        Chip {
            id: altChip

            Layout.alignment: Qt.AlignVCenter
            glyph: root.altKind === "burst" ? Glyphs.burst : Glyphs.altSpeed
            suffix: root.altChipSuffix
            text: root.altChipText
            tooltip: root.altTooltip.length > 0 ? root.altTooltip
                                                : root.altSpeedActive ? qsTr("alt-speed on · A")
                                                                      : qsTr("alt-speed · A")
            visible: !(root.level >= 3 && root.searchOpen)
            warn: root.altSpeedActive
            warnColor: root.altKind === "burst" ? Theme.green : Theme.yellow

            onClicked: root.altSpeedClicked()
        }

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.space(2)

            IconButton {
                id: addButton

                // Fig. 1 draws `.btn.icon.on`, the bare accent glyph; beside the other icons it
                // did not read as a button. The kit's primary outline does (docs/08 §3, REVIEW §9).
                glyph: Glyphs.add
                primary: true
                tooltip: qsTr("add · a")

                onClicked: root.addClicked()
            }


            IconButton {
                glyph: Glyphs.settings
                objectName: "settingsButton"
                tooltip: qsTr("settings · ,")
                visible: !root.compact

                onClicked: root.settingsClicked()
            }

            // docs/08 §3: global, occasional actions (the limits, columns, select all).
            IconButton {
                id: moreButton

                glyph: Glyphs.more
                tooltip: qsTr("more · ctrl+.")

                onClicked: root.moreClicked()
            }

            IconButton {
                glyph: Glyphs.help
                objectName: "helpButton"
                tooltip: qsTr("help · ?")
                visible: !root.compact

                onClicked: root.helpClicked()
            }
        }
    }

    // Hidden copies of the parts whose width changes with the step, so `level` can measure
    // the row at every step without reading the drawn one (review VER-6).
    Item {
        id: ladderMeasure

        visible: false

        Label {
            id: measureServerFull

            text: root.serverLine + root._switchMark
            tracked: true
        }

        Label {
            id: measureServerShort

            text: root.serverLineShort + root._switchMark
            tracked: true
        }

        // With their captions: the wider of the two forms, so the measure never falls short.
        Stat {
            id: measureDown

            glyph: Glyphs.arrowDown
            label: qsTr("download")
            value: root.downSpeed
        }

        Stat {
            id: measureUp

            glyph: Glyphs.arrowUp
            label: qsTr("upload")
            value: root.upSpeed
        }

        Chip {
            id: measureAltFull

            glyph: Glyphs.altSpeed
            suffix: root._altSuffix(0)
            text: root._altText(0)
        }

        Chip {
            id: measureAltShort

            glyph: Glyphs.altSpeed
            suffix: root._altSuffix(1)
            text: root._altText(1)
        }

        Chip {
            id: measureAltWord

            glyph: Glyphs.altSpeed
            suffix: root._altSuffix(2)
            text: root._altText(2)
        }

        Chip {
            id: measureAltGlyph

            glyph: Glyphs.altSpeed
            text: ""
        }
    }

    // Hidden copies of the filter chips, whole and folded, so `level` can count their width
    // without reading the drawn chips, whose text follows `level`.
    Repeater {
        id: chipMeasure

        model: root.filterChips

        Item {
            id: measured

            required property var modelData

            visible: false

            Chip {
                id: fullChip

                glyph: measured.modelData.glyph
                text: measured.modelData.text + "  " + Glyphs.close
            }

            Chip {
                id: foldedChip

                glyph: measured.modelData.glyph
                text: Glyphs.close
            }
        }
    }

    // The whole server line, whenever the hero shows less of it (from step 1, or elided).
    Tooltip {
        id: serverTip

        readonly property bool wanted: (identityHover.hovered && (serverLabel.truncated
                                                                  || root.level >= 1
                                                                  || networkHover.hovered
                                                                  || serverMouse.containsMouse))
                                       || dotHover.hovered || (markHover.hovered && root.level >= 4)
        // With a warning up, the tip also says what it means and where to change it.
        readonly property string fullLine: (root.networkText.length > 0
                                            ? root.serverLine + " · " + root.networkText
                                            : root.serverLine)
                                           + (root.networkAdvice.length > 0 ? "\n" + root.networkAdvice
                                                                            : "")
                                           + (root.serverSwitchable ? "\n" + qsTr("switch server · ctrl+s") : "")

        objectName: "serverTip"
        // Over the whole window rather than the hero (KIT-8): clamped inside a hero one line
        // tall, the panel had no room below and landed on the very line it spells out.
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 5

        onWantedChanged: {
            if (serverTip.wanted) {
                const at = root.mapToItem(serverTip, mark.x, root.height);
                serverTip.showAt(at.x, at.y, serverTip.fullLine);
            } else {
                serverTip.hide();
            }
        }
    }

    Hairline {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }
}
