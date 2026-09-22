pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The torrent actions menu of spec v2 §3 Fig. 11 (`.menu`), a thin `Kit/Menu` skin: the
// caller supplies which hashes the menu acts on and whether the cursor torrent is stopped
// (so the first row reads "start" or "stop"), MainView's `torrentAction()` gets the result
// through `activated`.
Menu {
    id: root

    /// Whether the cursor torrent (the row the menu was opened on) is currently stopped —
    /// decides the first row's text between "start" and "stop".
    property bool cursorStopped: false

    menuWidth: Theme.space(280)
    items: [
        {
            "text": root.cursorStopped ? qsTr("start") : qsTr("stop"),
            "key": "p",
            "action": "stopStart"
        },
        {
            "text": qsTr("force start"),
            "key": "P",
            "action": "forceStart"
        },
        {
            "separator": true
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
            "text": qsTr("set location"),
            "key": "m",
            "action": "location"
        },
        {
            "text": qsTr("limits"),
            "key": "L",
            "action": "limits"
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
            "text": qsTr("open folder"),
            "key": "o",
            "action": "openFolder"
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
    ]
}
