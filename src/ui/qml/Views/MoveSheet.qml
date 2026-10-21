pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// Every move's sheet (DECISIONS C15, C16 L1): one sheet, three ways in.
//
// "folder" (board 42 §3 S1–S3, M4–M9, #97): the field holds the folder's new path, prefilled
// with its current one; browse picks a parent and the sheet adds the folder's name (M4).
// Under it, every save path in the folder and where it goes, then the preflight, read again
// whenever the field changes, and the files no torrent owns as a dry run.
//
// "selection" (board 43 §2–§4, L1–L5, L13; `m`, "move…"): set location as it now is. The
// field is prefilled with the first torrent's save path, or the destination a failed move
// meant (docs/32 F1). ↓ or typing opens the places under it (L4); browse and ctrl+o go
// through the path map with the line under the field saying what a pick became (#41).
// Torrents from several save paths get the choice of L5. The checks speak only when they have
// something to say (L3). Torrents outside the selection that share the data are named and
// asked about, "move both" picked every time (#28, #35, docs/26).
//
// "back" (board 43 §7 E2, L11): the last job's torrents, each to where it was. No field.
//
// The words are App.move's (plan(), planSelection(), planBack()). It fetches what it still
// needs (file lists, the walk for loose files, a remote server's listings) as it goes and
// moves `App.move.planRevision` when there is more: the sheet reads the plan again then.
// Nothing is sent before the move button is pressed.
//
// "show" on a line, or the foot's "show the N", filters the table under the sheet to those
// torrents (`showRequested`); the host brings the sheet back with `esc` (reopen()).
Sheet {
    id: root

    /// "folder", "selection" or "back".
    property string mode: "folder"
    /// The folder being moved, as the server writes it ("folder").
    property string folder: ""
    /// The torrents the sheet was opened on ("selection").
    property var hashes: []
    /// L5: "oneFolder" or "keepPlaces", for a selection from several save paths.
    property string rule: "oneFolder"
    /// The folder picker: `App.pickDirectory`, which a test replaces (a portal can't be
    /// answered from one). Takes the folder to start in, returns the pick or "".
    property var pickDirectory: start => App.pickDirectory(start)
    /// The tick of "carry the N files after the torrents" (M17), on by default when offered.
    property bool carry: true
    property bool _carryTouched: false
    /// The plan for the field as it stands.
    property var plan: ({})
    /// #28: `App.sharedData(hashes)` at `openSelection()`.
    property var _shared: ({})
    /// #41: `App.serverPathForPick()` for the last pick, `{}` once the field is typed over.
    property var _pick: ({})
    /// L4: the places list is open under the field.
    property bool placesOpen: false
    /// The field's text as the sheet put it there: the places list it opens on ↓ is unnarrowed.
    property string _prefill: ""
    /// The sheet is writing the field itself: no places list for that.
    property bool _filling: false

    readonly property bool selection: root.mode === "selection"
    /// Torrents outside the selection share the data: the sheet asks.
    readonly property bool asking: root.selection && (root._shared.outside ?? []).length > 0
    /// The shared-data pick: 0 moves the sharers too, 1 only the selection.
    readonly property alias choice: sharedChoice.choice
    readonly property alias ruleChoice: ruleChoice
    readonly property alias pathField: pathField
    readonly property alias browsedLine: browsedLine
    readonly property alias placeList: placeList
    /// The most places the list shows; typing narrows the rest.
    readonly property int placesShown: 12

    /// "show": the table lists these torrents.
    signal showRequested(var hashes)
    /// "list ▸": the loose files' list (S2).
    signal looseListRequested(string folder)

    /// What the move sends: the selection, and the sharers when "move both" is picked.
    function moveHashes(): var {
        return root.asking && sharedChoice.choice === 0 ? root.hashes.concat(root._shared.outside) : root.hashes;
    }

    function _readPlan(): void {
        if (root.mode === "back") {
            root.plan = App.move.planBack();
            return;
        }
        if (root.selection) {
            if (root.hashes.length > 0)
                root.plan = App.move.planSelection(root.moveHashes(), pathField.text.trim(), root.rule);
            return;
        }
        if (root.folder.length === 0)
            return;
        root.plan = App.move.plan(root.folder, pathField.text.trim(), root.carry);
        if (!root._carryTouched)
            root.carry = root.plan.carryDefault === true;
    }

    function _kindColor(kind: string): color {
        switch (kind) {
        case "ok":
            return Theme.green;
        case "warn":
        case "unknown":
            return Theme.yellow;
        case "bad":
            return Theme.red;
        case "loose":
            return Theme.accent;
        default:
            return Theme.dimForeground;
        }
    }

    function _setField(text: string): void {
        root._filling = true;
        pathField.text = text;
        root._filling = false;
    }

    /// Opens on `folder`, the field on `newPath` when given (J3's "move the rest…", T2's
    /// "change the folder…"), else on the folder's own path.
    function openFor(folder: string, newPath: var): void {
        root.mode = "folder";
        root.folder = folder;
        root.hashes = [];
        root._shared = ({});
        root._pick = ({});
        root.placesOpen = false;
        root._carryTouched = false;
        root.carry = true;
        root._setField(typeof newPath === "string" && newPath.length > 0 ? newPath : folder);
        root._readPlan();
        root.open();
        pathField.selectAll();
    }

    /// Opens on `hashes` (L1), the field on `path` when given (docs/32 F1: the destination
    /// of a move that didn't happen, to try again), else on the first one's save path.
    function openSelection(hashes: var, path: var): void {
        root.mode = "selection";
        root.folder = "";
        root.hashes = hashes;
        root.rule = "oneFolder";
        ruleChoice.choice = 0;
        root._pick = ({});
        root.placesOpen = false;
        let prefill = "";
        if (hashes.length > 0)
            prefill = App.torrentRow(hashes[0]).savePath ?? "";
        root._prefill = typeof path === "string" && path.length > 0 ? path : prefill;
        root._setField(root._prefill);
        root._shared = App.sharedData(hashes, true);
        // "Move both" is picked every time.
        sharedChoice.choice = 0;
        root._readPlan();
        root.open();
        pathField.selectAll();
    }

    /// L11: the last job's torrents, each back to where it was.
    function openBack(): void {
        root.mode = "back";
        root.folder = "";
        root.hashes = [];
        root._shared = ({});
        root._pick = ({});
        root.placesOpen = false;
        root._readPlan();
        root.open();
    }

    /// Back after "show", as it was left.
    function reopen(): void {
        root._readPlan();
        root.open();
    }

    /// `browse` and `ctrl+o`. A folder's (M4): the pick is the new parent, and the folder's
    /// name goes after it. A selection's (#41): the pick is the path, as the server sees it.
    function browse(): void {
        root.placesOpen = false;
        const start = pathField.text.trim();
        if (root.selection) {
            const picked = root.pickDirectory(App.browseStartFor(start));
            if (!picked || picked.length === 0)
                return;
            const answer = App.serverPathForPick(picked);
            root._setField(answer.path);
            root._pick = answer;
            return;
        }
        const parentStart = start.lastIndexOf("/") > 0 ? start.substring(0, start.lastIndexOf("/")) : start;
        const picked = root.pickDirectory(App.browseStartFor(parentStart));
        if (!picked || picked.length === 0)
            return;
        const answer = App.serverPathForPick(picked);
        const name = root.folder.substring(root.folder.lastIndexOf("/") + 1);
        const parentPath = answer.path.replace(/\/+$/, "");
        pathField.text = (parentPath.length > 0 ? parentPath : "") + "/" + name;
    }

    /// L4: the places for what is typed, as the list's rows. Nothing typed over the prefill
    /// narrows nothing.
    function _placeRows(): var {
        const typed = pathField.text.trim() === root._prefill ? "" : pathField.text.trim();
        const sections = {
            "recent": qsTr("recent"),
            "folder": qsTr("library"),
            "category": qsTr("categories")
        };
        return App.move.places(typed).map(place => ({
                                                       "section": sections[place.kind] ?? "",
                                                       "text": place.path,
                                                       "detail": place.kind === "folder"
                                                                 ? String(place.count ?? "")
                                                                 : place.kind === "category"
                                                                   ? place.name ?? "" : ""
                                                   }));
    }

    function _openPlaces(): void {
        const all = root._placeRows();
        placeList.rows = all.slice(0, root.placesShown);
        placeList.more = all.length > root.placesShown
                ? qsTr("%1 more: type to narrow").arg(all.length - root.placesShown) : "";
        placeList.current = -1;
        root.placesOpen = all.length > 0;
    }

    /// tab or enter on a place: it goes into the field, and the checks run for it.
    function _fillPlace(): void {
        const row = placeList.currentRow;
        root.placesOpen = false;
        if (row === null)
            return;
        root._setField(row.text);
        root._pick = ({});
        replan.stop();
        root._readPlan();
    }

    /// "show the N": what the sheet would send, when looking at it says more than the count.
    readonly property bool _offersShow: {
        const count = root.plan.moveCount ?? 0;
        if (root.mode === "folder")
            return true;
        if (root.mode === "back")
            return false;
        return count > 0 && ((root.plan.savePaths ?? 1) > 1 || count !== root.moveHashes().length);
    }

    actions: {
        const list = [{
                          "text": qsTr("cancel"),
                          "cancel": true
                      }];
        if (root._offersShow)
            list.push({
                          "text": qsTr("show the %1").arg(root.plan.moveCount ?? 0),
                          "key": "show",
                          "disabled": (root.plan.moveCount ?? 0) === 0
                      });
        list.push({
                      "text": root.plan.moveText ?? qsTr("move"),
                      "primary": true,
                      "disabled": root.plan.canMove !== true
                  });
        return list;
    }
    glyph: root.mode === "back" ? "↺" : "▱"
    hints: [
        {
            "key": "enter",
            "label": qsTr("move")
        },
        root.selection ? {
                             "key": "↓",
                             "label": qsTr("places")
                         } : {
                             "key": "tab",
                             "label": qsTr("next")
                         },
        {
            "key": "esc",
            "label": qsTr("cancel")
        }
    ]
    sheetWidth: root.mode === "folder" ? Theme.space(640) : root.asking ? Theme.space(600) : Theme.space(560)
    subtitle: root.plan.header ?? ""
    title: root.plan.title ?? (root.selection ? qsTr("Move…") : qsTr("Move a folder"))

    onAccepted: {
        if (root.plan.canMove !== true)
            return;
        let started = false;
        if (root.mode === "back")
            started = App.move.startBack();
        else if (root.selection)
            started = App.move.startSelection(root.moveHashes(), pathField.text.trim(), root.rule);
        else
            started = App.move.start(root.folder, pathField.text.trim(),
                                     root.carry && root.plan.carryOffered === true);
        if (started)
            root.close();
    }
    onActionTriggered: text => {
        if (text === qsTr("show the %1").arg(root.plan.moveCount ?? 0)) {
            root.close();
            root.showRequested(root.plan.moveHashes ?? []);
        }
    }
    onOpenedChanged: {
        if (!root.opened)
            root.placesOpen = false;
    }

    // The field changes: the plan is read again, a moment after the typing stops.
    Timer {
        id: replan

        interval: 250

        onTriggered: root._readPlan()
    }

    Connections {
        function onPlanRevisionChanged(): void {
            if (root.opened)
                root._readPlan();
        }

        target: App.move
    }

    Connections {
        function onSharedFilesLoaded(): void {
            if (root.opened && root.selection) {
                root._shared = App.sharedData(root.hashes);
                root._readPlan();
            }
        }

        target: App
    }

    FormRow {
        Layout.fillWidth: true
        label: root.selection ? qsTr("new path on %1").arg(App.serverName) : qsTr("new path of the folder")
        visible: root.mode !== "back"

        Field {
            id: pathField

            Layout.fillWidth: true
            escClears: false
            objectName: "movePathField"
            // L4: ↓ opens the places; while they show, ↑↓ pick, tab and enter fill in, esc
            // closes them and leaves the sheet up.
            shortcuts: !root.selection ? [] : root.placesOpen ? [{
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
            // C18 D5 (board 45): "places ▾" opens the same list with the mouse; a second
            // click closes it. For a screen reader the field is a combobox named "new path".
            accessibleName: root.selection ? qsTr("new path") : ""
            opener: root.selection ? qsTr("places") : ""
            openerDescription: root.selection && !root.placesOpen
                               ? qsTr("%n places", "", root._placeRows().length) : ""
            openerExpanded: root.placesOpen
            trailing: qsTr("browse")
            trailingClickable: true
            trailingKey: "ctrl+o"

            // Field's own `Keys.onReturnPressed` already marks the key accepted (it emits
            // `accepted()` either way), so `enter` reaches the sheet only through here. On a
            // place, the first enter fills it in and checks it; the next one moves.
            onAccepted: {
                if (root.placesOpen && placeList.currentRow !== null) {
                    root._fillPlace();
                    return;
                }
                root.placesOpen = false;
                root.triggerPrimary();
            }
            onShortcutPressed: key => {
                if (key === Qt.Key_Down) {
                    if (root.placesOpen)
                        placeList.move(1);
                    else
                        root._openPlaces();
                } else if (key === Qt.Key_Up) {
                    placeList.move(-1);
                } else if (key === Qt.Key_Tab) {
                    root._fillPlace();
                } else if (key === Qt.Key_Escape) {
                    root.placesOpen = false;
                }
            }
            onTextChanged: {
                replan.restart();
                if (root._filling)
                    return;
                // Typing over a pick makes the line under the field stale.
                if (root._pick.path !== undefined && root._pick.path !== pathField.text)
                    root._pick = ({});
                if (root.selection && pathField.editing)
                    root._openPlaces();
            }
            onOpenerClicked: {
                pathField.focusInput();
                if (root.placesOpen)
                    root.placesOpen = false;
                else
                    root._openPlaces();
            }
            onTrailingClicked: root.browse()

            Keys.onPressed: event => {
                if ((event.modifiers & Qt.ControlModifier) !== 0 && event.key === Qt.Key_O) {
                    root.browse();
                    event.accepted = true;
                }
            }
        }

        PickList {
            id: placeList

            Layout.fillWidth: true
            hints: [{
                    "key": "↑↓",
                    "label": qsTr("pick")
                }, {
                    "key": "enter",
                    "label": qsTr("fill in")
                }, {
                    "key": "esc",
                    "label": qsTr("close")
                }, {
                    "key": "ctrl+o",
                    "label": qsTr("browse…")
                }]
            visible: root.placesOpen && root.selection

            onPicked: root._fillPlace()
        }

        BrowsedPathLine {
            id: browsedLine

            objectName: "moveBrowsedLine"
            pick: root._pick
            serverName: App.serverName
        }
    }

    // L5: torrents from several save paths go into one folder, or keep their places.
    SharedChoice {
        id: ruleChoice

        objectName: "moveRuleChoice"
        rows: {
            const count = root.moveHashes().length;
            const path = pathField.text.trim();
            return [{
                        "title": qsTr("into one folder"),
                        "detail": qsTr("all %1 go into %2").arg(count).arg(path)
                    }, {
                        "title": root.plan.keepText ?? qsTr("keep their places"),
                        "detail": qsTr("each goes to the same place below %1").arg(path)
                    }];
        }
        visible: root.selection && root.plan.ruleOffered === true

        onChoiceChanged: {
            const rule = ruleChoice.choice === 1 ? "keepPlaces" : "oneFolder";
            if (rule === root.rule)
                return;
            root.rule = rule;
            root._readPlan();
        }
    }

    Label {
        Layout.fillWidth: true
        dim: true
        objectName: "moveExplain"
        size: "small"
        text: root.plan.explain ?? ""
        // Q1: one torrent's field says it already.
        visible: text.length > 0 && !ruleChoice.visible && !(root.selection && root.moveHashes().length === 1)
        wrapMode: Text.Wrap
    }

    // Every save path and where it goes (M4; a selection's when it keeps their places, L5, or
    // moves back, L11).
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space(2)
        visible: (root.plan.pathRows ?? []).length > 0

        Repeater {
            model: root.plan.pathRows ?? []

            RowLayout {
                id: pathRow

                required property var modelData

                Accessible.name: qsTr("%1 to %2, %3").arg(pathRow.modelData.from).arg(pathRow.modelData.to).arg(pathRow.modelData.count)
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                spacing: Theme.space(8)

                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    elide: Text.ElideLeft
                    size: "small"
                    text: pathRow.modelData.from
                }

                Label {
                    dim: true
                    size: "small"
                    text: "→"
                }

                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    bright: true
                    elide: Text.ElideLeft
                    size: "small"
                    text: pathRow.modelData.to
                }

                Label {
                    Layout.preferredWidth: Theme.space(40)
                    dim: true
                    horizontalAlignment: Text.AlignRight
                    size: "small"
                    text: String(pathRow.modelData.count)
                }
            }
        }
    }

    // #28: who else uses these files, and whether they move too.
    SharedDataLine {
        mode: "move"
        selectionCount: root.hashes.length
        shared: root._shared
        visible: root.asking
    }

    SharedChoice {
        id: sharedChoice

        rows: {
            const others = (root._shared.outside ?? []).length;
            return [{
                        "title": root.hashes.length === 1 && others === 1 ? qsTr("move both") : qsTr("move all %1").arg(
                                                                                root.hashes.length + others),
                        "detail": qsTr("they keep sharing one copy in the new folder")
                    }, {
                        "title": root.hashes.length === 1 ? qsTr("move only this one") : qsTr("move only these %1").arg(
                                                                root.hashes.length),
                        "detail": others === 1 ? qsTr("the other one loses its files") : qsTr(
                                                     "the other %1 lose their files").arg(others)
                    }];
        }
        visible: root.asking

        onChoiceChanged: root._readPlan()
    }

    // A red line about the whole job turns the move off and says why (S1).
    Label {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        color: Theme.red
        objectName: "moveBlocker"
        size: "small"
        text: root.plan.blocker ?? ""
        visible: text.length > 0
        wrapMode: Text.Wrap
    }

    // L3: a selection's one green line needs no heading.
    Label {
        Layout.topMargin: Theme.space(6)
        text: qsTr("before anything moves")
        tracked: true
        visible: (root.plan.checks ?? []).length > (root.mode === "folder" ? 0 : 1)
    }

    Repeater {
        model: root.plan.checks ?? []

        MoveCheckLine {
            required property var modelData

            Layout.fillWidth: true
            line: modelData
            tone: root._kindColor(modelData.kind)

            onShowRequested: hashes => {
                root.close();
                root.showRequested(hashes);
            }
        }
    }

    Label {
        Layout.topMargin: Theme.space(6)
        text: qsTr("files no torrent owns · a dry run")
        tracked: true
        visible: root.mode === "folder"
    }

    Repeater {
        model: root.mode === "folder" ? (root.plan.loose ?? {}).rows ?? [] : []

        MoveCheckLine {
            required property var modelData

            Layout.fillWidth: true
            line: modelData
            linkText: modelData.kind === "loose" || modelData.kind === "warn" || modelData.kind === "unknown"
                      ? qsTr("list ▸") : ""
            tone: root._kindColor(modelData.kind)

            onLinkActivated: root.looseListRequested(root.folder)
        }
    }

    Toggle {
        checked: root.carry
        objectName: "moveCarry"
        text: qsTr("carry the %1 files after the torrents").arg((root.plan.loose ?? {}).count ?? 0)
        visible: root.mode === "folder" && root.plan.carryOffered === true

        onToggled: {
            root._carryTouched = true;
            root.carry = checked;
            root._readPlan();
        }
    }
}
