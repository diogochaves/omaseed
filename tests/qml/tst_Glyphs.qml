import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    name: "Glyphs"

    // A test that switches the skin leaves Omarchy's for the next, even when it fails.
    function cleanup() {
        TestTheme.setSkinChoice("omarchy"); // qmllint disable unqualified
    }

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
                       "questionGivingBack", "questionResting", "railGroups", "chevronDownGlyph",
                       "arrowRight", "diamond", "radioOn", "radioOff", "settingWarn",
                       "settingPending", "serverLocal", "serverRemote", "sort", "speedLimits",
                       "move", "moveBack", "moveQuit"];
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

    // #171: a name goes through the skin's table. Console's `ms:` icons draw only while its
    // icon font is installed; a name its callers write into words keeps a character.
    function test_the_skins_table_comes_first() {
        TestTheme.setSkinChoice("console");
        const icons = Theme.iconsFamily !== "";
        compare(Glyphs.add, icons ? "ms:add" : "+");
        compare(Glyphs.search, icons ? "ms:search" : "/");
        compare(Glyphs.category, icons ? "ms:category" : "icon:folder");
        compare(Glyphs.chevronDown, "▾");
        // Drawn alone in a Glyph, the same name takes the skin's icon.
        compare(Glyphs.chevronDownGlyph, icons ? "ms:expand_more" : "▾");
        compare(Glyphs.arrowDown, "↓");
        TestTheme.setSkinChoice("omarchy");
        if (Theme.skin.name === "omarchy")
            compare(Glyphs.add, "+");
    }
}
