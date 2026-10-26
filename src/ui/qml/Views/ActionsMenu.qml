pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The torrent actions menu of spec v2 §3 Fig. 11 (`.menu`), a thin `Kit/Menu` skin: the
// caller supplies which hashes the menu acts on and whether the cursor torrent is stopped
// (so the first row reads "start" or "stop"), MainView's `torrentAction()` gets the result
// through `activated`.
//
// DECISIONS C24 H9, as board 46 `menuHtml()` draws it (#201 F15): every row an icon, verbs
// grouped, keys right, delete last. The groups are board 46's: run (stop, retry, recheck,
// reannounce) · place (move…, category, tags, rename) · transfer (limits…, transfer ›,
// queue ›) · reach (open folder, copy ›) · delete. "transfer ›" holds force start and the
// four options (checked for the cursor torrent), "queue ›" queue up and down, "copy ›" the
// magnet, hash and name; their keys run them from the top as before (Kit/Menu). `groups` is
// that order as data, for whatever else draws these actions (the action bar, C32 D17). The
// head (the name over its state, or Q24's "N selected") is MainView's.
Menu {
    id: root

    /// Whether the cursor torrent (the row the menu was opened on) is currently stopped —
    /// decides the first row's text between "start" and "stop".
    property bool cursorStopped: false
    /// The cursor torrent's row (App.torrentRow), for the docs/12 G4 option rows' checks:
    /// each row turns its option to the other side of the cursor torrent's, on the whole
    /// selection.
    property var cursorRow: ({})
    /// The selection bar's ⋯ (docs/17 M9.5): the actions whose buttons folded away at this
    /// width come first, above a separator, and leave their usual place. Empty for `enter`
    /// and right-click, which get the menu as Fig. 11 draws it.
    property var leadActions: []

    /// A docs/12 G4 option: checked while the cursor torrent has it on.
    function _option(text: string, glyph: string, key: string, action: string, on: bool): var {
        return {
            "text": text,
            "glyph": glyph,
            "key": key,
            "action": action,
            "checked": on
        };
    }

    /// `all` with `leadActions`' rows moved to the top, in the menu's own order (out of their
    /// submenus too; a submenu left empty goes), and the separators they leave behind tidied
    /// (none first, last or twice in a row).
    function _arrange(all: var): var {
        if (root.leadActions.length === 0)
            return all;
        const leads = item => item.action !== undefined && root.leadActions.indexOf(item.action) >= 0;
        const lead = [];
        const rest = [];
        for (const item of all) {
            if (leads(item)) {
                lead.push(item);
                continue;
            }
            if (item.submenu !== undefined) {
                const kept = item.submenu.filter(child => !leads(child));
                for (const child of item.submenu) {
                    if (leads(child))
                        lead.push(child);
                }
                if (kept.length === 0)
                    continue;
                rest.push(Object.assign({}, item, {
                                            "submenu": kept
                                        }));
                continue;
            }
            if (item.separator !== true || (rest.length > 0
                                            && rest[rest.length - 1].separator !== true))
                rest.push(item);
        }
        while (rest.length > 0 && rest[rest.length - 1].separator === true)
            rest.pop();
        return lead.length === 0 ? rest : lead.concat([{
                                                           "separator": true
                                                       }], rest);
    }

    /// DECISIONS C20 (#107): an errored cursor torrent gets "retry" under its stop, since `p`
    /// stops it; nothing for the rest.
    function _retry(): var {
        return root.cursorRow.stateText === "error" ? [{
                                                           "text": qsTr("retry"),
                                                           "glyph": Glyphs.retry,
                                                           "key": "e",
                                                           "action": "retry"
                                                       }] : [];
    }

    /// Board 46's groups, in order: `[{name, items}]`, each row as Kit/Menu draws it.
    readonly property var groups: [
        {
            "name": "run",
            "items": [
                {
                    "text": root.cursorStopped ? qsTr("start") : qsTr("stop"),
                    "glyph": root.cursorStopped ? Glyphs.start : Glyphs.stop,
                    "key": "p",
                    "action": "stopStart"
                }].concat(root._retry(), [
                {
                    "text": qsTr("recheck"),
                    "glyph": Glyphs.recheck,
                    "key": "c",
                    "action": "recheck"
                },
                {
                    "text": qsTr("reannounce"),
                    "glyph": Glyphs.reannounce,
                    "key": "R",
                    "action": "reannounce"
                }])
        },
        {
            "name": "place",
            "items": [
                {
                    "text": qsTr("move…"),
                    "glyph": Glyphs.location,
                    "key": "m",
                    "action": "location"
                },
                {
                    "text": qsTr("category…"),
                    "glyph": Glyphs.category,
                    "key": "C",
                    "action": "category"
                },
                {
                    "text": qsTr("tags…"),
                    "glyph": Glyphs.tag,
                    "key": "t",
                    "action": "tags"
                },
                {
                    "text": qsTr("rename…"),
                    "glyph": Glyphs.renameRow,
                    "key": "r",
                    "action": "rename"
                }
            ]
        },
        {
            "name": "transfer",
            "items": [
                {
                    "text": qsTr("limits…"),
                    "glyph": Glyphs.limits,
                    "key": "L",
                    "action": "limits"
                },
                {
                    "text": qsTr("transfer"),
                    "glyph": Glyphs.more,
                    "action": "transfer",
                    "submenu": [
                        {
                            "text": qsTr("force start"),
                            "glyph": Glyphs.forceStart,
                            "key": "P",
                            "action": "forceStart"
                        },
                        root._option(qsTr("sequential download"), Glyphs.sequential, "s",
                                     "sequential", root.cursorRow.seqDl === true),
                        root._option(qsTr("first / last pieces first"), Glyphs.firstLast, "f",
                                     "firstLast", root.cursorRow.firstLastPiecePrio === true),
                        root._option(qsTr("super seeding"), Glyphs.superSeeding, "S",
                                     "superSeeding", root.cursorRow.superSeeding === true),
                        root._option(qsTr("automatic management"), Glyphs.autoManagement, "a",
                                     "autoManagement", root.cursorRow.autoTmm === true)
                    ]
                },
                {
                    "text": qsTr("queue"),
                    "glyph": Glyphs.queue,
                    "action": "queue",
                    "submenu": [
                        {
                            "text": qsTr("queue up"),
                            "glyph": Glyphs.queueUp,
                            "key": "[",
                            "action": "queueUp"
                        },
                        {
                            "text": qsTr("queue down"),
                            "glyph": Glyphs.queueDown,
                            "key": "]",
                            "action": "queueDown"
                        }
                    ]
                }
            ]
        },
        {
            "name": "reach",
            "items": [
                {
                    "text": qsTr("open folder"),
                    "glyph": Glyphs.openFolder,
                    "key": "o",
                    "action": "openFolder"
                },
                {
                    "text": qsTr("copy"),
                    "glyph": Glyphs.copy,
                    "action": "copy",
                    "submenu": [
                        {
                            "text": qsTr("copy magnet"),
                            "glyph": Glyphs.copyMagnet,
                            "key": "y",
                            "action": "copyMagnet"
                        },
                        {
                            "text": qsTr("copy hash"),
                            "glyph": Glyphs.copyHash,
                            "key": "Y",
                            "action": "copyHash"
                        },
                        {
                            "text": qsTr("copy name"),
                            "glyph": Glyphs.copyName,
                            "key": "n",
                            "action": "copyName"
                        }
                    ]
                }
            ]
        },
        {
            "name": "delete",
            "items": [
                {
                    "text": qsTr("delete…"),
                    "glyph": Glyphs.trash,
                    "key": "d",
                    "action": "delete",
                    "danger": true
                }
            ]
        }
    ]

    menuWidth: Theme.space(280)
    submenuWidth: Theme.space(260)
    items: root._arrange(root._rows(root.groups))

    /// `groups`' rows in order, a separator between two groups.
    function _rows(groups: var): var {
        const out = [];
        for (const group of groups) {
            if (out.length > 0)
                out.push({
                             "separator": true
                         });
            for (const item of group.items)
                out.push(item);
        }
        return out;
    }
}
