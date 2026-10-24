import QtQuick
import QtTest
import Omaseed.Kit

// docs/DECISIONS.md C30 F1–F7: the text roles on the ground a row or control paints, and
// Theme's choice of ground for a row's states.
TestCase {
    id: testCase

    name: "Ink"

    Component {
        id: inkComponent

        Ink {}
    }

    function test_without_a_ground_it_is_the_windows_roles() {
        const ink = createTemporaryObject(inkComponent, testCase);
        compare(ink.ground, Theme.on.window);
        compare(ink.foreground, Theme.foreground);
        compare(ink.dimForeground, Theme.dimForeground);
        compare(ink.accent, Theme.accent);
        compare(ink.stateSeeding, Theme.stateSeeding);
        compare(ink.fill.a, 0);
    }

    // The bundled Tokyo Night's caption reads at 4.5:1 on the window but not on the selected
    // fill: on it, the caption is lifted, and only there.
    function test_on_the_selected_fill_the_caption_is_lifted() {
        const ink = createTemporaryObject(inkComponent, testCase, {
                                              "ground": Theme.on.selected,
                                              "animated": false
                                          });
        compare(ink.dimForeground, Theme.on.selected.dimForeground);
        verify(!Qt.colorEqual(ink.dimForeground, Theme.dimForeground));
        // Still a caption: not the row's main text.
        verify(!Qt.colorEqual(ink.dimForeground, Theme.on.selected.brightForeground));
        compare(ink.stateStopped, Theme.on.selected.dimForeground);
        compare(ink.fill, Theme.on.selected.fill);
    }

    // C30 F4: the colours move with the fill's own 120 ms change, not at once.
    function test_a_change_of_ground_eases_with_the_fill() {
        const ink = createTemporaryObject(inkComponent, testCase);
        ink.ground = Theme.on.selectedCursor;
        verify(!Qt.colorEqual(ink.dimForeground, Theme.on.selectedCursor.dimForeground));
        tryCompare(ink, "dimForeground", Theme.on.selectedCursor.dimForeground, 1000);
    }

    // C30 F3: a caller's colour, one of the window's roles, is that ground's of the same.
    function test_of_maps_a_window_role_to_the_grounds() {
        const ink = createTemporaryObject(inkComponent, testCase, {
                                              "ground": Theme.on.pressed,
                                              "animated": false
                                          });
        compare(ink.of(Theme.yellow), Theme.on.pressed.yellow);
        compare(ink.of(Theme.dimForeground), Theme.on.pressed.dimForeground);
        compare(ink.of(Theme.stateError), Theme.on.pressed.red);
        compare(Theme.inkOf(Theme.magenta, Theme.on.warning), Theme.on.warning.magenta);
        // Not a role: as it is.
        compare(ink.of(Theme.muted), Theme.muted);
    }

    function test_theme_picks_the_strongest_ground_of_a_rows_states() {
        compare(Theme.ground(false, false, false, false), Theme.on.window);
        compare(Theme.ground(false, false, true, false), Theme.on.hover);
        compare(Theme.ground(false, true, true, false), Theme.on.cursor);
        compare(Theme.ground(true, false, true, false), Theme.on.selected);
        compare(Theme.ground(true, true, false, false), Theme.on.selectedCursor);
        compare(Theme.ground(true, true, true, true), Theme.on.pressed);
        // In a 4 % block: resting on the block, hovered on the hover fill over it.
        compare(Theme.groundIn(Theme.on.idle, false, false, false, false), Theme.on.idle);
        compare(Theme.groundIn(Theme.on.idle, false, false, true, false), Theme.on.blockHover);
        compare(Theme.groundIn(Theme.on.idle, true, false, false, false), Theme.on.selected);
    }

    // Each ground paints its own fill over the window, and its text reads on it.
    function test_every_ground_reads() {
        const grounds = ["window", "idle", "hover", "cursor", "selected", "selectedCursor",
                         "pressed", "warning", "attention", "danger", "ticked", "accentIdle",
                         "blockIdle", "blockHover", "highlight"];
        for (const name of grounds) {
            const ground = Theme.on[name];
            verify(ground !== null, name);
            compare(ground.name, name);
            for (const role of ["foreground", "dimForeground", "brightForeground", "accent",
                                "red", "yellow", "orange", "green", "cyan", "blue", "magenta"])
                verify(testCase.contrast(ground[role], ground.ground) >= 4.5,
                       name + " " + role);
        }
    }

    function luminance(c: color): real {
        const lin = v => v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
        return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
    }

    function contrast(a: color, b: color): real {
        const la = testCase.luminance(a);
        const lb = testCase.luminance(b);
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
    }
}
