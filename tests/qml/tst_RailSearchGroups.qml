import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// The rail on the search face (board 52 D5s a · D20 a, DECISIONS C34) on the demo's search
// (App.loadDemoSearch: board 48's invented plugins a, b and c): the plugins ticked with how each
// answered, a click asking again; a plugin turned off; the type; the recent searches; and
// "install a plugin…" at the plugins' foot.
TestCase {
    id: testCase

    height: 700
    name: "RailSearchGroups"
    visible: true
    when: windowShown
    width: 1180

    Component {
        id: viewComponent

        SearchView {
            height: 640
            serverName: "public"
            width: 900
            x: 260
        }
    }

    Component {
        id: groupsComponent

        RailSearchGroups {
            width: 240
        }
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    function cleanup() {
        App.loadDemoSearch("off");
    }

    /// A search view on demo stage `stage` and the rail's groups over it.
    function make(stage, folded) {
        App.loadDemoSearch(stage);
        const view = createTemporaryObject(viewComponent, testCase, {
                                               "railHolds": true
                                           });
        const groups = createTemporaryObject(groupsComponent, testCase, {
                                                 "view": view,
                                                 "collapsedGroups": folded || []
                                             });
        waitForItemPolished(groups);
        return groups;
    }

    function spy(target, signalName) {
        return createTemporaryObject(spyComponent, testCase, {
                                         "target": target,
                                         "signalName": signalName
                                     });
    }

    /// A row by name, once the rows a model reset replaced are gone (they linger, deleted
    /// later, where findChild and the Column's layout still see them).
    function row(groups, name) {
        wait(0);
        return findChild(groups, name);
    }

    function heading(groups, name) {
        return findChild(findChild(groups, name), "railGroupCount");
    }

    // Three groups; every plugin ticked while nothing is picked, with how it answered.
    function test_the_plugins_and_how_they_answered() {
        const groups = make("done");
        verify(findChild(groups, "railSearchPlugins").visible);
        verify(findChild(groups, "railSearchType").visible);
        compare(heading(groups, "railSearchPlugins").text, "3 of 3");
        const a = () => row(groups, "railSearchPlugin_plugin_a");
        const b = () => row(groups, "railSearchPlugin_plugin_b");
        compare(a().text, "plugin a");
        verify(a().tick);
        verify(a().ticked);
        compare(a().countText, "7");
        verify(a().lamp);
        verify(Qt.colorEqual(a().lampColor, Theme.green));
        compare(b().countText, "failed");
        verify(Qt.colorEqual(b().countColor, Theme.yellow));
        verify(!b().lampPulse);
        // A demo stage loads its plugins again: the rows are found again after each.
        App.loadDemoSearch("slow");
        compare(b().countText, "slow");
        verify(b().lampPulse || App.reducedMotion);
        App.loadDemoSearch("asking");
        compare(a().countText, "…");
        verify(Qt.colorEqual(a().lampColor, Theme.accent));
        // Before any search: ticks, no answers.
        App.loadDemoSearch("idle");
        compare(a().countText, "");
        verify(!findChild(a(), "railRowLamp").visible);
        verify(a().ticked);
    }

    // A click unticks a plugin and asks again; the last one ticked stays; a full pick is none.
    function test_a_click_ticks_and_asks_again() {
        const groups = make("done");
        const view = groups.view;
        // Each ask loads the demo's plugins again: the rows are found again after each.
        const b = () => row(groups, "railSearchPlugin_plugin_b");
        view.field.text = "open movie night";
        App.loadDemoSearch("asking");
        mouseClick(b());
        compare(view.pluginPick, ["plugin_a", "plugin_c"]);
        verify(!b().ticked);
        verify(b().dimText);
        compare(heading(groups, "railSearchPlugins").text, "2 of 3");
        // Asked again: the demo answers a search at once.
        compare(App.search.state, "done");
        mouseClick(row(groups, "railSearchPlugin_plugin_c"));
        compare(view.pluginPick, ["plugin_a"]);
        const notes = spy(view, "noteRequested");
        mouseClick(row(groups, "railSearchPlugin_plugin_a"));
        compare(view.pluginPick, ["plugin_a"], "the last plugin asked was left out");
        compare(notes.count, 1);
        mouseClick(b());
        mouseClick(row(groups, "railSearchPlugin_plugin_c"));
        compare(view.pluginPick, [], "every plugin that is on is no pick");
        compare(heading(groups, "railSearchPlugins").text, "3 of 3");
    }

    // C34's accepted call: a plugin off in Settings is dim, no tick, "off"; a click asks for
    // the pane on it, not a tick.
    function test_a_plugin_turned_off() {
        const groups = make("done");
        App.searchPlugins.setEnabled("plugin_b", false);
        const b = row(groups, "railSearchPlugin_plugin_b");
        tryVerify(() => !b.tick);
        compare(b.countText, "off");
        verify(b.dimText);
        verify(!b.lamp);
        compare(heading(groups, "railSearchPlugins").text, "2 of 3");
        const pane = spy(groups.view, "pluginPaneRequested");
        mouseClick(b);
        compare(pane.count, 1);
        compare(pane.signalArguments[0][0], "plugin_b");
        compare(groups.view.pluginPick, []);
    }

    // D20 a: the plugins' foot row asks for the pane, its install field focused.
    function test_install_a_plugin() {
        const groups = make("done");
        const install = row(groups, "railSearchInstall");
        compare(install.text, "install a plugin…");
        const asked = spy(groups.view, "pluginsInstallRequested");
        mouseClick(install);
        compare(asked.count, 1);
        // With no plugin at all, the group is that row alone.
        App.loadDemoSearch("none");
        verify(install.visible);
        verify(!findChild(groups, "railSearchType").visible);
    }

    // The type (C34 assumed: "type", the bar's word): any, then the plugins' types; one pick.
    function test_the_type() {
        const groups = make("done");
        compare(findChild(groups, "railSearchType").label, "type");
        const any = row(groups, "railSearchType_all");
        compare(any.text, "any");
        verify(any.selected);
        const music = row(groups, "railSearchType_music");
        mouseClick(music);
        compare(groups.view.category, "music");
        verify(music.selected);
        verify(!any.selected);
        mouseClick(any);
        compare(groups.view.category, "all");
    }

    // Recent: this server's searches this session, newest first, the one on show "now"; a click
    // asks it again.
    function test_recent_searches() {
        // The type folded, so recent's rows are inside the test's window.
        const groups = make("idle", ["searchType"]);
        const view = groups.view;
        verify(!findChild(groups, "railSearchRecent").visible);
        view.field.text = "sampler";
        view.ask();
        view.field.text = "open movie night";
        view.ask();
        verify(findChild(groups, "railSearchRecent").visible);
        compare(view.recent, ["open movie night", "sampler"]);
        // The demo's answer is always "open movie night".
        compare(row(groups, "railSearchRecent_0").countText, "now");
        compare(row(groups, "railSearchRecent_1").countText, "");
        mouseClick(row(groups, "railSearchRecent_1"));
        compare(view.field.text, "sampler");
        compare(view.recent, ["sampler", "open movie night"]);
        // Each server its own.
        view.serverKey = "another";
        compare(view.recent, []);
        view.serverKey = App.serverId;
        compare(view.recent.length, 2);
    }
}
