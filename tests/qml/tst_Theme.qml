import QtQuick
import QtTest
import Omaseed.Kit

// The test runner points XDG at an empty directory, so Theme resolves the bundled Tokyo
// Night at base size 12 — the spec's reference — no matter which theme this machine runs.
TestCase {
    id: testCase

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

    // #156: a binding made of `Theme.space()` alone, which reads no property of its own.
    Item {
        id: spaced

        width: Theme.space(10)
        height: Theme.space(24)
    }

    function cleanup() {
        TestTheme.setBaseSize(0); // qmllint disable unqualified
        TestTheme.setSkinChoice("omarchy"); // qmllint disable unqualified
    }

    // #156 (docs/DECISIONS.md C28): Theme.space() reads spaceScale, so a binding of nothing
    // but Theme.space(n) follows the base size live, both ways, as the shell's or the
    // Appearance pane's base size moves 12 → 18 → 12.
    function test_a_bare_space_binding_follows_the_base_size() {
        compare(Theme.font.baseSize, 12);
        compare(spaced.width, 10);
        compare(spaced.height, 24);

        TestTheme.setBaseSize(18); // qmllint disable unqualified
        compare(Theme.font.baseSize, 18);
        fuzzyCompare(Theme.spaceScale, 1.5, 0.0001);
        compare(spaced.width, 15);
        compare(spaced.height, 36);

        TestTheme.setBaseSize(0); // qmllint disable unqualified
        compare(Theme.font.baseSize, 12);
        compare(spaced.width, 10);
        compare(spaced.height, 24);
    }

    // #74's form and the bare call agree at every scale, rounding included.
    function test_space_is_space_at_the_current_scale() {
        for (const size of [12, 14, 17, 20]) {
            TestTheme.setBaseSize(size); // qmllint disable unqualified
            for (const px of [0, 1, 2, 3, 6, 8, 12, 40])
                compare(Theme.space(px), Theme.spaceAt(px, Theme.spaceScale), size + "/" + px);
        }
    }

    function test_focus_ring_sits_outside_the_edge() {
        compare(Theme.focusGap, Theme.space(2));
        compare(Theme.focusExtent, Theme.focusGap + 1);
        verify(Theme.focusGap >= 1);
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

    // Review KIT-17: the spec's other alphas are named roles, at the spec's values.
    function test_the_spec_only_alphas_are_roles() {
        fuzzyCompare(Theme.rowRuleAlpha, 0.05, 0.0001);
        fuzzyCompare(Theme.fillSelectedCursor, 0.24, 0.0001);
        fuzzyCompare(Theme.keyValueRuleAlpha, 0.06, 0.0001);
        fuzzyCompare(Theme.faintAlpha, 0.40, 0.0001);
    }

    function test_window_alpha_starts_at_the_shells_default_and_is_writable() {
        // The harness points XDG at an empty directory, so no shell.toml: the fallback of
        // docs/DECISIONS.md Q1 applies, and the ground starts there.
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

    // docs/DECISIONS.md Q19 (amended in #89): every text colour reads at 4.5:1 against the
    // window background. Tokyo Night's own dark_foreground is at 2.76, so the dim role only
    // passes because the floor lifts it.
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

    function test_dim_foreground_reads_at_four_and_a_half_to_one() {
        verify(contrast(Theme.foreground, Theme.background) >= 4.5);
        verify(contrast(Theme.dimForeground, Theme.background) >= 4.5);
        verify(contrast(Theme.dimForeground, Theme.background) < 4.6); // the smallest lift
        compare(Theme.stateStopped, Theme.dimForeground);
    }

    function test_text_colours_read_at_four_and_a_half_to_one() {
        const roles = ["accent", "red", "yellow", "orange", "green", "cyan", "blue", "magenta",
                       "stateDownloading", "stateSeeding", "stateError", "stateChecking",
                       "stateQueued", "stateStalled"];
        for (const name of roles)
            verify(contrast(Theme[name], Theme.background) >= 4.5, name);
    }

    // #171: Omarchy is the skin with no overrides, so every token the skin could set comes
    // out as the kit's own value.
    function test_omarchy_s_skin_tokens_are_the_kit_s_own() {
        if (Theme.skin.name !== "omarchy")
            skip("the suite's run under another skin");
        compare(Theme.railWidth, Theme.space(200));
        compare(Theme.titleBarHeight, Theme.space(46));
        compare(Theme.sheetEdge, 1);
        compare(Theme.hairWidth, 1);
        compare(Theme.edgeWidth, 1);
        compare(Theme.menuEdgeWidth, 1);
        compare(Theme.litWidth, 2);
        verify(Qt.colorEqual(Theme.hairColor, Theme.fill(Theme.hairlineAlpha)));
        verify(Qt.colorEqual(Theme.faintColor, Theme.fill(Theme.rowRuleAlpha)));
        verify(Qt.colorEqual(Theme.edgeColor, Theme.accent));
        verify(Qt.colorEqual(Theme.menuEdgeColor, Theme.accent));
        compare(Theme.labelFamily, Theme.fontFamily);
        compare(Theme.valueFamily, Theme.fontFamily);
        compare(Theme.kanaFamily, Theme.fontFamily);
        compare(Theme.iconsFamily, "");
        compare(Theme.labelUpper, true);
        compare(Theme.labelTrack, 0.08);
        compare(Theme.labelWeight, Font.Normal);
        compare(Theme.kana("library"), "");
        compare(Theme.numeral(2), "2");
    }

    // #171: Console's skin sets the tokens board 46 pins; one it leaves out is Omarchy's.
    function test_the_console_skin_sets_what_it_names() {
        TestTheme.setSkinChoice("console");
        compare(Theme.skin.name, "console");
        compare(Theme.railWidth, Theme.space(222));
        compare(Theme.titleBarHeight, Theme.space(58));
        compare(Theme.titleBarLineGap, Theme.space(1));
        compare(Theme.cornerRadius, 0);
        verify(Qt.colorEqual(Theme.hairColor, Qt.alpha(Theme.foreground, 0.21)));
        verify(Qt.colorEqual(Theme.faintColor, Qt.alpha(Theme.foreground, 0.1)));
        verify(Qt.colorEqual(Theme.menuEdgeColor, Theme.hairColor));
        verify(Qt.colorEqual(Theme.edgeColor, Theme.accent));
        compare(Theme.labelTrack, 0.16);
        compare(Theme.labelTrackTitle, 0.22);
        compare(Theme.labelWeight, 600);
        compare(Theme.kana("library"), "ライブラリ");
        compare(Theme.kana("no-such-label"), "");
        compare(Theme.kana(""), "");
        compare(Theme.kanaRole, "cyan");
        compare(Theme.numeral(2), "02");
        compare(Theme.numeral(12), "12");
        // Each family is the skin's while installed, else fontconfig's monospace (the icons:
        // none, and every glyph is Omarchy's): never a family that isn't there.
        const missing = Theme.skin.missingFonts;
        const families = [["labelFamily", "Chakra Petch", Theme.fontFamily],
                          ["valueFamily", "Share Tech Mono", Theme.fontFamily],
                          ["kanaFamily", "Noto Sans CJK JP", Theme.fontFamily],
                          ["iconsFamily", "Material Symbols Sharp", ""]];
        for (const [property, family, fallback] of families)
            compare(Theme[property], missing.indexOf(family) >= 0 ? fallback : family, property);
        TestTheme.setSkinChoice("omarchy");
    }

    Component {
        id: boundComponent

        Item {
            height: Theme.hairWidth
            width: Theme.railWidth
        }
    }

    // Rule 8: a binding made of a token follows a skin switch, both ways.
    function test_a_token_binding_follows_the_skin() {
        const item = createTemporaryObject(boundComponent, testCase);
        TestTheme.setSkinChoice("console");
        compare(item.width, Theme.space(222));
        TestTheme.setSkinChoice("omarchy");
        const pinned = Theme.skin.name !== "omarchy";
        compare(item.width, Theme.space(pinned ? 222 : 200));
    }

    // A skin's radius in px wins over Hyprland's; "hyprland" or none keeps it.
    function test_corner_radius_is_the_skins_when_it_sets_one() {
        TestTheme.setSkinChoice("console");
        compare(Theme.cornerRadius, 0);
        TestTheme.setSkinChoice("omarchy");
        verify(Theme.cornerRadius >= 0);
    }
}
