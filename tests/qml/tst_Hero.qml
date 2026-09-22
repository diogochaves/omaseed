import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Hero"

    Component {
        id: heroComponent

        Hero {
            width: 900
        }
    }

    function test_emits_search_changed_when_the_field_text_changes() {
        const hero = createTemporaryObject(heroComponent, testCase);
        let lastText = "";
        hero.searchChanged.connect(text => lastText = text);
        const field = hero.searchField;
        field.text = "fedora 45";
        compare(lastText, "fedora 45");
    }

    // docs/08 §3: the `+` carries the kit's primary outline so it reads as a button.
    function test_add_is_a_primary_outline() {
        const hero = createTemporaryObject(heroComponent, testCase);
        verify(hero.addButton.primary);
        compare(hero.addButton.border.color, Theme.accent);
        compare(hero.addButton.border.width, 1);
    }

    function test_emits_search_cleared() {
        const hero = createTemporaryObject(heroComponent, testCase);
        let cleared = 0;
        hero.searchCleared.connect(() => cleared++);
        hero.searchField.cleared();
        compare(cleared, 1);
    }

    // T5.4: with no server configured the dot is absent-looking (spec `.dot.off`), not the
    // green of a healthy connection.
    function test_the_status_dot_follows_the_connection_state() {
        const hero = createTemporaryObject(heroComponent, testCase);
        compare(hero.statusDot.color, Theme.green);
        hero.connected = false;
        compare(hero.statusDot.color, Theme.red);
        hero.syncing = true;
        compare(hero.statusDot.color, Theme.yellow);
        hero.serverConfigured = false;
        compare(hero.statusDot.color, Theme.dimForeground);
        hero.connected = true;
        hero.syncing = false;
        compare(hero.statusDot.color, Theme.dimForeground, "no server still drew a live dot");
    }

    function test_action_signals_fire() {
        const hero = createTemporaryObject(heroComponent, testCase);
        let addClicks = 0;
        let altSpeedClicks = 0;
        let settingsClicks = 0;
        let helpClicks = 0;
        hero.addClicked.connect(() => addClicks++);
        hero.altSpeedClicked.connect(() => altSpeedClicks++);
        hero.settingsClicked.connect(() => settingsClicks++);
        hero.helpClicked.connect(() => helpClicks++);
        hero.addClicked();
        hero.altSpeedClicked();
        hero.settingsClicked();
        hero.helpClicked();
        compare(addClicks, 1);
        compare(altSpeedClicks, 1);
        compare(settingsClicks, 1);
        compare(helpClicks, 1);
    }

    function test_speeds_sit_left_of_the_search_field_by_default() {
        // Wide enough that the row has real slack for a spacer to take; at the component's
        // own 900 there is none, and neither alignment could move anything.
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "width": 1400
                                           });
        compare(hero.speedsAlignment, "left");
        verify(hero.downStat.x < hero.searchField.x, "the speed pair is not left of the field");
        // Beside the server line rather than against the field: well inside the left half.
        verify(hero.downStat.x < hero.width / 2, "the speed pair drifted right");
    }

    function test_right_alignment_pushes_the_speeds_against_the_field() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "speedsAlignment": "right",
                                               "width": 1400
                                           });
        verify(hero.downStat.x < hero.searchField.x, "the pair must stay before the field");
        verify(hero.downStat.x > hero.width / 2, "the speed pair did not move right");
    }

    function test_an_unknown_alignment_is_drawn_as_left() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "speedsAlignment": "sideways",
                                               "width": 1400
                                           });
        verify(hero.downStat.x < hero.width / 2);
    }
}
