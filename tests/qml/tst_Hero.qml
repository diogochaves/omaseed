import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 200
    name: "Hero"
    // Shown, so the ladder tests can read `visible` (a hidden parent hides everything).
    visible: true
    when: windowShown
    width: 1500

    Component {
        id: heroComponent

        // Wide enough for the whole hero (docs/10 §2's step 0 at base 12); the ladder tests
        // set their own widths.
        Hero {
            width: 1400
        }
    }

    Component {
        id: filtersClickedSpy

        SignalSpy {
            signalName: "filtersClicked"
        }
    }

    function test_emits_search_changed_when_the_field_text_changes() {
        const hero = createTemporaryObject(heroComponent, testCase);
        let lastText = "";
        hero.searchChanged.connect(text => lastText = text);
        const field = hero.searchField;
        field.text = "fedora 45";
        compare(lastText, "fedora 45");
    }

    // docs/08 §3: the `+` carries the kit's primary outline so it reads as a button.
    function test_add_is_a_primary_outline() {
        const hero = createTemporaryObject(heroComponent, testCase);
        verify(hero.addButton.primary);
        compare(hero.addButton.border.color, Theme.accent);
        compare(hero.addButton.border.width, 1);
    }

    function test_emits_search_cleared() {
        const hero = createTemporaryObject(heroComponent, testCase);
        let cleared = 0;
        hero.searchCleared.connect(() => cleared++);
        hero.searchField.cleared();
        compare(cleared, 1);
    }

    // The icon buttons name their keys, as `more · ctrl+.` and `alt-speed · A` do.
    function test_the_icon_buttons_name_their_keys() {
        const hero = createTemporaryObject(heroComponent, testCase);
        compare(hero.addButton.tooltip, "add · a");
        compare(findChild(hero, "settingsButton").tooltip, "settings · ,");
        compare(findChild(hero, "helpButton").tooltip, "help · ?");
    }

    // #78: a screen reader hears what each one does; the key is the description, not the name.
    function test_the_icon_buttons_are_named_by_their_action_and_the_key_is_the_description() {
        const hero = createTemporaryObject(heroComponent, testCase);
        const cases = [[hero.addButton, "add torrent", "shortcut a"],
                       [findChild(hero, "settingsButton"), "settings", "shortcut ,"],
                       [hero.moreButton, "more", "shortcut ctrl+."],
                       [findChild(hero, "helpButton"), "help", "shortcut ?"],
                       [findChild(hero, "searchButton"), "filter", "shortcut /"]];
        for (const [button, name, description] of cases) {
            compare(button.Accessible.name, name);
            compare(button.Accessible.description, description);
        }
        hero.searchField.text = "iso";
        compare(findChild(hero, "searchButton").Accessible.description, "iso · shortcut /");
        const trailing = findChild(hero.searchField, "fieldTrailingButton");
        compare(trailing.Accessible.name, "clear the filter");
        hero.searchField.text = "";
    }

    // While the field filters, its count and `esc` cap clear it with a click, as `esc` does.
    function test_a_click_on_the_trailing_esc_clears_the_filter() {
        const hero = createTemporaryObject(heroComponent, testCase);
        let cleared = 0;
        hero.searchCleared.connect(() => cleared++);
        const field = hero.searchField;
        const cap = findChild(field, "fieldTrailingKey");
        verify(!cap.visible, "an empty filter shows the esc cap");
        field.text = "iso";
        verify(cap.visible);
        waitForItemPolished(hero.children[0]);
        mouseClick(cap, cap.width / 2, cap.height / 2);
        compare(cleared, 1);
        compare(field.text, "");
        verify(!cap.visible);
    }

    // Folded to its ✕ (step 3), a chip still names what it clears.
    function test_a_folded_chip_names_its_filter_in_a_tooltip() {
        const hero = createTemporaryObject(heroComponent, testCase);
        hero.filterChips = testCase.fourChips;
        const chip = findChild(hero, "filterChip_tag");
        compare(chip.tooltip, "", "an unfolded chip already shows its text");
        hero.width = Theme.space(700);
        waitForItemPolished(hero.children[0]);
        verify(hero.chipsFolded);
        compare(chip.tooltip, "keep");
    }

    function test_action_signals_fire() {
        const hero = createTemporaryObject(heroComponent, testCase);
        let addClicks = 0;
        let settingsClicks = 0;
        let helpClicks = 0;
        hero.addClicked.connect(() => addClicks++);
        hero.settingsClicked.connect(() => settingsClicks++);
        hero.helpClicked.connect(() => helpClicks++);
        hero.addClicked();
        hero.settingsClicked();
        hero.helpClicked();
        compare(addClicks, 1);
        compare(settingsClicks, 1);
        compare(helpClicks, 1);
    }

    // docs/10 §2: the steps the hero takes as it narrows.
    function test_the_ladder_steps_follow_the_width() {
        const hero = createTemporaryObject(heroComponent, testCase);
        const widths = [1400, 1100, 800, 650, 500];
        for (let i = 0; i < widths.length; i++) {
            hero.width = widths[i];
            compare(hero.level, i, "at " + widths[i]);
        }
    }

    function test_step_two_moves_the_gear_and_help_out() {
        const hero = createTemporaryObject(heroComponent, testCase);
        verify(!hero.compact);
        verify(findChild(hero, "settingsButton").visible);
        hero.width = Theme.space(800);
        verify(hero.compact);
        verify(!findChild(hero, "settingsButton").visible);
        verify(!findChild(hero, "helpButton").visible);
        verify(hero.addButton.visible);
        verify(hero.moreButton.visible);
    }

    function test_step_three_folds_the_filter_into_a_button() {
        const hero = createTemporaryObject(heroComponent, testCase);
        hero.width = Theme.space(650);
        verify(hero.searchFolded);
        verify(findChild(hero, "searchButton").visible);
        verify(!hero.searchField.visible);
        hero.openSearch();
        verify(hero.searchOpen);
        verify(hero.searchField.visible);
        verify(!findChild(hero, "searchButton").visible);
        verify(!findChild(hero, "heroName").visible, "the open field did not take the name's room");
        verify(hero.addButton.visible);
    }

    // The right edge of the rightmost thing the row draws, against the row's own width.
    function overflow(hero) {
        const row = hero.children[0];
        let right = 0;
        for (const child of row.children) {
            if (child.visible && child.width > 0)
                right = Math.max(right, child.x + child.width);
        }
        return right - row.width;
    }

    // KIT-5: the filter chips (shown while the rail is hidden) count in the ladder and fold to
    // glyph and ✕ from step 3, so `+` and ⋯ never leave the window with a chip up.
    function test_filter_chips_never_push_the_buttons_out() {
        const hero = createTemporaryObject(heroComponent, testCase);
        const sets = [[], [{
                               "key": "category",
                               "glyph": Glyphs.folder,
                               "text": "linux-isos"
                           }], [{
                                    "key": "category",
                                    "glyph": Glyphs.folder,
                                    "text": "linux-isos"
                                }, {
                                    "key": "tag",
                                    "glyph": Glyphs.tag,
                                    "text": "archive-2026"
                                }]];
        const over = [];
        for (const chips of sets) {
            hero.filterChips = chips;
            for (const w of [1400, 1286, 1100, 1000, 900, 800, 700, 600, 540, 480]) {
                for (const open of [false, true]) {
                    hero.width = Theme.space(w);
                    hero.searchOpen = open;
                    waitForItemPolished(hero.children[0]);
                    verify(hero.level < 3 || hero.chipsFolded);
                    // Step 4 too: there the chips go into the `/` button (docs/15 Q1 A).
                    if (overflow(hero) > 0.5)
                        over.push(chips.length + " chips at " + w + (open ? " open" : "") + ", level "
                                  + hero.level + ": " + Math.round(overflow(hero)) + " px over");
                }
            }
        }
        hero.searchOpen = false;
        compare(over, []);
    }

    // Review VER-6: three chips at a wide window (1 276 px at base 14) folded the chips, the
    // filter and the server line although the row had room to spare. The chips take the
    // slack first; a step is taken only when the row no longer holds them.
    function test_filter_chips_fold_only_when_the_row_is_full() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "filterChips": [{
                                                       "key": "status",
                                                       "glyph": Glyphs.statusStopped,
                                                       "text": "stopped"
                                                   }, {
                                                       "key": "category",
                                                       "glyph": Glyphs.folder,
                                                       "text": "fixtures"
                                                   }, {
                                                       "key": "tag",
                                                       "glyph": Glyphs.tag,
                                                       "text": "beta"
                                                   }]
                                           });
        // Just short of step 2's threshold plus the whole chips: the old ladder folded here.
        const width = Theme.space(771) + hero._chipsWidth(false) - Theme.space(40);
        hero.width = width;
        waitForItemPolished(hero.children[0]);
        verify(hero.level <= 2, "level " + hero.level + " at " + width + " with three chips");
        verify(!hero.chipsFolded);
        verify(!hero.searchFolded);
        verify(overflow(hero) <= 0.5, "the row runs " + overflow(hero) + " px over");

        // With the room gone, the chips still fold rather than push the buttons out.
        hero.width = Theme.space(700);
        waitForItemPolished(hero.children[0]);
        verify(hero.level >= 3);
        verify(overflow(hero) <= 0.5, "the row runs " + overflow(hero) + " px over");

        // No chips: the plain ladder, untouched.
        hero.filterChips = [];
        for (const [w, step] of [[1400, 0], [1100, 1], [800, 2], [650, 3], [500, 4]]) {
            hero.width = w;
            compare(hero.level, step, "at " + w);
        }
    }

    readonly property var fourChips: [{
            "key": "status",
            "glyph": Glyphs.arrowDown,
            "text": "downloading"
        }, {
            "key": "category",
            "glyph": Glyphs.category,
            "text": "linux"
        }, {
            "key": "tag",
            "glyph": Glyphs.tag,
            "text": "keep"
        }, {
            "key": "tracker",
            "glyph": Glyphs.tracker,
            "text": "tracker.example.org"
        }]

    // docs/15 Q1 A: with the chips in the `/` button, the row fits the floor (560 px at base
    // 14) with no filters and with all four.
    function test_step_four_fits_the_floor_with_zero_and_four_filters() {
        const hero = createTemporaryObject(heroComponent, testCase);
        hero.width = Theme.space(480);
        const row = hero.children[0];
        for (const chips of [[], testCase.fourChips]) {
            hero.filterChips = chips;
            hero.searchField.text = chips.length > 0 ? "iso" : "";
            waitForItemPolished(row);
            compare(hero.level, 4);
            verify(row.implicitWidth <= row.width, chips.length + " filters: row needs "
                   + row.implicitWidth + " of " + row.width);
            verify(overflow(hero) <= 0.5, chips.length + " filters: " + overflow(hero) + " px over");
            verify(hero.addButton.visible);
            verify(hero.moreButton.visible);
        }
        hero.searchField.text = "";
    }

    function test_step_four_puts_the_filters_in_the_slash_button() {
        const hero = createTemporaryObject(heroComponent, testCase);
        let asked = 0;
        hero.filtersClicked.connect(() => asked += 1);
        hero.filterChips = testCase.fourChips;
        const button = findChild(hero, "filterButton");

        // Steps 0–3 keep the chips and the plain `/`.
        hero.width = Theme.space(900);
        verify(hero.level < 4, "level " + hero.level);
        verify(!button.visible);
        verify(findChild(hero, "filterChip_tag").visible);

        hero.width = Theme.space(480);
        compare(hero.level, 4);
        verify(hero.filtersInButton);
        verify(button.visible);
        verify(!findChild(hero, "searchButton").visible);
        verify(!findChild(hero, "filterChip_tag").visible);
        compare(button.border.color, Theme.accent);
        compare(findChild(hero, "filterCount").text, "4");

        // The name filter counts too.
        hero.searchField.text = "iso";
        compare(findChild(hero, "filterCount").text, "5");

        // `/` and a click ask for the menu; the field stays folded.
        hero.openSearch();
        compare(asked, 1);
        verify(!hero.searchOpen);
        mouseClick(button);
        compare(asked, 2);

        // A button like its neighbours: Tab reaches it, enter opens the menu, and it shows
        // the kit's ring and tooltip while it holds the focus (review Q6, Q8).
        verify(button.activeFocusOnTab);
        button.forceActiveFocus();
        verify(findChild(button, "focusRing").visible);
        keyClick(Qt.Key_Return);
        compare(asked, 3);
        const tip = findChild(testCase.Window.window.contentItem, "filterButtonTip");
        tryCompare(tip, "shown", true);
        // (No catalogue in the tests: `%n filter(s)` keeps its brackets.)
        compare(tip.text, "5 filter(s) · /");
        // #78: the name says what it does; the count and the key are the description.
        compare(button.Accessible.name, "filters");
        compare(button.Accessible.description, "5 filter(s) up · shortcut /");
        button.focus = false;

        // No chips: the lone name filter keeps today's `/`, which opens the field.
        hero.filterChips = [];
        verify(!button.visible);
        verify(findChild(hero, "searchButton").visible);
        verify(findChild(hero, "searchButton").accent);
        hero.openSearch();
        compare(asked, 3);
        verify(hero.searchOpen);
        hero.searchField.text = "";
    }

    // The server's dot went to the status line's lane (DECISIONS C24 H15): step 4 keeps `+`
    // and ⋯, and lets the name go.
    function test_step_four_keeps_the_buttons_and_lets_the_name_go() {
        const hero = createTemporaryObject(heroComponent, testCase);
        hero.width = Theme.space(500);
        compare(hero.level, 4);
        verify(findChild(hero, "loneDot") === null, "the hero still draws the server's dot");
        verify(findChild(hero, "serverLine") === null, "the hero still draws the server line");
        verify(hero.addButton.visible);
        verify(hero.moreButton.visible);
    }

    // #75: a screen reader's press on the filters button is its click, once.
    function test_the_accessible_press_is_the_click() {
        const hero = createTemporaryObject(heroComponent, testCase);
        const filters = createTemporaryObject(filtersClickedSpy, testCase, {
                                                  "target": hero
                                              });
        findChild(hero, "filterButton").Accessible.pressAction();
        compare(filters.count, 1);
    }

    // #75: a disabled hero ignores a screen reader's press, as it ignores the pointer.
    function test_what_takes_no_click_ignores_the_accessible_press() {
        const hero = createTemporaryObject(heroComponent, testCase);
        const filters = createTemporaryObject(filtersClickedSpy, testCase, {
                                                  "target": hero
                                              });
        hero.enabled = false;
        findChild(hero, "filterButton").Accessible.pressAction();
        compare(filters.count, 0, "a disabled hero's filters button ran");
    }
}
