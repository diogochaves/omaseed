import QtQuick
import QtTest
import Omaseed
import Omaseed.Views

// docs/63 §6, docs/DECISIONS.md C42 Z7: `Views/SettingsTable.qml`, every setting as a row.
// omaseed's own rows (their count per pane and control, the cursor places the modal reads,
// what `set` and `enter` do) and the server's, derived from `QbtSettingsSections` through the
// pane's own tests: how many per engine and kind, which are left out, which only lead to
// their pane ("go"), and the second line against the modal's nav.
TestCase {
    id: testCase

    name: "SettingsTable"

    height: 720
    visible: true
    when: windowShown
    width: 1100

    Component {
        id: tableComponent

        SettingsTable {}
    }

    Component {
        id: modalComponent

        SettingsModal {
            anchors.fill: parent
        }
    }

    // A server that has every key the schema names, each one on (so every `dep` holds and
    // every `needs` is met), with Deluge's two named plugins on: the most a server offers.
    Component {
        id: fullServerComponent

        QtObject {
            property var applied: []
            property bool loaded: true
            property var values: new Proxy({}, {
                                               "get": (target, key) => key === "enabled_plugins" ? ["Label", "Scheduler"] : true
                                           })

            function apply(changes) {
                applied.push(changes);
            }

            function has(key) {
                return true;
            }

            function owns(changes) {
                return false;
            }
        }
    }

    // A server answering only `values`.
    Component {
        id: plainServerComponent

        QtObject {
            property var applied: []
            property bool loaded: true
            property var values: ({})

            function apply(changes) {
                applied.push(changes);
            }

            function has(key) {
                return values[key] !== undefined;
            }

            function owns(changes) {
                return false;
            }
        }
    }

    function init() {
        App.loadDemo(16);
    }

    function cleanup() {
        App.windowGround = "opaque";
        while (App.serverList.count > 0)
            App.serverList.remove(App.serverList.idAt(0));
    }

    /// The server rows per section, and by kind.
    function tally(table) {
        const tally = {
            "sections": {},
            "switch": 0,
            "choice": 0,
            "go": 0,
            "readOnly": 0,
            "total": 0
        };
        for (const row of table.serverRows()) {
            const section = row.pane.replace("server.", "");
            tally.sections[section] = (tally.sections[section] ?? 0) + 1;
            ++tally[row.kind];
            ++tally.total;
            if (row.readOnly)
                ++tally.readOnly;
        }
        return tally;
    }

    /// A table on a server with everything, speaking `engine` ("" for qBittorrent).
    function fullTable(engine) {
        const server = createTemporaryObject(fullServerComponent, testCase);
        return createTemporaryObject(tableComponent, testCase, {
                                         "connected": true,
                                         "prefs": server,
                                         "presets": server,
                                         "features": engine.length === 0 ? ({}) : ({
                                                                                      "qbtSettings": false,
                                                                                      "sessionSettings": true
                                                                                  }),
                                         "engineName": engine
                                     });
    }

    // The plan's inventory: 30 rows, 12 switches, 10 choices, 3 numbers, 5 actions.
    function test_the_own_rows_per_pane_and_control() {
        const table = createTemporaryObject(tableComponent, testCase);
        const count = (pane, control) => table.ownRows.filter(row => row.pane === pane && row.control === control).length;
        compare(table.ownRows.length, 30);
        compare(table.panes.map(pane => pane.id), ["general", "servers", "notifications", "keybindings", "appearance",
                                                   "backup", "about"]);
        const expected = {
            "general": [3, 1, 0, 1],
            "servers": [0, 0, 0, 0],
            "notifications": [6, 0, 1, 0],
            "keybindings": [0, 0, 0, 0],
            "appearance": [3, 8, 2, 1],
            "backup": [0, 1, 0, 2],
            "about": [0, 0, 0, 1]
        };
        for (const pane in expected) {
            compare([count(pane, "switch"), count(pane, "choice"), count(pane, "number"), count(pane, "action")],
                    expected[pane], pane);
        }
        const all = control => table.ownRows.filter(row => row.control === control).length;
        compare([all("switch"), all("choice"), all("number"), all("action")], [12, 10, 3, 5]);
        // Every row is in a pane there is, and titled.
        for (const row of table.ownRows) {
            verify(table.panes.some(pane => pane.id === row.pane), row.id);
            verify(row.title.length > 0, row.id);
            compare(row.id.indexOf(row.pane + "."), 0, row.id);
        }
    }

    function test_every_id_is_unique() {
        for (const engine of ["", "Transmission", "Deluge"]) {
            const ids = testCase.fullTable(engine).rows().map(row => row.id);
            compare(new Set(ids).size, ids.length, engine);
        }
        const demo = createTemporaryObject(tableComponent, testCase, {
                                               "connected": true
                                           });
        const ids = demo.rows().map(row => row.id);
        compare(new Set(ids).size, ids.length);
    }

    // The modal's cursor is counted here: the panes' counts, and the rows after Window move
    // down one while the ground is translucent (C24 H13).
    function test_the_cursor_counts_follow_what_a_pane_shows() {
        const table = createTemporaryObject(tableComponent, testCase);
        compare(["general", "servers", "notifications", "keybindings", "backup", "about"].map(pane => table.count(pane)),
                [5, 0, 7, 0, 3, 1]);
        compare(table.count("appearance"), 13);
        compare(table.cursorOf("appearance.window"), 3);
        compare(table.cursorOf("appearance.windowAlpha"), -1);
        compare(table.cursorOf("appearance.inTheBar"), 4);
        compare(table.cursorOf("appearance.speedsPlace"), 12);
        compare(table.rowAt("appearance", 4), "appearance.inTheBar");
        verify(table.row("appearance.windowAlpha") === null);
        compare(table.rows().filter(row => !row.server).length, 29);

        App.windowGround = "translucent";
        compare(table.count("appearance"), 14);
        compare(table.cursorOf("appearance.windowAlpha"), 4);
        compare(table.cursorOf("appearance.inTheBar"), 5);
        compare(table.cursorOf("appearance.speedsPlace"), 13);
        compare(table.rowAt("appearance", 4), "appearance.windowAlpha");
        compare(table.row("appearance.windowAlpha").kind, "go");
        compare(table.rows().filter(row => !row.server).length, 30);

        // Each pane's places are 0…count-1, in the table's order.
        for (const pane of table.panes) {
            for (let cursor = 0; cursor < table.count(pane.id); ++cursor)
                compare(table.cursorOf(table.rowAt(pane.id, cursor)), cursor, pane.id);
            compare(table.rowAt(pane.id, table.count(pane.id)), "");
        }
        compare(table.cursorOf("no.such"), -1);
        compare(table.activate("no.such"), "");
    }

    function test_a_switch_moves_its_app_property_and_back() {
        const table = createTemporaryObject(tableComponent, testCase);
        const before = App.pulseRows;
        const row = table.row("appearance.pulseRows");
        compare(row.kind, "switch");
        compare(row.value, before);
        compare(row.valueText, before ? "on" : "off");
        compare(row.paneText, "Settings › Appearance");
        row.set(!before);
        compare(App.pulseRows, !before);
        compare(table.row("appearance.pulseRows").value, !before);
        // enter, as the pane's row.
        compare(table.activate("appearance.pulseRows"), "switch");
        compare(App.pulseRows, before);

        const notices = App.notificationsEnabled.completed === true;
        table.row("notifications.completed").set(!notices);
        compare(App.notificationsEnabled.completed === true, !notices);
        table.activate("notifications.completed");
        compare(App.notificationsEnabled.completed === true, notices);
    }

    function test_a_choice_takes_a_value_and_enter_the_next() {
        const table = createTemporaryObject(tableComponent, testCase);
        const before = App.detailsPlacement;
        const row = table.row("appearance.detailsPlacement");
        compare(row.kind, "choice");
        compare(row.options.map(option => option[0]), ["auto", "right", "bottom"]);
        row.set("bottom");
        compare(App.detailsPlacement, "bottom");
        compare(table.row("appearance.detailsPlacement").valueText, "bottom");
        compare(table.activate("appearance.detailsPlacement"), "choice");
        compare(App.detailsPlacement, "auto"); // wraps round
        App.detailsPlacement = before;

        // The window's ground is a choice of two: enter is the other.
        compare(table.activate("appearance.window"), "choice");
        compare(App.windowGround, "translucent");
        table.activate("appearance.window");
        compare(App.windowGround, "opaque");
    }

    // A number and an action are the pane's to run: the table says which, and changes nothing.
    function test_a_number_and_an_action_only_say_what_they_are() {
        const table = createTemporaryObject(tableComponent, testCase);
        const threshold = App.lowDiskThresholdGiB;
        compare(table.activate("notifications.lowDiskThreshold"), "number");
        compare(App.lowDiskThresholdGiB, threshold);
        compare(table.row("notifications.lowDiskThreshold").kind, "go");
        compare(table.row("notifications.lowDiskThreshold").valueText, String(threshold));
        compare(table.activate("backup.backUp"), "action");
        compare(table.row("backup.backUp").kind, "go");
        compare(table.row("about.widgetLine").title, "Companion bar widget");
    }

    // docs/33 D25: the tests have no bar widget to add, so the choices that need it are not
    // offered, and enter steps over them.
    function test_in_the_bar_drops_the_widget_choices_when_it_cant_be_had() {
        const table = createTemporaryObject(tableComponent, testCase);
        verify(!App.barWidget.available && !App.barWidget.inBar);
        compare(table.allOptions("appearance.inTheBar").map(option => option[0]), ["widget", "tray", "both", "none"]);
        compare(table.options("appearance.inTheBar").map(option => option[0]), ["tray", "none"]);
        compare(table.row("appearance.inTheBar").options.map(option => option[0]), ["tray", "none"]);
        verify(!table.offers("appearance.inTheBar", "both"));
        const before = App.inTheBar;
        App.inTheBar = "tray";
        compare(table.nextOption("appearance.inTheBar"), "none");
        App.inTheBar = "none";
        compare(table.nextOption("appearance.inTheBar"), "tray");
        App.inTheBar = before;
    }

    // The most each engine offers (a server with every key, every `dep` holding): the rows
    // per section and by kind. A switch with a number beside it is a switch; what the pane
    // asks about first (the Web UI's 26 rows and six more) is "go".
    function test_the_server_rows_per_engine() {
        const qbt = testCase.tally(testCase.fullTable(""));
        compare(qbt.sections, {
                    "speed": 13,
                    "discovery": 12,
                    "connection": 14,
                    "proxy": 9,
                    "seeding": 4,
                    "queue": 5,
                    "downloads": 33,
                    "webui": 26,
                    "advanced": 59
                });
        compare([qbt.total, qbt.switch, qbt.choice, qbt.go, qbt.readOnly], [175, 69, 18, 88, 0]);

        const transmission = testCase.tally(testCase.fullTable("Transmission"));
        compare(transmission.sections, {
                    "speed": 10,
                    "discovery": 7,
                    "connection": 5,
                    "seeding": 2,
                    "queue": 3,
                    "downloads": 10
                });
        compare([transmission.total, transmission.switch, transmission.choice, transmission.go, transmission.readOnly],
                [37, 21, 3, 13, 0]);

        // Deluge's three plugin rows are read only (K15).
        const deluge = testCase.tally(testCase.fullTable("Deluge"));
        compare(deluge.sections, {
                    "speed": 6,
                    "discovery": 4,
                    "connection": 6,
                    "seeding": 2,
                    "queue": 9,
                    "downloads": 8,
                    "plugins": 3
                });
        compare([deluge.total, deluge.switch, deluge.choice, deluge.go, deluge.readOnly], [38, 19, 2, 17, 3]);

        // An engine omaseed has no settings for: none.
        const none = testCase.fullTable("Other");
        none.features = {
            "qbtSettings": false
        };
        compare(none.serverRows().length, 0);
        compare(none.serverEngine, "");
    }

    // The demo's qBittorrent 5.2.3 at its defaults: four of Downloads' rows read keys it does
    // not have, and a row whose `dep` is off is the pane's.
    function test_the_demo_servers_rows() {
        const table = createTemporaryObject(tableComponent, testCase, {
                                                "connected": true
                                            });
        const demo = testCase.tally(table);
        compare(demo.sections.downloads, 29);
        compare([demo.total, demo.switch, demo.choice, demo.go], [171, 52, 16, 103]);
    }

    // C14 E4: a row whose key the server's version lacks is left out.
    function test_a_row_the_server_lacks_is_left_out() {
        const table = createTemporaryObject(tableComponent, testCase, {
                                                "connected": true
                                            });
        verify(table.row("server.speed.dl_limit") !== null);
        const rows = table.serverRows().length;
        App.patchDemoPrefs({}, ["dl_limit"]);
        verify(table.row("server.speed.dl_limit") === null);
        compare(table.serverRows().length, rows - 1);
    }

    function test_no_server_rows_until_it_has_answered() {
        const table = createTemporaryObject(tableComponent, testCase, {
                                                "connected": false
                                            });
        compare(table.serverRows().length, 0);
        compare(table.rows().length, 29);
        verify(table.row("server.speed.dl_limit") === null);
        table.connected = true;
        verify(table.serverRows().length > 0);

        const server = createTemporaryObject(plainServerComponent, testCase, {
                                                 "loaded": false
                                             });
        table.prefs = server;
        compare(table.serverRows().length, 0);
        table.prefs = null;
        compare(table.serverRows().length, 0);
    }

    // What the pane asks about first only leads there: the listening port, the two that ask
    // on the way on (both ways: one rule), and every row of the Web UI section.
    function test_what_the_pane_confirms_is_go() {
        const table = testCase.fullTable("");
        compare(table.row("server.connection.listen_port").kind, "go");
        compare(table.row("server.discovery.anonymous_mode").kind, "go");
        compare(table.row("server.connection.ip_filter_enabled").kind, "go");
        const webui = table.serverRows().filter(row => row.pane === "server.webui");
        compare(webui.length, 26);
        verify(webui.every(row => row.kind === "go"));
        // "go" writes nothing.
        table.row("server.discovery.anonymous_mode").set(false);
        compare(table.prefs.applied.length, 0);
    }

    // A switch the pane would refuse (`needs` unmet) or has dimmed (`dep` off) is the pane's.
    function test_a_switch_the_pane_would_refuse_is_go() {
        const server = createTemporaryObject(plainServerComponent, testCase, {
                                                 "values": {
                                                     "secure": false,
                                                     "cert": "",
                                                     "parent": false,
                                                     "child": true
                                                 }
                                             });
        const table = createTemporaryObject(tableComponent, testCase, {
                                                "connected": true,
                                                "prefs": server,
                                                "presets": server
                                            });
        const section = {
            "id": "made",
            "name": "Made"
        };
        const needy = {
            "title": "Secure",
            "toggle": "secure",
            "needs": {
                "keys": ["cert"],
                "note": "needs a certificate"
            }
        };
        const sub = {
            "title": "Child",
            "toggle": "child",
            "dep": "parent"
        };
        compare(table._serverRow(section, needy, 0).kind, "go");
        compare(table._serverRow(section, sub, 1).kind, "go");
        verify(!table._serverRow(section, sub, 1).enabled);
        compare(table.schema.flipRefusal(needy, server.values, true), "needs a certificate");
        compare(table.schema.flipRefusal(needy, server.values, false), "");

        server.values = {
            "secure": false,
            "cert": "/etc/cert.pem",
            "parent": true,
            "child": true
        };
        compare(table._serverRow(section, needy, 0).kind, "switch");
        const row = table._serverRow(section, sub, 1);
        compare([row.kind, row.id, row.value, row.valueText], ["switch", "server.made.child", true, "on"]);
        row.set(false);
        compare(server.applied, [{
                                         "child": false
                                     }]);
    }

    // A server switch and choice write through the server's preferences (the demo's change in
    // memory, at once).
    function test_a_server_row_writes_to_the_server() {
        const table = createTemporaryObject(tableComponent, testCase, {
                                                "connected": true
                                            });
        const flip = table.serverRows().find(row => row.kind === "switch" && row.pane === "server.discovery");
        verify(flip !== undefined);
        flip.set(!flip.value);
        tryVerify(() => table.row(flip.id).value === !flip.value);
        compare(App.serverPrefs.values[flip.key], !flip.value);

        const choice = table.serverRows().find(row => row.kind === "choice");
        verify(choice !== undefined);
        const other = choice.options.find(option => option[0] !== choice.value)[0];
        choice.set(other);
        tryVerify(() => table.row(choice.id).value === other);
        compare(table.row(choice.id).valueText, choice.options.find(option => option[0] === other)[1]);

        // A number says its value, and only leads to its pane.
        const port = table.row("server.connection.listen_port");
        compare(port.kind, "go");
        compare(port.valueText, String(App.serverPrefs.values.listen_port));
    }

    // The second line is the modal's nav: "Settings › Appearance", "Settings › qBittorrent ›
    // Speed", and the server's name once there are two to tell apart.
    function test_the_second_line_is_the_modals_nav() {
        const modal = createTemporaryObject(modalComponent, testCase);
        const table = modal.settings;
        table.connected = true;
        compare(table.panes.length, modal.qbtStart);
        for (let i = 0; i < modal.qbtStart; ++i) {
            compare(table.paneText({
                                       "pane": table.panes[i].id
                                   }), "Settings › " + modal.allRows[i].text);
        }
        verify(table.serverSections.length > 0);
        const serverText = section => table.paneText({
                                                         "pane": "server." + section.id
                                                     });
        table.serverSections.forEach((section, i) => {
                                         compare(modal.qbtSections[i].id, section.id);
                                         compare(serverText(section), "Settings › " + modal.engineGroupName + " › "
                                                 + modal.allRows[modal.qbtStart + i].text);
                                     });
        compare(table.row("server.speed.dl_limit").paneText, "Settings › qBittorrent › Speed");
        compare(table.row("general.startAtLogin").paneText, "Settings › General");

        // A second server: the modal shows its select, and the line names the server.
        const count = table.serverCount;
        while (table.serverCount < 2) {
            App.serverList.add({
                                   "name": "seedbox" + table.serverCount,
                                   "url": "http://seedbox.example:8080",
                                   "authMode": "none"
                               }, "");
            verify(table.serverCount > count || table.serverCount === 2);
        }
        verify(App.serverName.length > 0);
        compare(table.row("server.speed.dl_limit").paneText, "Settings › qBittorrent › Speed · " + App.serverName);
        compare(table.row("general.startAtLogin").paneText, "Settings › General");

        // Another engine's group goes by its name.
        modal.features = {
            "qbtSettings": false,
            "sessionSettings": true
        };
        modal.engineName = "Transmission";
        compare(table.serverEngine, "transmission");
        compare(serverText(table.serverSections[0]), "Settings › Transmission › Speed · " + App.serverName);
    }
}
