pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The add sheet (spec v2 §3 Fig. 5, `.sheet` at 600): everything that can start a torrent
// arrives here — `a` and the hero's `+`, the clipboard, a drop on the window, a `magnet:` or
// `.torrent` handed over by the browser, and `ctrl+o`'s portal file picker. What the user
// gave is parsed locally first (`App.parseAddable`), so the head already confirms the name,
// the size and where it came from instead of asking; the rows below only adjust the add.
// `ctrl+o` or the field's `browse` button opens the chooser.
//
// While the field is empty, "In Downloads" under it lists the newest `.torrent` files in the
// user's Downloads (manual box M7, `App.downloadsTorrents`, scanned on every opening): a row
// fills the field as the chooser's answer would. One already in the library says so and
// still goes through, to Fig. 10's duplicate sheet.
//
// The sheet is `guarded` (manual box M5): a click outside it does nothing, and `esc` goes to
// `cancel` before it closes anything.
//
// A `.torrent` of more than one file also gets the file picker (docs/11 D1): its tree, read
// from the metainfo (`App.addFiles`), with a check mark and a priority per file and folder.
// It is the last row, and it takes the height the window has left, scrolling past that.
// A magnet, or a link to a `.torrent`, has no file list until qBittorrent fetches its
// metadata: the sheet asks for it as soon as one is in the field (docs/29 D1, `App.addLookup`),
// the Files row says how the wait goes (`AddFetchLine`, F1-F8), and the 200 brings the same
// tree, choices and renames as a `.torrent` (D9 a). `add` never waits for it (D4 a). No
// lookups in a batch (D10 a).
//
// The sheet never talks to the server itself: it emits `addRequested(request)` with the map
// `App.addTorrent` takes, and listens for `App.addFinished` to close, to move on to the next
// queued source, or to show the server's refusal and stay open. `App.addFinished` comes with
// qBittorrent's own OK to the add (DECISIONS C20, #124); what follows it (file names, file
// choices, the start) reports in the footer. One add at a time: while it is out, another
// press does nothing, a hand-off waits in the queue, and after a moment the button says
// "adding…". An infohash the library
// already holds never leaves as an add at all — `duplicateFound(row)` is emitted instead and
// the host opens Fig. 10's duplicate sheet.
//
// Several sources at once (#40, docs/27 B1–B3) make it a batch: browse with several files, a
// paste of several links, a drop of several, ticks in In Downloads, or a hand-off while a
// source the user has not touched is in the field. The field turns into a list of torrents
// over a field for more; Name and Files drop out, since they belong to one torrent; the
// options apply to all of them, and `add N` sends one request (`items`). A torrent already in
// the library, or listed twice, stays in the list dim and is left out. `one by one` turns the
// batch back into the queue, for per-torrent names and files.
//
// Ticks in In Downloads are what `add` adds while the field is empty: the button reads
// `add N`, and a click takes them, waits for their `.torrent`s to be read, and adds them.
//
// docs/33 #72 (D13–D15, D20; DECISIONS C12): with two servers or more the head says where the
// torrent goes, "Add torrent to NAME", the name in a chip (its state's dot, ▾); the chip or
// ctrl+s opens the server palette, and a pick switches the app (one connection, C8) with the
// sheet still open: what was typed stays (category and tags by name), what came from the old
// server is read again after the new one's first sync (the default save path, free space, the
// library check), and one line says what moved. With one server the head is "Add torrent" and
// nothing names the server (D20). A server that is down when the sheet opens, or goes while it
// is up, gets docs/35's band under the head (yellow, N7) and `add when back` holds the
// torrent until it answers or the app switches; an add the server refused or never took gets
// the same band in red, with its words from core/ErrorWords (N3).
//
//     AddSheet { anchors.fill: parent; onAddRequested: request => App.addTorrent(request) }
Sheet {
    id: sheet

    /// `App.parseAddable`'s map for what is in the field, or null while it holds nothing
    /// recognisable. `valid`, `name`, `sizeText`, `infoHashV1/V2`, `kind`, `value`, `token`.
    property var addable: null
    /// Where the current text came from: "clipboard", "magnet", "file", "drop", "handoff" (a
    /// browser or a file manager opened it with omaseed), or "" when it was typed. Only the
    /// head's subtitle reads it.
    property string source: ""
    /// The sources waiting behind this one (`one by one`, or hand-offs while a form the user
    /// has filled is open).
    property var queue: []
    /// #40: the batch, one `{text, map}` per source (`map` is `App.parseAddable`'s, replaced
    /// once a `.torrent` has been read). Two or more make the sheet a batch.
    property var batch: []
    /// docs/27 D10: each batch `.torrent`'s files-on-disk matches, by parse token.
    property var _batchMatches: ({})
    /// Test seam: the chooser for several `.torrent` files (App.pickTorrentFiles).
    property var pickTorrents: () => App.pickTorrentFiles()
    /// Test seam: the clipboard's text, as `Xdg.detectClipboardMagnet()` would read it. Non-empty
    /// wins over the real clipboard, which an offscreen test run has no control over; it goes
    /// through the same probe, so a bare info hash here is not offered either (#147).
    property string clipboardAddable: ""
    /// What the sheet could not do, drawn in the band under the head (docs/35 N7); "" hides it.
    property string errorText: ""
    /// D15 (docs/33 N3): the add the server refused or never took, App.addFailure's words.
    property var failure: ({})
    /// D14 (A2): what a switch from the sheet moved, one line; "" hides it.
    property string movedText: ""
    /// #147: a bare info hash is to be added (the field's, or a batch row's) and the server
    /// has DHT off, which is all such a torrent has to find peers with.
    readonly property bool bareHashWithoutDht: App.serverPrefs.values.dht === false
                                               && (sheet.batchMode ? sheet.batch.some(entry => entry.map.bareHash === true)
                                                                   : sheet.addable !== null && sheet.addable.bareHash === true)
    /// D15: `add when back` holds `{stopped}` until the server answers.
    property var _held: null
    /// The server the sheet's figures came from, to tell a switch from another server change.
    property string _serverId: ""
    /// A switch whose first sync has not come yet: the moved line waits for it.
    property bool _switchPending: false
    /// A test seam: the QML tests run on the demo library, whose server never goes.
    property bool _testServerDown: false
    /// docs/33 D13: there is another server to send it to.
    readonly property bool multiServer: App.serverCount > 1
    /// D15: the app's server is down (or refused the sign-in), not merely syncing.
    readonly property bool serverDown: sheet._testServerDown
                                       || (App.hasServer && !App.demoMode && App.serverLost
                                           && !App.globalState.connected)
    /// Why the field's text is not addable, as `App.parseAddable` put it ("" when it gave no
    /// reason); shown when `enter` is pressed on it (review RUN-8).
    property string _parseError: ""
    /// False once the save path has been typed into: the add then carries `autoTMM: false`.
    property bool autoTMM: true
    /// Names the category/tag palette answered with that the server does not list (yet):
    /// they join the chip rows so the choice is visible in the sheet.
    property var extraCategories: []
    property var extraTags: []

    /// The form, exposed for the host and the tests.
    readonly property alias sourceField: sourceField
    readonly property alias savePathField: savePathField
    readonly property alias upLimitField: upLimitField
    readonly property alias dlLimitField: dlLimitField
    readonly property alias renameField: renameField
    readonly property alias firstLastToggle: firstLastToggle
    readonly property alias filesTree: filesTree
    readonly property alias downloadsList: downloadsList
    readonly property alias batchList: batchList
    readonly property alias categoryChips: categoryChips
    readonly property alias tagChips: tagChips
    readonly property alias layoutChips: layoutChips
    readonly property alias sequentialToggle: sequentialToggle
    readonly property alias skipCheckToggle: skipCheckToggle
    readonly property alias freeLine: freeLine
    readonly property alias matchLine: matchLine
    readonly property alias fetchLine: fetchLine

    /// The names in `App.categories` / `App.tags`, as plain string lists for the chip rows.
    /// Both models notify on `count` alone, so it is read first to make this re-evaluate
    /// when the server's category or tag list changes.
    readonly property var categoryNames: {
        const known = App.categories.count;
        const base = known > 0 ? App.categories.names() : [];
        return base.concat(sheet.extraCategories.filter(name => base.indexOf(name) < 0));
    }
    readonly property var tagNames: {
        const known = App.tags.count;
        const base = known > 0 ? App.tags.tags() : [];
        return base.concat(sheet.extraTags.filter(name => base.indexOf(name) < 0));
    }

    /// A parsed `.torrent` of more than one file is on show: the picker is drawn and its
    /// choices travel with the add.
    readonly property bool batchMode: sheet.batch.length > 1
    /// The batch as its list draws it: `text`, `map`, `name`, `sizeText`, `flag`, `warn`,
    /// `out`, and `state` ("ok", "reading", "bad", "library", "twice"). Only "ok" is added.
    readonly property var batchRows: {
        const library = App.torrents.count; // a new library row can turn one into "library"
        const seen = [];
        return sheet.batch.map(entry => {
            const map = entry.map;
            const row = {
                "text": entry.text,
                "map": map,
                "name": map.name && map.name.length > 0 ? map.name : entry.text,
                "sizeText": map.sizeText || "",
                "flag": "",
                "warn": false,
                "out": false,
                "state": "ok",
                "library": library
            };
            if (map.pending === true) {
                row.flag = qsTr("reading…");
                row.state = "reading";
                return row;
            }
            if (map.valid !== true) {
                row.flag = qsTr("not a torrent · left out");
                row.out = true;
                row.state = "bad";
                return row;
            }
            const key = map.infoHashV1 || map.infoHashV2 || map.value;
            if (sheet._duplicateOf(map)) {
                row.flag = qsTr("in the library · left out");
                row.out = true;
                row.state = "library";
            } else if (seen.indexOf(key) >= 0) {
                row.flag = qsTr("twice in this list · left out");
                row.out = true;
                row.state = "twice";
            } else if ((sheet._batchMatches[map.token] || []).length > 0) {
                row.flag = qsTr("files on disk");
                row.warn = true;
            } else if (map.kind === "magnet" && !(map.totalSize > 0)) {
                row.flag = qsTr("magnet · size after metadata");
                row.sizeText = "—";
            } else if (map.kind === "url") {
                row.flag = qsTr("link");
                row.sizeText = "—";
            } else if (map.fileCount > 1) {
                row.flag = qsTr("%1 files").arg(map.fileCount);
            }
            seen.push(key);
            return row;
        });
    }
    readonly property var _batchOk: sheet.batchRows.filter(row => row.state === "ok")
    /// How many the batch adds.
    readonly property int batchCount: sheet._batchOk.length
    /// What the batch's known sizes add up to, and how many have none yet.
    readonly property real _batchBytes: sheet._batchOk.reduce(
                                            (sum, row) => sum + (row.map.totalSize > 0 ? row.map.totalSize : 0), 0)
    readonly property int _batchUnsized: sheet._batchOk.filter(row => !(row.map.totalSize > 0)).length
    /// docs/27 D10: the first `.torrent` of the batch whose files are already on disk.
    readonly property var batchMatch: {
        for (const row of sheet._batchOk) {
            const matches = sheet._batchMatches[row.map.token] || [];
            if (matches.length > 0)
                return {
                    "name": row.name,
                    "savePath": matches[0].savePath
                };
        }
        return null;
    }

    readonly property bool showFiles: sheet.addable !== null && sheet.addable.valid === true
                                      && (sheet.addable.kind === "file" || (sheet.addable.token || "").length > 0)
                                      && App.addFiles.fileCount > 1
    /// docs/29: a magnet or a link whose file list is not in yet, which the lookup is for.
    readonly property bool _lookable: sheet.addable !== null && sheet.addable.valid === true
                                      && (sheet.addable.kind === "magnet" || sheet.addable.kind === "url")
                                      && (sheet.addable.token || "").length === 0
    /// The Files row holds the lookup's line instead of a tree (F1, F3-F5, F7, F8).
    readonly property bool _showFetch: !sheet.batchMode && sheet._lookable && !sheet.showFiles && fetchLine.shown
    /// D5 a: "add stopped" before the list stops it once the list arrives, while the server
    /// can fetch one (not for a private tracker's magnet, nor below Web API 2.11.9).
    readonly property bool _untilMetadata: sheet._lookable
                                           && ["waiting", "quiet", "fetching", "slow", "never"].indexOf(App.addLookup.phase) >= 0
    /// Bytes the add writes (docs/25 §4): the ticked files of a picked `.torrent`, the whole
    /// size of one file or of a magnet that says (`xl=`), and -1 while nobody knows.
    readonly property real neededBytes: {
        if (sheet.batchMode)
            return sheet._batchBytes > 0 ? sheet._batchBytes : -1;
        if (!sheet.addable || sheet.addable.valid !== true)
            return -1;
        if (sheet.showFiles)
            return App.addFiles.wantedSize;
        return sheet.addable.totalSize !== undefined ? sheet.addable.totalSize : -1;
    }
    /// The "in Downloads" list is on show: nothing in the field yet, and the folder has a
    /// `.torrent` to offer (manual box M7).
    readonly property bool showDownloads: sourceField.text.trim().length === 0 && !sheet.batchMode
                                          && App.downloadsTorrents.length > 0

    /// What the tree may take: the body's room less every other row, so the sheet fits the
    /// window and only the tree scrolls. Siblings' heights never depend on the tree's, so
    /// this binding cannot loop.
    readonly property real _treeRoom: {
        let used = filesHead.height + filesSection.spacing
                + (nothingTickedLine.visible ? nothingTickedLine.height + filesSection.spacing : 0);
        const rows = sheet.body.children;
        for (let i = 0; i < rows.length; ++i) {
            const row = rows[i];
            if (row !== filesSection && row.visible)
                used += row.height + sheet.body.spacing;
        }
        return sheet.bodyRoom - used;
    }

    /// The hint line: the tree's own keys while it has the keyboard (Keymap "addFiles").
    readonly property var _formHints: sheet.filesMatches.length > 0 && !sheet.savedThere
                                      ? sheet._baseFormHints.concat([{
                                                                         "key": "alt+s",
                                                                         "label": qsTr("save there")
                                                                     }])
                                      : sheet._baseFormHints
    /// The keys every field of the form hands to the sheet instead of typing them.
    readonly property var _fieldShortcuts: sheet.filesMatches.length > 0 && !sheet.savedThere
                                           ? [{
                                                  "key": Qt.Key_S,
                                                  "modifiers": Qt.AltModifier
                                              }] : []
    readonly property var _baseFormHints: [
        {
            "key": "tab",
            "label": qsTr("fields")
        },
        {
            "key": "enter",
            "label": sheet._nothingTicked ? sheet._addStoppedText : sheet._addText
        }
    ].concat(sheet.multiServer ? [
                                     {
                                         "key": "ctrl+s",
                                         "label": qsTr("server")
                                     }] : [], [
                 {
                     "key": "shift+enter",
                     "label": qsTr("add stopped")
                 }])
    /// … and the "in Downloads" list's (Keymap "addDownloads").
    readonly property var _downloadsHints: [
        {
            "key": "j k",
            "label": Keymap.labelOf("addDownloads", "j")
        },
        {
            "key": "space",
            "label": Keymap.labelOf("addDownloads", "space")
        },
        {
            "key": "enter",
            "label": Keymap.labelOf("addDownloads", "enter")
        },
        {
            "key": "tab",
            "label": qsTr("fields")
        }
    ]
    /// … and the batch list's (Keymap "addBatch").
    readonly property var _batchHints: [
        {
            "key": "j k",
            "label": Keymap.labelOf("addBatch", "j")
        },
        {
            "key": "d",
            "label": Keymap.labelOf("addBatch", "d")
        },
        {
            "key": "tab",
            "label": qsTr("fields")
        },
        {
            "key": "enter",
            "label": sheet._addText
        },
        {
            "key": "shift+enter",
            "label": qsTr("add stopped")
        }
    ]
    readonly property var _treeHints: [
        {
            "key": "j k",
            "label": Keymap.labelOf("addFiles", "j")
        },
        {
            "key": "space",
            "label": Keymap.labelOf("addFiles", "space")
        },
        {
            "key": "ctrl+a",
            "label": Keymap.labelOf("addFiles", "ctrl+a")
        },
        {
            "key": "+ -",
            "label": Keymap.labelOf("addFiles", "+")
        },
        {
            "key": "h l",
            "label": Keymap.labelOf("addFiles", "h")
        },
        {
            "key": "r",
            "label": Keymap.labelOf("addFiles", "r")
        },
        {
            "key": "enter",
            "label": sheet._nothingTicked ? sheet._addStoppedText : sheet._addText,
            // #8: at the sheet's 600 wide the tree's keys run out of room; these go first.
            "keep": true
        }
    ]
    /// While a file tree row is being renamed (M18): its field takes both keys.
    readonly property var _renameHints: [
        {
            "key": "enter",
            "label": qsTr("rename")
        },
        {
            "key": "esc",
            "label": qsTr("cancel")
        }
    ]

    /// `add` / `add stopped`: the map `App.addTorrent` takes.
    signal addRequested(var request)
    /// This infohash is already on the server: `row` is `App.torrentRow`'s map, `source`
    /// the parse's `trackers` and `isPrivate` (docs/25 §2, the merge). Fig. 10.
    signal duplicateFound(var row, var source)
    /// The `…` chips: the host opens T4.4's palette in its `openForChoice` mode and hands the
    /// answer back through `applyChosenCategory` / `applyChosenTags`.
    signal categoryPaletteRequested
    signal tagPaletteRequested
    /// docs/33 D14: the head's server chip or ctrl+s: the host opens the server palette.
    signal serverPaletteRequested

    /// How many the add hands over: the batch's, or the ticks in In Downloads while it shows,
    /// less those in the library, which the batch they make leaves out.
    readonly property int _addCount: sheet.batchMode ? sheet.batchCount
                                                     : sheet.showDownloads ? App.downloadsTorrents.filter(
                                                                                 t => !t.inLibrary
                                                                                 && downloadsList.ticked.indexOf(t.path) >= 0).length
                                                                           : 0
    /// The ticks in In Downloads on torrents already in the library: the add leaves them out.
    readonly property int _tickedInLibrary: sheet.showDownloads ? App.downloadsTorrents.filter(
                                                                      t => t.inLibrary
                                                                      && downloadsList.ticked.indexOf(t.path) >= 0).length
                                                                : 0
    readonly property string _addText: sheet._addingShown ? qsTr("adding…")
                                                          : sheet.serverDown ? qsTr("add when back")
                                                        : sheet._addCount > 1 ? qsTr("add %1").arg(sheet._addCount)
                                                                              : qsTr("add")
    readonly property string _addStoppedText: sheet._addCount > 1 ? qsTr("add %1 stopped").arg(sheet._addCount)
                                                                  : qsTr("add stopped")

    /// In Downloads' line over its ticks: how many, and what the add takes of them, so it says
    /// what the button counts (#51 D18: "5 ticked" over "add 3").
    function tickedText(ticked: int, inLibrary: int): string {
        if (inLibrary <= 0)
            return qsTr("%1 ticked · add takes them together").arg(ticked);
        if (inLibrary >= ticked)
            return ticked === 1 ? qsTr("1 ticked · already in the library")
                                : qsTr("%1 ticked · all already in the library").arg(ticked);
        return qsTr("%1 ticked · %2 already in the library · add takes %3").arg(ticked).arg(inLibrary).arg(
                    ticked - inLibrary);
    }

    /// #43: every file of the `.torrent` unticked. It is still added, stopped, so its files
    /// can be chosen later in the Files tab; the primary button says so.
    readonly property bool _nothingTicked: sheet.showFiles && App.addFiles.wantedCount === 0

    /// Fig. 5's three layout values, in the order the figure lists them.
    readonly property var _layoutOptions: [qsTr("Original"), qsTr("Subfolder"), qsTr(
            "No subfolder")]
    readonly property var _layoutValues: ["Original", "Subfolder", "NoSubfolder"]

    /// "from clipboard" and its siblings, the third part of the head's subtitle. A bare info
    /// hash (#147) is named as one wherever it came from: that is what tells it from a magnet.
    readonly property string sourceText: {
        if (sheet.addable && sheet.addable.bareHash === true)
            return qsTr("from an info hash");
        switch (sheet.source) {
        case "clipboard":
            return qsTr("from clipboard");
        case "magnet":
            return qsTr("from magnet link");
        case "file":
            return qsTr("from file");
        case "drop":
            return qsTr("from drop");
        case "handoff":
            // A browser and a file manager hand over the same way, so name what arrived.
            switch (sheet.addable ? sheet.addable.kind : "") {
            case "magnet":
                return qsTr("from magnet link");
            case "file":
                return qsTr("from file");
            case "url":
                return qsTr("from link");
            }
            return "";
        }
        return "";
    }

    property bool _programmatic: false
    /// docs/25 §3 (#27): the library torrents holding this `.torrent`'s files
    /// (`App.filesMatchFound`), the copy picked, and whether `save there` was pressed, with
    /// what Save to, its automatic management and the layout were before it (for `undo`).
    property var filesMatches: []
    property int matchIndex: 0
    property bool savedThere: false
    property var _beforeSaveThere: null
    property string _matchToken: ""
    /// The field's length before its last change, and whether that change was a keystroke
    /// (a paste, a drop or a hand-off arrives whole): see `_checkDuplicate`.
    property int _typedLength: 0
    property bool _typing: false
    property string _pendingToken: ""
    property bool _awaitingPick: false
    /// `add` asked for while a `.torrent` was still being read: `stopped`, kept until the
    /// read comes back and `submit` runs again; null when nothing waits.
    property var _submitWhenRead: null
    /// #124: an add is out and `App.addFinished` has not answered it yet. A second press
    /// would send the same torrent again (qBittorrent refuses it, 409, and the refusal would
    /// land on the next queued torrent's form), so `_send` waits for the answer.
    property bool _adding: false
    /// The add has been out long enough to say so: the primary button reads "adding…".
    property bool _addingShown: false
    property bool _savePathEdited: false
    /// The parse whose files the picker holds, so a re-emit of the same map keeps the choices.
    property string _filesToken: ""
    /// #67: where the add would put those files, so a rename in the tree is refused when
    /// something not the torrent's already has that name on disk there (as #64 in the library).
    readonly property var _filesWhere: ({
            "token": sheet._filesToken,
            "savePath": savePathField.text,
            "autoTMM": sheet.autoTMM,
            "category": categoryChips.current,
            "contentLayout": sheet.layoutValue()
        })

    on_FilesWhereChanged: App.setAddFilesWhere(sheet._filesWhere)

    /// What the clipboard offers, or "" — the fake first, so a test can drive it. The fake
    /// goes through the same probe as the real clipboard: never a bare info hash (#147).
    function clipboardText(): string {
        const found = sheet.clipboardAddable.length > 0 ? Xdg.probeClipboardText(sheet.clipboardAddable)
                                                        : Xdg.detectClipboardMagnet();
        return found ? found.value : "";
    }

    /// `a`, the hero's `+`, the tray: an empty sheet, prefilled from the clipboard when it
    /// holds a magnet or a URL (and from `OMASEED_DEMO_ADD` under `--demo`, so Fig. 5 and
    /// Fig. 10 can be screenshot reproducibly).
    function openBlank(): void {
    if (sheet.opened) { // already up (the tray's add while the form is half filled): keep it
        App.scanDownloads(); // but look at Downloads again, as an opening would
        return;
    }
    sheet.reset();
    const demo = App.demoAddText();
    const prefill = demo.length > 0 ? demo : sheet.clipboardText();
    // Only something that really parses goes in: the clipboard usually holds prose, and the
    // sheet must not paste that over the field.
    const parsed = prefill.length > 0 ? App.parseAddable(prefill) : null;
    // docs/25 D1 a turns a duplicate into Fig. 10 at once; a magnet left on the clipboard
    // from an earlier add must not do that to every `a`, so it is simply not offered.
    if (parsed && (parsed.valid || parsed.pending) && (demo.length > 0 || !sheet._duplicateOf(parsed)))
    sheet.loadText(prefill, demo.length > 0 && parsed.kind === "file" ? "file" : "clipboard");
    sheet.open();
    sheet._checkDuplicate(); // only a demo prefill can still be one: Fig. 10 on `--demo`
}

    /// One source, from a known place ("drop", "handoff", …).
    function openWith(text: string, from: string): void {
    sheet.openList([text], from);
}

    /// Several (#40): they open the sheet as a batch. A hand-off that arrives while the sheet
    /// is up never wipes the form the user is in (`_takeMany`).
    function openList(list: var, from: string): void {
    if (!list || list.length === 0)
    return;
    if (sheet.opened) {
        // The sheet may have stayed up behind the widget since before an add: "in library"
        // must not be what it was then.
        App.scanDownloads();
        sheet._takeMany(list, from);
        return;
    }
    sheet.reset();
    if (list.length > 1) {
        sheet.addToBatch(list, from);
        sheet.open();
        return;
    }
    sheet.loadText(list[0], from);
    sheet.open();
    sheet._checkDuplicate();
}

    /// Sources arriving at an open sheet (docs/27 D7 a): an empty field takes one, or starts a
    /// batch with several; a batch takes them all; a source still as it arrived turns into a
    /// batch with them; one the user has chosen files for, renamed or named waits in the queue.
    function _takeMany(list: var, from: string): void {
    // #124: the field's torrent is on its way; what arrives now is the next one, not part of
    // it (a batch made now would close with the add's answer and lose it).
    if (sheet._adding) {
        sheet.queue = sheet.queue.concat(list);
        return;
    }
    if (sheet.batchMode) {
        sheet.addToBatch(list, from);
        return;
    }
    if (sourceField.text.trim().length === 0) {
        if (list.length === 1)
        sheet.loadText(list[0], from);
        else
        sheet.addToBatch(list, from);
        return;
    }
    const touched = renameField.text.trim().length > 0
            || (sheet.showFiles && (App.addFiles.customised || App.addFiles.renameList().length > 0));
    if (touched || sheet.addable === null) {
        sheet.queue = sheet.queue.concat(list);
        return;
    }
    sheet.addToBatch(list, from);
}

    /// #40: `texts` join the batch; what the field held, when it held something, is its first.
    function addToBatch(texts: var, from: string): void {
    if (!texts || texts.length === 0)
    return;
    let entries = sheet.batch;
    if (entries.length === 0 && sheet.addable !== null)
    entries = [{
                   "text": sourceField.text.trim(),
                   "map": sheet.addable
               }];
    for (const text of texts)
    entries = entries.concat([{
                                  "text": text,
                                  "map": App.parseAddable(text)
                              }]);
    if (sheet.source.length === 0)
    sheet.source = from;
    sheet._programmatic = true;
    sourceField.text = "";
    renameField.text = "";
    sheet._programmatic = false;
    sheet._typedLength = 0;
    sheet._pendingToken = "";
    sheet.addable = null;
    sheet.errorText = "";
    if (entries.length === 1) { // one source after all: the ordinary sheet
        sheet.batch = [];
        sheet.loadText(entries[0].text, from);
        return;
    }
    sheet.batch = entries;
    sheet._askBatchMatches();
}

    /// `✕` or `d`: drop a row; with one left, the sheet is the ordinary one again.
    function removeFromBatch(index: int): void {
    const next = sheet.batch.slice();
    next.splice(index, 1);
    if (next.length === 1) {
        sheet.batch = [];
        sheet.loadText(next[0].text, sheet.source);
        sheet._checkDuplicate();
        return;
    }
    sheet.batch = next;
}

    /// docs/27 D9: back to today's queue, one sheet each, for per-torrent names and files.
    function oneByOne(): void {
    const texts = sheet.batchRows.filter(row => row.state !== "twice").map(row => row.text);
    sheet.batch = [];
    if (texts.length === 0)
    return;
    sheet.queue = texts.slice(1).concat(sheet.queue);
    sheet.loadText(texts[0], sheet.source);
    sheet.focusFirst();
    sheet._checkDuplicate();
}

    /// docs/27 D10: ask the library about each read `.torrent` of the batch, once.
    function _askBatchMatches(): void {
    for (const entry of sheet.batch) {
        const map = entry.map;
        if (map.kind !== "file" || map.valid !== true || map.pending === true
                || sheet._batchMatches[map.token] !== undefined)
        continue;
        const asked = Object.assign({}, sheet._batchMatches);
        asked[map.token] = [];
        sheet._batchMatches = asked;
        App.findFilesMatch(map.token);
    }
}

    /// The batch's request: its torrents (`items`) and the options, without a name or files.
    function batchRequest(stopped: bool): var {
    return {
        "items": sheet._batchOk.map(row => row.map),
        "category": sheet.chosenCategory(),
        "tags": sheet.chosenTags(),
        "savePath": savePathField.text,
        "autoTMM": sheet.autoTMM,
        "contentLayout": sheet.layoutValue(),
        "upLimitKb": sheet._limitKb(upLimitField.text),
        "dlLimitKb": sheet._limitKb(dlLimitField.text),
        "firstLastPiecePrio": firstLastToggle.checked,
        "sequentialDownload": sequentialToggle.checked,
        "skipChecking": skipCheckToggle.checked,
        "stopped": stopped
    };
}

    /// A row of "in Downloads" (manual box M7): that `.torrent`, as if picked with `ctrl+o`.
    function useDownload(path: string): void {
        // The same as the file chooser's answer (`onAddableParsed`), and the keyboard goes
        // back to the field, where `enter` adds, since the list hides once the field is filled.
        sheet.loadText(path, "file");
        sheet.focusFirst();
    }

    /// `ctrl+o`: the portal's file chooser, several files at once (#40). One fills the field;
    /// several go where a hand-off would (`_takeMany`).
    function browse(): void {
        const paths = sheet.pickTorrents();
        if (!paths || paths.length === 0)
            return;
        sheet._takeMany(paths, "file");
        sheet.focusFirst();
    }

    /// #41 (docs/27 L4, D13 a): Save to's `browse`, through the server's path map like the
    /// set location sheet's. `pickDirectory` is the picker, which a test replaces;
    /// `_savePick` is `App.serverPathForPick()` for the last pick, `{}` once typed over.
    property var pickDirectory: start => App.pickDirectory(start)
    property var _savePick: ({})
    readonly property alias saveBrowsedLine: saveBrowsedLine

    function browseSavePath(): void {
        const picked = sheet.pickDirectory(App.browseStartFor(savePathField.text.trim()));
        if (!picked || picked.length === 0)
            return;
        const answer = App.serverPathForPick(picked);
        savePathField.text = answer.path;
        sheet._savePick = answer;
    }

    /// Put `text` in the field and parse it, without treating it as typing.
    function loadText(text: string, from: string): void {
    sheet._programmatic = true;
    sourceField.text = text;
    sheet._programmatic = false;
    sheet._typing = false;
    sheet.source = from;
    sheet.parseField();
}

    /// Re-read the field. Called on every keystroke, so the head follows a paste live.
    function parseField(): void {
                               const map = App.parseAddable(sourceField.text);
                               sheet._pendingToken = map.token;
                               sheet.addable = (map.valid || map.pending) ? map : null;
                               sheet._parseError = map.error || "";
                               if (sheet.source.length === 0 && map.kind === "magnet")
                               sheet.source = "magnet";
                               else if (sheet.source.length === 0 && map.kind === "file")
                               sheet.source = "file";
                           }

    /// Everything `App.addTorrent` needs: the parse plus the rows.
    function request(stopped: bool): var {
    const map = {};
    for (const key in sheet.addable)
    map[key] = sheet.addable[key];
    map["category"] = sheet.chosenCategory();
    map["tags"] = sheet.chosenTags();
    map["savePath"] = savePathField.text;
    map["autoTMM"] = sheet.autoTMM;
    map["contentLayout"] = sheet.layoutValue();
    map["upLimitKb"] = sheet._limitKb(upLimitField.text);
    map["dlLimitKb"] = sheet._limitKb(dlLimitField.text);
    map["rename"] = renameField.text.trim();
    map["firstLastPiecePrio"] = firstLastToggle.checked;
    map["filePriorities"] = sheet.showFiles && App.addFiles.customised
            ? App.addFiles.priorityList() : [];
    // M18: the file and folder renames, replayed on the server after the add.
    map["fileRenames"] = sheet.showFiles ? App.addFiles.renameList() : [];
    map["renameAnchor"] = sheet.showFiles ? App.addFiles.renameAnchor() : ({});
    map["sequentialDownload"] = sequentialToggle.checked;
    map["skipChecking"] = skipCheckToggle.checked;
    map["stopped"] = stopped;
    map["untilMetadata"] = stopped && sheet._untilMetadata;
    return map;
}

    /// KB/s for a limit field, read as the Limits sheets read theirs: empty is no limit (0),
    /// "1.5" is 2 (the API takes whole KB here), and anything else is NaN, which `submit`
    /// refuses instead of adding the torrent unlimited.
    function _limitKb(text: string): real {
        const typed = text.trim();
        if (typed.length === 0)
            return 0;
        const kb = Number(typed);
        return Number.isFinite(kb) && kb >= 0 ? Math.round(kb) : NaN;
    }

    /// The chip that is on, or "" for no category.
    function chosenCategory(): string {
        return categoryChips.current;
    }

        /// The chips that are on.
        function chosenTags(): var {
                                   return tagChips.selected.slice();
                               }

        /// The category palette's answer (`chosen(name)`): pick it, and keep it as a chip
        /// when the server's own list does not carry it yet.
        function applyChosenCategory(name: string): void {
        if (name.length > 0 && sheet.categoryNames.indexOf(name) < 0)
        sheet.extraCategories = sheet.extraCategories.concat([name]);
        categoryChips.current = name;
        categoryChips.cursorIndex = Math.max(0, sheet.categoryNames.indexOf(name));
        sheet._applyCategorySavePath();
    }

        /// The tag palette's answer (`chosen(tags)`).
        function applyChosenTags(tags: var): void {
                                                  const extra = [];
                                                  for (const tag of tags) {
                                                      if (sheet.tagNames.indexOf(tag) < 0)
                                                      extra.push(tag);
                                                  }
                                                  if (extra.length > 0)
                                                  sheet.extraTags = sheet.extraTags.concat(extra);
                                                  tagChips.selected = tags.slice();
                                              }

        /// "Original" | "Subfolder" | "NoSubfolder", whatever the chips are labelled.
        function layoutValue(): string {
            const at = sheet._layoutOptions.indexOf(layoutChips.current);
            return at >= 0 ? sheet._layoutValues[at] : "Original";
        }

        /// `enter` / `shift+enter` and the two buttons. A duplicate never leaves as an add.
        function submit(stopped: bool): void {
        if (sheet._adding)
            return;
        sheet._submitWhenRead = null;
        // Ticks in In Downloads: they become the field's source, or a batch, as a click on a
        // row would make them, and this add goes on with them.
        if (sheet.showDownloads && downloadsList.ticked.length > 0) {
            const paths = downloadsList.takeTicked();
            if (paths.length > 1)
                sheet.addToBatch(paths, "file");
            else
                sheet.loadText(paths[0], "file");
        }
        if (sheet.batchMode) {
            if (sheet.batchRows.some(row => row.state === "reading")) {
                sheet._submitWhenRead = stopped;
                return;
            }
            if (sheet.batchCount === 0) {
                sheet.errorText = qsTr("nothing left to add: every torrent is left out");
                return;
            }
            if (Number.isNaN(sheet._limitKb(dlLimitField.text))
                    || Number.isNaN(sheet._limitKb(upLimitField.text))) {
                sheet.errorText = qsTr("a speed limit is not a number");
                return;
            }
            sheet.errorText = "";
            sheet._send(stopped);
            return;
        }
        if (!sheet.addable && sourceField.text.trim().length > 0) {
            sheet.errorText = sheet._parseError.length > 0
                    ? sheet._parseError : qsTr("not a magnet link, URL or .torrent file");
            return;
        }
        if (sheet.addable && sheet.addable.pending === true) {
            sheet._submitWhenRead = stopped;
            return;
        }
        if (sheet.addable && sheet.addable.valid !== true) {
            // A `.torrent` that could not be read or parsed: say why instead of doing nothing.
            sheet.errorText = sheet.addable.error || qsTr("not a magnet link, URL or .torrent file");
            return;
        }
        if (!sheet.addable)
        return;
        // #43: nothing ticked is still an add, stopped (the button already says so).
        if (sheet._nothingTicked)
            stopped = true;
        if (Number.isNaN(sheet._limitKb(dlLimitField.text))
                || Number.isNaN(sheet._limitKb(upLimitField.text))) {
            sheet.errorText = qsTr("a speed limit is not a number");
            return;
        }
        if (sheet._checkDuplicate())
            return;
            sheet.errorText = "";
            sheet._send(stopped);
        }

            /// D15: the add now, or held until the server answers (`add when back`).
            function _send(stopped: bool): void {
            if (sheet._adding)
                return;
            sheet.failure = {};
            if (sheet.serverDown) {
                sheet._held = {
                    "stopped": stopped
                };
                return;
            }
            sheet._held = null;
            sheet._adding = true;
            addingTimer.restart();
            sheet.addRequested(sheet.batchMode ? sheet.batchRequest(stopped) : sheet.request(stopped));
        }

            /// The server answers again (or the app is on another one that does): a held add goes.
            function _releaseHeld(): void {
            if (!sheet.opened || sheet._held === null || sheet.serverDown)
                return;
            const held = sheet._held;
            sheet._held = null;
            sheet._send(held.stopped);
        }

            /// D14: the app moved to another server while the sheet is up. What was typed stays;
            /// the chips keep their names until the new server's lists are in.
            function _serverSwitched(): void {
            sheet.failure = {};
            sheet.movedText = "";
            const category = categoryChips.current;
            if (category.length > 0 && sheet.extraCategories.indexOf(category) < 0)
                sheet.extraCategories = sheet.extraCategories.concat([category]);
            const tags = tagChips.selected.filter(tag => sheet.extraTags.indexOf(tag) < 0);
            if (tags.length > 0)
                sheet.extraTags = sheet.extraTags.concat(tags);
            sheet._switchPending = true;
        }

            /// D14 (A2): the new server's first sync is in: read again what came from the server
            /// and say in one line what moved.
            function _afterSwitch(): void {
            sheet._switchPending = false;
            const parts = [];
            if (!sheet._savePathEdited) {
                sheet._applyCategorySavePath();
                parts.push(qsTr("save path back to its default"));
            } else {
                parts.push(qsTr("the save path you typed is checked again there"));
            }
            const names = App.categories.count > 0 ? App.categories.names() : [];
            const category = categoryChips.current;
            if (category.length > 0 && names.indexOf(category) < 0)
                parts.push(qsTr("category “%1” is new there and will be created").arg(category));
            for (const tag of tagChips.selected) {
                if (!App.tags.contains(tag))
                    parts.push(qsTr("tag “%1” is new there and will be created").arg(tag));
            }
            sheet.movedText = qsTr("Moved to %1: %2.").arg(App.serverName).arg(parts.join(", "));
            // The library check and the files match are the new server's now.
            sheet._matchToken = "";
            if (!sheet.batchMode && sheet._checkDuplicate())
                return;
            sheet._findMatches();
            sheet._lookUp();
            if (sheet.batchMode)
                sheet._askBatchMatches();
            sheet._releaseHeld();
        }

            /// A waiting `add` (`_submitWhenRead`) runs once nothing is being read any more.
            function _submitIfRead(): void {
            if (sheet._submitWhenRead === null)
            return;
            if (!sheet.opened) { // the read was a duplicate: Fig. 10 has it now
                sheet._submitWhenRead = null;
                return;
            }
            if (sheet.batchMode ? sheet.batchRows.some(row => row.state === "reading")
                                : sheet.addable !== null && sheet.addable.pending === true)
            return;
            sheet.submit(sheet._submitWhenRead);
        }

            /// `App.addFinished`: move to the next queued source, close, or stay open in red.
            function finish(ok: bool, message: string): void {
            sheet._adding = false;
            sheet._addingShown = false;
            addingTimer.stop();
            if (!ok) {
                // docs/33 N3: a refusal the API gave comes in ErrorWords' words; anything else
                // (nothing to add, a name already on disk) is the message itself.
                const failure = App.addFailure;
                if (failure.sentence !== undefined && failure.sentence === message) {
                    sheet.failure = failure;
                    sheet.errorText = "";
                    return;
                }
                sheet.errorText = message.length > 0 ? message : qsTr(
                                                                     "the server refused this torrent");
                return;
            }
            sheet.failure = {};
            if (sheet.queue.length > 0) {
                const next = sheet.queue[0];
                sheet.queue = sheet.queue.slice(1);
                // The Name row (docs/11 D1) belongs to the torrent just added; category, tags,
                // path, limits and toggles stay on purpose, as one batch's settings.
                sheet._programmatic = true;
                renameField.text = "";
                sheet._programmatic = false;
                sheet.errorText = "";
                sheet.loadText(next, sheet.source);
                return;
            }
                sheet.dismiss();
            }

                /// `cancel` (or `esc` on it): drop what is queued and give the keyboard back.
                function dismiss(): void {
                                        sheet.queue = [];
                                        sheet.close();
                                    }

                /// Back to Fig. 5's empty state.
                function reset(): void {
                sheet._programmatic = true;
                sourceField.text = "";
                savePathField.text = "";
                upLimitField.text = "";
                dlLimitField.text = "";
                renameField.text = "";
                sheet._programmatic = false;
                sheet.addable = null;
                sheet.source = "";
                sheet.queue = [];
                sheet.batch = [];
                sheet._batchMatches = {};
                downloadsList.ticked = [];
                sheet.errorText = "";
                sheet.failure = {};
                sheet.movedText = "";
                sheet._held = null;
                sheet._switchPending = false;
                sheet.autoTMM = true;
                sheet._savePathEdited = false;
                sheet._pendingToken = "";
                sheet._awaitingPick = false;
                sheet._submitWhenRead = null;
                sheet._adding = false;
                sheet._addingShown = false;
                sheet.extraCategories = [];
                sheet.extraTags = [];
                sheet.filesMatches = [];
                sheet.matchIndex = 0;
                sheet.savedThere = false;
                sheet._beforeSaveThere = null;
                sheet._matchToken = "";
                categoryChips.current = "";
                categoryChips.cursorIndex = 0;
                tagChips.selected = [];
                tagChips.cursorIndex = 0;
                layoutChips.current = sheet._layoutOptions[0];
                layoutChips.cursorIndex = 0;
                sequentialToggle.checked = false;
                skipCheckToggle.checked = false;
                firstLastToggle.checked = false;
                filesTree.cursorIndex = 0;
            }

                /// The library row a parse's infohash already occupies, or null.
                function _duplicateOf(addable) {
                    if (!addable || addable.valid !== true || addable.pending === true)
                        return null;
                    const duplicate = App.findDuplicate(addable.infoHashV1, addable.infoHashV2);
                    return duplicate && duplicate.hash ? duplicate : null;
                }

                /// docs/25 D1 a: a duplicate turns into Fig. 10 as soon as it is known, instead of
                /// a form to fill for nothing. The sheet moves on to the next queued source, or
                /// closes. True when it was one.
                function _checkDuplicate() {
                    const addable = sheet.addable;
                    const duplicate = sheet._duplicateOf(addable);
                    if (!duplicate)
                        return false;
                    sheet.duplicateFound(duplicate, {
                                             "trackers": addable.trackers || [],
                                             "isPrivate": addable.isPrivate === true
                                         });
                    if (sheet.queue.length > 0) {
                        const next = sheet.queue[0];
                        sheet.queue = sheet.queue.slice(1);
                        sheet._programmatic = true;
                        renameField.text = "";
                        sheet._programmatic = false;
                        sheet.errorText = "";
                        sheet.loadText(next, sheet.source);
                    } else {
                        sheet.dismiss();
                    }
                    return true;
                }

                /// docs/25 §3: ask the library for this `.torrent`'s files, once per parse.
                function _findMatches() {
                    const addable = sheet.addable;
                    // A `.torrent`'s parse, or a magnet's files once its metadata is in (D9 a).
                    const token = addable && addable.valid === true && addable.pending !== true
                            ? (addable.token || "") : "";
                    if (token.length > 0 && sheet._matchToken === token)
                        return;
                    sheet._matchToken = token;
                    sheet.filesMatches = [];
                    sheet.matchIndex = 0;
                    if (sheet.savedThere)
                        sheet.undoSaveThere();
                    if (token.length > 0)
                        App.findFilesMatch(token);
                }

                /// `save there` (X2): Save to and the layout put the files where the match's are,
                /// automatic management off as a typed path turns it; a category picked later
                /// leaves Save to alone. Hash checking stays as it is (D5 a).
                function saveThere(index) {
                    const match = sheet.filesMatches[index];
                    if (!match)
                        return;
                    if (!sheet.savedThere)
                        sheet._beforeSaveThere = {
                            "path": savePathField.text,
                            "autoTMM": sheet.autoTMM,
                            "edited": sheet._savePathEdited,
                            "layout": layoutChips.current
                        };
                    sheet._programmatic = true;
                    savePathField.text = match.savePath;
                    sheet._programmatic = false;
                    sheet.autoTMM = false;
                    sheet._savePathEdited = true;
                    layoutChips.current = sheet._layoutOptions[Math.max(0, sheet._layoutValues.indexOf(match.layout))];
                    sheet.matchIndex = index;
                    sheet.savedThere = true;
                }

                /// `alt+s` from a field or the sheet. True when it was taken.
                function _onShortcut(key, modifiers) {
                    if (key === Qt.Key_S && (modifiers & Qt.AltModifier)
                            && sheet.filesMatches.length > 0 && !sheet.savedThere) {
                        sheet.saveThere(sheet.matchIndex);
                        return true;
                    }
                    return false;
                }

                /// `undo` (X2): Save to, its management and the layout as they were.
                function undoSaveThere() {
                    const before = sheet._beforeSaveThere;
                    sheet.savedThere = false;
                    sheet._beforeSaveThere = null;
                    if (!before)
                        return;
                    sheet._programmatic = true;
                    savePathField.text = before.path;
                    sheet._programmatic = false;
                    sheet.autoTMM = before.autoTMM;
                    sheet._savePathEdited = before.edited;
                    layoutChips.current = before.layout;
                }

                /// Keep the picker on the parse in the field: load a `.torrent`'s files once it
                /// is read, empty it for anything else.
                function _syncFiles(): void {
                const addable = sheet.addable;
                const token = addable && addable.valid === true
                && (addable.fileCount || 0) > 1 ? (addable.token || "") : "";
                if (token === sheet._filesToken)
                return;
                sheet._filesToken = token;
                App.loadAddFiles(token);
                filesTree.cursorIndex = 0;
            }

                /// docs/29 D1: the magnet or link on show is looked up (once: App keys it by hash),
                /// unless it is a duplicate, which Fig. 10 takes before any lookup; anything else
                /// lets the lookup go (D6 a: qBittorrent goes on by itself).
                function _lookUp(): void {
                    if (!sheet.opened)
                        return;
                    const addable = sheet.addable;
                    if (sheet.batchMode || !addable || addable.valid !== true || addable.pending === true
                            || (addable.kind !== "magnet" && addable.kind !== "url") || sheet._duplicateOf(addable)) {
                        App.stopMetadataLookup();
                        return;
                    }
                    App.lookUpMetadata(addable);
                }

                /// The category's own save path, so `Save to` follows the chips until it is typed into.
                function _applyCategorySavePath(): void {
                                                       if (sheet._savePathEdited)
                                                       return;
                                                       sheet._programmatic = true;
                                                       savePathField.text
                                                       = categoryChips.current.length > 0
                                                       ? App.categories.savePathOf(
                                                             categoryChips.current) : "";
                                                       sheet._programmatic = false;
                                                   }

                actions: sheet._nothingTicked ? [
                                                    {
                                                        "text": qsTr("cancel"),
                                                        "cancel": true
                                                    },
                                                    {
                                                        "text": sheet._addStoppedText,
                                                        "primary": true
                                                    }
                                                ] : [
                                                    {
                                                        "text": qsTr("cancel"),
                                                        "cancel": true
                                                    },
                                                    {
                                                        "text": sheet._addStoppedText,
                                                        "secondary": true
                                                    },
                                                    {
                                                        "text": sheet._addText,
                                                        "primary": true
                                                    }
                                                ]
                // docs/33 D13, D20: the server is named in the head with two servers, nowhere
                // with one; the foot's "server: NAME" is gone.
                footerRight: ""
                glyph: Glyphs.add
                headBand: noticeBand
                titleTrailer: sheet.multiServer ? serverChip : null
                // Manual box M5: a stray click or `esc` must not lose a half-filled add.
                guarded: true
                hints: filesTree.editing ? sheet._renameHints : filesTree.activeFocus
                                                                ? sheet._treeHints
                                                                : batchList.activeFocus
                                                                  ? sheet._batchHints
                                                                  : downloadsList.activeFocus
                                                                  ? sheet._downloadsHints
                                                                  : sheet._formHints
                sheetWidth: Theme.space(600)
                subtitle: {
                    if (sheet.batchMode) {
                        const parts = [];
                        if (sheet._batchBytes > 0)
                            parts.push(App.sizeText(sheet._batchBytes));
                        if (sheet._batchUnsized > 0)
                            parts.push(sheet._batchUnsized === 1 ? qsTr("1 without a size yet")
                                                                 : qsTr("%1 without a size yet").arg(
                                                                       sheet._batchUnsized));
                        const out = sheet.batchRows.length - sheet.batchCount;
                        if (out > 0)
                            parts.push(qsTr("%1 left out").arg(out));
                        return parts.join(" · ");
                    }
                    if (!sheet.addable)
                        return qsTr("paste a magnet link, a URL or pick a file");
                    if (sheet.addable.pending === true)
                        return qsTr("reading %1…").arg(sheet.addable.name);
                    const parts = [sheet.addable.name];
                    if (sheet.addable.sizeText.length > 0)
                        parts.push(sheet.addable.sizeText);
                    if (sheet.sourceText.length > 0)
                        parts.push(sheet.sourceText);
                    return parts.join(" · ");
                }
                title: sheet.multiServer ? (sheet.batchMode ? qsTr("Add %1 torrents to").arg(sheet.batchCount)
                                                            : qsTr("Add torrent to"))
                                         : sheet.batchMode ? qsTr("Add %1 torrents").arg(sheet.batchCount) : qsTr("Add torrent")

                onAddableChanged: {
                    sheet._syncFiles();
                    // Typed text is checked on `add` instead (`submit`).
                    if (sheet.opened && !sheet._typing && sheet._checkDuplicate())
                        return;
                    sheet._findMatches();
                    sheet._lookUp();
                }
                // docs/25 §3: `alt+s` is `save there` while a files match is offered. A field
                // would type the "s", so the fields hand it on (`_fieldShortcuts`); anything
                // else lets it reach the sheet.
                keyHandler: event => {
                    if (sheet._onShortcut(event.key, event.modifiers)) {
                        event.accepted = true;
                    } else if (event.key === Qt.Key_S && (event.modifiers & Qt.ControlModifier)
                               && sheet.multiServer) {
                        // docs/33 D14: the server palette over the sheet.
                        sheet.serverPaletteRequested();
                        event.accepted = true;
                    }
                }
                // Each opening looks at Downloads again: the browser may have saved one since.
                onOpenedChanged: {
                    if (sheet.opened) {
                        sheet._serverId = App.serverId;
                        App.scanDownloads();
                        sheet._lookUp();
                    } else {
                        // D6 a: closing leaves a lookup that has not answered running on the
                        // server (F4 says so); only the polling stops.
                        App.stopMetadataLookup();
                    }
                }
                onRejected: sheet.dismiss()
                onActionTriggered: text => {
                    if (text === sheet._addText)
                        sheet.submit(false);
                    else if (text === sheet._addStoppedText)
                        sheet.submit(true);
                }

                // #124: the add has been out this long: the button says it is still going.
                Timer {
                    id: addingTimer

                    interval: 400
                    onTriggered: sheet._addingShown = sheet._adding
                }

                Connections {
                    function onAddFinished(ok: bool, message: string, hash: string): void {
                    if (sheet.opened) {
                        sheet.finish(ok, message);
                    } else {
                        // Cancelled while it was out: MainView says how it went, in the footer.
                        sheet._adding = false;
                        sheet._addingShown = false;
                    }
                }

                    // docs/25 §3: the files match for the `.torrent` in the field.
                    function onFilesMatchFound(token: string, matches: var): void {
                    if (sheet._batchMatches[token] !== undefined) {
                        const found = Object.assign({}, sheet._batchMatches);
                        found[token] = matches;
                        sheet._batchMatches = found;
                        return;
                    }
                    if (!sheet.addable || sheet.addable.token !== token)
                    return;
                    sheet.filesMatches = matches;
                    sheet.matchIndex = 0;
                }

                    // docs/29 F2: the magnet's (or link's) metadata is in. The keyboard stays where
                    // it is; the tree, the size and the files match follow from the new map.
                    function onMetadataFetched(result: var): void {
                    if (!sheet.opened || sheet.batchMode || !sheet.addable || sheet.addable.value !== result.value)
                    return;
                    sheet.addable = result;
                }

                    // The second half of a `.torrent` parse, and the whole answer of the file picker.
                    function onAddableParsed(result: var): void {
                    if (!sheet.opened)
                    return;
                    // #40: a batch `.torrent` read: its row gets its name, size and hashes.
                    const at = result.token.length > 0
                             ? sheet.batch.findIndex(entry => entry.map.token === result.token) : -1;
                    if (at >= 0) {
                        const next = sheet.batch.slice();
                        next[at] = {
                            "text": next[at].text,
                            "map": result
                        };
                        sheet.batch = next;
                        sheet._askBatchMatches();
                        sheet._submitIfRead();
                        return;
                    }
                    if (sheet._awaitingPick) {
                        sheet._awaitingPick = false;
                        sheet.loadText(result.value, "file");
                        return;
                    }
                        if (result.token.length > 0 && result.token === sheet._pendingToken) {
                            sheet.addable = result;
                            // Unless the duplicate check moved the sheet on to a queued source.
                            if (sheet.addable && sheet.addable.token === result.token)
                            sheet._submitIfRead();
                        }
                    }

                        target: App
                    }

                    // #147: a bare info hash names no tracker, so only DHT can find its peers.
                    Banner {
                        Layout.fillWidth: true
                        kind: "warning"
                        objectName: "addNoDhtLine"
                        text: qsTr("DHT is off on %1, and an info hash names no tracker: it may never find anyone.").arg(
                                  App.serverName)
                        visible: sheet.bareHashWithoutDht
                    }

                    // docs/33 D14 (A2): what a switch from the sheet moved, in one yellow line.
                    Banner {
                        Layout.fillWidth: true
                        kind: "warning"
                        objectName: "addMovedLine"
                        text: sheet.movedText
                        visible: sheet.movedText.length > 0
                    }

                    // D14, D15: the switch, the server's state and a held add.
                    Connections {
                        function onServerChanged(): void {
                            if (!sheet.opened || App.serverId === sheet._serverId)
                                return;
                            sheet._serverId = App.serverId;
                            sheet._serverSwitched();
                        }

                        target: App
                    }

                    Connections {
                        function onLastSyncTimeChanged(): void {
                            if (!sheet.opened)
                                return;
                            if (sheet._switchPending)
                                sheet._afterSwitch();
                            else
                                sheet._releaseHeld();
                        }

                        target: App.globalState
                    }

                    // docs/35 N7: the sheet's notice, under its head, edge to edge.
                    Component {
                        id: noticeBand

                        Notice {
                            readonly property var problem: App.serverProblem(App.globalState.lastError,
                                                                             App.globalState.retryInSeconds)
                            readonly property bool failed: sheet.failure.headline !== undefined

                            actions: sheet.multiServer && (failed || sheet.serverDown) ? [
                                                                                            {
                                                                                                "label": qsTr("switch server"),
                                                                                                "key": "switch"
                                                                                            }] : []
                            animated: !App.reducedMotion
                            body: failed ? (sheet.failure.line || "")
                                         : sheet.serverDown ? (sheet._held !== null ? qsTr("The torrent is held: it goes as soon as %1 answers.").arg(App.serverName)
                                                                                    : qsTr("“add when back” holds the torrent until it answers."))
                                                            : ""
                            bodyTail: !failed && sheet.serverDown ? (problem.retry || "") : ""
                            bodyTailChars: problem.retryChars || 0
                            details: failed ? (sheet.failure.details || "") : sheet.serverDown ? (problem.details || "") : ""
                            edge: "both"
                            // N3: nothing has failed yet while it is down (yellow); a refused add is red.
                            kind: failed || sheet.errorText.length > 0 ? "error" : "warning"
                            objectName: "addNotice"
                            shown: sheet.opened && (failed || sheet.serverDown || sheet.errorText.length > 0)
                            title: failed ? sheet.failure.headline
                                          : sheet.errorText.length > 0 ? sheet.errorText
                                                                       : qsTr("%1 isn't answering").arg(App.serverName)

                            onActionTriggered: key => {
                                if (key === "switch")
                                    sheet.serverPaletteRequested();
                            }
                            onCopyRequested: text => App.copyToClipboard(text)
                        }
                    }

                    // docs/33 D13 (A1): the server the torrent goes to, a chip after the title.
                    Component {
                        id: serverChip

                        Rectangle {
                            id: serverChipBox

                            // #77: a "switch server" button, as the hero's server line; the
                            // server is its description. Its press is the click.
                            Accessible.description: App.serverName
                            Accessible.name: qsTr("switch server")
                            Accessible.role: Accessible.Button
                            Accessible.onPressAction: if (serverChipBox.enabled)
                                                          sheet.serverPaletteRequested()
                            border.color: chipMouse.containsMouse ? Theme.fill(Theme.borderHover) : Theme.fill(Theme.borderIdle)
                            border.width: 1
                            color: chipMouse.containsMouse ? Theme.fill(Theme.fillHover) : "transparent"
                            implicitHeight: chipRow.implicitHeight + Theme.space(4)
                            implicitWidth: chipRow.implicitWidth + Theme.space(12)
                            objectName: "addServerChip"
                            radius: Theme.cornerRadius

                            Row {
                                id: chipRow

                                anchors.centerIn: parent
                                spacing: Theme.space(6)

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    // docs/35 N3: yellow while omaseed retries, red when it needs you.
                                    color: !sheet.serverDown ? Theme.green : App.serverRefused ? Theme.red : Theme.yellow
                                    height: Theme.space(7)
                                    radius: width / 2
                                    width: Theme.space(7)
                                }

                                Label {
                                    bright: true
                                    font.weight: Font.DemiBold
                                    objectName: "addServerName"
                                    text: App.serverName
                                }

                                Label {
                                    dim: true
                                    size: "caption"
                                    text: Glyphs.chevronDown
                                }
                            }

                            MouseArea {
                                id: chipMouse

                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true

                                onClicked: sheet.serverPaletteRequested()
                            }
                        }
                    }

                    // `.row` — Magnet, URL or file; in a batch (#40), the list over a field for more
                    FormRow {
                        Layout.fillWidth: true
                        label: sheet.batchMode ? "" : qsTr("Magnet, URL or file")

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space(10)
                            visible: sheet.batchMode

                            Label {
                                Accessible.name: text
                                Accessible.role: Accessible.StaticText
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                objectName: "batchHead"
                                text: qsTr("Torrents · %1").arg(sheet.batchRows.length)
                                tracked: true
                            }

                            // docs/27 D9: back to the queue.
                            Label {
                                id: oneByOneLink

                                // #77: a link back to the queue; its press is the click.
                                Accessible.name: oneByOneLink.text
                                Accessible.role: Accessible.Link
                                Accessible.onPressAction: if (oneByOneLink.enabled)
                                                              sheet.oneByOne()
                                color: oneByOneMouse.containsMouse ? Theme.accent : Theme.foreground
                                objectName: "batchOneByOne"
                                size: "caption"
                                text: qsTr("one by one")

                                MouseArea {
                                    id: oneByOneMouse

                                    anchors.fill: parent
                                    anchors.margins: -Theme.space(4)
                                    cursorShape: Qt.PointingHandCursor
                                    hoverEnabled: true

                                    onClicked: sheet.oneByOne()
                                }
                            }
                        }

                        AddBatchList {
                            id: batchList

                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            rows: sheet.batchRows
                            visible: sheet.batchMode

                            onRemoveRequested: index => sheet.removeFromBatch(index)
                        }

                        // docs/27 D10: a batch saves everything to one place.
                        Banner {
                            Layout.fillWidth: true
                            kind: "warning"
                            objectName: "batchMatchLine"
                            text: sheet.batchMatch
                                  ? qsTr("Files for %1 are already on disk in %2. A batch saves everything to one place: to add it on top of those files, use one by one.").arg(
                                        sheet.batchMatch.name).arg(sheet.batchMatch.savePath)
                                  : ""
                            visible: sheet.batchMode && sheet.batchMatch !== null
                        }

                        Field {
                            id: sourceField

                            Layout.fillWidth: true
                            placeholder: sheet.batchMode ? qsTr("paste more magnets or URLs, one per line")
                                                         : qsTr("magnet:?xt=urn:btih:…")
                            shortcuts: sheet._fieldShortcuts
                            // Manual box M6: a button to click, not only a key to know.
                            trailing: qsTr("browse")
                            trailingClickable: true
                            trailingKey: "ctrl+o"

                            onAccepted: {
                                // In a batch the field is for more: `enter` with text adds it
                                // to the list, `enter` on an empty one adds the batch.
                                if (sheet.batchMode && sourceField.text.trim().length > 0) {
                                    const more = App.splitAddables(sourceField.text);
                                    if (more.length > 0)
                                        sheet.addToBatch(more, "");
                                    else
                                        sheet.errorText = qsTr("not a magnet link, URL or .torrent file");
                                    return;
                                }
                                sheet.triggerPrimary();
                            }
                            onShortcutPressed: (key, modifiers) => sheet._onShortcut(key, modifiers)
                            onTrailingClicked: sheet.browse()
                            onTextChanged: {
                                // More than one character at once is a paste; one at a time is
                                // typing, which must not turn into Fig. 10 halfway through a
                                // hash (docs/25 D1 a applies to what arrives whole).
                                const grew = sourceField.text.length - sheet._typedLength;
                                sheet._typedLength = sourceField.text.length;
                                if (sheet._programmatic)
                                    return;
                                // #40: a paste of several, or anything pasted into a batch,
                                // joins the batch.
                                if (grew > 1) {
                                    const many = App.splitAddables(sourceField.text);
                                    if (many.length > 1 || (sheet.batchMode && many.length > 0)) {
                                        sheet.addToBatch(many, "");
                                        return;
                                    }
                                }
                                if (sheet.batchMode)
                                    return;
                                sheet.source = "";
                                sheet.errorText = "";
                                sheet._typing = grew <= 1;
                                sheet.parseField();
                            }
                            // `ctrl+o` has to be taken inside the field: a TextInput with the caret swallows
                            // it before the sheet's own handler ever runs.
                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_O && (event.modifiers
                                                               & Qt.ControlModifier)) {
                                    sheet.browse();
                                    event.accepted = true;
                                }
                            }
                        }
                    }

                    // docs/25 §3 (#27): these files are already on disk under another torrent.
                    AddMatchLine {
                        id: matchLine

                        Layout.fillWidth: true
                        index: sheet.matchIndex
                        matches: sheet.filesMatches
                        saved: sheet.savedThere

                        onIndexChanged: sheet.matchIndex = matchLine.index
                        onSaveRequested: index => sheet.saveThere(index)
                        onUndoRequested: sheet.undoSaveThere()
                    }

                    // In Downloads (manual box M7): the newest `.torrent` files there, while the
                    // field above is still empty. A row fills the field as the file chooser does.
                    ColumnLayout {
                        id: downloadsSection

                        Layout.fillWidth: true
                        spacing: Theme.space(4)
                        visible: sheet.showDownloads

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space(10)

                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: qsTr("In Downloads")
                                tracked: true
                            }

                            // #40 (docs/27 B3): what `enter` does with ticks.
                            Label {
                                Accessible.name: text
                                Accessible.role: Accessible.StaticText
                                dim: true
                                objectName: "downloadsTicked"
                                size: "caption"
                                text: sheet.tickedText(downloadsList.ticked.length, sheet._tickedInLibrary)
                                visible: downloadsList.ticked.length > 0
                            }
                        }

                        AddDownloadsList {
                            id: downloadsList

                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            torrents: App.downloadsTorrents

                            onChosen: path => sheet.useDownload(path)
                            onChosenMany: paths => {
                                sheet.addToBatch(paths, "file");
                                sheet.focusFirst();
                            }
                        }
                    }

                    // `.row` — Name (docs/11 D1): the torrent's name on the server. Empty keeps the
                    // metainfo's own, which the placeholder shows.
                    FormRow {
                        Layout.fillWidth: true
                        label: qsTr("Name")
                        visible: !sheet.batchMode

                        Field {
                            id: renameField

                            Layout.fillWidth: true
                            placeholder: sheet.addable && sheet.addable.name.length > 0
                                         ? sheet.addable.name : qsTr("the torrent's own name")
                            shortcuts: sheet._fieldShortcuts

                            onAccepted: sheet.triggerPrimary()
                            onShortcutPressed: (key, modifiers) => sheet._onShortcut(key, modifiers)
                        }
                    }

                    // `.cols` — Category | Tags
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space(10)

                        FormRow {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            label: qsTr("Category")

                            ChipSelect {
                                id: categoryChips

                                Layout.fillWidth: true
                                more: true
                                options: sheet.categoryNames

                                onChanged: sheet._applyCategorySavePath()
                                onMoreRequested: sheet.categoryPaletteRequested()
                            }
                        }

                        FormRow {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            label: qsTr("Tags")

                            ChipSelect {
                                id: tagChips

                                Layout.fillWidth: true
                                more: true
                                multiple: true
                                options: sheet.tagNames

                                onMoreRequested: sheet.tagPaletteRequested()
                            }
                        }
                    }

                    // `.row` — Save to
                    FormRow {
                        Layout.fillWidth: true
                        label: qsTr("Save to")

                        Field {
                            id: savePathField

                            Layout.fillWidth: true
                            placeholder: sheet.multiServer ? qsTr("%1's default").arg(App.serverName)
                                                           : qsTr("the server's default")
                            shortcuts: sheet._fieldShortcuts
                            // #41: where the path came from, then the button.
                            trailing: sheet.savedThere ? qsTr("from the files match · browse")
                                      : sheet.autoTMM && savePathField.text.length > 0 ? qsTr(
                                                                                           "from category · browse") :
                                                                                       qsTr("browse")
                            trailingClickable: true

                            onAccepted: sheet.triggerPrimary()
                            onShortcutPressed: (key, modifiers) => sheet._onShortcut(key, modifiers)
                            onTrailingClicked: sheet.browseSavePath()
                            onTextChanged: {
                                if (sheet._savePick.path !== undefined && sheet._savePick.path !== savePathField.text)
                                    sheet._savePick = ({});
                                if (sheet._programmatic)
                                    return;
                                // Typing a path of your own is what turns automatic torrent management off.
                                sheet._savePathEdited = true;
                                sheet.autoTMM = false;
                                // … and a path of your own is no longer the files match's.
                                sheet.savedThere = false;
                            }
                        }

                        BrowsedPathLine {
                            id: saveBrowsedLine

                            pick: sheet._savePick
                            serverName: App.serverName
                        }

                        // docs/25 §4 (#29): what the add needs and the room where it goes.
                        AddFreeLine {
                            id: freeLine

                            Layout.fillWidth: true
                            neededBytes: sheet.neededBytes
                            savePath: savePathField.text
                            totalBytes: sheet.batchMode ? sheet.neededBytes
                                                        : sheet.addable && sheet.addable.totalSize !== undefined
                                                          ? sheet.addable.totalSize : -1
                            alreadyThere: sheet.savedThere
                            serverDown: sheet.serverDown
                            visible: sheet.batchMode ? sheet.batchCount > 0
                                                     : sheet.addable !== null && sheet.addable.valid === true
                        }
                    }

                    // `.cols` — Layout | Upload limit
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space(10)

                        FormRow {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            label: qsTr("Layout")

                            ChipSelect {
                                id: layoutChips

                                Layout.fillWidth: true
                                current: sheet._layoutOptions[0]
                                options: sheet._layoutOptions
                            }
                        }

                        // The right half holds both limits (docs/11 D1 adds the download one).
                        // Near the 560 px floor the captions would elide, so they go to arrows.
                        RowLayout {
                            id: limitsRow

                            readonly property bool narrow: limitsRow.width < Theme.space(250)

                            Layout.alignment: Qt.AlignTop
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            spacing: Theme.space(10)

                            FormRow {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                label: limitsRow.narrow ? qsTr("↓ limit") : qsTr(
                                                              "Download limit")
                                spokenCaption: false // the field says it (#78)

                                Field {
                                    id: dlLimitField

                                    Layout.fillWidth: true
                                    // #78: named in words; `∞` (and a narrow row's ↓) is no name.
                                    accessibleName: qsTr("Download limit")
                                    implicitWidth: Theme.space(80)
                                    numeric: true
                                    placeholder: qsTr("∞")
                                    shortcuts: sheet._fieldShortcuts
                                    trailing: qsTr("KB/s")

                                    onAccepted: sheet.triggerPrimary()
                                    onShortcutPressed: (key, modifiers) => sheet._onShortcut(key, modifiers)
                                }
                            }

                            FormRow {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                label: limitsRow.narrow ? qsTr("↑ limit") : qsTr(
                                                              "Upload limit")
                                spokenCaption: false // the field says it (#78)

                                Field {
                                    id: upLimitField

                                    Layout.fillWidth: true
                                    // #78: named in words; `∞` (and a narrow row's ↑) is no name.
                                    accessibleName: qsTr("Upload limit")
                                    implicitWidth: Theme.space(80)
                                    numeric: true
                                    placeholder: qsTr("∞")
                                    shortcuts: sheet._fieldShortcuts
                                    trailing: qsTr("KB/s")

                                    onAccepted: sheet.triggerPrimary()
                                    onShortcutPressed: (key, modifiers) => sheet._onShortcut(key, modifiers)
                                }
                            }
                        }
                    }

                    // `.cols` — the toggles, two to a line
                    GridLayout {
                        Layout.fillWidth: true
                        columnSpacing: Theme.space(10)
                        // One to a line once the longest label no longer fits half the width.
                        columns: 2 * Math.max(sequentialToggle.implicitWidth,
                                              firstLastToggle.implicitWidth,
                                              skipCheckToggle.implicitWidth) + columnSpacing
                                 <= width ? 2 : 1
                        rowSpacing: Theme.space(10)

                        // A Kit `Toggle` sizes itself to its label, so each half of the `.cols` grid is an
                        // Item the layout can stretch instead.
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            implicitHeight: sequentialToggle.implicitHeight
                            implicitWidth: sequentialToggle.implicitWidth

                            Toggle {
                                id: sequentialToggle

                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("sequential download")
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            implicitHeight: firstLastToggle.implicitHeight
                            implicitWidth: firstLastToggle.implicitWidth

                            Toggle {
                                id: firstLastToggle

                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("first and last pieces first")
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            implicitHeight: skipCheckToggle.implicitHeight
                            implicitWidth: skipCheckToggle.implicitWidth

                            Toggle {
                                id: skipCheckToggle

                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("skip hash check")
                            }
                        }
                    }

                    // Files (docs/11 D1): the `.torrent`'s tree, last so it can take what the
                    // window leaves; past that it scrolls. A magnet's waits here for its list
                    // (docs/29 §4).
                    ColumnLayout {
                        id: filesSection

                        Layout.fillWidth: true
                        spacing: Theme.space(4)
                        visible: sheet.showFiles || sheet._showFetch

                        RowLayout {
                            id: filesHead

                            Layout.fillWidth: true
                            spacing: Theme.space(10)

                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: qsTr("Files")
                                tracked: true
                            }

                            Label {
                                Accessible.name: text
                                Accessible.role: Accessible.StaticText
                                color: !sheet.showFiles && fetchLine.summaryWarn ? Theme.yellow : Theme.dimForeground
                                objectName: "addFilesSummary"
                                size: "caption"
                                text: sheet.showFiles ? App.addFiles.summaryText : fetchLine.summaryText
                            }
                        }

                        // docs/29 F1, F3-F5, F7, F8: the lookup's wait, where the tree will be.
                        AddFetchLine {
                            id: fetchLine

                            Layout.fillWidth: true
                            kind: sheet.addable ? sheet.addable.kind : "magnet"
                            objectName: "addFetchLine"
                            phase: App.addLookup.phase
                            privateHost: App.addLookup.privateHost
                            seconds: App.addLookup.seconds
                            serverName: App.serverName
                            visible: sheet._showFetch
                        }

                        AddFilesTree {
                            id: filesTree

                            Layout.fillWidth: true
                            visible: sheet.showFiles
                            Layout.preferredHeight: implicitHeight
                            // The header and four rows at least: below that the whole body scrolls.
                            maximumHeight: Math.max(5 * Theme.railRowHeight + 2, sheet._treeRoom)
                            model: App.addFiles
                        }

                        // #43: why the button reads "add stopped".
                        Label {
                            id: nothingTickedLine

                            Accessible.name: text
                            Accessible.role: Accessible.StaticText
                            Layout.fillWidth: true
                            dim: true
                            elide: Text.ElideRight
                            objectName: "addNothingTicked"
                            size: "caption"
                            text: qsTr("nothing ticked: added stopped · choose its files later in the Files tab")
                            visible: sheet._nothingTicked
                        }
                    }
                }
