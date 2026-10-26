pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The alternative speeds' choice (board 51 D8 a `altPicker()`, DECISIONS C32 D8 a): opened
// from the status line's chip, or the menus' alternative speeds row, over the chip. One set
// at a time, written when it is picked: off (your normal limits), turtle (qBittorrent's set,
// the schedule's) and burst (omaseed's, written over it for as long as it lasts), the one in
// force checked; then how long a burst lasts, and the way to edit both in Settings › Speed.
// `activated` carries "off", "turtle", "burst", "minutes:30", "minutes:60", "minutes:0" or
// "edit". Its form is the kit Menu's.
Menu {
    id: root

    /// "off", "turtle" or "burst": what the server is on.
    property string current: "off"
    /// The server's name, for the heading.
    property string serverName: ""
    /// Each preset's limits, as the row's note prints them ("↓300K ↑50K").
    property string turtleLimits: ""
    property string burstLimits: ""
    /// 30, 60 or 0 (until it is stopped).
    property int burstMinutes: 60

    dimmed: false
    footer: qsTr("A turns off what is on, or the last one picked back on.")
    heading: root.serverName.length > 0 ? qsTr("%1 · one set at a time").arg(root.serverName)
                                        : qsTr("one set at a time")
    headingTitle: qsTr("Alternative speeds")
    items: [
        {
            "text": qsTr("off"),
            "action": "off",
            "note": qsTr("your normal limits"),
            "checked": root.current === "off"
        },
        {
            "text": qsTr("turtle"),
            "glyph": Glyphs.altSpeed,
            "glyphColor": Theme.yellow,
            "action": "turtle",
            "note": root.turtleLimits,
            "checked": root.current === "turtle"
        },
        {
            "text": qsTr("burst"),
            "glyph": Glyphs.burst,
            "glyphColor": Theme.green,
            "action": "burst",
            "note": root.burstLimits,
            "checked": root.current === "burst"
        },
        {
            "separator": true
        },
        {
            "caption": true,
            "text": qsTr("burst lasts")
        },
        {
            "text": qsTr("30 min"),
            "action": "minutes:30",
            "checked": root.burstMinutes === 30
        },
        {
            "text": qsTr("1 h"),
            "action": "minutes:60",
            "checked": root.burstMinutes === 60
        },
        {
            "text": qsTr("until I stop it"),
            "action": "minutes:0",
            "checked": root.burstMinutes === 0
        },
        {
            "separator": true
        },
        {
            "text": qsTr("edit the presets…"),
            "glyph": Glyphs.rename,
            "action": "edit"
        }
    ]
    menuWidth: Theme.space(330)
}
