pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// Settings (spec v2 §5 Fig. 16, opened with `,`, from the tray's `settings…` and from the
// hero's gear). One `Kit/Sheet` in the figure's "modal" form: no icon head, the whole body a
// two-column layout of a left nav and a pane, the pane's own title carrying the `esc` chip.
//
// This is an `Item` rather than a `Sheet` subclass because two more overlays belong with it
// and must sit *over* the modal rather than inside its body: the server sheet of Fig. 9 and
// the confirm dialog that guards a removal. `open()`/`close()` forward to the sheet.
//
// Keyboard (Fig. 16's own footer):
//   j k / 1–6   move the nav
//   tab         hand the keyboard to the pane, and back
//   j k         inside a pane, move the row cursor
//   enter space act on the cursor row (toggle, open the server sheet, copy)
//   a x d       Servers pane only: add, remove (confirmed), make default
//   esc         close — or, while the server sheet is up, close that first, because the
//               sheet has the keyboard and traps the key itself.
Item {
    id: root

    /// True between `open()` and `close()`.
    readonly property alias opened: sheet.opened
    readonly property alias sheet: sheet
    readonly property alias serverSheet: serverSheet
    readonly property alias confirm: confirm

    /// Which pane is on show, 0–5 (General, Servers, Notifications, Keybindings, Appearance,
    /// About) — the order of `navRows`.
    property int paneIndex: 0
    /// The row cursor inside the pane, -1 when the pane has no rows.
    property int paneCursor: 0
    /// "nav" or "pane": which half of the modal the keyboard is in.
    property string area: "nav"

    signal closed

    readonly property var navRows: [
        {
            "glyph": Glyphs.settings,
            "text": qsTr("General")
        },
        {
            "glyph": Glyphs.server,
            "text": qsTr("Servers")
        },
        {
            "glyph": Glyphs.notifications,
            "text": qsTr("Notifications")
        },
        {
            "glyph": Glyphs.keyboard,
            "text": qsTr("Keybindings")
        },
        {
            "glyph": Glyphs.appearance,
            "text": qsTr("Appearance")
        },
        {
            "glyph": Glyphs.about,
            "text": qsTr("About")
        }
    ]

    /// How many cursor rows the pane on show has. Keybindings is a read-only list in v1.
    readonly property int paneRowCount: {
        switch (root.paneIndex) {
        case 0:
            return 4;
        case 1:
            return App.serverList.count + 1; // the servers, then "+ add a server"
        case 2:
            return 4; // three toggles, then the low-disk threshold field
        case 4:
            // theme, base size, transparency, tray icon, speed alignment, then the rail's scroll
            // hint, surface tint and tracker hosts (docs/08 §5), then where Details sits (docs/10)
            return 9;
        case 5:
            return 1; // the copyable `omarchy plugin add` line
        default:
            return 0;
        }
    }

    function open(): void {
        sheet.open();
    }

    function close(): void {
        sheet.close();
    }

    /// Go to a pane by index and put the cursor on its first row.
    function showPane(index: int): void {
        if (index < 0 || index >= root.navRows.length)
            return;
        root.paneIndex = index;
        root.paneCursor = 0;
    }

    /// `enter` / `space` on the cursor row of the pane on show.
    function activateRow(): void {
        if (root.paneIndex === 0) {
            if (root.paneCursor === 0)
                App.showServersGroup = !App.showServersGroup;
            else if (root.paneCursor === 1)
                App.pathMapEnabled = !App.pathMapEnabled;
            else if (root.paneCursor === 2)
                App.deleteFilesDefault = !App.deleteFilesDefault;
            else if (root.paneCursor === 3)
                Xdg.registerAsDefaultHandler();
            return;
        }
        if (root.paneIndex === 1) {
            if (root.paneCursor >= App.serverList.count) {
                serverSheet.openNew();
                return;
            }
            const map = App.serverList.serverMap(root.cursorServerId());
            if (map.id !== undefined)
                serverSheet.openFor(map);
            return;
        }
        if (root.paneIndex === 2) {
            const keys = ["completed", "serverLost", "diskFull"];
            if (root.paneCursor >= keys.length) {
                thresholdField.focusInput();
                return;
            }
            const key = keys[root.paneCursor];
            App.setNotificationEnabled(key, !App.notificationsEnabled[key]);
            return;
        }
        if (root.paneIndex === 4) {
            if (root.paneCursor === 0)
                App.followOmarchyTheme = !App.followOmarchyTheme;
            else if (root.paneCursor === 1)
                baseSizeField.focusInput();
            else if (root.paneCursor === 2)
                alphaField.focusInput();
            else if (root.paneCursor === 3)
                App.trayIconMode = root._nextTrayMode();
            else if (root.paneCursor === 4)
                App.speedsAlignment = App.speedsAlignment === "left" ? "right" : "left";
            else if (root.paneCursor === 5)
                App.railScrollHint = root._next(root.scrollHints, App.railScrollHint);
            else if (root.paneCursor === 6)
                App.surfaceTint = root._next(root.tints, App.surfaceTint);
            else if (root.paneCursor === 7)
                App.trackerHostStyle = root._next(root.hostStyles, App.trackerHostStyle);
            else if (root.paneCursor === 8)
                App.detailsPlacement = root._next(root.placements, App.detailsPlacement);
            return;
        }
        if (root.paneIndex === 5)
            root.copyPluginLine();
    }

    // docs/08 §5: the three scroll-hint looks, as [value, chip text]. Rail's first, and since
    // N3 every palette's too; the stored key keeps its `railScrollHint` name.
    readonly property var scrollHints: [["chevrons", qsTr("counted")], ["fade", qsTr("fade")],
        ["scrollbar", qsTr("scrollbar")]]
    readonly property var tints: [[0, qsTr("none")], [4, qsTr("4 %")], [8, qsTr("8 %")]]
    readonly property var hostStyles: [["prefix", qsTr("drop prefix")], ["left", qsTr("cut start")],
        ["right", qsTr("cut end")]]
    // docs/10 §5: where Details sits.
    readonly property var placements: [["auto", qsTr("auto")], ["right", qsTr("right")], ["bottom",
                                                                                          qsTr("bottom")]]

    /// The value after `current` in `options` (`[[value, text]]`), wrapping round.
    function _next(options: var, current: var): var {
        const at = options.findIndex(option => option[0] === current);
        return options[(at + 1) % options.length][0];
    }

    /// The id of the server the Servers pane's cursor is on, or "" on the add row.
    function cursorServerId(): string {
        if (root.paneIndex !== 1 || root.paneCursor < 0
                || root.paneCursor >= App.serverList.count)
            return "";
        return App.serverList.idAt(root.paneCursor);
    }

    /// `x` / `delete` in the Servers pane, through the shell's confirm (spec §3 Fig. 6).
    function removeCursorServer(): void {
        const id = root.cursorServerId();
        if (id.length === 0)
            return;
        const map = App.serverList.serverMap(id);
        confirm.pendingId = id;
        confirm.message = qsTr("Remove the server %1?").arg(map.name);
        // Only a server with a credential has a stored secret to lose (review RUN-14).
        confirm.subtitle = map.authMode === "none"
                ? qsTr("%1 · nothing on the server is touched.").arg(map.url)
                : qsTr("%1 · the stored secret goes with it. Nothing on the server is touched.").arg(map.url);
        confirm.open();
    }

    /// The About pane's copyable `omarchy plugin add …` line.
    function copyPluginLine(): void {
        clipboard.text = App.pluginAddCommand;
        clipboard.selectAll();
        clipboard.copy();
        clipboard.deselect();
    }

    /// The threshold field shows the stored value. `Field.text` is an alias on the editor,
    /// so a binding would break at the first keystroke: it is written when the modal opens
    /// and whenever App changes the value (a clamp, or the field's own accept).
    function _syncThresholdField(): void {
        thresholdField.text = String(App.lowDiskThresholdGiB);
    }

    /// Base size and transparency likewise show a stored override, and nothing (the
    /// placeholder: follow / the shell's value) when there is none, so an override set in an
    /// earlier session is visible, and a value App refused does not stay in the field. A
    /// field with the caret is left alone: another setting changing must not eat its typing.
    function _syncAppearanceFields(): void {
        if (!baseSizeField.editing)
            baseSizeField.text = App.baseSizeOverride > 0 ? String(App.baseSizeOverride) : "";
        if (!alphaField.editing)
            alphaField.text = App.windowAlphaOverride > 0 ? String(App.windowAlphaOverride) : "";
    }

    function _nextTrayMode(): string {
        const modes = ["auto", "always", "never"];
        return modes[(modes.indexOf(App.trayIconMode) + 1) % modes.length];
    }

    /// The tallest pane's own height. Keybindings is left out: it is a list with a scroller of
    /// its own, and takes whatever height the others set.
    readonly property real _tallestPane: Math.max(generalPane.implicitHeight, serversPane.implicitHeight,
                                                  notificationsPane.implicitHeight,
                                                  appearancePane.implicitHeight, aboutPane.implicitHeight)
    /// The most the body may take so that the whole modal stays within 85 % of the window:
    /// the sheet adds its padding, the gap and its hint row around the body.
    readonly property real _bodyCap: Math.max(Theme.space(200), Math.floor(root.height * 0.85) - 2
                                              * Theme.space(16) - Theme.space(12) - Theme.space(8) - 1
                                              - Math.ceil(captionMetrics.height))

    /// The pane on show, in `navRows` order.
    readonly property Item _paneOnShow: [generalPane, serversPane, notificationsPane, keysPane,
        appearancePane, aboutPane][root.paneIndex]

    /// Scrolls the pane just enough to show the row with the cursor, so `j`/`k` never move
    /// it out of sight on a pane taller than the modal.
    function _revealCursor(): void {
        if (root.area !== "pane")
            return;
        const row = root._cursorRowIn(root._paneOnShow);
        if (row === null)
            return;
        const top = row.mapToItem(paneStack.contentItem, 0, 0).y;
        const bottom = top + row.height;
        if (top < paneStack.contentY)
            paneStack.contentY = top;
        else if (bottom > paneStack.contentY + paneStack.height)
            paneStack.contentY = bottom - paneStack.height;
    }

    function _cursorRowIn(item: Item): var {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i];
            if (child.hasCursor === true)
                return child;
            const inner = root._cursorRowIn(child);
            if (inner !== null)
                return inner;
        }
        return null;
    }

    function _moveNav(delta: int): void {
        root.showPane((root.paneIndex + delta + root.navRows.length) % root.navRows.length);
    }

    function _movePane(delta: int): void {
        if (root.paneRowCount <= 0)
            return;
        root.paneCursor = (root.paneCursor + delta + root.paneRowCount) % root.paneRowCount;
    }

    /// Every key the modal owns. `esc` is deliberately left alone — the Sheet closes on it —
    /// and everything else is swallowed, so nothing typed over the modal reaches the window
    /// behind it.
    function _onKey(event: var): void {
        if (event.key === Qt.Key_Escape)
            return;
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            root.area = root.area === "nav" ? "pane" : "nav";
            if (root.area === "pane" && root.paneCursor < 0)
                root.paneCursor = 0;
            event.accepted = true;
            return;
        }
        if (event.key >= Qt.Key_1 && event.key <= Qt.Key_6) {
            root.showPane(event.key - Qt.Key_1);
            event.accepted = true;
            return;
        }
        if (root.area === "nav") {
            switch (event.key) {
            case Qt.Key_J:
            case Qt.Key_Down:
                root._moveNav(1);
                break;
            case Qt.Key_K:
            case Qt.Key_Up:
                root._moveNav(-1);
                break;
            case Qt.Key_L:
            case Qt.Key_Right:
            case Qt.Key_Return:
            case Qt.Key_Enter:
                root.area = "pane";
                break;
            default:
                break;
            }
            event.accepted = true;
            return;
        }

        switch (event.key) {
        case Qt.Key_J:
        case Qt.Key_Down:
            root._movePane(1);
            break;
        case Qt.Key_K:
        case Qt.Key_Up:
            root._movePane(-1);
            break;
        case Qt.Key_H:
        case Qt.Key_Left:
            root.area = "nav";
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            root.activateRow();
            break;
        case Qt.Key_A:
            if (root.paneIndex === 1)
                serverSheet.openNew();
            break;
        case Qt.Key_X:
        case Qt.Key_Delete:
            if (root.paneIndex === 1)
                root.removeCursorServer();
            break;
        case Qt.Key_D:
            if (root.paneIndex === 1)
                App.setDefaultServer(root.cursorServerId());
            break;
        default:
            break;
        }
        event.accepted = true;
    }

    Connections {
        function onLowDiskThresholdGiBChanged(): void {
            root._syncThresholdField();
        }

        function onSettingsChanged(): void {
            root._syncAppearanceFields();
        }

        target: App
    }

    onOpenedChanged: {
        if (root.opened) {
            root._syncThresholdField();
            root._syncAppearanceFields();
        }
    }
    onPaneIndexChanged: paneStack.contentY = 0
    onPaneCursorChanged: Qt.callLater(root._revealCursor)
    onAreaChanged: Qt.callLater(root._revealCursor)

    FontMetrics {
        id: captionMetrics

        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.caption
    }

    // Nothing typed while any of the three overlays is up may reach the window behind them:
    // what the sheet, the server sheet and the confirm leave unhandled stops here.
    Keys.onPressed: event => {
        if (event.key !== Qt.Key_Escape
                && (sheet.opened || serverSheet.opened || confirm.opened))
            event.accepted = true;
    }

    // The clipboard, for the About pane's install line. QML has no clipboard API of its own;
    // an off-screen TextEdit is the standard way to reach it.
    TextEdit {
        id: clipboard

        visible: false
    }

    Sheet {
        id: sheet

        anchors.fill: parent
        // The head belongs to the pane in this figure, not to the sheet: no glyph, no title,
        // and no `esc` chip up there either.
        escText: ""
        hints: [
            {
                "key": "j k",
                "label": qsTr("move")
            },
            {
                "key": "tab",
                "label": qsTr("pane")
            },
            {
                "key": "enter",
                "label": qsTr("act")
            }
        ]
        sheetWidth: Theme.space(760)
        title: ""

        onRejected: root.closed()

        Item {
            id: bodyItem

            /// The Sheet's focus trap recognises anything with a `focusInput()`; this item is
            /// the one focusable thing in the body, and it owns every key the modal handles.
            function focusInput(): void {
                bodyItem.forceActiveFocus();
            }

            Layout.fillWidth: true
            // Batch-2 J3: one height whatever the pane, so moving through the nav never moves
            // the modal: the tallest pane's, never shorter than Fig. 16's own body, and capped
            // so the modal stays within 85 % of the window. A pane taller than that scrolls.
            implicitHeight: Math.min(root._bodyCap, Math.max(Theme.space(470), paneHead.implicitHeight
                                                             + Theme.space(12) + root._tallestPane))

            Keys.onPressed: event => root._onKey(event)

            RowLayout {
                id: bodyRow

                anchors.fill: parent
                spacing: 0

                // ---- the nav (`.settings .nav`) ---------------------------------------
                // A nested Layout fills by default, so the nav has to say it does not: the
                // pane is the one that takes what is left.
                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: false
                    Layout.maximumWidth: Theme.space(150)
                    Layout.preferredWidth: Theme.space(150)
                    spacing: 1

                    Label {
                        Layout.bottomMargin: Theme.space(8)
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.space(4)
                        text: qsTr("settings")
                        tracked: true
                    }

                    Repeater {
                        model: root.navRows

                        RailRow {
                            id: navRow

                            required property int index
                            required property var modelData

                            Layout.fillWidth: true
                            objectName: "navRow" + navRow.index
                            glyph: navRow.modelData.glyph
                            hasCursor: root.area === "nav" && root.paneIndex === navRow.index
                            selected: root.paneIndex === navRow.index
                            text: navRow.modelData.text

                            onClicked: {
                                root.showPane(navRow.index);
                                root.area = "nav";
                                bodyItem.forceActiveFocus();
                            }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }

                Hairline {
                    Layout.fillHeight: true
                    Layout.leftMargin: Theme.space(10)
                    Layout.rightMargin: Theme.space(18)
                    vertical: true
                }

                // ---- the pane (`.settings .pane`) --------------------------------------
                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    spacing: Theme.space(12)

                    RowLayout {
                        id: paneHead

                        Layout.fillWidth: true
                        spacing: Theme.space(10)

                        Label {
                            Layout.fillWidth: true
                            bright: true
                            elide: Text.ElideRight
                            font.weight: Font.DemiBold
                            size: "subtitle"
                            text: root.navRows[root.paneIndex].text
                        }

                        Label {
                            dim: true
                            size: "caption"
                            text: qsTr("esc")
                        }
                    }

                    // The panes, in what is left under the pane's title. Only one is ever
                    // visible, and it fills the content, which is as tall as the room or the
                    // pane, whichever is more: a pane taller than the room scrolls here under a
                    // bar, as the help sheet does (batch-2 J3). Keybindings always gets exactly
                    // the room, because its list scrolls itself.
                    Flickable {
                        id: paneStack

                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true
                        contentHeight: root.paneIndex === 3 ? height : Math.max(height,
                                                                                root._paneOnShow.implicitHeight)
                        contentWidth: width - (paneBar.needed ? paneBar.width + Theme.space(8) : 0)
                        objectName: "paneScroller"

                        // On the Flickable, not its content, so it stays put while the pane
                        // scrolls.
                        ScrollBar {
                            id: paneBar

                            anchors.bottom: parent.bottom
                            anchors.right: parent.right
                            anchors.top: parent.top
                            flickable: paneStack
                            objectName: "settingsScrollBar"
                            parent: paneStack
                            vertical: true
                        }

                        // ---- General ---------------------------------------------------
                        ColumnLayout {
                            id: generalPane

                            anchors.fill: parent
                            spacing: Theme.space(8)
                            visible: root.paneIndex === 0

                            SettingsRow {
                                Layout.fillWidth: true
                                // Batch-2 J8: plain words, and true to Rail.qml, which draws
                                // the group only from two servers and never while this is off.
                                description: qsTr("It appears once you add a second server. Off keeps it hidden even then.")
                                glyph: Glyphs.mark
                                hasCursor: root.area === "pane" && root.paneCursor === 0
                                title: qsTr("Show the Servers group in the rail")

                                onClicked: {
                                    root.paneCursor = 0;
                                    root.area = "pane";
                                    App.showServersGroup = !App.showServersGroup;
                                }

                                Toggle {
                                    checked: App.showServersGroup

                                    onToggled: App.showServersGroup = checked
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("Rewrites remote paths to local mounts before asking the file manager.")
                                glyph: Glyphs.home
                                hasCursor: root.area === "pane" && root.paneCursor === 1
                                title: qsTr("Open folder through path mapping")

                                onClicked: {
                                    root.paneCursor = 1;
                                    root.area = "pane";
                                    App.pathMapEnabled = !App.pathMapEnabled;
                                }

                                Toggle {
                                    checked: App.pathMapEnabled

                                    onToggled: App.pathMapEnabled = checked
                                }
                            }

                            // T4.8: T4.5 left this out because T4.3, which owns the flag,
                            // was not merged yet. It is the delete confirm's remembered
                            // default (Fig. 8's "also delete the files" toggle).
                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("The delete confirm opens with \"also delete the files\" already on.")
                                glyph: Glyphs.statusErrored
                                hasCursor: root.area === "pane" && root.paneCursor === 2
                                objectName: "deleteFilesRow"
                                title: qsTr("Delete files by default")

                                onClicked: {
                                    root.paneCursor = 2;
                                    root.area = "pane";
                                    App.deleteFilesDefault = !App.deleteFilesDefault;
                                }

                                Toggle {
                                    checked: App.deleteFilesDefault

                                    onToggled: App.deleteFilesDefault = checked
                                }
                            }

                            SettingsRow {
                                id: handlerRow

                                /// Filled by Xdg's answer; "" until the first check lands.
                                property string currentHandler: ""
                                property bool isDefault: false

                                Layout.fillWidth: true
                                description: handlerRow.isDefault ? qsTr("omaseed already opens magnet links and .torrent files.") : handlerRow.currentHandler.length
                                                                    > 0 ? qsTr("Currently %1.").arg(handlerRow.currentHandler) : qsTr("Makes omaseed open magnet links and .torrent files.")
                                glyph: Glyphs.add
                                hasCursor: root.area === "pane" && root.paneCursor === 3
                                title: qsTr("Register as the magnet and .torrent handler")

                                onClicked: {
                                    root.paneCursor = 3;
                                    root.area = "pane";
                                    Xdg.registerAsDefaultHandler();
                                }

                                Button {
                                    enabled: !handlerRow.isDefault
                                    text: qsTr("register")

                                    onClicked: Xdg.registerAsDefaultHandler()
                                }

                                Connections {
                                    function onIsDefaultMagnetHandlerResult(isDefault: bool, currentHandler: string): void {
                                        handlerRow.isDefault = isDefault;
                                        handlerRow.currentHandler = currentHandler;
                                    }

                                    function onRegistrationFinished(success: bool): void {
                                        if (success)
                                            Xdg.isDefaultMagnetHandler();
                                    }

                                    target: Xdg
                                }

                                Component.onCompleted: Xdg.isDefaultMagnetHandler()
                            }

                            Item {
                                Layout.fillHeight: true
                            }
                        }

                        // ---- Servers (the figure's own pane) ---------------------------
                        ColumnLayout {
                            id: serversPane

                            anchors.fill: parent
                            spacing: Theme.space(6)
                            visible: root.paneIndex === 1

                            Repeater {
                                model: App.serverList

                                SettingsServerRow {
                                    id: serverRow

                                    required property string authModeText
                                    required property int index
                                    required property bool isDefault
                                    required property string lastError
                                    required property string name
                                    required property bool reachable
                                    required property string reachableText
                                    required property int torrentCount
                                    required property string url
                                    required property string versionText

                                    Layout.fillWidth: true
                                    objectName: "serverRow" + serverRow.index
                                    description: {
                                        const parts = [serverRow.url, serverRow.authModeText];
                                        if (serverRow.versionText.length > 0)
                                            parts.push(serverRow.versionText);
                                        if (serverRow.torrentCount >= 0)
                                            parts.push(qsTr("%1 torrents").arg(serverRow.torrentCount));
                                        else if (serverRow.lastError.length > 0)
                                            parts.push(serverRow.lastError);
                                        return parts.join(" · ");
                                    }
                                    failing: serverRow.reachableText.length > 0
                                             && !serverRow.reachable
                                    hasCursor: root.area === "pane"
                                               && root.paneCursor === serverRow.index
                                    title: serverRow.name
                                    status: serverRow.isDefault ? qsTr("default") : (serverRow.reachableText.length
                                                                                     > 0 && !serverRow.reachable ? qsTr("retrying") : "")

                                    onClicked: {
                                        root.paneCursor = serverRow.index;
                                        root.area = "pane";
                                        bodyItem.forceActiveFocus();
                                        root.activateRow();
                                    }
                                }
                            }

                            SettingsServerRow {
                                Layout.fillWidth: true
                                objectName: "serverRowAdd"
                                dashed: true
                                glyph: Glyphs.plus
                                hasCursor: root.area === "pane"
                                           && root.paneCursor === App.serverList.count
                                status: qsTr("a")
                                title: qsTr("add a server")

                                onClicked: {
                                    root.paneCursor = App.serverList.count;
                                    root.area = "pane";
                                    serverSheet.openNew();
                                }
                            }

                            Item {
                                Layout.fillHeight: true
                            }
                        }

                        // ---- Notifications ---------------------------------------------
                        ColumnLayout {
                            id: notificationsPane

                            anchors.fill: parent
                            spacing: Theme.space(8)
                            visible: root.paneIndex === 2

                            Repeater {
                                model: [
                                    {
                                        "key": "completed",
                                        "glyph": Glyphs.check,
                                        "title": qsTr("Torrent completed"),
                                        "description": qsTr("One notice per finished torrent, with open folder and show.")
                                    },
                                    {
                                        "key": "serverLost",
                                        "glyph": Glyphs.trayDisconnected,
                                        "title": qsTr("Server lost and back"),
                                        "description": qsTr("One notice when the server stops answering, one when it returns.")
                                    },
                                    {
                                        "key": "diskFull",
                                        "glyph": Glyphs.folder,
                                        "title": qsTr("Disk almost full"),
                                        "description": qsTr("From the server's own free-space figure for its default save path. Other disks and per-category paths are not watched.")
                                    }
                                ]

                                SettingsRow {
                                    id: notifyRow

                                    required property int index
                                    required property var modelData

                                    Layout.fillWidth: true
                                    description: notifyRow.modelData.description
                                    glyph: notifyRow.modelData.glyph
                                    hasCursor: root.area === "pane"
                                               && root.paneCursor === notifyRow.index
                                    objectName: "notifyRow-" + notifyRow.modelData.key
                                    title: notifyRow.modelData.title

                                    onClicked: {
                                        root.paneCursor = notifyRow.index;
                                        root.area = "pane";
                                        App.setNotificationEnabled(notifyRow.modelData.key,
                                                                   !App.notificationsEnabled[notifyRow.modelData.key]);
                                    }

                                    Toggle {
                                        checked: App.notificationsEnabled[notifyRow.modelData.key] === true

                                        onToggled: App.setNotificationEnabled(notifyRow.modelData.key, checked)
                                    }
                                }
                            }

                            // Q16: the line the disk notice fires at, a setting rather than
                            // the fixed 2 GiB of T4.7. The same row + field shape as the
                            // Appearance pane's overrides.
                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("The notice fires once when the free space drops below this.")
                                glyph: Glyphs.altSpeed
                                hasCursor: root.area === "pane" && root.paneCursor === 3
                                objectName: "lowDiskThresholdRow"
                                title: qsTr("Low-disk threshold")

                                onClicked: {
                                    root.paneCursor = 3;
                                    root.area = "pane";
                                    thresholdField.focusInput();
                                }

                                Field {
                                    id: thresholdField

                                    implicitWidth: Theme.space(110)
                                    objectName: "lowDiskThresholdField"
                                    placeholder: qsTr("2")
                                    trailing: qsTr("GiB")

                                    onAccepted: {
                                        App.lowDiskThresholdGiB = parseInt(text, 10) || 2;
                                        root._syncThresholdField();
                                        bodyItem.forceActiveFocus();
                                    }
                                    onCleared: {
                                        App.lowDiskThresholdGiB = 2;
                                        root._syncThresholdField();
                                        bodyItem.forceActiveFocus();
                                    }
                                }
                            }

                            Item {
                                Layout.fillHeight: true
                            }
                        }

                        // ---- Keybindings (read-only in v1) -----------------------------
                        Item {
                            id: keysPane

                            anchors.fill: parent
                            visible: root.paneIndex === 3

                            // Batch-2 J4: a Settings page with nothing to change says so, and
                            // where else the same list lives.
                            Row {
                                id: keysNote

                                anchors.left: parent.left
                                anchors.top: parent.top
                                objectName: "keysNote"
                                spacing: Theme.space(6)

                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    dim: true
                                    size: "small"
                                    text: qsTr("Rebinding comes later.")
                                }

                                // `?` as a key, in the outline of the sheets' icon squares.
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    border.color: Theme.fill(Theme.borderIdle)
                                    border.width: 1
                                    color: Theme.fill(Theme.fillIdle)
                                    implicitHeight: keyText.implicitHeight + Theme.space(2)
                                    implicitWidth: Math.max(implicitHeight, keyText.implicitWidth
                                                            + Theme.space(10))
                                    radius: Theme.cornerRadius

                                    Text {
                                        id: keyText

                                        anchors.centerIn: parent
                                        color: Theme.brightForeground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.font.small
                                        font.weight: Font.Medium
                                        text: "?"
                                    }
                                }

                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    dim: true
                                    size: "small"
                                    text: qsTr("shows this anywhere.")
                                }
                            }

                            // The `?` sheet's own body: the same rows, from the same Keymap,
                            // so the two can never drift apart. Its list takes the rest of the
                            // pane and scrolls itself.
                            HelpSheet {
                                id: keysBody

                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: keysNote.bottom
                                anchors.topMargin: Theme.space(12)
                                height: parent.height - keysNote.height - Theme.space(12)
                                maxListHeight: keysBody.height - keysBody.filterField.height
                                               - Theme.space(14)

                                onCloseRequested: root.close()
                            }
                        }

                        // ---- Appearance -------------------------------------------------
                        ColumnLayout {
                            id: appearancePane

                            anchors.fill: parent
                            spacing: Theme.space(8)
                            visible: root.paneIndex === 4

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("Off keeps the bundled Tokyo Night whatever the desktop does.")
                                glyph: Glyphs.appearance
                                hasCursor: root.area === "pane" && root.paneCursor === 0
                                title: qsTr("Follow the Omarchy theme")

                                onClicked: {
                                    root.paneCursor = 0;
                                    root.area = "pane";
                                    App.followOmarchyTheme = !App.followOmarchyTheme;
                                }

                                Toggle {
                                    checked: App.followOmarchyTheme

                                    onToggled: App.followOmarchyTheme = checked
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("Everything scales from it. Empty follows shell.toml.")
                                glyph: Glyphs.mark
                                hasCursor: root.area === "pane" && root.paneCursor === 1
                                title: qsTr("Base size")

                                onClicked: {
                                    root.paneCursor = 1;
                                    root.area = "pane";
                                    baseSizeField.focusInput();
                                }

                                Field {
                                    id: baseSizeField

                                    objectName: "baseSizeField"
                                    implicitWidth: Theme.space(110)
                                    placeholder: qsTr("follow")
                                    trailing: qsTr("px")

                                    onAccepted: {
                                        App.baseSizeOverride = parseInt(text, 10) || 0;
                                        bodyItem.forceActiveFocus();
                                        root._syncAppearanceFields();
                                    }
                                    onCleared: {
                                        App.baseSizeOverride = 0;
                                        bodyItem.forceActiveFocus();
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("How much wallpaper shows through the window, 50–100. Empty follows shell.toml.")
                                glyph: Glyphs.altSpeed
                                hasCursor: root.area === "pane" && root.paneCursor === 2
                                title: qsTr("Window transparency")

                                onClicked: {
                                    root.paneCursor = 2;
                                    root.area = "pane";
                                    alphaField.focusInput();
                                }

                                Field {
                                    id: alphaField

                                    objectName: "windowAlphaField"
                                    implicitWidth: Theme.space(110)
                                    placeholder: String(Math.round(Theme.defaultWindowAlpha * 100))
                                    trailing: qsTr("%")

                                    onAccepted: {
                                        App.windowAlphaOverride = parseInt(text, 10) || 0;
                                        bodyItem.forceActiveFocus();
                                        root._syncAppearanceFields();
                                    }
                                    onCleared: {
                                        App.windowAlphaOverride = 0;
                                        bodyItem.forceActiveFocus();
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("auto hides the icon once the bar widget is running. It behaves as always until then.")
                                glyph: Glyphs.trayIdle
                                hasCursor: root.area === "pane" && root.paneCursor === 3
                                title: qsTr("Tray icon")

                                onClicked: {
                                    root.paneCursor = 3;
                                    root.area = "pane";
                                    App.trayIconMode = root._nextTrayMode();
                                }

                                RowLayout {
                                    spacing: Theme.space(6)

                                    Repeater {
                                        model: ["auto", "always", "never"]

                                        Chip {
                                            id: trayChip

                                            required property string modelData

                                            selected: App.trayIconMode === trayChip.modelData
                                            text: trayChip.modelData

                                            onClicked: App.trayIconMode = trayChip.modelData
                                        }
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("which side of the hero the download and upload figures sit on.")
                                glyph: Glyphs.arrowDown
                                hasCursor: root.area === "pane" && root.paneCursor === 4
                                title: qsTr("Speed figures")

                                onClicked: {
                                    root.paneCursor = 4;
                                    root.area = "pane";
                                    App.speedsAlignment = App.speedsAlignment === "left" ? "right" : "left";
                                }

                                RowLayout {
                                    spacing: Theme.space(6)

                                    Repeater {
                                        model: ["left", "right"]

                                        Chip {
                                            id: speedsChip

                                            required property string modelData

                                            selected: App.speedsAlignment === speedsChip.modelData
                                            text: speedsChip.modelData

                                            onClicked: App.speedsAlignment = speedsChip.modelData
                                        }
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("how lists say there is more above or below: the rail and every palette.")
                                glyph: Glyphs.scrollHint
                                hasCursor: root.area === "pane" && root.paneCursor === 5
                                title: qsTr("Scroll hint")

                                onClicked: {
                                    root.paneCursor = 5;
                                    root.area = "pane";
                                    App.railScrollHint = root._next(root.scrollHints, App.railScrollHint);
                                }

                                RowLayout {
                                    spacing: Theme.space(6)

                                    Repeater {
                                        model: root.scrollHints

                                        Chip {
                                            id: hintChip

                                            required property var modelData

                                            selected: App.railScrollHint === hintChip.modelData[0]
                                            text: hintChip.modelData[1]

                                            onClicked: App.railScrollHint = hintChip.modelData[0]
                                        }
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("a light fill behind the rail and the details.")
                                glyph: Glyphs.sidebar
                                hasCursor: root.area === "pane" && root.paneCursor === 6
                                title: qsTr("Surface tint")

                                onClicked: {
                                    root.paneCursor = 6;
                                    root.area = "pane";
                                    App.surfaceTint = root._next(root.tints, App.surfaceTint);
                                }

                                RowLayout {
                                    spacing: Theme.space(6)

                                    Repeater {
                                        model: root.tints

                                        Chip {
                                            id: tintChip

                                            required property var modelData

                                            selected: App.surfaceTint === tintChip.modelData[0]
                                            text: tintChip.modelData[1]

                                            onClicked: App.surfaceTint = tintChip.modelData[0]
                                        }
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("how a long tracker host fits the rail.")
                                glyph: Glyphs.host
                                hasCursor: root.area === "pane" && root.paneCursor === 7
                                title: qsTr("Tracker hosts")

                                onClicked: {
                                    root.paneCursor = 7;
                                    root.area = "pane";
                                    App.trackerHostStyle = root._next(root.hostStyles, App.trackerHostStyle);
                                }

                                RowLayout {
                                    spacing: Theme.space(6)

                                    Repeater {
                                        model: root.hostStyles

                                        Chip {
                                            id: hostChip

                                            required property var modelData

                                            selected: App.trackerHostStyle === hostChip.modelData[0]
                                            text: hostChip.modelData[1]

                                            onClicked: App.trackerHostStyle = hostChip.modelData[0]
                                        }
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("auto puts it right when the window is wide enough. I cycles it.")
                                glyph: Glyphs.sidebar
                                hasCursor: root.area === "pane" && root.paneCursor === 8
                                title: qsTr("Details panel")

                                onClicked: {
                                    root.paneCursor = 8;
                                    root.area = "pane";
                                    App.detailsPlacement = root._next(root.placements, App.detailsPlacement);
                                }

                                RowLayout {
                                    spacing: Theme.space(6)

                                    Repeater {
                                        model: root.placements

                                        Chip {
                                            id: placementChip

                                            required property var modelData

                                            selected: App.detailsPlacement === placementChip.modelData[0]
                                            text: placementChip.modelData[1]

                                            onClicked: App.detailsPlacement = placementChip.modelData[0]
                                        }
                                    }
                                }
                            }

                            Item {
                                Layout.fillHeight: true
                            }
                        }

                        // ---- About -------------------------------------------------------
                        ColumnLayout {
                            id: aboutPane

                            anchors.fill: parent
                            spacing: Theme.space(10)
                            visible: root.paneIndex === 5

                            KeyValue {
                                Layout.fillWidth: true
                                items: [
                                    {
                                        "key": qsTr("Version"),
                                        "value": App.versionText
                                    },
                                    {
                                        "key": qsTr("Qt"),
                                        "value": App.qtVersionText
                                    },
                                    {
                                        "key": qsTr("Licence"),
                                        "value": qsTr("MIT")
                                    },
                                    {
                                        "key": qsTr("Repository"),
                                        "value": App.repoUrl
                                    }
                                ]
                            }

                            FormRow {
                                Layout.fillWidth: true
                                label: qsTr("Companion bar widget")

                                RowLayout {
                                    spacing: Theme.space(8)

                                    Field {
                                        id: pluginField

                                        Layout.fillWidth: true
                                        hasCursor: root.area === "pane" && root.paneCursor === 0
                                        text: App.pluginAddCommand
                                    }

                                    Button {
                                        bordered: true
                                        text: qsTr("copy")

                                        onClicked: root.copyPluginLine()
                                    }
                                }
                            }

                            Item {
                                Layout.fillHeight: true
                            }
                        }
                    }
                }
            }
        }
    }

    // The server sheet of Fig. 9, over the modal: it takes the keyboard, so `esc` closes it
    // first and the modal stays up behind it.
    ServerSheet {
        id: serverSheet

        anchors.fill: parent
        z: 1

        onRemoveRequested: id => {
            root.paneCursor = Math.max(0, App.serverList.indexOfId(id));
            root.removeCursorServer();
        }
        onSaved: id => {
            root.showPane(1);
            root.paneCursor = Math.max(0, App.serverList.indexOfId(id));
            root.area = "pane";
        }
    }

    ConfirmDialog {
        id: confirm

        property string pendingId: ""

        anchors.fill: parent
        confirmText: qsTr("remove")
        destructive: true
        z: 2

        onAccepted: {
            App.removeServer(confirm.pendingId);
            confirm.pendingId = "";
            root.paneCursor = Math.min(root.paneCursor, Math.max(0, App.serverList.count));
        }
    }
}
