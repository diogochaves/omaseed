pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The menus of search's bar (board 48 `sbar()`, DECISIONS C26 SR6), in the kit Menu's form:
// plugins ▾ (every plugin that is on, or a pick; each with its types), type ▾ (any, then the
// types the plugins that are on know, in their own words), sort ▾ and, narrow, filters ▾
// (plugins…, type…, hide dead). `picked` carries "all", "p:NAME", "t:ID", "s:ORDER",
// "hideDead", or "menu:plugins" / "menu:type" from filters ▾.
Menu {
    id: root

    /// "plugins", "type", "sort" or "filters".
    property string which: "plugins"
    property var plugins: null
    property var pluginPick: []
    property string category: "all"
    property string order: "seeds"
    property bool hideDead: false

    signal picked(string action)

    function _pluginRows(): var {
        const rows = [
                  {
                      "text": qsTr("every plugin that is on"),
                      "action": "all",
                      "checked": root.pluginPick.length === 0
                  },
                  {
                      "separator": true
                  }
              ];
        const model = root.plugins;
        for (let i = 0; model && i < model.count; ++i) {
            const index = model.index(i, 0);
            if (!model.data(index, SearchPlugins.EnabledRole))
                continue;
            const name = model.data(index, SearchPlugins.NameRole);
            rows.push({
                          "text": model.data(index, SearchPlugins.FullNameRole),
                          "action": "p:" + name,
                          "note": model.data(index, SearchPlugins.TypesTextRole),
                          "checked": root.pluginPick.length === 0 || root.pluginPick.indexOf(name)
                                     >= 0
                      });
        }
        return rows;
    }

    function _typeRows(): var {
        const rows = [
                  {
                      "text": qsTr("any type"),
                      "action": "t:all",
                      "checked": root.category === "all"
                  }
              ];
        for (const type of (root.plugins ? root.plugins.types : []))
            rows.push({
                          "text": type.name,
                          "action": "t:" + type.id,
                          "checked": root.category === type.id
                      });
        return rows;
    }

    function _sortRows(): var {
        return [
                    {
                        "text": qsTr("most seeds"),
                        "action": "s:seeds",
                        "note": qsTr("then newest"),
                        "checked": root.order === "seeds"
                    },
                    {
                        "text": qsTr("newest"),
                        "action": "s:newest",
                        "checked": root.order === "newest"
                    },
                    {
                        "text": qsTr("size"),
                        "action": "s:size",
                        "note": qsTr("biggest first"),
                        "checked": root.order === "size"
                    },
                    {
                        "text": qsTr("name"),
                        "action": "s:name",
                        "checked": root.order === "name"
                    }
                ];
    }

    function _filterRows(): var {
        return [
                    {
                        "text": qsTr("plugins…"),
                        "action": "menu:plugins",
                        "note": root.pluginPick.length === 0 ? qsTr("all that are on") : qsTr(
                                                                   "%1 picked").arg(
                                                                   root.pluginPick.length)
                    },
                    {
                        "text": qsTr("type…"),
                        "action": "menu:type",
                        "note": root.category === "all" ? qsTr("any") : root.category
                    },
                    {
                        "text": qsTr("hide dead"),
                        "action": "hideDead",
                        "checked": root.hideDead
                    }
                ];
    }

    dimmed: false
    footer: root.which === "plugins" || root.which === "type" ? qsTr("A change asks again.") : ""
    items: root.which === "type" ? root._typeRows() : root.which === "sort" ? root._sortRows() :
                                                                              root.which
                                                                              === "filters"
                                                                              ? root._filterRows() :
                                                                                root._pluginRows()
    menuWidth: Theme.searchMenuWidth
    objectName: "searchBarMenu"

    onActivated: action => {
        root.close();
        root.picked(action);
    }
    onRejected: root.close()
}
