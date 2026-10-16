import QtQuick
import QtTest
import "../../plugin"
import "../../plugin/Status.js" as Status

// What a screen reader gets from the widget's rows and notices (#80), on the kit stand-in
// tst_plugin.cpp loads (`PluginKit`): Omarchy 4.0.4's, which publishes nothing, and PR
// #13212's, which names and presses its own controls and leaves an unnamed row out.
// Each pressable thing: its role and name, a press does what its click does, once, and
// nothing while it can't be clicked.
// A TestCase is not visible itself: the rows go on this stage, which is.
Item {
    id: stage
    width: 400
    height: 800

    TestCase {
        name: "PluginA11y"
        when: windowShown

        // Every item under `item` (itself included) that `pred` accepts. Reading an item's
        // `Accessible` makes one (a Text then names itself by its text), so the helpers below
        // look for a role first; only what is shown counts, as only that is published.
        function findAll(item, pred) {
            var out = [];
            if (pred(item))
                out.push(item);
            var kids = item.children || [];
            for (var i = 0; i < kids.length; i++)
                out = out.concat(findAll(kids[i], pred));
            return out;
        }

        function byRole(item, role) {
            return findAll(item, function (it) {
                return it.visible && it.Accessible.role === role && !it.Accessible.ignored;
            });
        }

        function byName(item, role, name) {
            var found = byRole(item, role).filter(function (it) {
                return it.Accessible.name === name;
            });
            verify(found.length === 1, "one " + role + " named " + name + " (" + found.length + ")");
            return found[0];
        }

        Component {
            id: listRow
            ListRow {
                property int clicks: 0
                width: 300
                onClicked: clicks++
            }
        }

        function test_list_row_is_a_named_list_item() {
            var row = createTemporaryObject(listRow, stage, {
                title: "debian-13.iso",
                meta: "48.9 % · 2 min",
                subline: "today"
            });
            compare(row.Accessible.role, Accessible.ListItem);
            compare(row.Accessible.name, "debian-13.iso");
            compare(row.Accessible.description, "48.9 %, 2 min, today");
            // PR #13212's CursorSurface leaves a row without `accessibleName` out; this one is in.
            verify(!row.Accessible.ignored);
            verify(!row.Accessible.checkable);
        }

        function test_list_row_press_clicks_once() {
            var row = createTemporaryObject(listRow, stage, { title: "a" });
            row.Accessible.pressAction();
            compare(row.clicks, 1);
            mouseClick(row);
            compare(row.clicks, 2);
        }

        function test_read_only_row_ignores_the_press() {
            var row = createTemporaryObject(listRow, stage, { title: "a", readOnly: true });
            row.Accessible.pressAction();
            row.Accessible.toggleAction();
            compare(row.clicks, 0);
        }

        function test_add_row_is_checkable() {
            var row = createTemporaryObject(listRow, stage, { title: "a", leading: "tick", ticked: true });
            verify(row.Accessible.checkable);
            verify(row.Accessible.checked);
            row.Accessible.toggleAction();
            compare(row.clicks, 1);
        }

        Component {
            id: section
            Section {
                property int toggles: 0
                width: 300
                title: "Your share"
                summary: "14.2 GB given"
                onToggled: toggles++
            }
        }

        function test_section_header_folds_once() {
            var s = createTemporaryObject(section, stage, { folded: true });
            var header = byName(s, Accessible.Button, "Your share");
            compare(header.Accessible.description, "folded: 14.2 GB given");
            header.Accessible.pressAction();
            compare(s.toggles, 1);
            s.folded = false;
            compare(header.Accessible.description, "unfolded");
        }

        Component {
            id: menuList
            MenuList {
                property var pickedRows: []
                width: 300
                rows: [
                    { kind: "caption", label: "Servers" },
                    { kind: "radio", id: "server:home", label: "home", checked: true, detail: "142 torrents" },
                    { kind: "separator" },
                    { kind: "check", id: "alt-speed", label: "Alternative speed limits", checked: false },
                    { kind: "action", id: "quit", label: "Quit", enabled: false }
                ]
                onPicked: function (row) {
                    pickedRows = pickedRows.concat([row.id]);
                }
            }
        }

        function test_menu_rows_are_menu_items() {
            var menu = createTemporaryObject(menuList, stage);
            compare(byRole(menu, Accessible.MenuItem).length, 3);
            var home = byName(menu, Accessible.MenuItem, "home");
            verify(home.Accessible.checkable);
            verify(home.Accessible.checked);
            compare(home.Accessible.description, "142 torrents");
            var alt = byName(menu, Accessible.MenuItem, "Alternative speed limits");
            compare(alt.Accessible.description, "shortcut A");
            verify(!alt.Accessible.checked);
            byName(menu, Accessible.StaticText, "Servers");
        }

        function test_menu_press_picks_once_and_never_a_disabled_row() {
            var menu = createTemporaryObject(menuList, stage);
            byName(menu, Accessible.MenuItem, "home").Accessible.pressAction();
            compare(menu.pickedRows, ["server:home"]);
            byName(menu, Accessible.MenuItem, "Alternative speed limits").Accessible.toggleAction();
            compare(menu.pickedRows, ["server:home", "alt-speed"]);
            byName(menu, Accessible.MenuItem, "Quit").Accessible.pressAction();
            compare(menu.pickedRows, ["server:home", "alt-speed"]);
            menu.readOnly = true;
            byName(menu, Accessible.MenuItem, "home").Accessible.pressAction();
            compare(menu.pickedRows, ["server:home", "alt-speed"]);
        }

        Component {
            id: settingRow
            SettingRow {
                property var steps: []
                width: 300
                onStepped: function (dir) {
                    steps = steps.concat([dir]);
                }
            }
        }

        function rowFor(key) {
            return Status.settingRows().find(function (r) {
                return r.key === key;
            });
        }

        function test_a_switch_row_is_a_check_box() {
            var row = createTemporaryObject(settingRow, stage, { row: rowFor("showSpeed"), value: true });
            compare(row.Accessible.role, Accessible.CheckBox);
            compare(row.Accessible.name, rowFor("showSpeed").title);
            verify(row.Accessible.checkable);
            verify(row.Accessible.checked);
            verify(!row.Accessible.ignored);
            row.Accessible.toggleAction();
            compare(row.steps, [1]);
            row.Accessible.pressAction();
            compare(row.steps, [1, 1]);
        }

        function test_a_pick_row_says_its_value() {
            var row = createTemporaryObject(settingRow, stage, { row: rowFor("speedMetric"), value: "both" });
            compare(row.Accessible.role, Accessible.ListItem);
            compare(row.Accessible.description.indexOf(Status.settingValueText(rowFor("speedMetric"), "both")), 0);
            verify(!row.Accessible.checkable);
            row.Accessible.pressAction();
            compare(row.steps, [1]);
        }

        Component {
            id: settingsPage
            SettingsPage {
                property int appSettingsCalls: 0
                width: 300
                onAppSettings: appSettingsCalls++
            }
        }

        function test_the_settings_page_foot_row_opens_omaseed_settings_once() {
            var page = createTemporaryObject(settingsPage, stage);
            var foot = byName(page, Accessible.ListItem, "omaseed settings");
            foot.Accessible.pressAction();
            compare(page.appSettingsCalls, 1);
            // Every setting is a row a screen reader finds.
            compare(byRole(page, Accessible.CheckBox).length + byRole(page, Accessible.ListItem).length,
                    Status.settingRows().length + 1);
        }

        Component {
            id: notice
            Notice {
                property int actions: 0
                width: 300
                title: "unreachable · retrying in 7 s"
                spokenTitle: "unreachable"
                detail: "Showing the last known state."
                action: "Retry"
                onActionClicked: actions++
            }
        }

        // The kit's Button: on PR #13212's kit it presses itself, and the widget's guard steps
        // aside; on 4.0.4's the widget's press is the only one. Either way, once.
        function test_a_notice_action_presses_once_on_this_kit() {
            var n = createTemporaryObject(notice, stage);
            compare(n.Accessible.role, Accessible.AlertMessage);
            compare(n.Accessible.name, "unreachable");
            compare(n.Accessible.description, "Showing the last known state.");
            var retry = byName(n, Accessible.Button, "Retry");
            compare("accessibleName" in retry, PluginKit === "pr-13212");
            retry.Accessible.pressAction();
            compare(n.actions, 1);
        }

        Component {
            id: stat
            Stat {
                caption: "Download"
                value: "1.18 MB/s"
                arrow: "↓"
            }
        }

        function test_a_stat_is_one_label_without_its_arrow() {
            var s = createTemporaryObject(stat, stage);
            compare(s.Accessible.role, Accessible.StaticText);
            compare(s.Accessible.name, "Download 1.18 MB/s");
        }
    }
}
