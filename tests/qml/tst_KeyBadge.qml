import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 200
    name: "KeyBadge"
    visible: true
    when: windowShown
    width: 300

    Component {
        id: buttonRowComponent

        Row {
            readonly property alias badge: badge
            readonly property alias button: button

            x: 40
            y: 40

            Rectangle {
                id: button

                height: 28
                width: 28

                KeyBadge {
                    id: badge

                    key: "p"
                }
            }

            Rectangle {
                height: 28
                width: 28
            }
        }
    }

    Component {
        id: placedComponent

        Item {
            readonly property alias badge: badge

            height: 24
            width: 200

            KeyBadge {
                id: badge

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                corner: false
                key: "1"
                shown: true
            }
        }
    }

    // DECISIONS C24 H10: hidden until alt is held, and never with no key.
    function test_it_shows_only_while_asked_with_a_key() {
        const row = createTemporaryObject(buttonRowComponent, testCase);
        verify(!row.badge.visible);
        row.badge.shown = true;
        verify(row.badge.visible);
        compare(row.badge.label.text, "p");
        row.badge.key = "";
        verify(!row.badge.visible);
    }

    // Board 46 `.kb`: over the control's top-right corner, the accent's ground with the
    // window's for the key; the control and its row keep their size, so the action bar's
    // folds never move while alt is held.
    function test_it_hangs_over_the_corner_and_moves_nothing() {
        const row = createTemporaryObject(buttonRowComponent, testCase);
        const width = row.width;
        row.badge.shown = true;
        compare(row.width, width);
        compare(row.button.width, 28);
        compare(row.badge.x + row.badge.width, row.button.width + Theme.keyBadgeOverhangX);
        compare(row.badge.y, -Theme.keyBadgeOverhangY);
        compare(row.badge.height, Theme.keyBadgeHeight);
        verify(row.badge.width >= row.badge.height);
        verify(Qt.colorEqual(row.badge.color, Theme.accent));
        verify(Qt.colorEqual(row.badge.label.color, Theme.background));
        compare(row.badge.label.font.pixelSize, Theme.keyBadgeFontSize);
        // A longer key widens the badge, never the button.
        row.badge.key = "shift+m";
        verify(row.badge.width > row.badge.height);
        compare(row.button.width, 28);
        compare(row.width, width);
        compare(row.badge.Accessible.ignored, true);
    }

    // `corner: false`: the parent places it (a rail row's right end).
    function test_the_parent_can_place_it() {
        const item = createTemporaryObject(placedComponent, testCase);
        verify(item.badge.visible);
        compare(item.badge.x + item.badge.width, item.width);
        compare(item.badge.y, Math.round((item.height - item.badge.height) / 2));
    }
}
