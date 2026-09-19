import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    name: "Smoke"

    function test_theme_is_reachable() {
        verify(Theme.font.body > 0);
        verify(Theme.font.caption < Theme.font.body);
        verify(Theme.font.display > Theme.font.heading);
    }

    function test_theme_space_scales_from_the_base_size() {
        compare(Theme.space(12), 12);
        compare(Theme.space(0), 0);
    }

    function test_theme_fill_is_the_foreground_at_an_alpha() {
        compare(Theme.fill(0.18).r, Theme.foreground.r);
        compare(Theme.fill(0.18).a, 0.18);
    }
}
