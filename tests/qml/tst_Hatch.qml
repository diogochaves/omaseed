import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Hatch"

    Component {
        id: hatchComponent

        Hatch {}
    }

    function test_unsized_draws_nothing() {
        const hatch = createTemporaryObject(hatchComponent, testCase);
        compare(hatch._path, "");
        compare(hatch._lineCount, 0);
    }

    // Stripes start a full height to the left so the bottom-left corner is covered, and run
    // past the right edge so the top-right one is too.
    function test_stripes_cover_the_whole_box() {
        const hatch = createTemporaryObject(hatchComponent, testCase, {
                                                "width": 60,
                                                "height": 12,
                                                "spacing": 6
                                            });
        // x from -12 up to 60 in steps of 6: 13 stripes.
        compare(hatch._lineCount, 13);
        verify(hatch._path.startsWith("M-12.00 12.00 L0.00 0 "));
    }

    function test_clips_and_defaults_to_the_selected_fill() {
        const hatch = createTemporaryObject(hatchComponent, testCase);
        verify(hatch.clip);
        compare(hatch.color, Theme.fill(Theme.fillSelected));
        compare(hatch.lineWidth, 1);
    }
}
