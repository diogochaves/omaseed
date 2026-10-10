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
//   j k / 1–7   move the nav
//   tab         hand the keyboard to the pane, and back
//   j k         inside a pane, move the row cursor
//   enter space act on the cursor row (toggle, open the server sheet, copy)
//   a x d       Servers pane only: add, remove (confirmed), make default
//   esc         close — or, while the server sheet is up, close that first, because the
//               sheet has the keyboard and traps the key itself.
//
// docs/16: a second group in the nav, "qBittorrent · <server> <version>", holds the server's
// own settings in nine sections (`QbtSettingsSections`, drawn by `QbtSettingsPane`). Its
// panes follow omaseed's seven in `paneIndex` (7–15). There the digits pick a section of that
// group, space flips a switch, enter edits, `d` puts an Advanced row back to its default and
// `s` moves the app to the next server. `openQbt()` (ctrl+shift+,) opens straight on it.
Item {
    id: root

    /// True between `open()` and `close()`.
    readonly property alias opened: sheet.opened
    readonly property alias sheet: sheet
    readonly property alias serverSheet: serverSheet
    readonly property alias confirm: confirm
    readonly property alias restoreSheet: restoreSheet

    /// Which pane is on show: 0–6 are omaseed's own (General, Servers, Notifications,
    /// Keybindings, Appearance, Backup, About, the order of `navRows`), 7 on the qBittorrent
    /// group's sections (`qbtSections`).
    property int paneIndex: 0
    /// The row cursor inside the pane, -1 when the pane has no rows.
    property int paneCursor: 0
    /// "nav" or "pane": which half of the modal the keyboard is in.
    property string area: "nav"
    /// Where API keys and passwords sit in plain files when no keychain answered; the
    /// Servers pane names it in one muted warning row (review Q10). Empty otherwise.
    property string plainSecretsPath: App.plainSecretsPath
    /// docs/15 Q3 A: below 700 px (base 14) the nav folds into a row of tabs across the top,
    /// so the rows get the sheet's whole width. The keyboard then lives in the pane, and
    /// `h`/`l` or `[`/`]` move between the sections.
    readonly property bool folded: root.width < Theme.space(600)

    onFoldedChanged: {
        if (root.folded && root.opened)
            root.area = "pane";
    }

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
            "text": qsTr("Keybindings"),
            // The folded tabs' word (docs/15 Q3 A): six whole names are wider than the sheet
            // at the 560 px floor, and this one is the longest.
            "shortText": qsTr("Keys")
        },
        {
            "glyph": Glyphs.appearance,
            "text": qsTr("Appearance")
        },
        {
            // #31, docs/24 §3: omaseed's own backup, after Appearance.
            "glyph": Glyphs.backup,
            "text": qsTr("Backup")
        },
        {
            "glyph": Glyphs.about,
            "text": qsTr("About")
        }
    ]

    /// docs/16: the qBittorrent group's sections.
    readonly property var qbtSections: qbtSchema.sections
    /// Where the qBittorrent group starts in `paneIndex`.
    readonly property int qbtStart: root.navRows.length
    readonly property int paneCount: root.navRows.length + root.qbtSections.length
    /// The pane on show is one of the qBittorrent group's.
    readonly property bool inQbt: root.paneIndex >= root.qbtStart
    readonly property var qbtSection: root.inQbt ? root.qbtSections[root.paneIndex - root.qbtStart]
                                                 : null
    /// Every nav row in `paneIndex` order, both groups.
    readonly property var allRows: root.navRows.concat(root.qbtSections.map(section => ({
                                                                                          "glyph": section.glyph,
                                                                                          "text": section.name,
                                                                                          "shortText": section.shortName
                                                                                      })))
    /// The qBittorrent group's head: "playground 5.2.3".
    readonly property string qbtServerText: {
        const version = App.globalState.serverVersion.replace(/^v/, "");
        return App.serverName + (version.length > 0 ? " " + version : "");
    }
    /// A line for the pane's head after a change: why the server said no, or what it kept.
    property string qbtNote: ""
    property bool qbtNoteError: false

    /// How many cursor rows the pane on show has. Keybindings is a read-only list in v1.
    readonly property int paneRowCount: {
        if (root.inQbt)
            return qbtPane.cursorCount;
        switch (root.paneIndex) {
        case 0:
            return 5; // three switches, the handler button, then start at login
        case 1:
            return App.serverList.count + 1; // the servers, then "+ add a server"
        case 2:
            return 4; // three toggles, then the low-disk threshold field
        case 4:
            // theme, base size, transparency, tray icon, the bar widget (#17), speed alignment,
            // the speed trends (#24 T2), then the rail's scroll hint, surface tint and tracker
            // hosts (docs/08 §5), then where Details sits (docs/10)
            return 11;
        case 5:
            return 3; // back up, restore, automatic copies (#31)
        case 6:
            return 1; // the copyable `omaseed --install-bar-widget` line
        default:
            return 0;
        }
    }

    function open(): void {
        if (root.folded)
            root.area = "pane";
        // The bar can change outside omaseed (`omarchy plugin disable`, the shell's own menu).
        App.barWidget.refresh();
        sheet.open();
        if (root.inQbt)
            root._readQbt();
    }

    /// Settings › Servers with server `id`'s sheet open and testing (issue #2: its certificate
    /// was refused, so the sheet shows it to trust).
    function reviewServer(id: string): void {
        const map = App.serverList.serverMap(id);
        if (map.id === undefined)
            return;
        root.showPane(1);
        root.paneCursor = Math.max(0, App.serverList.indexOfId(id));
        root.area = "pane";
        if (!root.opened)
            root.open();
        serverSheet.reviewFor(map);
    }

    /// ctrl+shift+, (docs/16 §2): Settings, on the qBittorrent group.
    function openQbt(): void {
        if (!root.inQbt)
            root.showPane(root.qbtStart);
        root.open();
    }

    /// The server's settings again, when a qBittorrent pane comes up, and its network
    /// interfaces for Connection.
    function _readQbt(): void {
        if (!App.serverPrefs.loading)
            App.serverPrefs.reload();
        if (root.qbtSection && root.qbtSection.id === "connection")
            App.serverPrefs.loadInterfaces(App.serverPrefs.values.current_network_interface ?? "");
    }

    function _flashQbtNote(text: string, error: bool): void {
        root.qbtNote = text;
        root.qbtNoteError = error;
        qbtNoteTimer.restart();
    }

    /// A change from the qBittorrent group: through the confirm when it can cut you off
    /// (docs/16 §3), straight to the server otherwise.
    function qbtRequest(changes: var, confirmKind: string, what: string): void {
        if (confirmKind.length === 0) {
            App.serverPrefs.apply(changes);
            return;
        }
        const first = Object.keys(changes)[0];
        const value = changes[first];
        let message = "";
        let subtitle = "";
        let confirmText = qsTr("change");
        switch (confirmKind) {
        case "port":
            message = qsTr("Move the listening port to %1?").arg(value);
            subtitle = qsTr("Peers learn the new port at the next announce. Your router has to forward it too, or UPnP has to, or nobody can reach you.");
            break;
        case "anon":
            message = qsTr("Turn on anonymous mode?");
            subtitle = qsTr("It hides which client you use. Most private trackers allow only known clients, and may refuse you while it is on.");
            confirmText = qsTr("turn on");
            break;
        case "iface":
            message = first === "current_network_interface"
                    ? qsTr("Bind qBittorrent to %1?").arg(value ? value : qsTr("every interface"))
                    : qsTr("Use only %1?").arg(value ? value : qsTr("every address"));
            subtitle = qsTr("If that interface goes away (a VPN that drops, say), every transfer stops until it is back.");
            break;
        case "ipfilter":
            message = qsTr("Block the addresses in the filter?");
            subtitle = qsTr("Every address in the file is refused. A wrong file can block every peer, and your trackers too if you asked for that.");
            break;
        case "rotateKey":
            message = qsTr("Make a new API key?");
            subtitle = qsTr("The old key stops working at once, for anything that uses it. omaseed takes the new one if it signs in with a key.");
            confirmText = qsTr("new key");
            break;
        default: // "webui"
            if (changes.use_https === true) {
                // Issue #2: most certificates given to qBittorrent are self-signed.
                message = qsTr("Turn on HTTPS for the Web UI?");
                subtitle = qsTr("omaseed moves its saved server to https://. If the certificate is self-signed, the server sheet shows its fingerprint to trust; until you trust it, omaseed cannot reach the server.");
                confirmText = qsTr("turn on");
                break;
            }
            message = qsTr("Change the Web UI's %1?").arg(what.toLowerCase());
            subtitle = qsTr("omaseed signs in through the Web UI. It follows a new port, HTTPS or login by itself; anything else that shuts it out has to be put back in qBittorrent.");
            break;
        }
        qbtConfirm.pending = changes;
        qbtConfirm.kind = confirmKind;
        qbtConfirm.message = message;
        qbtConfirm.subtitle = subtitle;
        qbtConfirm.confirmText = confirmText;
        qbtConfirm.open();
    }

    /// A row's button: a random port, or a new API key.
    function qbtAction(action: string): void {
        if (action === "randomPort") {
            const port = 1024 + Math.floor(Math.random() * (65535 - 1024));
            root.qbtRequest({
                                "listen_port": port
                            }, "port", qsTr("Listening port"));
        } else if (action === "rotateKey") {
            root.qbtRequest({}, "rotateKey", qsTr("API key"));
        }
    }

    /// The list sheet for a row's `list` (docs/16 §4).
    function qbtOpenList(list: var, what: string, confirmKind: string): void {
        const value = App.serverPrefs.values[list.key];
        const lines = list.dirs ? Object.keys(value ?? {}) : String(value ?? "").split(list.sep).map(
                                      line => line.trim()).filter(line => line.length > 0);
        qbtListSheet.list = list;
        qbtListSheet.confirmKind = confirmKind;
        qbtListSheet.openWith(what, list.what ?? "", lines);
    }

    /// The menu for a row's `pick`, under the row's button.
    function qbtOpenPick(pick: var, anchor: Item, confirmKind: string): void {
        let options = pick.options ?? [];
        if (pick.source === "interfaces")
            options = [["", qsTr("any")]].concat(App.serverPrefs.networkInterfaces.map(entry => [entry.value,
                                                                                                 entry.name]));
        else if (pick.source === "addresses")
            options = [["", qsTr("every address")]].concat(App.serverPrefs.interfaceAddresses.filter(
                                                               address => address.length > 0).map(
                                                               address => [address, address]));
        const current = App.serverPrefs.values[pick.key];
        qbtMenu.pick = pick;
        qbtMenu.options = options;
        qbtMenu.confirmKind = confirmKind;
        qbtMenu.items = options.map((option, index) => ({
                                                            "text": option[1],
                                                            "glyph": option[0] === current ? Glyphs.check : "",
                                                            "action": String(index)
                                                        }));
        const at = anchor.mapToItem(root, 0, anchor.height + Theme.space(4));
        qbtMenu.openAt(Math.round(at.x), Math.round(at.y));
        // On the value the server has now, not the first row.
        qbtMenu.cursorIndex = Math.max(0, options.findIndex(option => option[0] === current));
    }

    /// `s` in the qBittorrent group: the app moves to the next server, and the group with it.
    function qbtNextServer(): void {
        const count = App.serverList.count;
        if (count < 2)
            return;
        const current = App.tableStateKey();
        let at = -1;
        for (let i = 0; i < count; ++i) {
            if (App.serverList.idAt(i) === current)
                at = i;
        }
        App.setDefaultServer(App.serverList.idAt((at + 1) % count));
    }

    function close(): void {
        sheet.close();
    }

    /// Go to a pane by index and put the cursor on its first row.
    function showPane(index: int): void {
        if (index < 0 || index >= root.paneCount)
            return;
        root.paneIndex = index;
        root.paneCursor = 0;
    }

    /// `enter` / `space` on the cursor row of the pane on show. `how` is "enter" or "space":
    /// on a qBittorrent row with a switch and a field, space flips and enter edits.
    function activateRow(how = "enter"): void {
        if (root.inQbt) {
            qbtPane.activate(root.paneCursor, how);
            return;
        }
        if (root.paneIndex === 0) {
            if (root.paneCursor === 0)
                App.showServersGroup = !App.showServersGroup;
            else if (root.paneCursor === 1)
                App.pathMapEnabled = !App.pathMapEnabled;
            else if (root.paneCursor === 2)
                App.deleteFilesDefault = !App.deleteFilesDefault;
            else if (root.paneCursor === 3)
                Xdg.registerAsDefaultHandler();
            else if (root.paneCursor === 4)
                App.startAtLogin = root._next(root.loginStarts, App.startAtLogin);
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
                root.toggleBarWidget();
            else if (root.paneCursor === 5)
                App.speedsAlignment = App.speedsAlignment === "left" ? "right" : "left";
            else if (root.paneCursor === 6)
                root.cycleTrends();
            else if (root.paneCursor === 7)
                App.railScrollHint = root._next(root.scrollHints, App.railScrollHint);
            else if (root.paneCursor === 8)
                App.surfaceTint = root._next(root.tints, App.surfaceTint);
            else if (root.paneCursor === 9)
                App.trackerHostStyle = root._next(root.hostStyles, App.trackerHostStyle);
            else if (root.paneCursor === 10)
                App.detailsPlacement = root._next(root.placements, App.detailsPlacement);
            return;
        }
        if (root.paneIndex === 5) {
            if (root.paneCursor === 0)
                root.backUp();
            else if (root.paneCursor === 1)
                root.chooseRestore();
            else if (root.paneCursor === 2)
                App.autoBackupMode = root._next(root.backupModes, App.autoBackupMode);
            return;
        }
        if (root.paneIndex === 6)
            root.copyPluginLine();
    }

    // #31: the automatic copies' three choices, as [value, chip text].
    readonly property var backupModes: [["off", qsTr("off")], ["weekly", qsTr("weekly")], ["daily",
                                                                                         qsTr("daily")]]
    /// What the last backup or restore said, under the Backup pane's rows.
    property string backupNote: ""
    property bool backupNoteError: false

    /// "back up…": the portal's save dialog, then the file.
    function backUp(): void {
        const path = App.pickBackupSavePath();
        if (path.length === 0)
            return;
        bodyItem.forceActiveFocus(); // back from the file dialog
        const error = App.backUpTo(path);
        root.backupNote = error.length > 0 ? error : qsTr("Backed up to %1.").arg(path);
        root.backupNoteError = error.length > 0;
    }

    /// "restore…": the portal's open dialog, then the sheet of what the file holds.
    function chooseRestore(): void {
        const path = App.pickBackupFile();
        if (path.length === 0)
            return;
        bodyItem.forceActiveFocus(); // back from the file dialog
        const info = App.readBackup(path);
        if (info.ok !== true) {
            root.backupNote = info.error;
            root.backupNoteError = true;
            return;
        }
        root.backupNote = "";
        restoreSheet.openFor(info);
    }

    // docs/08 §5: the three scroll-hint looks, as [value, chip text]. Rail's first, and since
    // N3 every palette's too; the stored key keeps its `railScrollHint` name.
    readonly property var scrollHints: [["chevrons", qsTr("counted")], ["fade", qsTr("fade")],
        ["scrollbar", qsTr("scrollbar")]]
    // Start at login (charter §2, opt-in): no entry, in the tray (`--hidden`), or the window.
    readonly property var loginStarts: [["off", qsTr("off")], ["tray", qsTr("in the tray")], ["window",
                                                                                          qsTr("window")]]
    readonly property var tints: [[0, qsTr("none")], [4, qsTr("4 %")], [8, qsTr("8 %")]]
    readonly property var hostStyles: [["prefix", qsTr("drop prefix")], ["left", qsTr("cut start")],
        ["right", qsTr("cut end")]]
    // docs/10 §5: where Details sits.
    readonly property var placements: [["auto", qsTr("auto")], ["right", qsTr("right")], ["bottom",
                                                                                          qsTr("bottom")]]

    /// #24 T2: enter on Speed trends steps off → both → download → upload → off.
    function cycleTrends(): void {
        const down = App.downloadTrend;
        const up = App.uploadTrend;
        if (!down && !up) {
            App.downloadTrend = true;
            App.uploadTrend = true;
        } else if (down && up) {
            App.uploadTrend = false;
        } else if (down) {
            App.downloadTrend = false;
            App.uploadTrend = true;
        } else {
            App.uploadTrend = false;
        }
    }

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

    /// The About pane's copyable `omaseed --install-bar-widget` line.
    function copyPluginLine(): void {
        clipboard.text = App.barWidgetCommand;
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

    /// #17: in the bar or out of it; nothing while the widget cannot be had or is busy.
    function toggleBarWidget(): void {
        if (!App.barWidget.available || App.barWidget.busy)
            return;
        if (App.barWidget.inBar)
            App.barWidget.remove();
        else
            App.barWidget.install();
    }

    function _barWidgetText(): string {
        if (!App.barWidget.available)
            return qsTr("needs the Omarchy shell and omaseed's installed widget.");
        if (App.barWidget.busy)
            return App.barWidget.inBar ? qsTr("taking it out of the bar…") : qsTr("putting it in the bar…");
        if (App.barWidget.message.length > 0 && !App.barWidget.lastOk)
            return App.barWidget.message;
        return qsTr("the omaseed mark in the Omarchy bar: speeds, alt-speed and adds without the window.") + "\n"
                + qsTr("What it shows and what its clicks do: the gear in its popup.");
    }

    function _nextTrayMode(): string {
        const modes = ["auto", "always", "never"];
        return modes[(modes.indexOf(App.trayIconMode) + 1) % modes.length];
    }

    /// The tallest pane's own height. Keybindings is left out: it is a list with a scroller of
    /// its own, and takes whatever height the others set.
    readonly property real _tallestPane: Math.max(generalPane.implicitHeight, serversPane.implicitHeight,
                                                  notificationsPane.implicitHeight,
                                                  appearancePane.implicitHeight, backupPane.implicitHeight,
                                                  aboutPane.implicitHeight)
    /// The most the body may take so that the whole modal stays within 85 % of the window:
    /// the sheet adds its padding, the gap and its hint row around the body.
    readonly property real _bodyCap: Math.max(Theme.space(200), Math.floor(root.height * 0.85) - 2
                                              * Theme.space(16) - Theme.space(12) - Theme.space(8) - 1
                                              - Math.ceil(captionMetrics.height))

    /// The pane on show, in `navRows` order; every qBittorrent section is the one pane.
    readonly property Item _paneOnShow: root.inQbt ? qbtPane : [generalPane, serversPane,
                                                                notificationsPane, keysPane,
                                                                appearancePane, backupPane, aboutPane][root.paneIndex]

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
        root.showPane((root.paneIndex + delta + root.paneCount) % root.paneCount);
    }

    /// Scrolls the nav to keep its current row in view (both groups are taller than a short
    /// window).
    function _revealNav(): void {
        const row = root.paneIndex < root.qbtStart ? navRepeater.itemAt(root.paneIndex)
                                                   : qbtNavRepeater.itemAt(root.paneIndex - root.qbtStart);
        if (!row)
            return;
        const top = row.mapToItem(navColumn, 0, 0).y;
        if (top < navScroller.contentY)
            navScroller.contentY = Math.max(0, top - Theme.space(24));
        else if (top + row.height > navScroller.contentY + navScroller.height)
            navScroller.contentY = top + row.height - navScroller.height;
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
        // Folded, there is no nav to hand the keyboard to: the sideways keys move the tabs.
        if (root.folded) {
            switch (event.key) {
            case Qt.Key_H:
            case Qt.Key_Left:
            case Qt.Key_BracketLeft:
                root._moveNav(-1);
                event.accepted = true;
                return;
            case Qt.Key_L:
            case Qt.Key_Right:
            case Qt.Key_BracketRight:
                root._moveNav(1);
                event.accepted = true;
                return;
            case Qt.Key_Tab:
            case Qt.Key_Backtab:
                event.accepted = true;
                return;
            default:
                break;
            }
        }
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            root.area = root.area === "nav" ? "pane" : "nav";
            if (root.area === "pane" && root.paneCursor < 0)
                root.paneCursor = 0;
            event.accepted = true;
            return;
        }
        // The digits pick a section of the group on show: 1–6 in omaseed's, 1–9 in the
        // qBittorrent group's.
        if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {
            const base = root.inQbt ? root.qbtStart : 0;
            const count = root.inQbt ? root.qbtSections.length : root.navRows.length;
            const at = event.key - Qt.Key_1;
            if (at < count)
                root.showPane(base + at);
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_S && root.inQbt) {
            root.qbtNextServer();
            event.accepted = true;
            return;
        }
        // The Servers pane's "add a server" row prints `a` whichever side has the keyboard,
        // so `2 a` from the nav adds one too (servers-walk: it used to need a tab first).
        if (event.key === Qt.Key_A && root.paneIndex === 1) {
            serverSheet.openNew();
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
            root.activateRow("enter");
            break;
        case Qt.Key_Space:
            root.activateRow("space");
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
            else if (root.inQbt)
                qbtPane.resetDefaults(root.paneCursor);
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

        // Another server: its settings, read afresh.
        function onServerChanged(): void {
            if (root.opened && root.inQbt)
                Qt.callLater(root._readQbt);
        }

        // HTTPS turned on that qBittorrent could not start: the app stays where it is.
        function onWebUiNotFollowed(why: string): void {
            root._flashQbtNote(why, true);
        }

        target: App
    }

    Connections {
        function onApplyFailed(keys: var, error: string): void {
            root._flashQbtNote(qsTr("not changed: %1").arg(error), true);
            qbtPane.syncFields();
        }

        function onApplied(keys: var, kept: var): void {
            if (keys.indexOf("current_network_interface") >= 0)
                App.serverPrefs.loadInterfaces(App.serverPrefs.values.current_network_interface ?? "");
            if (kept.length > 0)
                root._flashQbtNote(qsTr("the server kept its own value for %1").arg(kept.join(", ")),
                                   true);
            else
                root._flashQbtNote(qsTr("saved"), false);
        }

        function onApiKeyRotated(key: string): void {
            root._flashQbtNote(qsTr("new API key made"), false);
        }

        function onApiKeyRotateFailed(error: string): void {
            root._flashQbtNote(qsTr("no new key: %1").arg(error), true);
        }

        target: App.serverPrefs
    }

    QbtSettingsSections {
        id: qbtSchema
    }

    Timer {
        id: qbtNoteTimer

        interval: 6000

        onTriggered: root.qbtNote = ""
    }

    onOpenedChanged: {
        if (root.opened) {
            root._syncThresholdField();
            root._syncAppearanceFields();
        }
    }
    onPaneIndexChanged: {
        paneStack.contentY = 0;
        // A click on a tab sets the tabs' own index, which ends a binding; keep them in step.
        // From paneIndex itself: `inQbt` may not have caught up yet inside this handler.
        const qbt = root.paneIndex >= root.qbtStart;
        sectionTabs.currentIndex = root.paneIndex - (qbt ? root.qbtStart : 0);
        groupTabs.currentIndex = qbt ? 1 : 0;
        root.qbtNote = "";
        root.backupNote = "";
        if (root.paneIndex === 5)
            App.refreshBackupInfo();
        Qt.callLater(root._revealNav);
        if (root.opened && qbt)
            Qt.callLater(root._readQbt);
    }
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
                && (sheet.opened || serverSheet.opened || confirm.opened || qbtConfirm.opened
                    || qbtListSheet.opened || qbtMenu.opened))
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
        hints: {
            const hints = [{
                               "key": "j k",
                               "label": qsTr("move")
                           }, root.folded ? {
                                                "key": "h l",
                                                "label": qsTr("section")
                                            } : {
                                                "key": "tab",
                                                "label": qsTr("pane")
                                            }];
            if (!root.inQbt)
                return hints.concat([{
                                         "key": "enter",
                                         "label": qsTr("act")
                                     }]);
            hints.push({
                           "key": "space",
                           "label": qsTr("toggle")
                       }, {
                           "key": "enter",
                           "label": qsTr("edit")
                       });
            if (root.qbtSection && root.qbtSection.id === "advanced")
                hints.push({
                               "key": "d",
                               "label": qsTr("default")
                           });
            if (App.serverList.count > 1)
                hints.push({
                               "key": "s",
                               "label": qsTr("server")
                           });
            return hints;
        }
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
                // Two groups (docs/16 §2): omaseed's own six, then the server's nine under a
                // head that names it. Taller than a short window, so it scrolls.
                Flickable {
                    id: navScroller

                    Layout.fillHeight: true
                    Layout.fillWidth: false
                    Layout.maximumWidth: Theme.space(150)
                    Layout.preferredWidth: Theme.space(150)
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    contentHeight: navColumn.implicitHeight
                    contentWidth: width
                    objectName: "settingsNav"
                    visible: !root.folded

                    ColumnLayout {
                        id: navColumn

                        spacing: 1
                        width: navScroller.width

                        Label {
                            Layout.bottomMargin: Theme.space(8)
                            Layout.fillWidth: true
                            Layout.leftMargin: Theme.space(4)
                            text: qsTr("settings")
                            tracked: true
                        }

                        Repeater {
                            id: navRepeater

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

                        // docs/16 §2: the group's head names the server it edits:
                        // "qbittorrent", then "playground 5.2.3" under it.
                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: Theme.space(4)
                            Layout.topMargin: Theme.space(14)
                            objectName: "qbtNavHead"
                            text: qsTr("qbittorrent")
                            tracked: true
                        }

                        Label {
                            Layout.bottomMargin: Theme.space(8)
                            Layout.fillWidth: true
                            Layout.leftMargin: Theme.space(4)
                            dim: true
                            elide: Text.ElideRight
                            objectName: "qbtNavServer"
                            size: "small"
                            text: root.qbtServerText
                            visible: root.qbtServerText.length > 0
                        }

                        Repeater {
                            id: qbtNavRepeater

                            model: root.qbtSections

                            RailRow {
                                id: qbtNavRow

                                required property int index
                                required property var modelData
                                readonly property int pane: root.qbtStart + qbtNavRow.index

                                Layout.fillWidth: true
                                objectName: "qbtNavRow" + qbtNavRow.index
                                glyph: qbtNavRow.modelData.glyph
                                hasCursor: root.area === "nav" && root.paneIndex === qbtNavRow.pane
                                selected: root.paneIndex === qbtNavRow.pane
                                // The short name: the nav is 150 px, the pane's head has
                                // the whole one.
                                text: qbtNavRow.modelData.shortName

                                onClicked: {
                                    root.showPane(qbtNavRow.pane);
                                    root.area = "nav";
                                    bodyItem.forceActiveFocus();
                                }
                            }
                        }
                    }
                }

                Hairline {
                    Layout.fillHeight: true
                    Layout.leftMargin: Theme.space(10)
                    Layout.rightMargin: Theme.space(18)
                    vertical: true
                    visible: !root.folded
                }

                // ---- the pane (`.settings .pane`) --------------------------------------
                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    spacing: Theme.space(12)

                    // docs/16 §2: folded, the tabs start with a two-way switch between the
                    // groups, and the section tabs below it follow the group.
                    Tabs {
                        id: groupTabs

                        Layout.fillWidth: true
                        currentIndex: root.inQbt ? 1 : 0
                        model: [{
                                "text": qsTr("omaseed")
                            }, {
                                "text": qsTr("qBittorrent")
                            }]
                        objectName: "settingsGroupTabs"
                        visible: root.folded

                        onActivated: index => {
                            if ((index === 1) !== root.inQbt)
                                root.showPane(index === 1 ? root.qbtStart : 0);
                            root.area = "pane";
                            bodyItem.forceActiveFocus();
                        }
                    }

                    // docs/15 Q3 A: the nav, folded into tabs across the top of the sheet.
                    Tabs {
                        id: sectionTabs

                        Layout.fillWidth: true
                        clip: true
                        currentIndex: root.paneIndex - (root.inQbt ? root.qbtStart : 0)
                        model: (root.inQbt ? root.allRows.slice(root.qbtStart) : root.navRows).map(
                                   row => ({
                                           "text": row.shortText ?? row.text
                                       }))
                        objectName: "settingsTabs"
                        visible: root.folded

                        onActivated: index => {
                            root.showPane(index + (root.inQbt ? root.qbtStart : 0));
                            root.area = "pane";
                            bodyItem.forceActiveFocus();
                        }
                    }

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
                            text: root.allRows[root.paneIndex].text
                        }

                        // docs/16 §3: which server this is, and that a change is saved as it
                        // is made; after a change, what the server said about it.
                        Label {
                            Layout.maximumWidth: Theme.space(320)
                            color: root.qbtNote.length > 0 ? (root.qbtNoteError ? Theme.yellow
                                                                                 : Theme.green)
                                                           : Theme.dimForeground
                            elide: Text.ElideRight
                            objectName: "qbtHeadNote"
                            size: "caption"
                            text: root.qbtNote.length > 0 ? root.qbtNote : qsTr("%1 · saved as you change it").arg(
                                                                               App.serverName)
                            visible: root.inQbt
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

                            // Start at login: the XDG autostart entry is the setting
                            // (integration/Autostart), so the chips show what the next login
                            // will do. Off by default (charter §2, opt-in).
                            SettingsRow {
                                Layout.fillWidth: true
                                description: App.startAtLogin === "tray" ? qsTr("omaseed starts in the tray, without its window, and keeps the seed history going. Launch it to open the window.") : App.startAtLogin
                                                                           === "window" ? qsTr("omaseed opens its window when you log in.") : qsTr("The seed history only grows while omaseed runs. Start it at login, in the tray or with its window.")
                                glyph: Glyphs.start
                                hasCursor: root.area === "pane" && root.paneCursor === 4
                                objectName: "startAtLoginRow"
                                title: qsTr("Start at login")

                                onClicked: {
                                    root.paneCursor = 4;
                                    root.area = "pane";
                                    App.startAtLogin = root._next(root.loginStarts, App.startAtLogin);
                                }

                                RowLayout {
                                    spacing: Theme.space(6)

                                    Repeater {
                                        model: root.loginStarts

                                        Chip {
                                            id: loginChip

                                            required property var modelData

                                            selected: App.startAtLogin === loginChip.modelData[0]
                                            text: loginChip.modelData[1]

                                            onClicked: App.startAtLogin = loginChip.modelData[0]
                                        }
                                    }
                                }
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
                                            parts.push(serverRow.torrentCount === 1 ? qsTr("1 torrent") : qsTr(
                                                                                          "%1 torrents").arg(
                                                           serverRow.torrentCount));
                                        else if (serverRow.lastError.length > 0)
                                            parts.push(serverRow.lastError);
                                        return parts.join(" · ");
                                    }
                                    /// The server the app is on (`tableStateKey()` is its id; the
                                    /// serverName read re-runs this on a switch).
                                    readonly property bool current: {
                                        void App.serverName;
                                        return App.hasServer && !App.demoMode
                                                && App.serverList.idAt(serverRow.index) === App.tableStateKey();
                                    }
                                    /// ...and syncing with it: known to answer without a test.
                                    readonly property bool live: serverRow.current && App.globalState.connected

                                    failing: serverRow.reachableText.length > 0
                                             && !serverRow.reachable
                                    untested: serverRow.reachableText.length === 0 && !serverRow.live
                                    hasCursor: root.area === "pane"
                                               && root.paneCursor === serverRow.index
                                    title: serverRow.name
                                    // Only the server being synced is retried; another one that failed
                                    // its test is just unreachable.
                                    status: serverRow.isDefault ? qsTr("default")
                                                                : !serverRow.failing ? ""
                                                                                     : serverRow.current ? qsTr("retrying")
                                                                                                         : qsTr("unreachable")

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

                            // Review Q10: not a cursor row, only a fact about where the
                            // secrets above live.
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.space(6)
                                objectName: "plainSecretsRow"
                                spacing: Theme.space(8)
                                visible: root.plainSecretsPath.length > 0

                                Label {
                                    Layout.alignment: Qt.AlignTop
                                    color: Theme.yellow
                                    size: "small"
                                    text: "!"
                                }

                                Label {
                                    Layout.fillWidth: true
                                    dim: true
                                    objectName: "plainSecretsText"
                                    size: "small"
                                    text: qsTr(
                                              "No keychain answered, so API keys and passwords are kept in plain files, readable only by you, in %1").arg(
                                              root.plainSecretsPath)
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
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
                                description: qsTr("auto hides the icon while the omaseed bar widget runs, and shows it again within 90 s of the widget going.")
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

                            // #17: the bar widget is part of the install; this puts it in the
                            // Omarchy bar and takes it out (App.barWidget).
                            SettingsRow {
                                Layout.fillWidth: true
                                description: root._barWidgetText()
                                glyph: Glyphs.mark
                                hasCursor: root.area === "pane" && root.paneCursor === 4
                                objectName: "barWidgetRow"
                                title: qsTr("Bar widget")

                                onClicked: {
                                    root.paneCursor = 4;
                                    root.area = "pane";
                                    root.toggleBarWidget();
                                }

                                RowLayout {
                                    spacing: Theme.space(6)

                                    // docs/21: the widget's own settings live in its popup; this
                                    // opens it there.
                                    Chip {
                                        objectName: "barWidgetCustomize"
                                        text: qsTr("customize")
                                        visible: App.barWidget.inBar && !App.barWidget.busy

                                        onClicked: App.barWidget.openSettings()
                                    }

                                    Toggle {
                                        id: barWidgetToggle

                                        checked: App.barWidget.inBar
                                        enabled: App.barWidget.available && !App.barWidget.busy
                                        objectName: "barWidgetToggle"
                                        opacity: enabled ? 1 : 0.45

                                        onToggled: {
                                            // The switch shows what the shell says, not the click.
                                            barWidgetToggle.checked = Qt.binding(() => App.barWidget.inBar);
                                            root.toggleBarWidget();
                                        }
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("which side of the hero the download and upload figures sit on.")
                                glyph: Glyphs.arrowDown
                                hasCursor: root.area === "pane" && root.paneCursor === 5
                                title: qsTr("Speed figures")

                                onClicked: {
                                    root.paneCursor = 5;
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
                                description: qsTr("the last minute of each speed, drawn faintly behind its figure. Each chip turns one on or off.")
                                glyph: Glyphs.arrowUp
                                hasCursor: root.area === "pane" && root.paneCursor === 6
                                title: qsTr("Speed trends")

                                onClicked: {
                                    root.paneCursor = 6;
                                    root.area = "pane";
                                    root.cycleTrends();
                                }

                                RowLayout {
                                    spacing: Theme.space(6)

                                    Chip {
                                        objectName: "downloadTrendChip"
                                        selected: App.downloadTrend
                                        text: qsTr("download")

                                        onClicked: App.downloadTrend = !App.downloadTrend
                                    }

                                    Chip {
                                        objectName: "uploadTrendChip"
                                        selected: App.uploadTrend
                                        text: qsTr("upload")

                                        onClicked: App.uploadTrend = !App.uploadTrend
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("how lists say there is more above or below: the rail and every palette.")
                                glyph: Glyphs.scrollHint
                                hasCursor: root.area === "pane" && root.paneCursor === 7
                                title: qsTr("Scroll hint")

                                onClicked: {
                                    root.paneCursor = 7;
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
                                hasCursor: root.area === "pane" && root.paneCursor === 8
                                title: qsTr("Surface tint")

                                onClicked: {
                                    root.paneCursor = 8;
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
                                hasCursor: root.area === "pane" && root.paneCursor === 9
                                title: qsTr("Tracker hosts")

                                onClicked: {
                                    root.paneCursor = 9;
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
                                description: qsTr("auto puts it right when the window is 16:10 or wider, else at the bottom. I cycles it.")
                                glyph: Glyphs.sidebar
                                hasCursor: root.area === "pane" && root.paneCursor === 10
                                title: qsTr("Details panel")

                                onClicked: {
                                    root.paneCursor = 10;
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

                        // ---- qBittorrent (docs/16) -------------------------------------
                        QbtSettingsPane {
                            id: qbtPane

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            cursor: root.paneCursor
                            keyboard: root.area === "pane"
                            objectName: "qbtPane"
                            prefs: App.serverPrefs
                            section: root.qbtSection
                            serverName: App.serverName
                            visible: root.inQbt

                            onActionRequested: action => root.qbtAction(action)
                            onChangeRequested: (changes, confirmKind, what) => root.qbtRequest(changes,
                                                                                               confirmKind,
                                                                                               what)
                            // A field whose change asks first keeps the keyboard in the confirm
                            // it just opened; the confirm hands it back when it closes.
                            onFieldDone: {
                                if (!qbtConfirm.opened)
                                    bodyItem.forceActiveFocus();
                            }
                            onListRequested: (list, what, confirmKind) => root.qbtOpenList(list, what,
                                                                                           confirmKind)
                            onNoteRequested: text => root._flashQbtNote(text, true)
                            onPickRequested: (pick, anchor, confirmKind) => root.qbtOpenPick(pick,
                                                                                             anchor,
                                                                                             confirmKind)
                            onRowClicked: index => {
                                if (index >= 0)
                                    root.paneCursor = index;
                                root.area = "pane";
                            }
                        }

                        // ---- Backup (#31, docs/24 §3) --------------------------------------
                        ColumnLayout {
                            id: backupPane

                            anchors.fill: parent
                            spacing: Theme.space(8)
                            visible: root.paneIndex === 5

                            SettingsRow {
                                Layout.fillWidth: true
                                description: App.backupSummary
                                glyph: Glyphs.arrowDown
                                hasCursor: root.area === "pane" && root.paneCursor === 0
                                objectName: "backupRow"
                                title: qsTr("Back up omaseed")

                                onClicked: {
                                    root.paneCursor = 0;
                                    root.area = "pane";
                                    root.backUp();
                                }

                                Button {
                                    bordered: true
                                    primary: true
                                    text: qsTr("back up…")

                                    onClicked: {
                                        root.paneCursor = 0;
                                        root.backUp();
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: qsTr("From an omaseed backup. You pick the parts; what is here now is copied aside first.")
                                glyph: Glyphs.arrowUp
                                hasCursor: root.area === "pane" && root.paneCursor === 1
                                objectName: "restoreRow"
                                title: qsTr("Restore")

                                onClicked: {
                                    root.paneCursor = 1;
                                    root.area = "pane";
                                    root.chooseRestore();
                                }

                                Button {
                                    bordered: true
                                    text: qsTr("restore…")

                                    onClicked: {
                                        root.paneCursor = 1;
                                        root.chooseRestore();
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                description: {
                                    const where = qsTr("Into %1, the last 4 kept.").arg(App.backupsDirText);
                                    if (App.autoBackupMode === "off")
                                        return qsTr("Off: backups are made by hand only. On, they go into %1, the last 4 kept.").arg(App.backupsDirText);
                                    return App.lastBackupText.length > 0 ? where + " " + qsTr("Last one: %1.").arg(App.lastBackupText)
                                                                         : where + " " + qsTr("The first one is made within the hour.");
                                }
                                glyph: Glyphs.recheck
                                hasCursor: root.area === "pane" && root.paneCursor === 2
                                objectName: "autoBackupRow"
                                title: qsTr("Automatic copies")

                                onClicked: {
                                    root.paneCursor = 2;
                                    root.area = "pane";
                                    App.autoBackupMode = root._next(root.backupModes, App.autoBackupMode);
                                }

                                RowLayout {
                                    spacing: Theme.space(6)

                                    Button {
                                        text: qsTr("open folder")

                                        onClicked: App.openBackupsFolder()
                                    }

                                    Repeater {
                                        model: root.backupModes

                                        Chip {
                                            id: backupModeChip

                                            required property var modelData

                                            selected: App.autoBackupMode === backupModeChip.modelData[0]
                                            text: backupModeChip.modelData[1]

                                            onClicked: App.autoBackupMode = backupModeChip.modelData[0]
                                        }
                                    }
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                color: root.backupNoteError ? Theme.yellow : Theme.green
                                objectName: "backupNote"
                                size: "small"
                                text: root.backupNote
                                visible: root.backupNote.length > 0
                                wrapMode: Text.WordWrap
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
                            visible: root.paneIndex === 6

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
                                        text: App.barWidgetCommand
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

    // #31: the restore sheet, over the modal like the server sheet.
    RestoreSheet {
        id: restoreSheet

        anchors.fill: parent
        z: 1

        // The file dialog took the window's focus before the sheet opened, so the sheet has
        // nothing to give it back to: the modal takes it.
        onRestored: (text, ok) => {
            root.backupNote = text;
            root.backupNoteError = !ok;
            bodyItem.forceActiveFocus();
        }
        onRejected: bodyItem.forceActiveFocus()
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

    // docs/16 §3: a change that can cut you off asks first.
    ConfirmDialog {
        id: qbtConfirm

        property var pending: ({})
        property string kind: ""

        anchors.fill: parent
        objectName: "qbtConfirm"
        z: 2

        onAccepted: {
            if (qbtConfirm.kind === "rotateKey")
                App.serverPrefs.rotateApiKey();
            else
                App.serverPrefs.apply(qbtConfirm.pending);
            qbtConfirm.pending = {};
            bodyItem.forceActiveFocus();
        }
        onRejected: {
            qbtConfirm.pending = {};
            // Closing gave the keyboard back to the field the change came from; a field being
            // edited keeps its text, so the keyboard leaves it before the server's value goes in.
            bodyItem.forceActiveFocus();
            qbtPane.syncFields();
        }
    }

    QbtListSheet {
        id: qbtListSheet

        property var list: ({})
        property string confirmKind: ""

        anchors.fill: parent
        objectName: "qbtListSheet"
        z: 1

        onSaved: lines => {
            const list = qbtListSheet.list;
            let value;
            if (list.dirs) {
                // scan_dirs is {path: where to save}; a folder already there keeps its choice,
                // a new one saves to the default path (0).
                const before = App.serverPrefs.values[list.key] ?? {};
                value = {};
                for (const line of lines)
                    value[line] = before[line] !== undefined ? before[line] : 0;
            } else {
                value = lines.join(list.sep);
            }
            root.qbtRequest({
                                [list.key]: value
                            }, qbtListSheet.confirmKind, qbtListSheet.title);
        }
    }

    Menu {
        id: qbtMenu

        property var pick: ({})
        property var options: []
        property string confirmKind: ""

        anchors.fill: parent
        dimmed: false
        objectName: "qbtMenu"
        z: 1

        onActivated: action => {
            const option = qbtMenu.options[parseInt(action, 10)];
            if (!option)
                return;
            const changes = {
                [qbtMenu.pick.key]: option[0]
            };
            // A new interface starts on all of its addresses.
            if (qbtMenu.pick.key === "current_network_interface")
                changes.current_interface_address = "";
            if (option[0] !== App.serverPrefs.values[qbtMenu.pick.key])
                root.qbtRequest(changes, qbtMenu.confirmKind, qbtMenu.pick.key);
            bodyItem.forceActiveFocus();
        }
        onRejected: bodyItem.forceActiveFocus()
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
