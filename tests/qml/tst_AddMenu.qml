import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// #146 (board 49 `menuHtml("add")`, DECISIONS C24 H17, C27 CR1): "+ add ▾"'s rows, keys and
// what each one carries; create only where the server can create, search only where it is
// offered.
TestCase {
    id: testCase

    height: 600
    name: "AddMenu"
    visible: true
    when: windowShown
    width: 800

    AddMenu {
        id: menu

        anchors.fill: parent
    }

    SignalSpy {
        id: activated

        signalName: "activated"
        target: menu
    }

    function cleanup() {
        menu.close();
        menu.createShown = true;
        menu.searchShown = true;
        activated.clear();
    }

    function test_the_rows() {
        compare(menu.items.map(row => row.text ?? "-"),
                ["add a torrent or magnet…", "create a torrent from a folder…", "-", "search…"]);
        compare(menu.items.map(row => row.key ?? ""), ["a", "ctrl+n", "", "ctrl+f"]);
        compare(menu.items[1].glyph, Glyphs.create);
        compare(menu.menuWidth, Theme.addMenuWidth);
        menu.openAt(10, 10);
        menu.activate(1);
        compare(activated.count, 1);
        compare(activated.signalArguments[0][0], "create");
    }

    // C27: no create entry on a server that can't; C25 K30: no search where it isn't offered.
    function test_rows_the_server_can_not_do() {
        menu.createShown = false;
        compare(menu.items.map(row => row.action ?? "-"), ["add", "-", "search"]);
        menu.searchShown = false;
        compare(menu.items.map(row => row.action ?? "-"), ["add"]);
    }
}
