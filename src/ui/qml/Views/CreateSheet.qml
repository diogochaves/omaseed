pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// Create a torrent (board 49 `publicSheet()` and `privateSheet()`, DECISIONS C27 CR1–CR19): one
// sheet whose first question is who the torrent is for (CR2).
//
//   from        the folder (or one file), with the Move sheet's places (↓, "places ▾") and
//               browse (ctrl+o): the portal's folder picker when this machine sees the server's
//               disks, else the server's own list under the field, names only (CR3, C16 L13).
//               Under it what the folder holds, read before anything is hashed (CR17)
//   for         anyone · a private tracker, remembered per server
//   anyone      trackers: last time's, else the library's most used public ones, each with ×,
//               and a field for more; none is fine (CR4). Format and pieces side by side
//   private     the tracker by host from the library, or a pasted announce URL; the passkey only
//               as dots (CR5); the private flag, v1 and the source tag as facts with a change
//               link (CR6); where the .torrent is saved and the round trip's note (CR7)
//   more        web seeds, a comment, hidden files left out (CR10–CR11)
//   when done   anyone: seeds from where it is, in a category (CR12, CR14)
//
// Pieces are automatic and shown before creating, with the override's list (CR11); the format
// is v1 on both paths (CR10). Nothing goes to the server before the main button: App.startCreate
// sends it all. One creation per server at a time: while one runs, the button says so (CR13).
// A server that doesn't answer keeps what was chosen, and "create when back" sends it then.
//
// Hand-formatted (qmlformat rewrites typed function bodies). Its form is Theme's `sheet*` and
// `create*` tokens.
Sheet {
    id: root

    /// The folder picker (`App.pickDirectory`), which a test replaces.
    property var pickDirectory: start => App.pickDirectory(start)
    /// The server is lost: nothing can start (board 49 §7).
    property bool serverDown: false
    /// Two servers or more: the head names the one it is made on.
    property bool multiServer: false

    /// App.createSheet() when the sheet opened.
    property var sheetData: ({})
    /// App.createFolderChecked's answer for the field as it stands; {} while asked.
    property var check: ({})
    /// The public trackers, one tier each, in order.
    property var trackers: []
    /// The private tracker's host, and whether an announce URL is pasted instead (CR5).
    property string host: ""
    property bool pasting: false
    /// The source tag (CR6), and whether it was typed here (then it is remembered for the host).
    property string source: ""
    property bool sourceTyped: false
    property bool sourceEditing: false
    property string format: "v1"
    /// Bytes; 0 is automatic.
    property real pieceSize: 0
    /// The list open under its row: "", "places", "browse", "hosts", "format", "pieces".
    property string listOpen: ""
    property bool moreOpen: false
    property bool ignoreDotfiles: true
    /// "create when back" was pressed: it goes as the server answers.
    property bool held: false
    /// The folder browsed on the server (its own list).
    property string browsing: ""
    /// The field's text as the sheet put it there: the places open on it unnarrowed.
    property string _prefill: ""
    property bool _filling: false

    readonly property alias forChoice: forChoice
    readonly property alias folderField: folderField
    readonly property alias trackerField: trackerField
    readonly property alias pasteField: pasteField
    readonly property alias sourceField: sourceField
    readonly property alias placeList: placeList
    readonly property alias optionsList: optionList
    readonly property alias categoryChips: categoryChips
    readonly property alias webSeedsField: webSeedsField
    readonly property alias commentField: commentField

    readonly property bool isPrivate: forChoice.choice === 1
    readonly property string folder: folderField.text.trim()
    readonly property var privateHosts: root.sheetData.privateHosts ?? []
    readonly property var hostRow: {
        for (const row of root.privateHosts) {
            if (row.host === root.host)
                return row;
        }
        return null;
    }
    readonly property bool checked: (root.check.state ?? "") !== "" && root.check.path === root.folder
    readonly property real totalBytes: root.checked && root.check.sized === true ? root.check.bytes : -1
    readonly property int fileCount: root.checked ? (root.check.files ?? 0) : 0
    /// The automatic piece size's row for what the folder holds.
    readonly property var autoRow: {
        if (root.totalBytes <= 0)
            return ({});
        for (const row of App.createPieceRows(root.totalBytes, Math.max(1, root.fileCount))) {
            if (row.automatic)
                return row;
        }
        return ({});
    }
    readonly property string running: App.createRunning
    readonly property bool canCreate: {
        if (root.folder.length === 0 || !root.checked || root.check.blocking === true)
            return false;
        if (root.running.length > 0)
            return false;
        if (!root.isPrivate)
            return true;
        return root.pasting ? pasteField.text.trim().length > 0 : root.hostRow !== null;
    }
    readonly property string primaryText: {
        if (root.running.length > 0)
            return qsTr("wait for %1").arg(root.running);
        if (root.serverDown)
            return qsTr("create when back");
        return root.isPrivate ? qsTr("create .torrent") : qsTr("create and seed");
    }
    /// The host the saved file is named after.
    readonly property string fileHost: {
        if (!root.pasting)
            return root.host;
        return App.maskAnnounce(pasteField.text).replace(/^[a-z]+:\/\//, "").replace(/[:/].*$/, "");
    }

    /// The places for what is typed, as the Move sheet lists them (C16 L4).
    function _placeRows(): var {
        const sections = {
            "recent": qsTr("recent"),
            "folder": qsTr("library folders"),
            "category": qsTr("categories' folders")
        };
        const typed = root.folder === root._prefill ? "" : root.folder;
        return App.move.places(typed).map(place => ({
            "section": sections[place.kind] ?? "",
            "text": place.path,
            "detail": place.kind === "folder" ? String(place.count ?? "")
                                              : place.kind === "category" ? place.name ?? "" : ""
        }));
    }

    function _setFolder(text: string): void {
        root._filling = true;
        folderField.text = text;
        root._filling = false;
        recheck.restart();
    }

    /// The last part of the folder's path: the torrent's name.
    function _name(): string {
        const trimmed = root.folder.replace(/\/+$/, "");
        return trimmed.substring(trimmed.lastIndexOf("/") + 1);
    }

    /// Opens on nothing chosen: last time's for and trackers (CR2, CR4).
    function openBlank(): void {
        root.openFor("");
    }

    /// Opens with `folder` in the field (`:`'s "create torrent from NAME…", a folder row's menu,
    /// a dropped folder).
    function openFor(folder: string): void {
        root._reset();
        root._prefill = folder;
        root._setFolder(folder);
        root.open();
        folderField.focusInput();
        if (folder.length > 0)
            folderField.cursorPosition = folder.length;
    }

    /// "create again…": the choices of the creation that didn't happen.
    function openWith(request: var): void {
        root._reset();
        forChoice.choice = request.forWhom === "private" ? 1 : 0;
        if ((request.trackers ?? []).length > 0)
            root.trackers = request.trackers.slice();
        if ((request.host ?? "").length > 0)
            root.host = request.host;
        root.format = request.format ?? "v1";
        root.pieceSize = request.pieceSize ?? 0;
        root.ignoreDotfiles = request.ignoreDotfiles ?? true;
        if ((request.source ?? "").length > 0) {
            root.source = request.source;
            root.sourceTyped = request.sourceTyped === true;
        }
        webSeedsField.text = (request.webSeeds ?? []).join(" ");
        commentField.text = request.comment ?? "";
        categoryChips.current = request.category ?? "";
        root._prefill = request.folder ?? "";
        root._setFolder(root._prefill);
        root.open();
        folderField.focusInput();
    }

    function _reset(): void {
        root.sheetData = App.createSheet();
        root.check = ({});
        root.listOpen = "";
        root.moreOpen = false;
        root.held = false;
        root.pasting = false;
        root.format = "v1";
        root.pieceSize = 0;
        root.ignoreDotfiles = true;
        root.browsing = "";
        forChoice.choice = root.sheetData.forWhom === "private" && root.privateHosts.length > 0 ? 1 : 0;
        const last = root.sheetData.lastTrackers ?? [];
        const top = (root.sheetData.publicTrackers ?? []).map(row => row.url);
        root.trackers = last.length > 0 ? last.slice() : top;
        const lastHost = root.sheetData.host ?? "";
        const known = root.privateHosts.some(row => row.host === lastHost);
        root.host = known ? lastHost : root.privateHosts.length > 0 ? root.privateHosts[0].host : "";
        root.source = "";
        root.sourceTyped = false;
        root.sourceEditing = false;
        trackerField.text = "";
        pasteField.text = "";
        sourceField.text = "";
        webSeedsField.text = "";
        commentField.text = "";
        categoryChips.current = "";
        if (root.host.length > 0)
            App.loadCreateSource(root.host);
    }

    /// What App.startCreate takes.
    function request(): var {
        return {
            "folder": root.folder,
            "forWhom": root.isPrivate ? "private" : "anyone",
            "trackers": root.isPrivate ? [] : root.trackers,
            "host": root.isPrivate && !root.pasting ? root.host : root.fileHost,
            "announce": root.isPrivate && root.pasting ? pasteField.text.trim() : "",
            "format": root.format,
            "pieceSize": root.pieceSize,
            "source": root.isPrivate ? root.source : "",
            "sourceTyped": root.isPrivate && root.sourceTyped,
            "comment": commentField.text.trim(),
            "webSeeds": webSeedsField.text.split(/[\s|]+/).filter(url => url.length > 0),
            "ignoreDotfiles": root.ignoreDotfiles,
            "category": root.isPrivate ? "" : categoryChips.current,
            "totalBytes": root.totalBytes,
            "files": root.fileCount
        };
    }

    function create(): void {
        if (!root.canCreate)
            return;
        if (root.serverDown) {
            root.held = true;
            return;
        }
        if (App.startCreate(root.request()))
            root.close();
    }

    /// What is typed in the trackers' field joins the list (one tier each).
    function _addTracker(): void {
        const urls = trackerField.text.split(/[\s|]+/).filter(url => url.length > 0);
        const next = root.trackers.slice();
        for (const url of urls) {
            if (next.indexOf(url) < 0)
                next.push(url);
        }
        root.trackers = next;
        trackerField.text = "";
    }

    function _removeTracker(url: string): void {
        root.trackers = root.trackers.filter(item => item !== url);
    }

    /// `browse`, ctrl+o: the portal's picker where this machine sees the server's folders, else
    /// the server's own list under the field.
    function browse(): void {
        if (root.sheetData.seesDisks === true) {
            root.listOpen = "";
            const picked = root.pickDirectory(App.browseStartFor(root.folder));
            if (picked && picked.length > 0)
                root._setFolder(App.serverPathForPick(picked).path);
            return;
        }
        root._browseTo(root.folder.length > 0 ? root.folder : "/");
    }

    function _browseTo(path: string): void {
        root.browsing = path;
        placeList.rows = [];
        placeList.current = -1;
        root.listOpen = "browse";
        App.listServerFolder(path);
    }

    function _browseUp(): void {
        const at = root.browsing.lastIndexOf("/");
        const up = at > 0 ? root.browsing.substring(0, at) : "/";
        root._setFolder(up);
        root._browseTo(up);
    }

    function _openPlaces(): void {
        const all = root._placeRows();
        placeList.rows = all.slice(0, 12);
        placeList.current = -1;
        root.listOpen = all.length > 0 ? "places" : "";
    }

    /// tab or enter on a place: it goes in the field. On a server's folder: into it.
    function _fillPlace(): void {
        const row = placeList.currentRow;
        const browsing = root.listOpen === "browse";
        root.listOpen = "";
        if (row === null)
            return;
        if (browsing) {
            root._setFolder(row.path);
            root._browseTo(row.path);
            return;
        }
        root._setFolder(row.text);
    }

    function _formatRows(): var {
        return [{
            "text": qsTr("v1"),
            "detail": qsTr("every client · hashed once · the default"),
            "value": "v1"
        }, {
            "text": qsTr("hybrid · v1 + v2"),
            "detail": qsTr("v1 and v2 clients · hashed twice · pad files some old clients show"),
            "value": "hybrid"
        }, {
            "text": qsTr("v2"),
            "detail": qsTr("v2 clients only · a hash per file"),
            "value": "v2"
        }];
    }

    function _pieceRows(): var {
        const auto = root.autoRow;
        const rows = [{
            "text": qsTr("automatic · qBittorrent's choice"),
            "detail": auto.text !== undefined
                      ? qsTr("%1 · %2 · .torrent about %3").arg(auto.text).arg(auto.countText).arg(auto.torrentText)
                      : qsTr("chosen when it ends"),
            "value": 0
        }];
        const files = Math.max(1, root.fileCount);
        for (const row of App.createPieceRows(Math.max(root.totalBytes, 0), files)) {
            rows.push({
                "text": row.text,
                "detail": root.totalBytes > 0 ? qsTr("%1 · %2").arg(row.countText).arg(row.torrentText) : "",
                "value": row.size
            });
        }
        return rows;
    }

    function _hostRows(): var {
        const rows = root.privateHosts.map(row => ({
            "section": qsTr("private trackers in your library"),
            "text": row.host,
            "detail": row.count === 1 ? qsTr("1 torrent") : qsTr("%1 torrents").arg(row.count),
            "value": row.host
        }));
        rows.push({
            "section": qsTr("not in your library"),
            "text": qsTr("paste an announce URL…"),
            "detail": qsTr("from the tracker's upload page"),
            "value": ""
        });
        return rows;
    }

    function _openList(which: string): void {
        if (root.listOpen === which) {
            root.listOpen = "";
            return;
        }
        const rows = which === "format" ? root._formatRows()
                                        : which === "pieces" ? root._pieceRows() : root._hostRows();
        const now = which === "format" ? root.format : which === "pieces" ? root.pieceSize
                                                                           : root.pasting ? "" : root.host;
        optionList.rows = rows;
        optionList.current = Math.max(0, rows.findIndex(row => row.value === now));
        root.listOpen = which;
        // The list takes the keys (keyHandler): no field keeps them meanwhile.
        root.panel.forceActiveFocus();
    }

    function _pickOption(): void {
        const row = optionList.currentRow;
        const which = root.listOpen;
        root.listOpen = "";
        if (row === null)
            return;
        if (which === "format") {
            root.format = row.value;
        } else if (which === "pieces") {
            root.pieceSize = row.value;
        } else if (row.value === "") {
            root.pasting = true;
            root.source = "";
            root.sourceTyped = false;
            pasteField.focusInput();
        } else {
            root.pasting = false;
            root.host = row.value;
            root.source = "";
            root.sourceTyped = false;
            App.loadCreateSource(row.value);
        }
    }

    function _takeSource(): void {
        root.source = sourceField.text.trim();
        root.sourceTyped = true;
        root.sourceEditing = false;
    }

    accessibleName: qsTr("Create a torrent")
    // The tracker, format and pieces lists take ↑↓ (j k), enter picks, esc closes them first.
    keyHandler: event => {
        if (root.listOpen !== "hosts" && root.listOpen !== "format" && root.listOpen !== "pieces")
            return;
        if (event.key === Qt.Key_Down || event.key === Qt.Key_J) {
            optionList.move(1);
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_K) {
            optionList.move(-1);
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root._pickOption();
        } else if (event.key === Qt.Key_Escape) {
            root.listOpen = "";
        } else {
            return;
        }
        event.accepted = true;
    }
    actions: [{
        "text": qsTr("cancel"),
        "cancel": true
    }, {
        "text": root.held ? qsTr("waiting for %1…").arg(App.serverName) : root.primaryText,
        "primary": true,
        "disabled": !root.canCreate || root.held
    }]
    glyph: Glyphs.create
    guarded: true
    note: qsTr("hashing runs on %1; you can close the window").arg(App.serverName)
    objectName: "createSheet"
    sheetWidth: Theme.createSheetWidth
    subtitle: root.multiServer ? qsTr("on %1").arg(App.serverName) : ""
    title: qsTr("Create a torrent")

    onAccepted: root.create()
    onServerDownChanged: {
        if (!root.serverDown && root.held && root.opened) {
            root.held = false;
            root.create();
        }
    }

    // The field changes: the folder is read again, a moment after the typing stops.
    Timer {
        id: recheck

        interval: 300

        onTriggered: {
            root.check = ({});
            if (root.folder.length > 0)
                App.checkCreateFolder(root.folder, root.ignoreDotfiles);
        }
    }

    Connections {
        function onCreateFolderChecked(path: string, check: var): void {
            if (path.trim() !== root.folder)
                return;
            const next = Object.assign({}, check);
            next.path = root.folder;
            root.check = next;
        }

        function onCreateSourceLoaded(host: string, source: string): void {
            if (host === root.host && !root.sourceTyped)
                root.source = source;
        }

        function onServerFolderListed(path: string, entries: var, found: bool): void {
            if (path !== root.browsing || root.listOpen !== "browse")
                return;
            // A file (qBittorrent lists no folder by its name): the pick stands, as one file.
            if (!found) {
                root.listOpen = "";
                return;
            }
            const section = qsTr("%1 on %2").arg(path).arg(App.serverName);
            placeList.rows = entries.map(entry => ({
                "section": section,
                "text": entry.substring(entry.lastIndexOf("/") + 1),
                "detail": "",
                "path": entry
            }));
            placeList.current = entries.length > 0 ? 0 : -1;
        }

        target: App
    }

    // ---- from ----------------------------------------------------------------------------
    FormRow {
        Layout.fillWidth: true
        label: qsTr("from")

        Field {
            id: folderField

            readonly property bool listing: root.listOpen === "places" || root.listOpen === "browse"

            Layout.fillWidth: true
            accessibleName: qsTr("folder or file to share")
            escClears: false
            objectName: "createFolderField"
            opener: qsTr("places")
            openerExpanded: root.listOpen === "places"
            placeholder: qsTr("a folder, or one file, on %1").arg(App.serverName)
            shortcuts: folderField.listing ? [{
                "key": Qt.Key_Down,
                "modifiers": 0
            }, {
                "key": Qt.Key_Up,
                "modifiers": 0
            }, {
                "key": Qt.Key_Tab,
                "modifiers": 0
            }, {
                "key": Qt.Key_Escape,
                "modifiers": 0
            }] : [{
                "key": Qt.Key_Down,
                "modifiers": 0
            }]
            trailing: qsTr("browse")
            trailingClickable: true
            trailingKey: "ctrl+o"
            // C24 H10: keys on demand; the `?` sheet and a screen reader have it.
            trailingKeyShown: false

            Keys.onPressed: event => {
                const ctrl = (event.modifiers & Qt.ControlModifier) !== 0;
                if (ctrl && event.key === Qt.Key_O) {
                    root.browse();
                    event.accepted = true;
                } else if (ctrl && event.key === Qt.Key_Backspace && root.listOpen === "browse") {
                    root._browseUp();
                    event.accepted = true;
                }
            }
            onAccepted: {
                if (folderField.listing && placeList.currentRow !== null) {
                    root._fillPlace();
                    return;
                }
                root.listOpen = "";
                root.triggerPrimary();
            }
            onOpenerClicked: {
                folderField.focusInput();
                if (root.listOpen === "places")
                    root.listOpen = "";
                else
                    root._openPlaces();
            }
            onShortcutPressed: key => {
                if (key === Qt.Key_Down) {
                    if (folderField.listing)
                        placeList.move(1);
                    else
                        root._openPlaces();
                } else if (key === Qt.Key_Up) {
                    placeList.move(-1);
                } else if (key === Qt.Key_Tab) {
                    root._fillPlace();
                } else if (key === Qt.Key_Escape) {
                    root.listOpen = "";
                }
            }
            onTextChanged: {
                recheck.restart();
                if (!root._filling && folderField.editing && root.listOpen === "places")
                    root._openPlaces();
            }
            onTrailingClicked: root.browse()
        }

        PickList {
            id: placeList

            Layout.fillWidth: true
            more: root.listOpen === "browse" ? qsTr("names only: qBittorrent lists what is there, not sizes") : ""
            objectName: "createPlaces"
            visible: folderField.listing

            onPicked: root._fillPlace()
        }

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            color: root.check.blocking === true ? Theme.red : Theme.dimForeground
            objectName: "createFolderLine"
            size: "caption"
            text: root.folder.length === 0 ? ""
                                           : root.checked ? root.check.line ?? "" : qsTr("reading %1…").arg(root.folder)
            visible: text.length > 0
            wrapMode: Text.Wrap
        }

        Label {
            Accessible.name: text
            Accessible.role: Accessible.Button
            Accessible.onPressAction: {
                root.ignoreDotfiles = false;
                recheck.restart();
            }
            color: Theme.accent
            objectName: "createIncludeHidden"
            size: "caption"
            text: qsTr("include them")
            visible: root.checked && root.check.state === "hidden"

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor

                onClicked: {
                    root.ignoreDotfiles = false;
                    recheck.restart();
                }
            }
        }
    }

    // ---- for (CR2) -------------------------------------------------------------------------
    FormRow {
        Layout.fillWidth: true
        label: qsTr("for")

        SharedChoice {
            id: forChoice

            objectName: "createFor"
            rows: [{
                "title": qsTr("Anyone"),
                "detail": qsTr("public trackers, DHT and peer exchange")
            }, {
                "title": qsTr("A private tracker"),
                "detail": qsTr("its members only, through your passkey")
            }]

            onChoiceChanged: root.listOpen = ""
        }
    }

    // ---- anyone: trackers (CR4) ------------------------------------------------------------
    FormRow {
        Layout.fillWidth: true
        label: qsTr("trackers")
        visible: !root.isPrivate

        Flow {
            Layout.fillWidth: true
            objectName: "createTrackers"
            spacing: Theme.createChipsGap
            visible: root.trackers.length > 0

            Repeater {
                model: root.trackers

                Chip {
                    required property string modelData

                    Accessible.description: qsTr("remove")
                    selected: true
                    suffix: Glyphs.close
                    text: modelData
                    tooltip: qsTr("remove %1").arg(modelData)

                    onClicked: root._removeTracker(modelData)
                }
            }
        }

        Field {
            id: trackerField

            Layout.fillWidth: true
            accessibleName: qsTr("add a tracker")
            objectName: "createTrackerField"
            placeholder: qsTr("+ tracker: an announce URL")

            onAccepted: root._addTracker()
        }

        Label {
            Layout.fillWidth: true
            dim: true
            objectName: "createTrackersLine"
            size: "caption"
            text: {
                const top = root.sheetData.publicTrackers ?? [];
                const last = root.sheetData.lastTrackers ?? [];
                let lead = qsTr("no public tracker in your library");
                if (last.length > 0)
                    lead = qsTr("last time's trackers");
                else if (top.length > 0)
                    lead = top.length === 1
                           ? qsTr("the public tracker most used in your library (%1 torrents)").arg(top[0].count)
                           : qsTr("the %1 public trackers most used in your library (%2 torrents)").arg(top.length)
                             .arg(top.map(row => row.count).join(qsTr(" and ")));
                return qsTr("%1 · DHT and peer exchange find peers too · none is fine").arg(lead);
            }
            wrapMode: Text.Wrap
        }
    }

    // ---- a private tracker (CR5–CR7) ---------------------------------------------------------
    FormRow {
        Layout.fillWidth: true
        label: qsTr("tracker")
        visible: root.isPrivate

        Button {
            Layout.fillWidth: true
            bordered: true
            objectName: "createHostButton"
            text: {
                if (root.pasting)
                    return qsTr("another tracker ▾");
                if (root.hostRow === null)
                    return qsTr("paste an announce URL ▾");
                return root.hostRow.count === 1 ? qsTr("%1 · 1 torrent in your library ▾").arg(root.host)
                                                : qsTr("%1 · %2 torrents in your library ▾").arg(root.host).arg(root.hostRow.count);
            }

            onClicked: root._openList("hosts")
        }
    }

    PickList {
        id: optionList

        Layout.fillWidth: true
        objectName: "createOptions"
        visible: root.listOpen === "hosts" || root.listOpen === "format" || root.listOpen === "pieces"

        onPicked: root._pickOption()
    }

    FormRow {
        Layout.fillWidth: true
        label: qsTr("announce")
        visible: root.isPrivate

        // CR5: the library's own, only as the host and dots: no reveal, no hover, no copy.
        Label {
            Accessible.name: qsTr("announce URL, the passkey hidden")
            Layout.fillWidth: true
            bright: true
            elide: Text.ElideMiddle
            objectName: "createAnnounce"
            text: root.hostRow !== null ? root.hostRow.masked : ""
            visible: !root.pasting
        }

        // A pasted one shows in full only while it is typed (CR5).
        Field {
            id: pasteField

            Layout.fillWidth: true
            accessibleName: qsTr("announce URL")
            echoMode: pasteField.editing ? TextInput.Normal : TextInput.Password
            objectName: "createPasteField"
            placeholder: qsTr("paste the announce URL from the tracker's upload page")
            visible: root.pasting
        }

        Label {
            Layout.fillWidth: true
            dim: true
            objectName: "createAnnounceLine"
            size: "caption"
            text: {
                if (root.pasting) {
                    const typed = pasteField.text.trim();
                    return typed.length > 0 && !pasteField.editing
                           ? qsTr("%1 · for this torrent only, never kept").arg(App.maskAnnounce(typed))
                           : qsTr("used for this torrent only, never kept");
                }
                if (root.hostRow === null)
                    return "";
                const never = qsTr("never shown, never written to a log or a file of omaseed's");
                if (root.hostRow.otherKeys)
                    return qsTr("your passkey from %1, your newest torrent from %2 (added %3); another key for it is in your library · %4")
                           .arg(root.hostRow.newestName).arg(root.host).arg(root.hostRow.newestText).arg(never);
                return qsTr("your passkey, from your newest torrent from %1 (added %2) · %3")
                       .arg(root.host).arg(root.hostRow.newestText).arg(never);
            }
            wrapMode: Text.Wrap
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        objectName: "createFacts"
        spacing: Theme.createFactsGap
        visible: root.isPrivate

        CreateFact {
            Layout.fillWidth: true
            detail: qsTr("no DHT, no peer exchange: peers come only through the tracker")
            kind: "ok"
            title: qsTr("Private flag on")
        }

        CreateFact {
            Layout.fillWidth: true
            detail: root.format === "v1" ? qsTr("many private trackers refuse v2 and hybrid")
                                         : qsTr("check the tracker's rules: many refuse v2 and hybrid")
            kind: root.format === "v1" ? "ok" : "warn"
            link: qsTr("change")
            objectName: "createFormatFact"
            title: qsTr("Format %1").arg(root.format)

            onLinkClicked: root._openList("format")
        }

        CreateFact {
            Layout.fillWidth: true
            detail: {
                if (root.pasting)
                    return qsTr("a tracker with no torrents in your library: its rules say whether it wants one");
                if (root.source.length === 0)
                    return qsTr("none of your torrents from %1 has one").arg(root.host);
                return root.sourceTyped ? qsTr("typed for %1").arg(root.host)
                                        : qsTr("as in your newest torrent from %1").arg(root.host);
            }
            kind: root.source.length > 0 ? "ok" : root.pasting ? "warn" : "info"
            link: root.source.length > 0 ? qsTr("change") : qsTr("type one")
            objectName: "createSourceFact"
            title: root.source.length > 0 ? qsTr("Source %1").arg(root.source)
                                          : root.pasting ? qsTr("Source tag: none yet") : qsTr("No source tag")
            visible: !root.sourceEditing

            onLinkClicked: {
                root.sourceEditing = true;
                sourceField.text = root.source;
                sourceField.focusInput();
            }
        }

        Field {
            id: sourceField

            Layout.fillWidth: true
            accessibleName: qsTr("source tag")
            objectName: "createSourceField"
            placeholder: qsTr("the source tag the tracker asks for")
            visible: root.sourceEditing

            onAccepted: root._takeSource()
            onEditingChanged: {
                if (!sourceField.editing && root.sourceEditing)
                    root._takeSource();
            }
        }
    }

    // ---- format and pieces (CR10, CR11) ----------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.createPairGap

        FormRow {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            label: qsTr("format")
            visible: !root.isPrivate

            Button {
                Layout.fillWidth: true
                bordered: true
                objectName: "createFormatButton"
                text: root.format === "v1" ? qsTr("v1 · every client ▾")
                                           : root.format === "hybrid" ? qsTr("hybrid · v1 + v2 ▾") : qsTr("v2 · v2 clients only ▾")

                onClicked: root._openList("format")
            }
        }

        FormRow {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            label: qsTr("pieces")

            Button {
                Layout.fillWidth: true
                bordered: true
                objectName: "createPiecesButton"
                text: {
                    if (root.pieceSize > 0)
                        return qsTr("%1 ▾").arg(App.pieceSizeText(root.pieceSize));
                    if (root.autoRow.text !== undefined)
                        return qsTr("automatic · %1, about %2 ▾").arg(root.autoRow.text).arg(root.autoRow.countText);
                    return qsTr("automatic ▾");
                }

                onClicked: root._openList("pieces")
            }
        }
    }

    // ---- more --------------------------------------------------------------------------------
    FormRow {
        Layout.fillWidth: true
        label: qsTr("more")

        Label {
            Accessible.name: text
            Accessible.role: Accessible.Button
            Accessible.onPressAction: root.moreOpen = !root.moreOpen
            color: Theme.accent
            objectName: "createMore"
            size: "small"
            text: root.moreOpen ? qsTr("web seeds · comment · hidden files ▾")
                                : qsTr("web seeds · comment · hidden files left out ▸")

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor

                onClicked: root.moreOpen = !root.moreOpen
            }
        }

        Field {
            id: webSeedsField

            Layout.fillWidth: true
            accessibleName: qsTr("web seeds")
            objectName: "createWebSeeds"
            placeholder: qsTr("web seeds: URLs where the same files can be downloaded")
            visible: root.moreOpen
        }

        Field {
            id: commentField

            Layout.fillWidth: true
            accessibleName: qsTr("comment")
            objectName: "createComment"
            placeholder: qsTr("comment")
            visible: root.moreOpen
        }

        Toggle {
            checked: root.ignoreDotfiles
            objectName: "createHidden"
            text: !root.checked || !(root.check.hidden > 0) ? qsTr("leave hidden files out")
                  : root.check.hidden === 1 ? qsTr("leave hidden files out · 1 file")
                                            : qsTr("leave hidden files out · %1 files").arg(root.check.hidden)
            visible: root.moreOpen

            onToggled: {
                root.ignoreDotfiles = !root.ignoreDotfiles;
                recheck.restart();
            }
        }
    }

    // ---- when done ---------------------------------------------------------------------------
    FormRow {
        Layout.fillWidth: true
        label: qsTr("when done")
        visible: !root.isPrivate

        ChipSelect {
            id: categoryChips

            Layout.fillWidth: true
            objectName: "createCategory"
            options: App.categories.count > 0 ? App.categories.names() : []
        }

        Label {
            Layout.fillWidth: true
            dim: true
            objectName: "createDoneLine"
            size: "caption"
            text: {
                const trimmed = root.folder.replace(/\/+$/, "");
                const at = trimmed.lastIndexOf("/");
                const parent = trimmed.length === 0 ? qsTr("its parent folder") : at > 0 ? trimmed.substring(0, at) : "/";
                return qsTr("seeds from where it is: added to %1 in seed mode at %2, nothing copied or moved · the magnet goes to the clipboard")
                       .arg(App.serverName).arg(parent);
            }
            wrapMode: Text.Wrap
        }
    }

    FormRow {
        Layout.fillWidth: true
        label: qsTr("save .torrent")
        visible: root.isPrivate

        Label {
            Layout.fillWidth: true
            bright: true
            elide: Text.ElideMiddle
            objectName: "createSavedName"
            text: qsTr("%1/%2 [%3].torrent").arg(root.sheetData.downloads ?? "")
                  .arg(root.folder.length > 0 ? root._name() : qsTr("NAME")).arg(root.fileHost)
        }
    }

    // CR7: why it isn't added, and what happens when the site's copy comes back.
    Rectangle {
        id: roundTripNote

        /// The note's words on its own 4 % fill (DECISIONS C30 F1).
        readonly property Ink ink: Ink {
            animated: false
            ground: Theme.on.idle
        }

        Layout.fillWidth: true
        border.color: Theme.fill(Theme.hairlineAlpha)
        border.width: Theme.createNoteEdge
        color: roundTripNote.ink.ground.fill
        implicitHeight: noteRow.implicitHeight + 2 * Theme.createNotePadding
        objectName: "createRoundTripNote"
        radius: Theme.createNoteRadius
        visible: root.isPrivate && root.listOpen === ""

        RowLayout {
            id: noteRow

            anchors.left: parent.left
            anchors.leftMargin: Theme.createNotePadding
            anchors.right: parent.right
            anchors.rightMargin: Theme.createNotePadding
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.createNoteGap

            Label {
                Layout.alignment: Qt.AlignTop
                color: roundTripNote.ink.accent
                text: Glyphs.roundTrip
            }

            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                ink: roundTripNote.ink
                size: "small"
                text: qsTr("Not added here. Upload the .torrent on %1's site, then add the site's copy: omaseed knows these files and points it here, so it seeds without downloading.")
                      .arg(root.pasting ? qsTr("the tracker") : root.host)
                wrapMode: Text.Wrap
            }
        }
    }

    // Board 49 §7: a server that doesn't answer keeps what was chosen.
    Label {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        color: Theme.yellow
        objectName: "createServerDown"
        size: "small"
        text: qsTr("%1 isn't answering. A torrent is made inside %1's qBittorrent, so nothing can start now. What you chose stays here; create when back sends it once %1 answers.")
              .arg(App.serverName)
        visible: root.serverDown
        wrapMode: Text.Wrap
    }
}
