import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Field"

    Component {
        id: fieldComponent

        Field {}
    }

    function test_height_is_control_height() {
        const field = createTemporaryObject(fieldComponent, testCase);
        compare(field.height, Theme.controlHeight);
    }

    function test_idle_fill_and_border_match_theme() {
        const field = createTemporaryObject(fieldComponent, testCase);
        fuzzyCompare(field.color.a, Theme.fill(Theme.fillIdle).a, 0.01);
    }

    function test_escape_clears_and_emits_cleared() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "text": "fedora 45"
                                            });
        let cleared = 0;
        field.cleared.connect(() => cleared++);
        field.text = "";
        field.cleared();
        compare(field.text, "");
        compare(cleared, 1);
    }

    function test_accepted_signal_fires() {
        const field = createTemporaryObject(fieldComponent, testCase);
        let accepted = 0;
        field.accepted.connect(() => accepted++);
        field.accepted();
        compare(accepted, 1);
    }
}
