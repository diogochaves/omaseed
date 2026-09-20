import QtQuick
import QtTest
import Omaseed.Kit

// The test runner points XDG at an empty directory, so Theme resolves the bundled Tokyo
// Night at base size 12 — the spec's reference — no matter which theme this machine runs.
TestCase {
    name: "Theme"

    function test_colour_roles_are_valid() {
        verify(Theme.background.a > 0);
        verify(Theme.foreground.a > 0);
        verify(Theme.accent.a > 0);
        verify(Theme.brown.a > 0);
        verify(Theme.background !== Theme.foreground);
        compare(Theme.themeName, "tokyo-night");
        compare(Theme.mode, "dark");
        verify(Theme.fontFamily.length > 0);
    }

    function test_state_roles_map_to_base_roles() {
        compare(Theme.stateDownloading, Theme.blue);
        compare(Theme.stateSeeding, Theme.green);
        compare(Theme.stateStopped, Theme.dimForeground);
        compare(Theme.stateError, Theme.red);
        compare(Theme.stateChecking, Theme.yellow);
        compare(Theme.stateQueued, Theme.magenta);
        compare(Theme.stateStalled, Theme.orange);
    }

    function test_type_scale_hangs_off_the_base_size() {
        compare(Theme.font.body, Theme.font.baseSize);
        compare(Theme.font.caption, Math.round(Theme.font.baseSize * 0.833));
        compare(Theme.font.small, Math.round(Theme.font.baseSize * 0.917));
        compare(Theme.font.subtitle, Math.round(Theme.font.baseSize * 1.083));
        compare(Theme.font.title, Math.round(Theme.font.baseSize * 1.167));
        compare(Theme.font.heading, Math.round(Theme.font.baseSize * 1.333));
        compare(Theme.font.display, Math.round(Theme.font.baseSize * 2));
        compare(Theme.font.iconSmall, Theme.font.small);
        compare(Theme.font.icon, Theme.font.subtitle);
    }

    function test_heights_are_multiples_of_the_body_size() {
        compare(Theme.rowHeight, Math.round(2.4 * Theme.font.body));
        compare(Theme.controlHeight, Math.round(2.333 * Theme.font.body));
        compare(Theme.railRowHeight, Math.round(2.1 * Theme.font.body));
    }

    function test_space_scales_from_the_base_size() {
        compare(Theme.space(0), 0);
        compare(Theme.space(12), Math.round(12 * Theme.font.baseSize / 12));
        compare(Theme.space(1), 1);
    }

    function test_fill_is_the_foreground_at_an_alpha() {
        compare(Theme.fill(0.5).r, Theme.foreground.r);
        compare(Theme.fill(0.5).g, Theme.foreground.g);
        compare(Theme.fill(0.5).b, Theme.foreground.b);
        fuzzyCompare(Theme.fill(0.5).a, 0.5, 0.01);
        fuzzyCompare(Theme.fill(Theme.hairlineAlpha).a, 0.12, 0.01);
    }

    function test_state_alphas_are_the_spec_values() {
        fuzzyCompare(Theme.fillIdle, 0.04, 0.001);
        fuzzyCompare(Theme.fillHover, 0.08, 0.001);
        fuzzyCompare(Theme.fillSelected, 0.18, 0.001);
        fuzzyCompare(Theme.fillPressed, 0.22, 0.001);
        fuzzyCompare(Theme.borderIdle, 0.40, 0.001);
        fuzzyCompare(Theme.borderHover, 0.25, 0.001);
        fuzzyCompare(Theme.hairlineAlpha, 0.12, 0.001);
    }

    function test_window_alpha_starts_at_the_shells_default_and_is_writable() {
        // The harness points XDG at an empty directory, so no shell.toml: the fallback of
        // QUESTIONS Q1 applies, and the ground starts there.
        fuzzyCompare(Theme.defaultWindowAlpha, 0.95, 0.001);
        fuzzyCompare(Theme.windowAlpha, Theme.defaultWindowAlpha, 0.001);
        Theme.windowAlpha = 0.75; // the Appearance override, through MainView's Binding
        fuzzyCompare(Theme.windowAlpha, 0.75, 0.001);
        fuzzyCompare(Theme.defaultWindowAlpha, 0.95, 0.001); // the default never follows
        Theme.windowAlpha = Theme.defaultWindowAlpha; // …and clearing the override restores it
        fuzzyCompare(Theme.windowAlpha, 0.95, 0.001);
    }

    function test_corner_radius_is_never_negative() {
        verify(Theme.cornerRadius >= 0);
    }

    // docs/QUESTIONS.md Q19: the dim role reads at 3:1 against the window background. Tokyo
    // Night's own dark_foreground is at 2.76, so this only passes because the floor lifts it.
    function luminance(c) {
        function linear(v) {
            return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
    }

    function contrast(a, b) {
        const la = luminance(a), lb = luminance(b);
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
    }

    function test_dim_foreground_reads_at_three_to_one() {
        verify(contrast(Theme.foreground, Theme.background) >= 3);
        verify(contrast(Theme.dimForeground, Theme.background) >= 3);
        verify(contrast(Theme.dimForeground, Theme.background) < 3.1); // the smallest lift
        compare(Theme.stateStopped, Theme.dimForeground);
    }
}
