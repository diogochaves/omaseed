pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Kit

// Board 43 P1 (DECISIONS C16 L4): the places under the move sheet's field.
TestCase {
    id: testCase

    height: 400
    name: "PickList"
    visible: true
    when: windowShown
    width: 500

    readonly property var places: [
        {
            "section": "recent",
            "text": "/mnt/fast/films",
            "detail": ""
        },
        {
            "section": "library",
            "text": "/mnt/fast/downloads",
            "detail": "12"
        },
        {
            "section": "library",
            "text": "/mnt/fast/games",
            "detail": "212"
        },
        {
            "section": "categories",
            "text": "/mnt/fast/incoming",
            "detail": "incoming"
        }
    ]

    Component {
        id: listComponent

        PickList {
            width: 480
        }
    }

    function visibleNamed(list, name) {
        const found = [];
        const walk = item => {
            for (let i = 0; i < item.children.length; ++i) {
                const child = item.children[i];
                if (child.objectName === name && child.visible)
                    found.push(child);
                walk(child);
            }
        };
        walk(list);
        return found;
    }

    function test_rows_come_in_sections_each_captioned_once() {
        const list = createTemporaryObject(listComponent, testCase, {
                                               "rows": testCase.places
                                           });
        compare(visibleNamed(list, "pickListRow").length, 4);
        compare(visibleNamed(list, "pickListSection").map(label => label.text), ["recent", "library",
                                                                                 "categories"]);
        compare(list.current, -1);
        compare(list.currentRow, null);
    }

    function test_the_cursor_moves_and_stops_at_the_ends() {
        const list = createTemporaryObject(listComponent, testCase, {
                                               "rows": testCase.places
                                           });
        list.move(1);
        compare(list.current, 0, "down from none starts at the top");
        list.move(-1);
        compare(list.current, 0);
        list.move(2);
        list.move(5);
        compare(list.current, 3);
        compare(list.currentRow.text, "/mnt/fast/incoming");
        // Fewer rows (typing narrowed them): the cursor stays on the list.
        list.rows = testCase.places.slice(0, 2);
        compare(list.current, 1);
        list.rows = [];
        list.move(1);
        compare(list.current, -1);
    }

    function test_a_click_picks_the_row() {
        const list = createTemporaryObject(listComponent, testCase, {
                                               "rows": testCase.places
                                           });
        const spy = createTemporaryObject(spyComponent, testCase, {
                                              "target": list
                                          });
        const rows = visibleNamed(list, "pickListRow");
        // Offscreen, the test runner delivers no mouse events: the row's own handler.
        rows[2].children[1].clicked(null);
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], 2);
        compare(list.current, 2);
    }

    function test_the_foot_shows_more_and_the_keys_only_when_given() {
        const list = createTemporaryObject(listComponent, testCase, {
                                               "rows": testCase.places
                                           });
        compare(visibleNamed(list, "pickListMore").length, 0);
        list.more = "8 more: type to narrow";
        compare(visibleNamed(list, "pickListMore")[0].text, "8 more: type to narrow");
        const bare = list.implicitHeight;
        list.hints = [
                    {
                        "key": "tab",
                        "label": "fill in"
                    }
                ];
        tryVerify(() => list.implicitHeight > bare, 1000, "the keys took no room");
    }

    Component {
        id: spyComponent

        SignalSpy {
            signalName: "picked"
        }
    }
}
