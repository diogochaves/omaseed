import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Hairline"

    Component {
        id: hairlineComponent

        Hairline {}
    }

    function test_horizontal_is_one_pixel_tall() {
        const hairline = createTemporaryObject(hairlineComponent, testCase);
        compare(hairline.height, 1);
    }

    function test_vertical_is_one_pixel_wide() {
        const hairline = createTemporaryObject(hairlineComponent, testCase, {
                                                   "vertical": true
                                               });
        compare(hairline.width, 1);
    }

    function test_colour_is_the_theme_hairline_fill() {
        const hairline = createTemporaryObject(hairlineComponent, testCase);
        // The skin's hair (#171): Omarchy's is the theme's hairline fill.
        const expected = Theme.hairColor;
        if (Theme.skin.name === "omarchy")
            verify(Qt.colorEqual(expected, Theme.fill(Theme.hairlineAlpha)));
        fuzzyCompare(hairline.color.a, expected.a, 0.01);
        compare(hairline.color.r, expected.r);
    }
}
