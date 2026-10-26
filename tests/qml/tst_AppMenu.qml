pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// Views/AppMenu (board 46 `menuHtml("app")`, DECISIONS C24 H9): C13's rows in C13's order
// from the shared list, the window's own block before quit, the head, and what each pick
// sends.
TestCase {
    id: testCase

    // App.appMenuRows with two servers, the second one refusing its sign-in, and turtle on.
    readonly property var twoServers: [
        {
            "id": "servers",
            "label": "servers",
            "kind": "caption",
            "enabled": false
        },
        {
            "id": "server:a",
            "label": "public",
            "kind": "radio",
            "checked": true,
            "enabled": true,
            "detail": "4 torrents",
            "urgent": false,
            "state": "ok"
        },
        {
            "id": "server:b",
            "label": "private",
            "kind": "radio",
            "checked": false,
            "enabled": true,
            "detail": "sign-in refused",
            "urgent": true,
            "state": "refused"
        },
        {
            "id": "separator:servers",
            "kind": "separator",
            "enabled": false
        },
        {
            "id": "alt-speed",
            "label": "turtle · until 07:00",
            "kind": "check",
            "checked": true,
            "enabled": true
        },
        {
            "id": "stop-all",
            "label": "stop all",
            "kind": "action",
            "enabled": true
        },
        {
            "id": "start-all",
            "label": "start all",
            "kind": "action",
            "enabled": true
        },
        {
            "id": "separator:actions",
            "kind": "separator",
            "enabled": false
        },
        {
            "id": "add",
            "label": "add torrent…",
            "kind": "action",
            "enabled": true
        },
        {
            "id": "settings",
            "label": "settings…",
            "kind": "action",
            "enabled": true
        },
        {
            "id": "separator:quit",
            "kind": "separator",
            "enabled": false
        },
        {
            "id": "quit",
            "label": "quit",
            "kind": "action",
            "enabled": true
        }
    ]

    height: 700
    name: "AppMenu"
    visible: true
    when: windowShown
    width: 900

    Component {
        id: menuComponent

        AppMenu {
            anchors.fill: parent
            serverLine: "public · qBittorrent 5.3.0"
            sharedRows: testCase.twoServers
            version: "0.5.0"
        }
    }

    Component {
        id: spyComponent

        SignalSpy {
            signalName: "activated"
        }
    }

    function _kinds(menu: var): var {
        return menu.items.map(item => item.separator ? "|" : item.caption ? "~" + item.text :
                                                                            item.action);
    }

    // C13's order, the servers block first with its caption; this window's block after
    // settings, under its own caption, before quit's rule; quit last.
    function test_c13s_rows_then_the_windows_then_quit() {
        const menu = createTemporaryObject(menuComponent, testCase);
        compare(testCase._kinds(menu), ["~servers", "server:a", "server:b", "|", "alt-speed",
                                        "stop-all", "start-all", "|", "add", "settings", "|",
                                        "~this window only", "toggleConsole", "toggleLog",
                                        "commands", "help", "hide", "|", "quit"]);
        compare(menu.items.map(item => item.key ?? "").filter(key => key.length > 0), ["A", "a", ",",
                                                                                       "`", "~", ":",
                                                                                       "?", "q", "ctrl+q"]);
        compare(menu.items[menu.items.length - 1].text, "quit omaseed");
        compare(menu.items[4].text, "turtle · until 07:00");
        compare(menu.items[4].checked, true);
        compare(menu.items[4].glyph, Glyphs.altSpeed);
        menu.altKind = "burst";
        compare(menu.items[4].glyph, Glyphs.burst);
    }

    // One server (D19): no servers block; the rest as with two.
    function test_one_server_has_no_servers_block() {
        const menu = createTemporaryObject(menuComponent, testCase, {
                                               "sharedRows": testCase.twoServers.slice(4)
                                           });
        compare(testCase._kinds(menu)[0], "alt-speed");
        verify(menu.items.every(item => !(item.action ?? "").startsWith("server:")));
    }

    // Board 51 D14 b (DECISIONS C32): no head of its own, the mark says omaseed; the first
    // line is the version, then the status line's server words, on one line.
    function test_the_first_line_is_the_version_and_the_server() {
        const menu = createTemporaryObject(menuComponent, testCase);
        compare(menu.headingTitle, "0.5.0");
        compare(menu.heading, "public · qBittorrent 5.3.0");
        compare(menu.headingOneLine, true);
        compare(menu.menuWidth, Theme.appMenuWidth);
        compare(Theme.appMenuWidth, Theme.skin.name === "omarchy" ? 292 : Theme.appMenuWidth);
        menu.open();
        compare(findChild(menu, "menuHeadingTitle").visible, false);
        compare(findChild(menu, "menuHeadingVersion").text, "0.5.0");
        compare(findChild(menu, "menuHeadingLine").text, " · public · qBittorrent 5.3.0");
        compare(menu.panel.Accessible.name, "0.5.0 · public · qBittorrent 5.3.0");
    }

    // C12's colours: the current server's dot green with a ✓, a refused one's red, its words
    // red too.
    function test_server_rows_take_c12s_colours() {
        const menu = createTemporaryObject(menuComponent, testCase);
        compare(menu.items[1].glyphColor, Theme.green);
        compare(menu.items[1].checked, true);
        compare(menu.items[1].note, "4 torrents");
        compare(menu.items[2].glyphColor, Theme.red);
        compare(menu.items[2].noteColor, Theme.red);
        compare(menu.items[2].checked, false);
        compare(menu.serverColor({
                                     "state": "down"
                                 }), Theme.yellow);
        compare(menu.serverColor({
                                     "state": "ok",
                                     "checked": false
                                 }), Theme.dimForeground);
    }

    // What a pick sends: a C13 id, or the window row's Keymap action; a row's own key picks it.
    function test_picks_send_c13_ids_and_window_actions() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const spy = createTemporaryObject(spyComponent, testCase, {
                                              "target": menu
                                          });
        menu.open();
        compare(menu.cursorIndex, 1); // past the caption, on the current server
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_Return);
        compare(spy.signalArguments[0][0], "server:b");
        menu.open();
        keyClick("~");
        compare(spy.signalArguments[1][0], "toggleLog");
        menu.open();
        keyClick("A");
        compare(spy.signalArguments[2][0], "alt-speed");
        menu.open();
        keyClick(Qt.Key_Q);
        compare(spy.signalArguments[3][0], "hide");
        compare(menu.windowActions, ["toggleConsole", "toggleLog", "commands", "help", "hide"]);
    }
}
