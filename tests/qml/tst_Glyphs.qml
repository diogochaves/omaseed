import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    name: "Glyphs"

    function test_every_glyph_is_one_non_empty_string() {
        const names = ["mark", "arrowDown", "arrowUp", "add", "altSpeed", "burst", "settings", "help",
                       "search", "folder", "trayIdle", "trayDownloading", "trayAltSpeed",
                       "trayError", "trayDisconnected", "statusAll", "statusDownloading",
                       "statusSeeding", "statusCompleted", "statusStopped", "statusActive",
                       "statusStalled", "statusChecking", "statusErrored", "statusQueued",
                       "category", "tag", "tracker", "chevronRight", "chevronDown", "check", "close",
                       "dot", "scrollHint", "host", "lookup", "lookupSlow", "lookupNever",
                       "privateTracker", "copyFrom", "viewLibrary", "viewSeed", "viewLog", "viewSearch",
                       "searchLink", "searchMagnet",
                       "questionNeedsYou", "questionArriving", "questionLanded",
                       "questionGivingBack", "questionResting", "railGroups"];
        for (let i = 0; i < names.length; ++i) {
            const value = Glyphs[names[i]];
            verify(typeof value === "string", names[i] + " should be a string");
            verify(value.length > 0, names[i] + " should not be empty");
        }
    }

    function test_speed_glyphs_are_the_spec_arrows() {
        compare(Glyphs.arrowDown, "↓");
        compare(Glyphs.arrowUp, "↑");
    }
}
