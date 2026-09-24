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

    // docs/08 §3: silent while connected; firewalled and disconnected are named in colour.
    function test_the_network_status_is_named_only_when_it_is_not_connected() {
        const hero = createTemporaryObject(heroComponent, testCase);
        compare(hero.networkWarning, "");
        compare(hero.statusDot.color, Theme.green);

        hero.connectionStatus = "firewalled";
        compare(hero.networkWarning, "firewalled");
        verify(hero.networkLabel.text.indexOf("firewalled") >= 0);
        compare(hero.networkLabel.color, Theme.yellow);
        compare(hero.statusDot.color, Theme.yellow);

        hero.connectionStatus = "disconnected";
        compare(hero.networkLabel.color, Theme.red);
        compare(hero.statusDot.color, Theme.red);

        // While the API itself is unreachable, or before the first sync, the line already says
        // so; qBittorrent's own network state is not read then.
        hero.connected = false;
        compare(hero.networkWarning, "");
        hero.connected = true;
        hero.syncing = true;
        compare(hero.networkWarning, "");
    }

    // docs/08 §3: alt-speed is a chip that names itself, and prints the limits while on.
    function test_the_alt_speed_chip_shows_the_limits_while_on() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "altDownLimit": "10 KB/s",
                                               "altUpLimit": "∞"
                                           });
        compare(hero.altChip.text, "alt-speed");
        verify(!hero.altChip.warn);
        hero.altSpeedActive = true;
        compare(hero.altChip.text, "↓10 KB/s ↑∞");
        verify(hero.altChip.warn);
        compare(hero.altChip.border.color, Theme.yellow);

        let clicks = 0;
        hero.altSpeedClicked.connect(() => clicks++);
        hero.altChip.clicked();
        compare(clicks, 1);
    }

    function test_emits_search_cleared() {
        const hero = createTemporaryObject(heroComponent, testCase);
        let cleared = 0;
        hero.searchCleared.connect(() => cleared++);
        hero.searchField.cleared();
        compare(cleared, 1);
    }

    // T5.4: with no server configured the dot is absent-looking (spec `.dot.off`), not the
    // green of a healthy connection.
    function test_the_status_dot_follows_the_connection_state() {
        const hero = createTemporaryObject(heroComponent, testCase);
        compare(hero.statusDot.color, Theme.green);
        hero.connected = false;
        compare(hero.statusDot.color, Theme.red);
        hero.syncing = true;
        compare(hero.statusDot.color, Theme.yellow);
        hero.serverConfigured = false;
        compare(hero.statusDot.color, Theme.dimForeground);
        hero.connected = true;
        hero.syncing = false;
        compare(hero.statusDot.color, Theme.dimForeground, "no server still drew a live dot");
    }

    function test_action_signals_fire() {
        const hero = createTemporaryObject(heroComponent, testCase);
        let addClicks = 0;
        let altSpeedClicks = 0;
        let settingsClicks = 0;
        let helpClicks = 0;
        hero.addClicked.connect(() => addClicks++);
        hero.altSpeedClicked.connect(() => altSpeedClicks++);
        hero.settingsClicked.connect(() => settingsClicks++);
        hero.helpClicked.connect(() => helpClicks++);
        hero.addClicked();
        hero.altSpeedClicked();
        hero.settingsClicked();
        hero.helpClicked();
        compare(addClicks, 1);
        compare(altSpeedClicks, 1);
        compare(settingsClicks, 1);
        compare(helpClicks, 1);
    }

    function test_speeds_sit_left_of_the_search_field_by_default() {
        // Wide enough that the row has real slack for a spacer to take; at the component's
        // own 900 there is none, and neither alignment could move anything.
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "width": 1400
                                           });
        compare(hero.speedsAlignment, "left");
        verify(hero.downStat.x < hero.searchField.x, "the speed pair is not left of the field");
        // Beside the server line rather than against the field: well inside the left half.
        verify(hero.downStat.x < hero.width / 2, "the speed pair drifted right");
    }

    function test_right_alignment_pushes_the_speeds_against_the_field() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "speedsAlignment": "right",
                                               "width": 1400
                                           });
        verify(hero.downStat.x < hero.searchField.x, "the pair must stay before the field");
        verify(hero.downStat.x > hero.width / 2, "the speed pair did not move right");
    }

    function test_an_unknown_alignment_is_drawn_as_left() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "speedsAlignment": "sideways",
                                               "width": 1400
                                           });
        verify(hero.downStat.x < hero.width / 2);
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

    function test_step_one_shortens_the_server_line_and_the_chip() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "serverLine": "home · qBittorrent 5.2.3",
                                               "serverLineShort": "home",
                                               "altSpeedActive": true,
                                               "altDownLimit": "1.00 MB/s",
                                               "altUpLimit": "200 KB/s",
                                               "altDownLimitShort": "1.00M",
                                               "altUpLimitShort": "200K"
                                           });
        compare(findChild(hero, "serverLine").text, "home · qBittorrent 5.2.3");
        compare(hero.altChip.text, "↓1.00 MB/s ↑200 KB/s");
        hero.width = Theme.space(1000);
        compare(hero.level, 1);
        compare(findChild(hero, "serverLine").text, "home");
        compare(hero.altChip.text, "↓1.00M ↑200K");
        hero.altSpeedActive = false;
        compare(hero.altChip.text, ""); // the glyph alone
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
        verify(!hero.downStat.visible);
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
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "downSpeed": "12.3 MB/s",
                                               "upSpeed": "999.9 KB/s",
                                               "altSpeedActive": true,
                                               "altDownLimit": "10.0 MB/s",
                                               "altUpLimit": "1.00 MB/s",
                                               "altDownLimitShort": "10.0M",
                                               "altUpLimitShort": "1.00M",
                                               "serverLine": "home · qBittorrent 5.2.3",
                                               "serverLineShort": "home"
                                           });
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
                    // Step 4 is the ladder's last: what gives there with chips up is still
                    // Diogo's call (docs/QUESTIONS.md), so only steps 0–3 must fit.
                    if (hero.level < 4 && overflow(hero) > 0.5)
                        over.push(chips.length + " chips at " + w + (open ? " open" : "") + ", level "
                                  + hero.level + ": " + Math.round(overflow(hero)) + " px over");
                }
            }
        }
        hero.searchOpen = false;
        compare(over, []);
    }

    // KIT-9: the server line elides only when the row really has no room left for it.
    function test_the_server_line_does_not_elide_while_there_is_room() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "downSpeed": "12.3 MB/s",
                                               "upSpeed": "999.9 KB/s",
                                               "serverLine": "seedbox-in-a-far-away-datacentre.a-rather-long-domain.example.org · qBittorrent 5.2.3",
                                               "serverLineShort": "seedbox-in-a-far-away-datacentre.a-rather-long-domain.example.org"
                                           });
        const identity = findChild(hero, "serverLine").parent.parent;
        const spacer = hero.children[0].children.find(c => c.objectName === "speedsSpacerRight");
        verify(spacer, "no right spacer");
        let squeezedOnce = false;
        for (let w = 700; w <= 1300; w += 10) {
            hero.width = Theme.space(w);
            waitForItemPolished(hero.children[0]);
            verify(overflow(hero) <= 0.5, "at " + w + " the row runs " + overflow(hero) + " px over");
            if (!identity.squeezed)
                continue;
            squeezedOnce = true;
            // Eliding, the slack spacer must have nothing left to give it.
            verify(spacer.width < 1, "at " + w + " the server line elides with " + spacer.width
                   + " px spare");
        }
        verify(squeezedOnce, "the probe never squeezed the server line");
    }

    function test_step_four_keeps_the_dot_beside_the_mark() {
        const hero = createTemporaryObject(heroComponent, testCase);
        hero.width = Theme.space(500);
        compare(hero.level, 4);
        verify(findChild(hero, "loneDot").visible);
        verify(hero.downStat.visible);
        verify(hero.addButton.visible);
        verify(hero.moreButton.visible);
    }
}
