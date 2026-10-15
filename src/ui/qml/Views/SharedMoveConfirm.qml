pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// #28 (docs/26 §4, D4 a): a category change with automatic management on moves the files to
// the category's folder. When a torrent outside the change uses the same data, the category
// palette hands the question here instead of applying it: the shared-data line, then "move
// both" (picked every time: the other torrents get the same category and automatic
// management, so the server moves them to the same folder) or "only these" (they lose their
// files). Asked only when something would actually move and something shares; otherwise the
// palette applies the category as before.
//
// #65: turning automatic management on moves the files to the category's folder too, so it
// asks the same question (`turnOnAutoManagement`): "move both" gives the sharers the same
// category and management. Like the palette (#35), a check that needs a no-root sharer's file
// lists waits for them.
//
//     SharedMoveConfirm { anchors.fill: parent }   // CategoryPalette.sharedMoveFound → openFor()
ConfirmDialog {
    id: root

    /// The torrents the category was chosen for.
    property var hashes: []
    /// The category ("" is none) and whether the palette's `create` row made it.
    property string category: ""
    property bool create: false
    /// #65: the question is turning automatic management on, not a category.
    property bool autoManagement: false
    /// The torrents the change would move, and their `App.sharedData()`.
    property var _moved: []
    property var _shared: ({})
    /// #65: turning-on requests waiting for the file lists their check needs, in the order
    /// they came: `[{hashes, moved}]`.
    property var _held: []

    readonly property var _outside: root._shared.outside ?? []
    /// The pick: 0 moves the sharers too, 1 only the chosen torrents.
    readonly property alias choice: sharedChoice.choice

    /// The torrents that get the category.
    function applyHashes(): var {
    return sharedChoice.choice === 0 ? root.hashes.concat(root._outside) : root.hashes;
}

/// Opens on `hashes` → `category`; `moved` are the ones whose files the change moves.
function openFor(hashes: var, category: string, create: bool, moved: var): void {
    root.autoManagement = false;
    root.hashes = hashes;
    root.category = category;
    root.create = create;
    root._moved = moved;
    // Not fresh: the palette has just fetched the file lists this check needs (#35).
    root._shared = App.sharedData(moved);
    sharedChoice.choice = 0;
    root.open();
}

/// #65: turns automatic management on for `hashes`, first asking, as a category change does,
/// when that moves files a torrent outside them uses.
function turnOnAutoManagement(hashes: var): void {
    const change = {
        "hashes": hashes,
        "moved": App.movedByAutoManagement(hashes)
    };
    if (change.moved.length === 0) {
        App.setAutoManagement(hashes, true);
        return;
    }
    const shared = App.sharedData(change.moved, true);
    if (shared.pending === true)
        root._held = root._held.concat([change]);
    else
        root._settle(change, shared);
}

/// A turning-on whose check has its answer: asked about when it moves a sharer's files,
/// otherwise done.
function _settle(change: var, shared: var): void {
    if ((shared.outside ?? []).length === 0) {
        App.setAutoManagement(change.hashes, true);
        return;
    }
    root.autoManagement = true;
    root.hashes = change.hashes;
    root.category = "";
    root.create = false;
    root._moved = change.moved;
    root._shared = shared;
    sharedChoice.choice = 0;
    root.open();
}

/// #65: the held turning-on requests whose lists are in, in the order they came: one still
/// waiting holds back the ones after it, and so does an open question (they go on once it
/// is answered).
function _settleHeld(): void {
    const still = [];
    for (const change of root._held) {
        const shared = still.length > 0 || root.opened ? null : App.sharedData(change.moved);
        if (shared === null || shared.pending === true)
            still.push(change);
        else
            root._settle(change, shared);
    }
    root._held = still;
}

/// #65: the category each sharer gets with "move both": the one of the torrent it shares
/// with (hash → category), left out where it already has it.
function _sharerCategories(): var {
    const categories = {};
    const seen = new Set();
    for (const entry of root._shared.sharing ?? []) {
        const category = App.torrentRow(entry.hash).category ?? "";
        for (const sharer of entry.sharers) {
            if (seen.has(sharer.hash))
                continue;
            seen.add(sharer.hash);
            if ((App.torrentRow(sharer.hash).category ?? "") !== category)
                categories[sharer.hash] = category;
        }
    }
    return categories;
}

cancelText: qsTr("cancel")
confirmText: sharedChoice.choice === 0 ? qsTr("move %1").arg(root.applyHashes().length) :
                                         root.autoManagement ? qsTr("turn on") : qsTr(
                                                                   "set category")

keyHandler: event => {
    if (sharedChoice.activeFocus)
        return false;
    if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
        sharedChoice.move(1);
        return true;
    }
    if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
        sharedChoice.move(-1);
        return true;
    }
    return false;
}
message: root.autoManagement ? qsTr("Turn on automatic management?") : root.category.length > 0 ?
                                                                     qsTr("Move to %1?").arg(
                                                                         root.category) : qsTr(
                                                                         "Clear the category?")
panelWidth: Theme.space(520)
subtitle: root.hashes.length === 1 ? qsTr("%1 · automatic management moves its files").arg(
                                         App.torrentRow(root.hashes[0]).name ?? "") : qsTr(
                                         "%1 torrents · automatic management moves their files").arg(
                                         root.hashes.length)

onOpenedChanged: {
    if (!root.opened && root._held.length > 0)
        Qt.callLater(root._settleHeld);
}
onAccepted: {
    const targets = root.applyHashes();
    if (root.autoManagement) {
        // The sharers take their partner's category first, so they land in the same folder.
        if (sharedChoice.choice === 0)
            App.setAutoManagementWithCategories(targets, root._sharerCategories());
        else
            App.setAutoManagement(targets, true);
        return;
    }
    if (root.create)
        App.createAndApplyCategory(targets, root.category);
    else
        App.applyCategory(targets, root.category);
    if (sharedChoice.choice !== 0)
        return;
    // The sharers follow only under automatic management: turn it on where it is off.
    const manual = root._outside.filter(hash => App.torrentRow(hash).autoTmm !== true);
    if (manual.length > 0)
        App.setAutoManagement(manual, true);
}

Connections {
    function onServerChanged(): void {
        // The held hashes were the last server's.
        root._held = [];
    }

    function onSharedFilesLoaded(): void {
        if (root.opened)
            root._shared = App.sharedData(root._moved);
        root._settleHeld();
    }

    target: App
}

SharedDataLine {
    mode: "move"
    selectionCount: root.hashes.length
    shared: root._shared
}

SharedChoice {
    id: sharedChoice

    rows: {
        const others = root._outside.length;
        return [
            {
                "title": root.hashes.length === 1 && others === 1 ? qsTr("move both") : qsTr(
                                                                        "move all %1").arg(
                                                                        root.hashes.length + others),
                "detail": root.autoManagement ? (others === 1 ? qsTr(
                                                                    "the other torrent gets the same category and moves with it") :
                                                                qsTr("the other %1 get the same category and move with it").arg(
                                                                    others)) : others === 1 ? qsTr(
                                                                                                  "the other torrent gets this category too and moves with it") :
                                                                                              qsTr("the other %1 get this category too and move with it").arg(
                                                                                                  others)
            },
            {
                "title": root.hashes.length === 1 ? qsTr("move only this one") : qsTr(
                                                        "move only these %1").arg(
                                                        root.hashes.length),
                "detail": others === 1 ? qsTr("the other one loses its files") : qsTr(
                                             "the other %1 lose their files").arg(others)
            }
        ];
    }
}
}
