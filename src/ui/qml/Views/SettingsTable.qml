import QtQuick
import Omaseed
import Omaseed.Kit

// Every setting as a row of one table (docs/63 §6, docs/DECISIONS.md C42 Z7): omaseed's own,
// listed here, and the settings of the server on show, read from `QbtSettingsSections`
// through the same tests its pane draws with. The Settings modal counts, names and runs its
// own rows from it, and the everything palette lists `rows()`; nothing here draws.
//
// An own row is {id, pane, title, control, read, write, options, offered, shown, enabled}:
//   control   "switch" | "choice" | "number" | "action": what the pane's row holds
//   read      () => its value now; write: value => stores it (none on an action)
//   options   [[value, text], …] for a choice, in the pane's order
//   offered   value => whether that option can be had now (none: every one)
//   shown     () => whether the pane has the row now (none: always); the pane's cursor
//             counts the shown ones, in this order, which is the markup's
//   enabled   () => whether `enter` does anything on it (none: always)
//
// `rows()` gives own and server rows in one shape:
//   {id, pane, title, kind: "switch" | "choice" | "go", value, valueText, options, enabled,
//    set(value), paneText, server, control, readOnly}
// "go" is every row the palette only leads to: a number, a path, a list, an action, a row
// the pane asks about first, a dimmed one.
QtObject {
    id: root

    /// What the server can do and its engine's name (App's; a test gives its own).
    property var features: App.features
    property string engineName: App.serverEngineName
    /// The server's settings and the alternative speeds' presets (C32 D8 a).
    property var prefs: App.serverPrefs
    property var presets: App.altPresets
    property bool connected: App.globalState.connected
    /// The server's name, said on a server row's second line while two or more are saved.
    property string serverName: App.serverName
    property int serverCount: App.serverList.count

    /// The servers' settings as data, and what a row of them means.
    readonly property QbtSettingsSections schema: QbtSettingsSections {}

    /// omaseed's own panes, in the nav's order.
    readonly property var panes: [
        {
            "id": "general",
            "glyph": Glyphs.settings,
            "text": qsTr("General")
        },
        {
            "id": "servers",
            "glyph": Glyphs.server,
            "text": qsTr("Servers")
        },
        {
            "id": "notifications",
            "glyph": Glyphs.notifications,
            "text": qsTr("Notifications")
        },
        {
            "id": "keybindings",
            "glyph": Glyphs.keyboard,
            "text": qsTr("Keybindings"),
            // The folded tabs' word (docs/15 Q3 A): six whole names are wider than the sheet
            // at the 560 px floor, and this one is the longest.
            "shortText": qsTr("Keys")
        },
        {
            "id": "appearance",
            "glyph": Glyphs.appearance,
            "text": qsTr("Appearance"),
            // Its own kana word (board 46 `.shh h4 .kn`); every other pane's is settings'.
            "labelId": "appearance"
        },
        {
            // #31, docs/24 §3: omaseed's own backup, after Appearance.
            "id": "backup",
            "glyph": Glyphs.backup,
            "text": qsTr("Backup")
        },
        {
            "id": "about",
            "glyph": Glyphs.about,
            "text": qsTr("About")
        }
    ]

    /// docs/33 D25 (DECISIONS C13): the tray icon and the bar widget as one choice. The ones
    /// the widget needs are skipped while it cannot be had.
    function _widgetCanBeHad(value: string): bool {
        return (value !== "widget" && value !== "both") || App.barWidget.available || App.barWidget.inBar;
    }

    function _notification(key: string, title: string): var {
        return {
            "id": "notifications." + key,
            "pane": "notifications",
            "title": title,
            "control": "switch",
            "read": () => App.notificationsEnabled[key] === true,
            "write": on => App.setNotificationEnabled(key, on)
        };
    }

    /// omaseed's own settings, pane by pane, each pane's in its rows' order.
    readonly property var ownRows: [
        {
            "id": "general.serversGroup",
            "pane": "general",
            "title": qsTr("Show the Servers group in the rail"),
            "control": "switch",
            "read": () => App.showServersGroup,
            "write": on => App.showServersGroup = on
        },
        {
            "id": "general.pathMap",
            "pane": "general",
            "title": qsTr("Open folder through path mapping"),
            "control": "switch",
            "read": () => App.pathMapEnabled,
            "write": on => App.pathMapEnabled = on
        },
        {
            "id": "general.deleteFiles",
            "pane": "general",
            "title": qsTr("Delete files by default"),
            "control": "switch",
            "read": () => App.deleteFilesDefault,
            "write": on => App.deleteFilesDefault = on
        },
        {
            "id": "general.handler",
            "pane": "general",
            "title": qsTr("Register as the magnet and .torrent handler"),
            "control": "action"
        },
        {
            // Start at login (charter §2, opt-in): no entry, in the tray (`--hidden`), or
            // the window.
            "id": "general.startAtLogin",
            "pane": "general",
            "title": qsTr("Start at login"),
            "control": "choice",
            "options": [["off", qsTr("off")], ["tray", qsTr("in the tray")], ["window", qsTr("window")]],
            "read": () => App.startAtLogin,
            "write": value => App.startAtLogin = value
        },
        root._notification("completed", qsTr("Torrent completed")),
        root._notification("added", qsTr("Torrent added")),
        root._notification("errored", qsTr("Torrent errored")),
        root._notification("serverLost", qsTr("Server lost and back")),
        root._notification("diskFull", qsTr("Disk almost full")),
        {
            "id": "notifications.lowDiskThreshold",
            "pane": "notifications",
            "title": qsTr("Low-disk threshold"),
            "control": "number",
            "read": () => App.lowDiskThresholdGiB,
            "write": value => App.lowDiskThresholdGiB = value
        },
        // #24 T8: the weekly recap.
        root._notification("weekly", qsTr("Weekly recap")),
        {
            // DECISIONS C24 H11 (#171): the skins, board 46's three choices in its order.
            "id": "appearance.skin",
            "pane": "appearance",
            "title": qsTr("Skin"),
            "control": "choice",
            "options": [["omarchy", qsTr("Omarchy")], ["console", qsTr("Console")],
                ["desktop", qsTr("follow the desktop")]],
            "read": () => App.skinChoice,
            "write": value => App.skinChoice = value
        },
        {
            "id": "appearance.followTheme",
            "pane": "appearance",
            "title": qsTr("Follow the Omarchy theme"),
            "control": "switch",
            "read": () => App.followOmarchyTheme,
            "write": on => App.followOmarchyTheme = on
        },
        {
            // 0 is no override: the shell's size.
            "id": "appearance.baseSize",
            "pane": "appearance",
            "title": qsTr("Base size"),
            "control": "number",
            "read": () => App.baseSizeOverride,
            "write": value => App.baseSizeOverride = value
        },
        {
            // The window's ground (C24 H13).
            "id": "appearance.window",
            "pane": "appearance",
            "title": qsTr("Window"),
            "control": "choice",
            "options": [["opaque", qsTr("opaque")], ["translucent", qsTr("translucent")]],
            "read": () => App.windowGround,
            "write": value => App.windowGround = value
        },
        {
            // C24 H13: a row only while the ground is translucent; 0 is the shell's value.
            "id": "appearance.windowAlpha",
            "pane": "appearance",
            "title": qsTr("Window transparency"),
            "control": "number",
            "shown": () => App.windowGround === "translucent",
            "read": () => App.windowAlphaOverride,
            "write": value => App.windowAlphaOverride = value
        },
        {
            "id": "appearance.inTheBar",
            "pane": "appearance",
            "title": qsTr("In the bar"),
            "control": "choice",
            "options": [["widget", qsTr("widget")], ["tray", qsTr("tray icon")], ["both", qsTr("both")],
                ["none", qsTr("none")]],
            "offered": value => root._widgetCanBeHad(value),
            "read": () => App.inTheBar,
            "write": value => App.inTheBar = value
        },
        {
            // docs/21: the widget's own settings live in its popup (#17).
            "id": "appearance.widget",
            "pane": "appearance",
            "title": qsTr("Widget"),
            "control": "action",
            "enabled": () => App.barWidget.inBar && !App.barWidget.busy
        },
        {
            // C36 M12.
            "id": "appearance.omarchyMenu",
            "pane": "appearance",
            "title": qsTr("In the Omarchy menu"),
            "control": "switch",
            "enabled": () => App.omarchyMenuOffered,
            "read": () => App.inOmarchyMenu,
            "write": on => App.inOmarchyMenu = on
        },
        {
            // docs/08 §5: the three scroll-hint looks. Rail's first, and since N3 every
            // palette's too; the stored key keeps its `railScrollHint` name.
            "id": "appearance.scrollHint",
            "pane": "appearance",
            "title": qsTr("Scroll hint"),
            "control": "choice",
            "options": [["chevrons", qsTr("counted")], ["fade", qsTr("fade")], ["scrollbar", qsTr("scrollbar")]],
            "read": () => App.railScrollHint,
            "write": value => App.railScrollHint = value
        },
        {
            // C32 D16 a: the rail's tint, in percent.
            "id": "appearance.surfaceTint",
            "pane": "appearance",
            "title": qsTr("Surface tint"),
            "control": "choice",
            "options": [[0, qsTr("off")], [4, qsTr("4 %")], [8, qsTr("8 %")]],
            "read": () => App.surfaceTint,
            "write": value => App.surfaceTint = value
        },
        {
            // docs/08 §5.
            "id": "appearance.trackerHosts",
            "pane": "appearance",
            "title": qsTr("Tracker hosts"),
            "control": "choice",
            "options": [["prefix", qsTr("drop prefix")], ["left", qsTr("cut start")], ["right", qsTr("cut end")]],
            "read": () => App.trackerHostStyle,
            "write": value => App.trackerHostStyle = value
        },
        {
            // docs/10 §5: where Details sits.
            "id": "appearance.detailsPlacement",
            "pane": "appearance",
            "title": qsTr("Details panel"),
            "control": "choice",
            "options": [["auto", qsTr("auto")], ["right", qsTr("right")], ["bottom", qsTr("bottom")]],
            "read": () => App.detailsPlacement,
            "write": value => App.detailsPlacement = value
        },
        {
            // #24 T5.
            "id": "appearance.pulseRows",
            "pane": "appearance",
            "title": qsTr("Pulse sending rows"),
            "control": "switch",
            "read": () => App.pulseRows,
            "write": on => App.pulseRows = on
        },
        {
            // C32 D2 a: where ↓ and ↑ are drawn.
            "id": "appearance.speedsPlace",
            "pane": "appearance",
            "title": qsTr("Speeds"),
            "control": "choice",
            "options": [["title", qsTr("title bar")], ["status", qsTr("status line")]],
            "read": () => App.speedsPlace,
            "write": value => App.speedsPlace = value
        },
        {
            "id": "backup.backUp",
            "pane": "backup",
            "title": qsTr("Back up omaseed"),
            "control": "action"
        },
        {
            "id": "backup.restore",
            "pane": "backup",
            "title": qsTr("Restore"),
            "control": "action"
        },
        {
            // #31: the automatic copies.
            "id": "backup.auto",
            "pane": "backup",
            "title": qsTr("Automatic copies"),
            "control": "choice",
            "options": [["off", qsTr("off")], ["weekly", qsTr("weekly")], ["daily", qsTr("daily")]],
            "read": () => App.autoBackupMode,
            "write": value => App.autoBackupMode = value
        },
        {
            // The copyable `omaseed --install-bar-widget` line.
            "id": "about.widgetLine",
            "pane": "about",
            "title": qsTr("Companion bar widget"),
            "control": "action"
        }
    ]

    readonly property var _ownById: {
        const map = {};
        for (const row of root.ownRows)
            map[row.id] = row;
        return map;
    }

    /// The own row `id`, or null.
    function own(id: string): var {
        return root._ownById[id] ?? null;
    }

    function _shown(row: var): bool {
        return row.shown === undefined || row.shown();
    }

    function _enabled(row: var): bool {
        return row.enabled === undefined || row.enabled();
    }

    /// How many cursor rows own pane `pane` has now.
    function count(pane: string): int {
        return root.ownRows.filter(row => row.pane === pane && root._shown(row)).length;
    }

    /// Where own row `id` is in its pane's cursor, or -1 while the pane does not have it.
    function cursorOf(id: string): int {
        const target = root.own(id);
        if (target === null || !root._shown(target))
            return -1;
        let at = 0;
        for (const row of root.ownRows) {
            if (row === target)
                return at;
            if (row.pane === target.pane && root._shown(row))
                ++at;
        }
        return -1;
    }

    /// The id of the row at `cursor` in own pane `pane`, or "".
    function rowAt(pane: string, cursor: int): string {
        const rows = root.ownRows.filter(row => row.pane === pane && root._shown(row));
        return cursor >= 0 && cursor < rows.length ? rows[cursor].id : "";
    }

    /// Every option of own choice `id`, as `[[value, text], …]`, offered or not.
    function allOptions(id: string): var {
        const row = root.own(id);
        return row !== null && row.options ? row.options : [];
    }

    /// Own choice `id` can take `value` now.
    function offers(id: string, value: var): bool {
        const row = root.own(id);
        return row !== null && (row.offered === undefined || row.offered(value));
    }

    /// The options of own choice `id` that can be had now.
    function options(id: string): var {
        return root.allOptions(id).filter(option => root.offers(id, option[0]));
    }

    /// The option after the one own choice `id` holds, wrapping round and skipping those
    /// that can't be had; what it holds when there is no other.
    function nextOption(id: string): var {
        const row = root.own(id);
        const all = root.allOptions(id);
        const current = row.read();
        let at = all.findIndex(option => option[0] === current);
        for (let step = 0; step < all.length; ++step) {
            at = (at + 1) % all.length;
            if (root.offers(id, all[at][0]))
                return all[at][0];
        }
        return current;
    }

    /// `enter` on own row `id`: a switch flips, a choice takes its next option. A number and
    /// an action are the pane's to run (its field, its function). Returns the row's control,
    /// "" for no such row.
    function activate(id: string): string {
        const row = root.own(id);
        if (row === null)
            return "";
        if (root._enabled(row)) {
            if (row.control === "switch")
                row.write(!row.read());
            else if (row.control === "choice")
                row.write(root.nextOption(id));
        }
        return row.control;
    }

    // ---- the server on show ---------------------------------------------------------------

    /// The engine whose settings the server on show has: "qbittorrent", "transmission",
    /// "deluge"; "" for one omaseed has no settings for (App.features.qbtSettings, docs/36
    /// §4.4; another engine's are read through its session, C14 E6).
    readonly property string serverEngine: root.features.qbtSettings !== false ? "qbittorrent"
                                           : root.features.sessionSettings === true ? root.engineName.toLowerCase()
                                                                                    : ""
    /// C25 K31–K32: the group's name as its head and pane say it.
    readonly property string engineGroupName: root.features.qbtSettings !== false ? qsTr("qBittorrent")
                                                                                  : (root.engineName || qsTr("server"))

    /// An engine's sections that hold settings rows, in the nav's order; [] for an engine
    /// with none. (Search plugins and Backup are the modal's own panes, with no rows here.)
    function sectionsOf(engine: string): var {
        if (engine === "qbittorrent")
            return root.schema.sections;
        if (engine === "transmission")
            return root.schema.transmissionSections;
        if (engine === "deluge")
            return root.schema.delugeSections;
        return [];
    }
    /// Those of the server on show.
    readonly property var serverSections: root.sectionsOf(root.serverEngine)

    function _applyServer(changes: var): void {
        // C32 D8 a: the turtle's and burst's keys go through the presets.
        if (root.presets && root.presets.owns(changes))
            root.presets.apply(changes);
        else
            root.prefs.apply(changes);
    }

    function _serverRow(section: var, spec: var, index: int): var {
        const schema = root.schema;
        const values = (spec.presets ? root.presets : root.prefs).values;
        const key = schema.rowKey(spec) || "row" + index;
        const confirm = schema.confirmOf(spec, section);
        const enabled = schema.depOk(spec, values);
        // Deluge's plugins are read only (K15, board 47 §J).
        const readOnly = spec.plugin !== undefined || spec.others !== undefined;
        const choice = spec.choice ?? (spec.pick && spec.pick.options ? spec.pick : undefined);
        let kind = "go";
        let value = undefined;
        let valueText = "";
        let options = [];
        if (spec.toggle) {
            value = !!values[spec.toggle];
            valueText = value ? qsTr("on") : qsTr("off");
            // A switch the pane would refuse, dim or ask about is the pane's.
            if (confirm.length === 0 && enabled && schema.flipRefusal(spec, values, !value).length === 0)
                kind = "switch";
        } else if (choice !== undefined) {
            options = choice.options;
            value = values[choice.key];
            const held = options.find(option => option[0] === value);
            valueText = held !== undefined ? held[1] : "";
            if (confirm.length === 0 && enabled)
                kind = "choice";
        } else if (spec.fields && spec.fields.length === 1 && spec.fields[0].kind !== "password") {
            const field = spec.fields[0];
            const text = schema.fieldText(field, values);
            valueText = text.length === 0 ? (field.noneText ?? "") : field.unit ? text + " " + field.unit : text;
        }
        const row = {
            "id": "server." + section.id + "." + key,
            "pane": "server." + section.id,
            "title": (spec.titleFor ? spec.titleFor[App.altSpeed.kind] : undefined) ?? spec.title,
            "kind": kind,
            "control": kind === "go" ? "action" : kind,
            "value": value,
            "valueText": valueText,
            "options": options,
            "enabled": enabled,
            "server": true,
            "readOnly": readOnly,
            "key": key,
            "set": wanted => {
                if (kind === "switch")
                    root._applyServer(schema.flipChanges(spec, values, wanted === true));
                else if (kind === "choice")
                    root._applyServer({
                                          [choice.key]: wanted
                                      });
            }
        };
        row.paneText = root.paneText(row);
        return row;
    }

    /// The settings of the server on show, section by section, each as its pane has it (a
    /// row whose key the server lacks is left out, C14 E4); none until it has answered.
    function serverRows(): var {
        if (!root.connected || !root.prefs || !root.prefs.loaded)
            return [];
        const rows = [];
        for (const section of root.serverSections) {
            root.schema.offered(section, root.prefs, root.presets).forEach((spec, index) => {
                if (spec.title !== undefined)
                    rows.push(root._serverRow(section, spec, index));
            });
        }
        return rows;
    }

    function _ownRow(own: var): var {
        const control = own.control;
        const kind = control === "switch" || control === "choice" ? control : "go";
        const value = own.read ? own.read() : undefined;
        const options = control === "choice" ? root.options(own.id) : [];
        let valueText = "";
        if (control === "switch") {
            valueText = value ? qsTr("on") : qsTr("off");
        } else if (control === "choice") {
            const held = root.allOptions(own.id).find(option => option[0] === value);
            valueText = held !== undefined ? held[1] : "";
        } else if (control === "number") {
            valueText = value > 0 ? String(value) : "";
        }
        const row = {
            "id": own.id,
            "pane": own.pane,
            "title": own.title,
            "kind": kind,
            "control": control,
            "value": value,
            "valueText": valueText,
            "options": options,
            "enabled": root._enabled(own),
            "server": false,
            "readOnly": false,
            "set": wanted => {
                if (own.write && root._enabled(own))
                    own.write(wanted);
            }
        };
        row.paneText = root.paneText(row);
        return row;
    }

    /// Every setting there is now: omaseed's own, pane by pane, then the server's.
    function rows(): var {
        return root.ownRows.filter(row => root._shown(row)).map(row => root._ownRow(row)).concat(root.serverRows());
    }

    /// The row `id` as `rows()` gives it, or null when there is none now.
    function row(id: string): var {
        const own = root.own(id);
        if (own !== null)
            return root._shown(own) ? root._ownRow(own) : null;
        return root.serverRows().find(row => row.id === id) ?? null;
    }

    /// Where a row is, as the modal's nav says it: "Settings › Appearance"; a server's,
    /// "Settings › qBittorrent › Speed", and its name while two servers or more are saved
    /// (when the modal shows its server select). `row` needs only its `pane`.
    function paneText(row: var): string {
        const pane = String(row.pane);
        if (pane.indexOf("server.") !== 0) {
            const own = root.panes.find(candidate => candidate.id === pane);
            return qsTr("Settings › %1").arg(own !== undefined ? own.text : "");
        }
        const section = root.serverSections.find(candidate => "server." + candidate.id === pane);
        const text = qsTr("Settings › %1 › %2").arg(root.engineGroupName).arg(section !== undefined ? section.name : "");
        return root.serverCount >= 2 ? qsTr("%1 · %2").arg(text).arg(root.serverName) : text;
    }
}
