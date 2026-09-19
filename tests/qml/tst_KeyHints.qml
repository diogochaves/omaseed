import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "KeyHints"

    Component {
        id: keyHintsComponent

        KeyHints {
            width: 400
        }
    }

    function test_renders_one_row_per_model_pair() {
        const model = [
                  {
                      "key": "j k",
                      "label": "move"
                  },
                  {
                      "key": "space",
                      "label": "select"
                  },
                  {
                      "key": "enter",
                      "label": "actions"
                  }
              ];
        const hints = createTemporaryObject(keyHintsComponent, testCase, {
                                                "model": model
                                            });
        // First layout child is the unboxed Row holding the Repeater and its N delegate rows.
        const row = hints.children[0];
        compare(row.children.length - 1, model.length);
    }

    function test_pinned_hint_is_hidden_when_unset() {
        const hints = createTemporaryObject(keyHintsComponent, testCase, {
                                                "model": []
                                            });
        compare(hints.hasPinned, false);
    }

    function test_pinned_hint_shows_when_set() {
        const hints = createTemporaryObject(keyHintsComponent, testCase, {
                                                "model": [],
                                                "pinned": {
                                                    "key": "?",
                                                    "label": "help"
                                                }
                                            });
        compare(hints.hasPinned, true);
    }
}
