import QtQuick
import QtTest
import Omaseed.Kit

// `Kit/Icon.qml` (docs/08 §2): the line icons drawn from SVG path data on a 24-unit grid. An
// unknown name draws nothing, every entry carries its two path strings, and the stroke stays
// near one device pixel at the sizes the kit draws them (KIT-14).
TestCase {
    id: testCase

    name: "Icon"

    Component {
        id: iconComponent

        Icon {}
    }

    function shapeOf(icon) {
        return icon.children[0];
    }

    function test_an_unknown_name_draws_nothing() {
        const icon = createTemporaryObject(iconComponent, testCase, {
                                               "name": "no-such-icon"
                                           });
        verify(!icon.known);
        verify(!shapeOf(icon).visible, "an unknown name still draws");
        const empty = createTemporaryObject(iconComponent, testCase);
        verify(!empty.known);
    }

    function test_every_entry_carries_its_two_paths() {
        const icon = createTemporaryObject(iconComponent, testCase);
        const names = Object.keys(icon._paths);
        verify(names.length > 0);
        for (const name of names) {
            const entry = icon._paths[name];
            compare(typeof entry.stroke, "string", name + ".stroke");
            compare(typeof entry.fill, "string", name + ".fill");
            verify(entry.stroke.length + entry.fill.length > 0, name + " draws nothing");
            for (const path of [entry.stroke, entry.fill])
                verify(path.length === 0 || /^M[-0-9. MLHVCAZmlhvcaz]*$/.test(path),
                       name + " has a path that is not plain SVG path data: " + path);
            icon.name = name;
            verify(icon.known, name);
        }
    }

    function test_the_stroke_stays_near_one_device_pixel() {
        for (const size of [13, 19.5]) {
            const icon = createTemporaryObject(iconComponent, testCase, {
                                                   "name": "gear",
                                                   "width": size,
                                                   "height": size
                                               });
            const shape = shapeOf(icon);
            const strokePath = shape.data[0];
            const device = strokePath.strokeWidth * shape.unit;
            verify(device >= 1 && device <= 1.3, "at " + size + " px the stroke is " + device
                   + " device px");
        }
    }

    function test_it_is_square_at_the_icon_size() {
        const icon = createTemporaryObject(iconComponent, testCase, {
                                               "name": "folder"
                                           });
        compare(icon.implicitWidth, Theme.font.icon);
        compare(icon.implicitHeight, Theme.font.icon);
    }
}
