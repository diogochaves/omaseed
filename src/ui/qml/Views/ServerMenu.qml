pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// A server's menu: a right-click on its row in the rail, or the menu key on it (board 51 D4 a,
// DECISIONS C32). On the current server: its alternative speeds, then pause torrents, or resume
// torrents while a pause is held (DECISIONS C36 M10, settled 2026-11-05); on another, "switch
// to it" first. Both then edit…, test the connection, open its web UI, copy its address, and
// remove…, which asks first (Settings' confirm). Its head is the server's name over its
// address, its version or when it was last seen, and its torrents.
//
// It runs nothing itself: `activated` carries "switch", a C13 id ("alt-speed", "pause",
// "resume", as the omaseed menu's), "edit", "test", "web", "copy" or "remove", and MainView
// answers for `serverId` (`serverMenuAction`).
Menu {
    id: root

    /// The server the menu is about.
    property string serverId: ""
    /// `App.appMenuRows`, for the alternative speeds' and the pause row as the omaseed menu
    /// words them.
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

    /// C36 M10: one row, "pause torrents", or "resume torrents" while a pause is held, as the
    /// omaseed menu has it; off while the server doesn't answer.
    function _pauseRow(): var {
        const row = root.sharedRows.find(r => r.id === "pause" || r.id === "resume");
        const resume = row !== undefined && row.id === "resume";
        return {
            "text": row !== undefined ? row.label : qsTr("pause torrents"),
            "glyph": resume ? Glyphs.start : Glyphs.stop,
            "action": resume ? "resume" : "pause",
            "disabled": row !== undefined && row.enabled === false
        };
    }

    headingTitle: root.server.name ?? ""
    heading: {
        const parts = [root.server.urlText ?? root.server.url ?? ""];
        // The API answers "v5.3.0"; board 51 prints "qBittorrent 5.3.0" for the current one,
        // C25 K11 its engine's name ("Transmission 4.1.3").
        const version = root.current ? App.globalState.serverVersion.replace(/^v/, "") : "";
        if (version.length > 0)
            parts.push(qsTr("%1 %2").arg(App.serverEngineName || "qBittorrent").arg(version));
        else if (!root.current && (root.server.lastSeen ?? "").length > 0)
            parts.push(qsTr("last seen %1").arg(root.server.lastSeen));
        // The rail's count, "10", read out: "10 torrents".
        const count = root.server.countLabel ?? "";
        if (/^\d+$/.test(count))
            parts.push(qsTr("%n torrents", "", parseInt(count, 10)));
        else if (count.length > 0)
            parts.push(count);
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
                                       root._pauseRow()
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
    // Board 51 draws 300; the head's "address · qBittorrent 5.3.0 · N torrents" wants more.
    menuWidth: Theme.space(376)
    objectName: "serverMenu"
}
