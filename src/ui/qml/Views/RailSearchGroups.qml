pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The rail on the search face (board 52 D5s a · D20 a, DECISIONS C34; settles C32 D5's search
// half), as the log's rail holds its levels (RailLogGroups):
//
// - **plugins**: every plugin installed on the server, a tick for whether the next search asks
//   it, then how it answered this one, with the plugins' line's lamp and words (a count, "…"
//   asking, slow, failed, skipped). A click ticks or unticks it, and a search on show asks
//   again (SearchView.toggleAsked). A plugin turned off in Settings is dim, without a tick,
//   "off"; a click opens the plugins' pane on it (C34's accepted call). The foot row, "install a
//   plugin…" (D20 a), opens the pane with its install field focused.
// - **type** (C34 assumed: the bar's word, not the board's "kind"): any, or one of the types the
//   plugins that are on know; one pick, which asks again.
// - **recent**: this server's searches this session (SearchView.recent, in memory only), the one
//   on show marked "now"; a click asks it again.
//
// The rows answer the pointer; the keys stay the search's own (the bar and the rows), so none is
// a stop of the rail's cursor. The groups fold as the rail's do, under `searchPlugins`,
// `searchType` and `searchRecent` (`groupToggled`).
Column {
    id: root

    required property SearchView view
    property ThemeInk block: Theme.on.window
    property var collapsedGroups: []

    signal groupToggled(string group)
    /// The hover label of a row whose name is cut, as the rail's others have.
    signal nameTip(RailRow row, bool hovering, string full)

    readonly property real headPadding: pluginsGroup.headPadding
    readonly property SearchPlugins plugins: root.view.plugins
    /// How many plugins the next search asks: every one that is on, or the pick's.
    readonly property int askedCount: {
        // Read first: the names below come through calls the binding can't follow.
        const on = root.plugins.enabledCount;
        if (root.view.pluginPick.length === 0)
            return on;
        return root.view.enabledNames().filter(name => root.view.isAsked(name)).length;
    }
    readonly property var typeRows: [
        {
            "id": "all",
            "name": qsTr("any")
        }
    ].concat(root.plugins.types)

    /// A plugin's answer in the rail: `{text, color, lamp, pulse}` (board 52 `.pa`), or text ""
    /// while this search didn't ask it.
    function answer(name: string, on: bool): var {
        if (!on)
            return {
                "text": qsTr("off"),
                "color": Theme.dimForeground,
                "lamp": false,
                "pulse": false
            };
        const row = root.view.answerOf(name);
        if (row === null)
            return {
                "text": "",
                "color": Theme.dimForeground,
                "lamp": false,
                "pulse": false
            };
        switch (row.state) {
        case "answered":
            return {
                "text": String(row.count),
                "color": Theme.green,
                "lamp": true,
                "pulse": false
            };
        case "failed":
            return {
                "text": qsTr("failed"),
                "color": Theme.yellow,
                "lamp": true,
                "pulse": false
            };
        case "slow":
            return {
                "text": qsTr("slow"),
                "color": Theme.yellow,
                "lamp": true,
                "pulse": true
            };
        case "stopped":
            return {
                "text": qsTr("stopped"),
                "color": Theme.dimForeground,
                "lamp": false,
                "pulse": false
            };
        case "skipped":
            return {
                "text": qsTr("skipped"),
                "color": Theme.dimForeground,
                "lamp": false,
                "pulse": false
            };
        }
        // Asking, or queued behind the four at once (SR1).
        return {
            "text": qsTr("…"),
            "color": Theme.accent,
            "lamp": true,
            "pulse": true
        };
    }

    spacing: Theme.space(14) - root.headPadding

    RailGroup {
        id: pluginsGroup

        block: root.block
        collapsed: root.collapsedGroups.indexOf("searchPlugins") >= 0
        count: root.plugins.count
        // Board 52 `.hc`: asked of installed.
        countText: root.plugins.count > 0 && !pluginsGroup.collapsed ? qsTr("%1 of %2").arg(
                                                                           root.askedCount).arg(
                                                                           root.plugins.count) : ""
        label: qsTr("plugins")
        objectName: "railSearchPlugins"
        width: root.width

        onToggled: root.groupToggled("searchPlugins")

        Repeater {
            model: root.plugins

            RailRow {
                id: pluginRow

                required property int index
                required property string name
                required property string fullName
                required property bool pluginOn

                readonly property var said: root.answer(pluginRow.name, pluginRow.pluginOn)

                block: root.block
                countColor: pluginRow.said.color
                countText: pluginRow.said.text
                dimText: !pluginRow.pluginOn || !pluginRow.ticked
                lamp: pluginRow.said.lamp
                lampColor: pluginRow.said.color
                lampPulse: pluginRow.said.pulse && !App.reducedMotion
                objectName: "railSearchPlugin_" + pluginRow.name
                text: pluginRow.fullName
                // C34: off in Settings, no tick at all; on, ticked while asked.
                tick: pluginRow.pluginOn
                ticked: pluginRow.pluginOn && root.view.isAsked(pluginRow.name)
                visible: !pluginsGroup.collapsed
                width: root.width

                Accessible.description: pluginRow.pluginOn ? [pluginRow.ticked ? qsTr("asked") :
                                                                                 qsTr("not asked"),
                                                              pluginRow.said.text].filter(part
                                                                                          => part.length
                                                                                             > 0).join(
                                                                 " · ") : qsTr(
                                                                 "turned off in Settings")

                onClicked: {
                    if (pluginRow.pluginOn)
                        root.view.toggleAsked(pluginRow.name);
                    else
                        root.view.pluginPaneRequested(pluginRow.name);
                }
                onHoverChanged: hovering => root.nameTip(pluginRow, hovering, pluginRow.fullName)
            }
        }

        // D20 a: the way to install one, at the group's foot.
        RailRow {
            block: root.block
            glyph: Glyphs.plus
            glyphColor: Theme.accent
            objectName: "railSearchInstall"
            text: qsTr("install a plugin…")
            // Board 52 `.add2`: the row and its glyph in the accent, as + add.
            tickColor: Theme.accent
            toneName: true
            visible: !pluginsGroup.collapsed
            width: root.width

            onClicked: root.view.pluginsInstallRequested()
        }
    }

    RailGroup {
        id: typeGroup

        block: root.block
        collapsed: root.collapsedGroups.indexOf("searchType") >= 0
        count: root.typeRows.length
        label: qsTr("type")
        objectName: "railSearchType"
        visible: root.plugins.enabledCount > 0
        width: root.width

        onToggled: root.groupToggled("searchType")

        Repeater {
            model: root.typeRows

            RailRow {
                id: typeRow

                required property var modelData

                block: root.block
                glyph: Glyphs.middot
                objectName: "railSearchType_" + typeRow.modelData.id
                selected: root.view.category === typeRow.modelData.id
                text: typeRow.modelData.name
                visible: !typeGroup.collapsed
                width: root.width

                onClicked: root.view.pickType(typeRow.modelData.id)
            }
        }
    }

    RailGroup {
        id: recentGroup

        block: root.block
        collapsed: root.collapsedGroups.indexOf("searchRecent") >= 0
        count: root.view.recent.length
        label: qsTr("recent")
        objectName: "railSearchRecent"
        visible: root.view.recent.length > 0
        width: root.width

        onToggled: root.groupToggled("searchRecent")

        Repeater {
            model: root.view.recent

            RailRow {
                id: recentRow

                required property int index
                required property string modelData

                block: root.block
                countText: root.view.search.state !== "idle" && root.view.search.pattern
                           === recentRow.modelData ? qsTr("now") : ""
                glyph: Glyphs.viewSearch
                objectName: "railSearchRecent_" + recentRow.index
                text: recentRow.modelData
                visible: !recentGroup.collapsed
                width: root.width

                onClicked: root.view.askRecent(recentRow.modelData)
                onHoverChanged: hovering => root.nameTip(recentRow, hovering, recentRow.modelData)
            }
        }
    }
}
