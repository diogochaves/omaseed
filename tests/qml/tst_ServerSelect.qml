pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// `Views/ServerSelect.qml` (docs/34 D3 a, D5 a; #70): the qBittorrent group's server, as a
// select. One server is a plain label with nothing to open. Three are a bordered select whose
// list names each with its state (the one omaseed is on "on it ✓", another that answered with
// its torrents in green, one that didn't "offline", dim), the cursor on the next one; picking
// another asks for the switch, picking the current one or esc doesn't. App's demo server list
// is the model; no server is reached.
TestCase {
    id: testCase

    height: 600
    name: "ServerSelect"
    visible: true
    when: windowShown
    width: 800

    Component {
        id: selectComponent

        Item {
            property alias select: select

            anchors.fill: parent

            ServerSelect {
                id: select

                connected: true
                menuParent: parent
                serverList: App.serverList
                version: "5.2.3"
                width: 200
                x: 20
                y: 40
            }
        }
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }

    function add(name, port) {
        const id = App.serverList.add({
                                          "name": name,
                                          "url": "http://127.0.0.1:" + port,
                                          "authMode": "none"
                                      }, "");
        verify(id.length > 0);
        return id;
    }

    function init() {
        App.loadDemo(16);
    }

    function cleanup() {
        while (App.serverList.count > 0)
            App.serverList.remove(App.serverList.idAt(0));
    }

    function test_one_server_is_a_label() {
        const id = testCase.add("public", 8090);
        const host = createTemporaryObject(selectComponent, testCase);
        const select = host.select;
        select.currentId = id;
        select.currentName = "public";
        verify(!select.switches);
        const plain = findChild(select, "serverSelectPlain");
        verify(plain.visible);
        compare(plain.text, "public 5.2.3");
        compare(plain.dim, true);
        verify(!findChild(select, "serverSelectBox").visible);
        mouseClick(select);
        verify(!select.listOpen);
        select.openList();
        verify(!select.listOpen);
    }

    function test_three_servers_open_a_list_with_their_state() {
        const pub = testCase.add("public", 8090);
        const priv = testCase.add("private", 8091);
        const box = testCase.add("seedbox", 8092);
        App.serverList.setProbeResult(priv, true, "5.2.3", "2.15.1", 4, 3, "");
        App.serverList.setProbeResult(box, false, "", "", -1, 3, "connection refused");
        const host = createTemporaryObject(selectComponent, testCase);
        const select = host.select;
        select.currentId = pub;
        select.currentName = "public";
        verify(select.switches);
        verify(!findChild(select, "serverSelectPlain").visible);
        const box_ = findChild(select, "serverSelectBox");
        verify(box_.visible);
        compare(box_.border.color, Theme.fill(Theme.borderIdle));
        compare(findChild(select, "serverSelectName").text, "public");
        compare(findChild(select, "serverSelectKey").text, "s ▾");
        compare(findChild(select, "serverSelectDot").color, Theme.green);
        select.hasCursor = true;
        compare(box_.border.color, Theme.accent);
        select.hasCursor = false;

        const opened = createTemporaryObject(spyComponent, testCase, {
                                                 "target": select,
                                                 "signalName": "listOpened"
                                             });
        mouseClick(box_);
        verify(select.listOpen);
        compare(opened.count, 1);
        const items = select.list.items;
        compare(items.map(item => item.text), ["public", "private", "seedbox"]);
        compare(items[0].note, "on it ✓");
        compare(items[0].glyphColor, Theme.green);
        compare(items[1].note, "4 torrents");
        compare(items[1].glyphColor, Theme.green);
        compare(items[2].note, "offline");
        compare(items[2].glyphColor, Theme.dimForeground);
        compare(items[2].dim, true);
        compare(select.list.cursorIndex, 1);
        // The list sits under the select, inside the host.
        verify(select.list.panel.y >= select.y + select.height);

        // Picking the current server or esc asks for nothing.
        const asked = createTemporaryObject(spyComponent, testCase, {
                                                "target": select,
                                                "signalName": "switchRequested"
                                            });
        keyClick(Qt.Key_Escape);
        verify(!select.listOpen);
        select.openList();
        keyClick(Qt.Key_K);
        keyClick(Qt.Key_Return);
        compare(asked.count, 0);
        // Another one asks for it.
        select.openList();
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_Return);
        compare(asked.count, 1);
        compare(asked.signalArguments[0][0], box);
    }

    // The list follows the model: a probe that comes back while it is open shows at once.
    function test_the_list_follows_the_server_list() {
        const pub = testCase.add("public", 8090);
        const priv = testCase.add("private", 8091);
        const host = createTemporaryObject(selectComponent, testCase);
        const select = host.select;
        select.currentId = priv;
        select.currentName = "private";
        select.connected = false;
        compare(findChild(select, "serverSelectDot").color, Theme.red);
        select.openList();
        compare(select.list.items[0].note, "");
        compare(select.list.items[1].glyphColor, Theme.red);
        compare(select.list.cursorIndex, 0); // the next one wraps round to the first
        App.serverList.setProbeResult(pub, true, "5.2.3", "2.15.1", 1, 3, "");
        compare(select.list.items[0].note, "1 torrent");
        keyClick(Qt.Key_Escape);
    }
}
