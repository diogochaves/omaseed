import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "KeyCap"
    visible: true
    when: windowShown

    Component {
        id: capComponent

        KeyCap {}
    }

    function test_the_cap_wraps_its_key_in_an_idle_border() {
        const cap = createTemporaryObject(capComponent, testCase, {
                                              "text": "esc"
                                          });
        verify(cap.implicitWidth > Theme.space(10));
        compare(cap.border.width, 1);
        compare(cap.border.color, Theme.fill(Theme.borderIdle));
        compare(cap.color.a, 0);
    }

    function test_a_wider_key_makes_a_wider_cap() {
        const slash = createTemporaryObject(capComponent, testCase, {
                                                "text": "/"
                                            });
        const esc = createTemporaryObject(capComponent, testCase, {
                                              "text": "esc"
                                          });
        verify(esc.implicitWidth > slash.implicitWidth);
    }
}
