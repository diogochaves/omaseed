pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Views

// The add sheet and what it calls on, for every window that adds (docs/54 §6, DECISIONS C35,
// #217): `MainView`, and the add window, which opens where the user is. One `AddSheet` (spec
// v2 §3 Fig. 5), board 25's Fig. 10 over it (`DuplicateSheet`), and the wiring between them
// and to App: an add goes to `App.addTorrent`, a merge to `App.mergeTrackers`, the `…` chips
// to the category and tag palettes in `openForChoice` mode.
//
// The palettes stay the host's (`categoryPicker`, `tagPicker`): the main window's also act on
// torrents, and a palette draws over everything in its own window. What only a host can
// answer leaves as a signal: `show it` (after the sheets have gone), the empty state's search,
// and the server palette, whose one-server note is the main window's footer.
//
// The duplicate sheet is drawn over the add sheet; the host places this item at the add
// sheet's height in its own stack.
//
//     AddFlow {
//         anchors.fill: parent
//         categoryPicker: categoryPalette
//         tagPicker: tagPalette
//         onShowRequested: hash => root.showTorrent(hash)
//     }
Item {
    id: flow

    /// The palette the category chip's `…` opens; it answers with `chosen(name)`.
    required property CategoryPalette categoryPicker
    /// The palette the tag chips' `…` opens; it answers with `chosen(tags)`.
    required property TagPalette tagPicker

    readonly property alias addSheet: addSheet
    readonly property alias duplicateSheet: duplicateSheet

    /// Fig. 10's `show it`: the add is given up and both sheets are closed; the host shows
    /// the library's torrent.
    signal showRequested(string hash)
    /// C24 H16: the empty state's "search your plugins…", and typed words no add takes.
    signal searchRequested(string text)
    /// docs/33 D14: the head's chip, ctrl+s in the sheet, a down server's `switch`.
    signal serverPaletteRequested
    /// Fig. 10's merge has gone to `App.mergeTrackers` and both sheets are closed: the add
    /// window's host sends board 54's W5 notice for it (W4 a).
    signal merged(string hash)

    AddSheet {
        id: addSheet

        anchors.fill: parent

        onAddRequested: request => App.addTorrent(request)
        onDuplicateFound: (duplicate, source) => duplicateSheet.openFor(duplicate, source)
        onSearchRequested: text => flow.searchRequested(text)
        // The `…` chips: T4.4's palettes, asked to answer instead of to act.
        onCategoryPaletteRequested: flow.categoryPicker.openForChoice()
        onTagPaletteRequested: flow.tagPicker.openForChoice(addSheet.tagChips.selected)
        onServerPaletteRequested: flow.serverPaletteRequested()
    }

    DuplicateSheet {
        id: duplicateSheet

        anchors.fill: parent
        z: 1

        // `show it` gives up on the add: the sheet behind this one goes away with it.
        onShowRequested: hash => {
            addSheet.dismiss();
            flow.showRequested(hash);
        }
        // docs/25 D3 a: `trackersMerged` then moves to the torrent's Trackers tab.
        onMergeRequested: (hash, urls) => {
            addSheet.dismiss();
            App.mergeTrackers(hash, urls);
            flow.merged(hash);
        }
    }

    Connections {
        function onChosen(name: string): void {
            addSheet.applyChosenCategory(name);
        }

        target: flow.categoryPicker
    }

    Connections {
        function onChosen(tags: var): void {
            addSheet.applyChosenTags(tags);
        }

        target: flow.tagPicker
    }
}
