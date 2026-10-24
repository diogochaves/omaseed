pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The torrent actions menu of spec v2 §3 Fig. 11 (`.menu`), a thin `Kit/Menu` skin: the
// caller supplies which hashes the menu acts on and whether the cursor torrent is stopped
// (so the first row reads "start" or "stop"), MainView's `torrentAction()` gets the result
// through `activated`.
//
// DECISIONS C24 H9 (board 46 `menuHtml()`, #170): verbs grouped, keys right, delete last. The
// groups are board 46's: run (stop, retry, force start, recheck, reannounce) · place (move…,
// category, tags, rename) · transfer (limits, the four options, the queue) · reach (open
// folder, copy) · delete. Board 46's "transfer ›", "queue ›" and "copy ›" are drawn as their
// groups' rows: Kit/Menu has no submenus. The head (the name over its state, or Q24's "N
// selected") is MainView's.
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

    function _option(text: string, key: string, action: string, on: bool): var {
        return {
            "text": text,
            "key": key,
            "action": action,
            "glyph": on ? Glyphs.check : ""
        };
    }

    /// `all` with `leadActions`' rows moved to the top, in the menu's own order, and the
    /// separators they leave behind tidied (none first, last or twice in a row).
    function _arrange(all: var): var {
        if (root.leadActions.length === 0)
            return all;
        const lead = [];
        const rest = [];
        for (const item of all) {
            if (item.action !== undefined && root.leadActions.indexOf(item.action) >= 0)
                lead.push(item);
            else if (item.separator !== true || (rest.length > 0
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
                                                           "key": "e",
                                                           "action": "retry"
                                                       }] : [];
    }

    menuWidth: Theme.space(280)
    items: root._arrange([
        {
            "text": root.cursorStopped ? qsTr("start") : qsTr("stop"),
            "key": "p",
            "action": "stopStart"
        }].concat(root._retry(), [
        {
            "text": qsTr("force start"),
            "key": "P",
            "action": "forceStart"
        },
        {
            "text": qsTr("recheck"),
            "key": "c",
            "action": "recheck"
        },
        {
            "text": qsTr("reannounce"),
            "key": "R",
            "action": "reannounce"
        },
        {
            "separator": true
        },
        {
            "text": qsTr("move…"),
            "key": "m",
            "action": "location"
        },
        {
            "text": qsTr("category"),
            "key": "C",
            "action": "category"
        },
        {
            "text": qsTr("tags"),
            "key": "t",
            "action": "tags"
        },
        {
            "text": qsTr("rename"),
            "key": "r",
            "action": "rename"
        },
        {
            "separator": true
        },
        {
            "text": qsTr("limits"),
            "key": "L",
            "action": "limits"
        },
        root._option(qsTr("sequential download"), "s", "sequential", root.cursorRow.seqDl === true),
        root._option(qsTr("first / last pieces first"), "f", "firstLast", root.cursorRow.firstLastPiecePrio
                     === true),
        root._option(qsTr("super seeding"), "S", "superSeeding", root.cursorRow.superSeeding === true),
        root._option(qsTr("automatic management"), "a", "autoManagement", root.cursorRow.autoTmm
                     === true),
        {
            "text": qsTr("queue up"),
            "key": "[",
            "action": "queueUp"
        },
        {
            "text": qsTr("queue down"),
            "key": "]",
            "action": "queueDown"
        },
        {
            "separator": true
        },
        {
            "text": qsTr("open folder"),
            "key": "o",
            "action": "openFolder"
        },
        {
            "text": qsTr("copy magnet"),
            "key": "y",
            "action": "copyMagnet"
        },
        {
            "text": qsTr("copy hash"),
            "key": "Y",
            "action": "copyHash"
        },
        {
            "text": qsTr("copy name"),
            "key": "n",
            "action": "copyName"
        },
        {
            "separator": true
        },
        {
            "text": qsTr("delete"),
            "key": "d",
            "action": "delete",
            "danger": true
        }
    ]))
}
