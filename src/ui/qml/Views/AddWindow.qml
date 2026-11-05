pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// The add window (board 54 W1 a, DECISIONS C35, #217): the add sheet in a small window of its
// own, for an add that comes from outside, so it opens on the workspace the user is on and the
// main window stays where it is. It hosts `AddFlow` (the add sheet, Fig. 10 over it) with
// palettes of its own: the category and tag palettes answer the chips (`openForChoice`), and
// the server palette the head's chip, ctrl+s and a down server's `switch`.
//
// The window is the sheet (W3): its size is the panel's, fixed (minimum = maximum, which
// Hyprland floats and centres with no rule), up to 80 % of the screen's height, and set again
// whenever the sheet grows or shrinks. The panel fills it, so the sheet's own margin falls
// outside the window and its scrim is never seen. The sheet's edge gives way to the window's
// (ARCHITECTURE rule 5): Hyprland's border, or the 1 px edge drawn here without it.
//
// The first title, `omaseed · add`, is fixed and never translated: the snippet's window rule
// matches it (W3). `transientParent` is null on purpose: a Window declared inside another
// becomes its transient child, which a compositor may place with the main window, on its
// workspace.
//
// Board 25's Fig. 10 shows in it as in the main window (W4): alone, it is the window, at its
// own size; over a sheet still holding the next source, it floats over that sheet.
//
// A hand-off while the window is up joins the sheet's list (`AddSheet.openList`) and the window
// maps again, which puts it on the workspace the user is on now (W7, board 54 check 2): a
// raise would fetch the user to wherever it was. `esc` goes to `cancel` first; `cancel`, and
// closing the window (super+w), drop the add and all waiting; an add already sent goes on
// (W8, C20). Every map waits for the sheet's height: a hidden window runs no polish, so its
// layouts still hold the last mapped size, and the window would flash at it for a frame.
//
// What only the main window can answer leaves as signals: `show it` (W4), a merge (W5's notice,
// #219), the empty state's search (C26: a search add stays in the main window) and the server
// palette's `manage…`, which opens Settings › Servers there (C35, settled 2026-11-05).
//
//     AddWindow {
//         onShowRequested: hash => …   // raise the main window on it
//         onMerged: hash => …
//         onSettingsRequested: id => …
//     }
//     addWindow.openList(uris, "handoff");
Window {
    id: root

    readonly property alias flow: flow
    readonly property alias addSheet: flow.addSheet
    readonly property alias duplicateSheet: flow.duplicateSheet
    readonly property alias categoryPalette: categoryPalette
    readonly property alias tagPalette: tagPalette
    readonly property alias serverPalette: serverPalette

    /// The overlay on top, the one `esc` answers, or null: a palette, then Fig. 10, then the
    /// sheet (MainView's `activeOverlay`, in this window's stack).
    readonly property var activeOverlay: {
        if (serverPalette.opened)
            return serverPalette;
        if (categoryPalette.opened)
            return categoryPalette;
        if (tagPalette.opened)
            return tagPalette;
        if (flow.duplicateSheet.opened)
            return flow.duplicateSheet;
        if (flow.addSheet.opened)
            return flow.addSheet;
        return null;
    }

    /// Fig. 10 with no sheet behind it: the window is Fig. 10 (W4).
    readonly property bool _figureAlone: flow.duplicateSheet.opened && !flow.addSheet.opened

    /// W3: the most the window grows to, 80 % of the screen's height. `Screen` is the screen the
    /// window is on, read again when it moves to another. A test sets its own.
    property real heightCap: Math.floor(0.8 * (Screen.height > 0 ? Screen.height : 1e6))
    /// The window's size: the panel's width, and its height with every row at its own height
    /// (`AddSheet.naturalHeight`, never from the window's own), Fig. 10's while it is taller;
    /// Fig. 10's own when it is alone.
    readonly property size sheetSize: Qt.size(
                                          Math.min(root._figureAlone ? flow.duplicateSheet.sheetWidth
                                                                     : flow.addSheet.sheetWidth,
                                                   Screen.desktopAvailableWidth > 0 ? Screen.desktopAvailableWidth : 1e6),
                                          Math.max(1, Math.ceil(Math.min(root.heightCap, root._figureAlone
                                                                         ? flow.duplicateSheet.panel.implicitHeight
                                                                         : Math.max(flow.addSheet.naturalHeight,
                                                                                    flow.duplicateSheet.opened
                                                                                    ? flow.duplicateSheet.panel.implicitHeight
                                                                                    : 0)))))

    /// Fig. 10's `show it`: both sheets have closed; the host shows the torrent in the main
    /// window, raising it on purpose (W4).
    signal showRequested(string hash)
    /// Fig. 10's merge has gone to App and the window is closing: W5's notice (#219).
    signal merged(string hash)
    /// The empty state's "search your plugins…": the main window's search.
    signal searchRequested(string text)
    /// The server palette's `manage…`: Settings › Servers on server `id`, in the main window.
    signal settingsRequested(string id)

    /// Open the sheet on `uris` (a hand-off, `from` as `AddSheet.openList` takes it) and map
    /// the window.
    function openList(uris: var, from: string): void {
        flow.addSheet.openList(uris, from);
        root._map();
    }

    /// Open the sheet on an empty field and map the window.
    function openBlank(): void {
        flow.addSheet.openBlank();
        root._map();
    }

    /// W8: `cancel`, and the window's close: the palettes, Fig. 10, the add and all waiting go,
    /// and the window with them. An add already out goes on: the sheet's answer only finds it
    /// closed (C20).
    function cancel(): void {
        serverPalette.close();
        categoryPalette.close();
        tagPalette.close();
        flow.duplicateSheet.close();
        flow.addSheet.dismiss();
        root._hideWhenClosed();
    }

    function _hideWhenClosed(): void {
        if (!flow.addSheet.opened && !flow.duplicateSheet.opened)
            root.hide();
    }

    /// Map the window on what the sheets now hold. Hidden, the window ran no polish since they
    /// changed, so its layouts and lists are polished here, deepest first, until the size holds:
    /// `show()` then maps at the sheet's height, never at the last one for a frame. Up but not
    /// active, it is hidden first, so it maps again on the workspace the user is on (W7).
    function _map(): void {
        if (!flow.addSheet.opened && !flow.duplicateSheet.opened)
            return;
        // A pass can make rows (a list's delegates) that the next one lays out.
        for (let pass = 0; pass < 4; ++pass) {
            // Numbers, not the size: a value type read from a property follows it.
            const width = root.sheetSize.width;
            const height = root.sheetSize.height;
            root._polish(flow);
            if (pass > 0 && root.sheetSize.width === width && root.sheetSize.height === height)
                break;
        }
        if (root.visible && !root.active)
            root.hide();
        root.show();
        root.requestActivate();
        root._askedSince = Date.now();
        activateAgain.restart();
    }

    /// How long, in ms, the window goes on asking for the keyboard after it maps, while it has
    /// not had it once (#218, #230). Tests shorten it.
    property int keyboardAskLimit: 30000
    /// True while the window is still asking (`activateAgain` runs).
    readonly property bool askingForKeyboard: activateAgain.running
    /// When `_map` last asked, the start of `keyboardAskLimit`.
    property double _askedSince: 0

    function _polish(item: Item): void {
        const children = item.children;
        for (let i = 0; i < children.length; ++i)
            root._polish(children[i]);
        item.ensurePolished();
    }

    title: "omaseed · add"
    transientParent: null
    color: Theme.background
    width: root.sheetSize.width
    height: root.sheetSize.height
    minimumWidth: root.sheetSize.width
    maximumWidth: root.sheetSize.width
    minimumHeight: root.sheetSize.height
    maximumHeight: root.sheetSize.height

    // W8: super+w is a cancel. The window hides itself rather than close, so a close never
    // counts as the app's last window.
    onClosing: close => {
        close.accepted = false;
        root.cancel();
    }

    // #218's box check: mapped from a bar popup (the widget's ⋯ `add torrent…`), the window
    // shows but never gets the keyboard: the popup holds it, and when it closes Hyprland gives
    // it back to the main window. Omarchy's menu does the same, held open as long as the user
    // reads it (#230). So the window asks again, every 100 ms, until it has had the keyboard
    // once, for up to `keyboardAskLimit`. A hand-off from another app is focused at the first
    // ask and stops this at once; once it has been active, a move elsewhere is the user's and
    // is never undone.
    Timer {
        id: activateAgain

        interval: 100
        repeat: true

        onTriggered: {
            if (!root.visible || root.active || Date.now() - root._askedSince >= root.keyboardAskLimit) {
                activateAgain.stop();
                return;
            }
            root.requestActivate();
        }
    }

    // The flow reaches the sheet's margin past every side, so the panel, centred in it, is the
    // whole window.
    AddFlow {
        id: flow

        addSheet.framed: false
        categoryPicker: categoryPalette
        // W5 (#219): an add from outside says it went in, from wherever the user is.
        notifiesAdds: true
        // Alone, Fig. 10 is the window and its edge the window's; over the sheet, it is framed.
        duplicateSheet.framed: flow.addSheet.opened
        focus: true
        height: root.height + 2 * Theme.sheetMargin
        tagPicker: tagPalette
        width: root.width + 2 * Theme.sheetMargin
        x: -Theme.sheetMargin
        y: -Theme.sheetMargin

        onMerged: hash => root.merged(hash)
        onSearchRequested: text => root.searchRequested(text)
        onShowRequested: hash => root.showRequested(hash)
        // docs/33 D14, D19: with one server there is nothing to switch to; the sheet's foot
        // says so, as the main window's footer does.
        onServerPaletteRequested: {
            if (App.serverCount > 1)
                serverPalette.openOnNext();
            else
                flow.addSheet.flashNote(qsTr("one server · add another in Settings › Servers"));
        }
    }

    CategoryPalette {
        id: categoryPalette

        anchors.fill: parent
        scrollHint: App.railScrollHint
        z: 1
    }

    TagPalette {
        id: tagPalette

        anchors.fill: parent
        scrollHint: App.railScrollHint
        z: 1
    }

    // Over the sheet, which opens it.
    ServerPalette {
        id: serverPalette

        anchors.fill: parent
        scrollHint: App.railScrollHint
        z: 2

        onManageRequested: id => root.settingsRequested(id)
    }

    // The 1 px window edge, as Main.qml draws it: only without Hyprland, whose border is the
    // edge when it is there.
    Rectangle {
        anchors.fill: parent
        border.color: root.active ? Theme.accent : Theme.muted
        border.width: Theme.edgeWidth
        color: "transparent"
        radius: Theme.cornerRadius
        visible: Theme.windowEdge
        z: 3
    }

    // W1, W8: the window goes when the sheets do.
    Connections {
        function onOpenedChanged(): void {
            root._hideWhenClosed();
        }

        target: flow.addSheet
    }

    Connections {
        function onOpenedChanged(): void {
            root._hideWhenClosed();
        }

        target: flow.duplicateSheet
    }
}
