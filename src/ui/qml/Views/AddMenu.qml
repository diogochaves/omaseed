pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// "+ add ▾"'s menu (board 49 `menuHtml("add")`, DECISIONS C24 H17, C27 CR1): add a torrent or
// a magnet (a), create a torrent from a folder (ctrl+n), and search (ctrl+f). Create shows
// only on a server that can create (C27: no entry otherwise), search only where it is offered
// (C25 K30). `activated` carries "add", "create" or "search"; the window runs them. Its form
// is the kit Menu's, its width `Theme.addMenuWidth`.
Menu {
    id: root

    /// The server can create a torrent (App.createOffered).
    property bool createShown: true
    /// The server has search plugins to offer (App.searchOffered).
    property bool searchShown: true

    dimmed: false
    items: {
        const rows = [
                  {
                      "text": qsTr("add a torrent or magnet…"),
                      "glyph": Glyphs.add,
                      "key": "a",
                      "action": "add"
                  }
              ];
        if (root.createShown)
            rows.push({
                          "text": qsTr("create a torrent from a folder…"),
                          "glyph": Glyphs.create,
                          "key": "ctrl+n",
                          "action": "create"
                      });
        if (root.searchShown)
            rows.push({
                          "separator": true
                      }, {
                          "text": qsTr("search…"),
                          "glyph": Glyphs.viewSearch,
                          "key": "ctrl+f",
                          "action": "search"
                      });
        return rows;
    }
    menuWidth: Theme.addMenuWidth
    objectName: "addMenu"
}
