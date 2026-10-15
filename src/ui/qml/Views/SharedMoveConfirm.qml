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
//     SharedMoveConfirm { anchors.fill: parent }   // CategoryPalette.sharedMoveFound → openFor()
ConfirmDialog {
    id: root

    /// The torrents the category was chosen for.
    property var hashes: []
    /// The category ("" is none) and whether the palette's `create` row made it.
    property string category: ""
    property bool create: false
    /// The torrents the change would move, and their `App.sharedData()`.
    property var _moved: []
    property var _shared: ({})

    readonly property var _outside: root._shared.outside ?? []
    /// The pick: 0 moves the sharers too, 1 only the chosen torrents.
    readonly property alias choice: sharedChoice.choice

    /// The torrents that get the category.
    function applyHashes(): var {
    return sharedChoice.choice === 0 ? root.hashes.concat(root._outside) : root.hashes;
}

/// Opens on `hashes` → `category`; `moved` are the ones whose files the change moves.
function openFor(hashes: var, category: string, create: bool, moved: var): void {
    root.hashes = hashes;
    root.category = category;
    root.create = create;
    root._moved = moved;
    // Not fresh: the palette has just fetched the file lists this check needs (#35).
    root._shared = App.sharedData(moved);
    sharedChoice.choice = 0;
    root.open();
}

cancelText: qsTr("cancel")
confirmText: sharedChoice.choice === 0 ? qsTr("move %1").arg(root.applyHashes().length) : qsTr(
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
message: root.category.length > 0 ? qsTr("Move to %1?").arg(root.category) : qsTr(
                                        "Clear the category?")
panelWidth: Theme.space(520)
subtitle: root.hashes.length === 1 ? qsTr("%1 · automatic management moves its files").arg(
                                         App.torrentRow(root.hashes[0]).name ?? "") : qsTr(
                                         "%1 torrents · automatic management moves their files").arg(
                                         root.hashes.length)

onAccepted: {
    const targets = root.applyHashes();
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
    function onSharedFilesLoaded(): void {
        if (root.opened)
            root._shared = App.sharedData(root._moved);
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
                "detail": others === 1 ? qsTr(
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
