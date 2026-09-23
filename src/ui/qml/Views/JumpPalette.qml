pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// `ctrl+k` (spec v2 §4 Fig. 12): jump to any torrent in the whole library, not only what the
// rail/search box currently show. Rows come from `App.jumpMatches()`, which searches name,
// hash (v1/v2) and tracker host over every torrent App knows about — the palette itself does
// no filtering, it only asks App for the next 50 rows on every keystroke.
//
// `enter` moves the table's cursor to the row, clearing the table's own filter first if that
// row would otherwise stay hidden. `tab` does the same and also asks for the row's actions
// menu (`actionsRequested`), the way Fig. 12's footer promises ("tab actions").
Palette {
    id: root

    /// The table whose cursor `enter`/`tab` move. Required so the palette can jump even when
    /// a filter is hiding the row.
    required property TorrentTable table

    counter: qsTr("%1 of %2").arg(App.jumpMatchCount(root.query)).arg(App.torrents.count)
    footerRight: qsTr("also matches hash and tracker")
    glyph: Glyphs.jump
    hints: [
        {
            "key": "↑↓",
            "label": qsTr("move")
        },
        {
            "key": "enter",
            "label": qsTr("jump")
        },
        {
            "key": "tab",
            "label": qsTr("actions")
        }
    ]
    items: {
        // `App.torrents.count` is a real NOTIFY-backed dependency, unlike the invokable
        // `jumpMatches()` call itself: without it this binding would keep whatever it saw
        // the moment the palette was created if `query` happened not to change afterwards
        // (every qmltestrunner file's tree is built before any test function runs).
        void App.torrents.count;
        return App.jumpMatches(root.query, 50);
    }

    /// Moves `table`'s cursor to `hash`, clearing the filter (and the status rail) first
    /// when the row is not currently visible.
    function jumpTo(hash: string): void {
        if (hash.length === 0 || root.table === null)
            return;
        if (App.filtered.rowOfHash(hash) < 0) {
            App.filtered.text = "";
            App.filtered.clearCategory();
            App.filtered.clearTag();
            App.filtered.clearTracker();
            App.filtered.statusFilter = TorrentFilterModel.All;
        }
        const row = App.filtered.rowOfHash(hash);
        if (row >= 0)
            root.table.moveCursorTo(row);
    }

    // Every jump starts from an empty query, like the other palettes: the last one's text
    // would otherwise stay in the row and the next name be typed after it.
    onOpenedChanged: {
        if (root.opened)
            root.query = "";
    }
    onActivated: (index, item) => {
        root.jumpTo(item.key);
        root.close();
    }
    onRejected: root.close()
    onSecondary: (index, item) => {
        const hash = item.key;
        root.jumpTo(hash);
        root.close();
        // T4.3 has no actions-menu signal on this branch yet; MainView's `Connections {
        // target: App }` block is where a future `showActionsRequested` handler lands.
        App.showActionsRequested(hash);
    }
}
