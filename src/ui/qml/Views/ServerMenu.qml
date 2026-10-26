pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// A server's menu: a right-click on its row in the rail, or the menu key on it (board 51 D4 a,
// DECISIONS C32). On the current server: its alternative speeds, stop all, start all; on
// another, "switch to it" first. Both then edit…, test the connection, open its web UI, copy
// its address, and remove…, which asks first (Settings' confirm). Its head is the server's
// name over its address, its version or when it was last seen, and its torrents.
//
// It runs nothing itself: `activated` carries "switch", a C13 id ("alt-speed", "stop-all",
// "start-all", as the omaseed menu's), "edit", "test", "web", "copy" or "remove", and MainView
// answers for `serverId` (`serverMenuAction`).
Menu {
    id: root

    /// The server the menu is about.
    property string serverId: ""
    /// `App.appMenuRows`, for the alternative speeds' row as the omaseed menu words it.
    property var sharedRows: []
    /// docs/31: the alternative set's kind, "burst", "turtle" or "alt"; the row's glyph.
    property string altKind: "alt"
    /// Bumped by the host when the server list changes, so the head reads it again.
    property int revision: 0

    readonly property var server: root.revision >= 0 && root.serverId.length > 0
                                  ? App.serverList.serverMap(root.serverId) : ({})
    /// The rail's lit server, the one omaseed shows.
    readonly property bool current: root.server.isDefault === true

    function _altRow(): var {
        const row = root.sharedRows.find(r => r.id === "alt-speed");
        return {
            "text": row !== undefined ? row.label : qsTr("alternative speeds"),
            "glyph": root.altKind === "burst" ? Glyphs.burst : Glyphs.altSpeed,
            "key": "A",
            "action": "alt-speed",
            "checked": row !== undefined && row.checked === true
        };
    }

    headingTitle: root.server.name ?? ""
    heading: {
        const parts = [root.server.urlText ?? root.server.url ?? ""];
        if (root.current && (root.server.versionText ?? "").length > 0)
            parts.push(root.server.versionText);
        else if (!root.current && (root.server.lastSeen ?? "").length > 0)
            parts.push(qsTr("last seen %1").arg(root.server.lastSeen));
        if ((root.server.countLabel ?? "").length > 0)
            parts.push(root.server.countLabel);
        return parts.filter(part => part.length > 0).join(" · ");
    }
    items: {
        const top = root.current ? [
                                       {
                                           "text": qsTr("the current server"),
                                           "glyph": Glyphs.check,
                                           "disabled": true
                                       },
                                       root._altRow(),
                                       {
                                           "text": qsTr("stop all"),
                                           "glyph": Glyphs.stop,
                                           "action": "stop-all"
                                       },
                                       {
                                           "text": qsTr("start all"),
                                           "glyph": Glyphs.start,
                                           "action": "start-all"
                                       }
                                   ] : [
                                       {
                                           "text": qsTr("switch to it"),
                                           "glyph": Glyphs.arrowRight,
                                           "note": qsTr("enter"),
                                           "action": "switch"
                                       }
                                   ];
        return top.concat([
                              {
                                  "separator": true
                              },
                              {
                                  "text": qsTr("edit…"),
                                  "glyph": Glyphs.renameRow,
                                  "action": "edit"
                              },
                              {
                                  "text": qsTr("test the connection"),
                                  "glyph": Glyphs.recheck,
                                  "action": "test"
                              },
                              {
                                  "text": qsTr("open its web UI"),
                                  "glyph": Glyphs.host,
                                  "action": "web"
                              },
                              {
                                  "text": qsTr("copy its address"),
                                  "glyph": Glyphs.copy,
                                  "action": "copy"
                              },
                              {
                                  "separator": true
                              },
                              {
                                  "text": qsTr("remove…"),
                                  "glyph": Glyphs.trash,
                                  "action": "remove",
                                  "danger": true
                              }
                          ]);
    }
    menuWidth: Theme.space(300)
    objectName: "serverMenu"
}
