import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 200
    name: "KeyHints"
    visible: true
    when: windowShown
    width: 600

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

    // #8: a `keep` hint outlasts the others; they still go from the right.
    function test_a_kept_hint_is_dropped_last() {
        const model = [
                  {
                      "key": "j k",
                      "label": "move"
                  },
                  {
                      "key": "space",
                      "label": "check"
                  },
                  {
                      "key": "x",
                      "label": "skip"
                  },
                  {
                      "key": "enter",
                      "label": "add",
                      "keep": true
                  }
              ];
        const hints = createTemporaryObject(keyHintsComponent, testCase, {
                                                "model": model,
                                                "width": 2000
                                            });
        compare(hints._shown, [true, true, true, true]);
        let narrower = 2000;
        while (hints.shownCount === 4 && narrower > 0) {
            narrower -= 5;
            hints.width = narrower;
        }
        // The first to go is `x skip`, the last plain one, not `enter add`.
        compare(hints._shown, [true, true, false, true]);
        while (hints.shownCount > 1 && narrower > 0) {
            narrower -= 5;
            hints.width = narrower;
        }
        compare(hints._shown, [false, false, false, true]);
        hints.width = 10;
        compare(hints.shownCount, 0);
    }

    // A `clickable` hint names a safe action: a pointing hand, and a click reports it. The
    // others stay plain text.
    function test_a_clickable_hint_reports_a_click() {
        const hints = createTemporaryObject(keyHintsComponent, testCase, {
                                                "model": [
                                                    {
                                                        "key": "space",
                                                        "label": "toggle"
                                                    },
                                                    {
                                                        "key": "enter",
                                                        "label": "apply",
                                                        "clickable": true
                                                    }
                                                ]
                                            });
        const clicked = [];
        hints.hintClicked.connect((index, hint) => clicked.push(index + ":" + hint.key));
        const plain = findChild(hints, "keyHint_space");
        const apply = findChild(hints, "keyHint_enter");
        verify(plain.width > 0 && apply.width > 0);
        mouseClick(plain);
        compare(clicked, [], "a plain hint ran");
        mouseClick(apply);
        compare(clicked, ["1:enter"]);
        compare(findChild(apply, "keyHintArea").cursorShape, Qt.PointingHandCursor);
        compare(findChild(plain, "keyHintArea").enabled, false);
    }
}
