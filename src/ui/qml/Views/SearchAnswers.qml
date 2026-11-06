pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// Each plugin's answer in this search (board 48 `failPop()`, DECISIONS C26 SR2), opened from
// "1 of 3 plugins didn't answer ›", in the kit Menu's form: a row per plugin with what it
// answered and how soon; a failed one with its reason from qBittorrent's log, then "the log
// line", "search it again" and "turn it off…"; a slow one (no answer in 30 s, still heard until
// qBittorrent's 3 min) with "stop asking" and "turn it off…".
Menu {
    id: root

    property var pluginRows: []
    property string serverName: ""

    signal askAgain(string name)
    signal stopAsking(string name)
    signal turnOff(string name)
    signal logRequested

    function nameOf(name: string): string {
        const row = root.pluginRows.find(each => each.name === name);
        return row ? row.fullName : name;
    }

    function _rows(): var {
        const rows = [];
        for (const plugin of root.pluginRows) {
            if (rows.length > 0)
                rows.push({
                              "separator": true
                          });
            let what = "";
            switch (plugin.state) {
            case "answered":
                what = plugin.count === 1 ? qsTr("1 result") : qsTr("%1 results").arg(plugin.count);
                break;
            case "failed":
                what = plugin.seconds >= 0 ? qsTr("failed after %1 s").arg(plugin.seconds) : qsTr(
                                                 "failed");
                break;
            case "slow":
                what = qsTr("no answer in 30 s");
                break;
            case "stopped":
                what = qsTr("stopped");
                break;
            case "skipped":
                what = qsTr("skipped: it doesn't have this type");
                break;
            default:
                what = qsTr("still asked");
            }
            rows.push({
                          "text": qsTr("%1 · %2").arg(plugin.fullName).arg(what),
                          "glyph": Glyphs.dot,
                          "glyphColor": plugin.state === "answered" ? Theme.green : plugin.state
                                                                      === "failed" || plugin.state
                                                                      === "slow" ? Theme.yellow :
                                                                                   Theme.dimForeground,
                          "note": plugin.state === "answered" && plugin.seconds >= 0 ? qsTr(
                                                                                           "in %1 s").arg(
                                                                                           plugin.seconds) :
                                                                                       "",
                          "action": "none:" + plugin.name,
                          "dim": plugin.state !== "failed" && plugin.state !== "slow"
                      });
            if (plugin.state === "failed") {
                if (plugin.reason.length > 0)
                    rows.push({
                                  "caption": true,
                                  "text": plugin.logTime.length > 0 ? qsTr(
                                                                          "qBittorrent's log, %1: “%2”").arg(
                                                                          plugin.logTime).arg(
                                                                          plugin.reason) : qsTr(
                                                                          "“%1”").arg(plugin.reason)
                              });
                rows.push({
                              "text": qsTr("the log line"),
                              "action": "log:" + plugin.name
                          });
                rows.push({
                              "text": qsTr("search it again"),
                              "action": "again:" + plugin.name
                          });
                rows.push({
                              "text": qsTr("turn it off…"),
                              "action": "off:" + plugin.name,
                              "danger": true
                          });
            } else if (plugin.state === "slow") {
                rows.push({
                              "caption": true,
                              "text": qsTr(
                                          "still asked: what it sends before qBittorrent's 3 min limit joins the rows")
                          });
                rows.push({
                              "text": qsTr("stop asking"),
                              "action": "stop:" + plugin.name
                          });
                rows.push({
                              "text": qsTr("turn it off…"),
                              "action": "off:" + plugin.name,
                              "danger": true
                          });
            }
        }
        return rows;
    }

    dimmed: false
    footer: root.pluginRows.some(row => row.state === "slow") ? qsTr(
                                                                    "A slow plugin counts as “didn't answer” after 30 s; it isn't stopped.") :
                                                                qsTr("qBittorrent tells a failure only in its log; omaseed reads it there.")
    heading: qsTr("each asked on its own")
    headingTitle: root.serverName.length > 0 ? qsTr("%1 plugins on %2, this search").arg(
                                                   root.pluginRows.length).arg(root.serverName) :
                                               qsTr("%1 plugins, this search").arg(
                                                   root.pluginRows.length)
    items: root._rows()
    menuWidth: Theme.searchAnswersWidth
    objectName: "searchAnswers"

    onActivated: action => {
        const colon = action.indexOf(":");
        const verb = action.slice(0, colon);
        const name = action.slice(colon + 1);
        if (verb === "none")
            return;
        root.close();
        if (verb === "log")
            root.logRequested();
        else if (verb === "again")
            root.askAgain(name);
        else if (verb === "stop")
            root.stopAsking(name);
        else if (verb === "off")
            root.turnOff(name);
    }
    onRejected: root.close()
}
