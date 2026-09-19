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

    function test_emits_search_cleared() {
        const hero = createTemporaryObject(heroComponent, testCase);
        let cleared = 0;
        hero.searchCleared.connect(() => cleared++);
        hero.searchField.cleared();
        compare(cleared, 1);
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
}
