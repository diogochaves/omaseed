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

    // docs/10 §3: too narrow for every hint, the row drops them from the right and keeps the
    // pinned `? help`.
    function test_a_narrow_row_drops_hints_from_the_right_and_keeps_the_pinned_one() {
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
                  },
                  {
                      "key": "o",
                      "label": "folder"
                  }
              ];
        const hints = createTemporaryObject(keyHintsComponent, testCase, {
                                                "model": model,
                                                "pinned": {
                                                    "key": "?",
                                                    "label": "help"
                                                },
                                                "width": 2000
                                            });
        compare(hints.shownCount, 4);
        hints.width = 10;
        compare(hints.shownCount, 0);
        hints.width = 2000;
        compare(hints.shownCount, 4);
        // Somewhere in between: fewer than all, never more than fit.
        let narrower = 2000;
        while (hints.shownCount === 4 && narrower > 0) {
            narrower -= 10;
            hints.width = narrower;
        }
        compare(hints.shownCount, 3);
        verify(hints.hasPinned);
    }
}
