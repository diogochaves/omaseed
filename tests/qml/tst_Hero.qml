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
        id: serverClickedSpy

        SignalSpy {
            signalName: "serverClicked"
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

    // docs/33 D3: with two servers or more the server line takes a ▾ and a click; one server,
    // nothing. docs/35 N3: a server omaseed retries on its own has a yellow dot, one that
    // needs you a red one.
    function test_the_server_line_switches_with_two_servers() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "serverLine": "private · qBittorrent 5.2.3"
                                           });
        const label = findChild(hero, "serverLine");
        const mouse = findChild(hero, "serverLineMouse");
        compare(label.text, "private · qBittorrent 5.2.3");
        verify(!mouse.enabled);
        hero.serverSwitchable = true;
        compare(label.text, "private · qBittorrent 5.2.3 " + Glyphs.chevronDown);
        verify(mouse.enabled);
        let clicks = 0;
        hero.serverClicked.connect(() => clicks++);
        mouse.clicked(null);
        compare(clicks, 1);
        verify(findChild(hero, "loneDotMouse").enabled);

        hero.connected = false;
        compare(hero.statusDot.color, Theme.red);
        hero.retrying = true;
        compare(hero.statusDot.color, Theme.yellow);
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

        // docs/16 §5: with the port known, the line says which one peers cannot reach.
        hero.listenPort = 26999;
        verify(hero.networkLabel.text.indexOf("port 26999 not reachable") >= 0,
               hero.networkLabel.text);
        verify(hero.networkAdvice.indexOf("UPnP") >= 0);

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

    // docs/16 §5: anonymous mode with private torrents is named in the warning colour.
    function test_anonymous_mode_is_named_while_it_is_on() {
        const hero = createTemporaryObject(heroComponent, testCase);
        verify(!hero.networkLabel.visible);
        hero.anonymousWarning = true;
        verify(hero.networkLabel.visible);
        verify(hero.networkLabel.text.indexOf("anonymous mode") >= 0, hero.networkLabel.text);
        compare(hero.networkLabel.color, Theme.yellow);
        compare(hero.statusDot.color, Theme.yellow);
        verify(hero.networkAdvice.indexOf("private trackers") >= 0);
        // Both at once, joined.
        hero.connectionStatus = "firewalled";
        hero.listenPort = 26999;
        compare(hero.networkText, "port 26999 not reachable · anonymous mode");
        // Not while the API itself is down: that is what the line says then.
        hero.connected = false;
        compare(hero.networkText, "");
    }

    // docs/08 §3 and manual box M14: off, alt-speed is its glyph alone, named by its tooltip;
    // on, it prints the limits.
    function test_the_alt_speed_chip_shows_the_limits_while_on() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "altDownLimit": "10 KB/s",
                                               "altUpLimit": "∞"
                                           });
        compare(hero.altChip.text, "");
        compare(hero.altChip.tooltip, "alt-speed · A");
        verify(!hero.altChip.warn);
        hero.altSpeedActive = true;
        compare(hero.altChip.text, "↓10 KB/s ↑∞");
        compare(hero.altChip.tooltip, "alt-speed on · A");
        verify(hero.altChip.warn);
        compare(hero.altChip.border.color, Theme.yellow);

        let clicks = 0;
        hero.altSpeedClicked.connect(() => clicks++);
        hero.altChip.clicked();
        compare(clicks, 1);
    }

    // docs/31 H1 and the burst steps 0 to 3: the word, the limits and "until", in green with
    // the needle up; from step 2 the word and "until", from step 3 the glyph.
    function test_a_burst_chip_names_itself_and_its_end() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "altKind": "burst",
                                               "altUntil": "07:00",
                                               "altDownLimit": "∞",
                                               "altUpLimit": "∞",
                                               "altTooltip": "burst until 07:00, by the schedule · A"
                                           });
        // Off: the glyph alone, its needle already up.
        compare(hero.altChip.text, "");
        compare(hero.altChip.suffix, "");
        compare(hero.altChip.glyph, Glyphs.burst);
        verify(!hero.altChip.warn);
        hero.altSpeedActive = true;
        compare(hero.level, 0);
        compare(hero.altChip.text, "burst ↓∞ ↑∞");
        compare(hero.altChip.suffix, "· until 07:00");
        compare(hero.altChip.tooltip, "burst until 07:00, by the schedule · A");
        verify(hero.altChip.warn);
        compare(hero.altChip.border.color, Theme.green);
        compare(findChild(hero.altChip, "chipLabel").color, Theme.green);
        compare(hero._altText(1), "burst ↓∞ ↑∞");
        compare(hero._altText(2), "burst");
        compare(hero._altSuffix(2), "· until 07:00");
        compare(hero._altText(3), "");
        compare(hero._altSuffix(3), "");
        hero.width = Theme.space(800);
        compare(hero.level, 2);
        compare(hero.altChip.text, "burst");
        compare(hero.altChip.suffix, "· until 07:00");
        // No schedule, no end.
        hero.altUntil = "";
        compare(hero.altChip.suffix, "");
    }

    // docs/31 H2 and the turtle steps: today's yellow chip with its word and its end. Someone
    // who uses the set as a turtle keeps everything they had.
    function test_a_turtle_chip_is_todays_with_its_word() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "altKind": "turtle",
                                               "altUntil": "18:00",
                                               "altSpeedActive": true,
                                               "altDownLimit": "10 KB/s",
                                               "altUpLimit": "10 KB/s",
                                               "altDownLimitShort": "10K",
                                               "altUpLimitShort": "10K"
                                           });
        compare(hero.altChip.text, "turtle ↓10 KB/s ↑10 KB/s");
        compare(hero.altChip.suffix, "· until 18:00");
        compare(hero.altChip.glyph, Glyphs.altSpeed);
        compare(hero.altChip.border.color, Theme.yellow);
        compare(hero._altText(1), "turtle ↓10K ↑10K");
        compare(hero._altText(2), "turtle");
        compare(hero._altText(3), "");
        // Today's tooltip until the caller gives one.
        compare(hero.altChip.tooltip, "alt-speed on · A");
    }

    // Mixed or the same: today's chip, unchanged at every step, and no "until" on it.
    function test_a_mixed_set_keeps_todays_chip() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "altKind": "alt",
                                               "altUntil": "07:00",
                                               "altSpeedActive": true,
                                               "altDownLimit": "2.00 MB/s",
                                               "altUpLimit": "∞",
                                               "altDownLimitShort": "2.00M",
                                               "altUpLimitShort": "∞"
                                           });
        compare(hero.altChip.text, "↓2.00 MB/s ↑∞");
        compare(hero.altChip.suffix, "");
        compare(hero._altText(1), "↓2.00M ↑∞");
        compare(hero._altText(2), "↓2.00M ↑∞");
        compare(hero._altText(3), "");
        compare(hero.altChip.border.color, Theme.yellow);
    }

    // The ladder measures the chip as it is drawn at each step, word and end included.
    function test_the_ladder_measures_the_named_chip() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "altSpeedActive": true,
                                               "altDownLimit": "10 KB/s",
                                               "altUpLimit": "10 KB/s"
                                           });
        const plain = hero._rowWidth(0);
        hero.altKind = "turtle";
        hero.altUntil = "18:00";
        // The measuring copies are Rows: they lay out at the next polish.
        tryVerify(() => hero._rowWidth(0) > plain);
        tryVerify(() => hero._rowWidth(2) < hero._rowWidth(1));
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

    // #78: the alt-speed chip is named by its set, never by the limits or the time it prints
    // (read out again with every change), and off it is no unnamed glyph; its tooltip is the
    // description.
    function test_the_alt_speed_chip_is_named_by_its_set() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "altDownLimit": "10 KB/s",
                                               "altUpLimit": "∞"
                                           });
        compare(hero.altChip.Accessible.name, "alt-speed");
        compare(hero.altChip.Accessible.description, "alt-speed · A");
        hero.altSpeedActive = true;
        compare(hero.altChip.Accessible.name, "alt-speed");
        compare(hero.altChip.Accessible.description, "alt-speed on · A");
        hero.altKind = "burst";
        compare(hero.altChip.Accessible.name, "burst");
        hero.altKind = "turtle";
        compare(hero.altChip.Accessible.name, "turtle");
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
                                               "downSpeedShort": "12.3M",
                                               "upSpeed": "999.9 KB/s",
                                               "upSpeedShort": "1000K",
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
                                               "downSpeed": "0 B/s",
                                               "upSpeed": "0 B/s",
                                               "serverLine": "playground · qBittorrent 5.1.2",
                                               "serverLineShort": "playground",
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

    // KIT-8: the whole server line opens below the hero, not over the line it spells out.
    function test_the_server_tip_opens_below_the_hero() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "serverLine": "home · qBittorrent 5.2.3",
                                               "serverLineShort": "home"
                                           });
        hero.width = Theme.space(1000); // step 1: the tip carries the version
        const server = findChild(hero, "serverLine");
        waitForItemPolished(hero.children[0]);
        mouseMove(server, 2, server.height / 2);
        tryVerify(() => hero.serverTip.shown, 2000, "the tip never opened");
        const panel = hero.serverTip.panelItem;
        const top = panel.mapToItem(hero, 0, 0).y;
        verify(top >= hero.height, "the tip covers the hero: its top is at " + top);
        compare(hero.serverTip.text, "home · qBittorrent 5.2.3");
        mouseMove(testCase, testCase.width - 1, testCase.height - 1);
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

    // docs/15 Q1 A, measured: with "4.21 MB/s" and "812 KB/s" the last step needed 562 px at
    // the 560 px floor (base 14) and ~62 px more per chip. With the speeds short and the chips
    // in the `/` button, the row fits the floor with no filters and with all four.
    function test_step_four_fits_the_floor_with_zero_and_four_filters() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "downSpeed": "4.21 MB/s",
                                               "upSpeed": "812 KB/s",
                                               "downSpeedShort": "4.21M",
                                               "upSpeedShort": "812K",
                                               "altSpeedActive": true,
                                               "altDownLimit": "10.0 MB/s",
                                               "altUpLimit": "1.00 MB/s",
                                               "altDownLimitShort": "10.0M",
                                               "altUpLimitShort": "1.00M",
                                               "serverLine": "home · qBittorrent 5.2.3",
                                               "serverLineShort": "home"
                                           });
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
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "downSpeed": "4.21 MB/s",
                                               "downSpeedShort": "4.21M",
                                               "upSpeed": "812 KB/s",
                                               "upSpeedShort": "812K"
                                           });
        let asked = 0;
        hero.filtersClicked.connect(() => asked += 1);
        hero.filterChips = testCase.fourChips;
        const button = findChild(hero, "filterButton");

        // Steps 0–3 keep the chips and the plain `/`.
        hero.width = Theme.space(900);
        verify(hero.level < 4, "level " + hero.level);
        verify(!button.visible);
        verify(findChild(hero, "filterChip_tag").visible);
        compare(hero.downStat.value, "4.21 MB/s");

        hero.width = Theme.space(480);
        compare(hero.level, 4);
        verify(hero.filtersInButton);
        verify(button.visible);
        verify(!findChild(hero, "searchButton").visible);
        verify(!findChild(hero, "filterChip_tag").visible);
        compare(button.border.color, Theme.accent);
        compare(findChild(hero, "filterCount").text, "4");
        compare(hero.downStat.value, "4.21M");

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

    function test_step_four_keeps_the_dot_beside_the_mark() {
        const hero = createTemporaryObject(heroComponent, testCase);
        hero.width = Theme.space(500);
        compare(hero.level, 4);
        verify(findChild(hero, "loneDot").visible);
        verify(hero.downStat.visible);
        verify(hero.addButton.visible);
        verify(hero.moreButton.visible);
    }

    // #75: a screen reader's press on the server line (when it switches) and on the filters
    // button is their click, once each.
    function test_the_accessible_press_is_the_click() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "serverLine": "private · qBittorrent 5.2.3",
                                               "serverSwitchable": true
                                           });
        const server = createTemporaryObject(serverClickedSpy, testCase, {
                                                 "target": hero
                                             });
        const filters = createTemporaryObject(filtersClickedSpy, testCase, {
                                                  "target": hero
                                              });
        findChild(hero, "serverLineMouse").Accessible.pressAction();
        compare(server.count, 1);
        findChild(hero, "filterButton").Accessible.pressAction();
        compare(filters.count, 1);
    }

    // #75: a server line that can't switch, and a disabled hero, ignore a screen reader's
    // press, as they ignore the pointer.
    function test_what_takes_no_click_ignores_the_accessible_press() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "serverLine": "private · qBittorrent 5.2.3"
                                           });
        const server = createTemporaryObject(serverClickedSpy, testCase, {
                                                 "target": hero
                                             });
        const filters = createTemporaryObject(filtersClickedSpy, testCase, {
                                                  "target": hero
                                              });
        const line = findChild(hero, "serverLineMouse");
        line.Accessible.pressAction();
        compare(server.count, 0, "a server line that can't switch ran");
        hero.serverSwitchable = true;
        hero.enabled = false;
        line.Accessible.pressAction();
        findChild(hero, "filterButton").Accessible.pressAction();
        compare(server.count, 0, "a disabled hero's server line ran");
        compare(filters.count, 0, "a disabled hero's filters button ran");
    }

    readonly property var twoViews: [{
            "key": "library",
            "label": "library"
        }, {
            "key": "seed",
            "label": "seed",
            "hint": "S"
        }]

    // #77: the views are page tabs, the current one selected, and the ↑ number a button to
    // the seed console; a screen reader's press is their click, once each.
    function test_the_view_tabs_and_the_up_number_take_the_accessible_press() {
        const hero = createTemporaryObject(heroComponent, testCase, {
                                               "views": testCase.twoViews,
                                               "currentView": "library"
                                           });
        const library = findChild(hero, "viewTab_library");
        const seed = findChild(hero, "viewTab_seed");
        compare(seed.Accessible.role, Accessible.PageTab);
        compare(seed.Accessible.name, "seed");
        compare(seed.Accessible.description, "S");
        verify(library.Accessible.selected);
        verify(!seed.Accessible.selected);
        const picked = [];
        hero.viewSelected.connect(key => picked.push(key));
        seed.Accessible.pressAction();
        compare(picked, ["seed"]);

        const up = findChild(hero, "upBox");
        compare(up.Accessible.role, Accessible.Button);
        compare(up.Accessible.name, "seed console");
        verify(!up.Accessible.ignored);
        let ups = 0;
        hero.upClicked.connect(() => ups++);
        up.Accessible.pressAction();
        compare(ups, 1);

        hero.enabled = false;
        seed.Accessible.pressAction();
        up.Accessible.pressAction();
        compare(picked.length, 1, "a disabled hero's tab ran");
        compare(ups, 1, "a disabled hero's ↑ ran");
    }

    // #77: without the views the ↑ number is no control; the lone dot is the server line's
    // button only while the server can switch.
    function test_the_up_number_and_the_lone_dot_without_a_click() {
        const hero = createTemporaryObject(heroComponent, testCase);
        const up = findChild(hero, "upBox");
        verify(up.Accessible.ignored);
        let ups = 0;
        hero.upClicked.connect(() => ups++);
        up.Accessible.pressAction();
        compare(ups, 0);

        const dot = findChild(hero, "loneDot");
        compare(dot.Accessible.role, Accessible.Button);
        compare(dot.Accessible.name, "switch server");
        verify(dot.Accessible.ignored);
        const server = createTemporaryObject(serverClickedSpy, testCase, {
                                                 "target": hero
                                             });
        dot.Accessible.pressAction();
        compare(server.count, 0, "a dot that can't switch ran");
        hero.serverSwitchable = true;
        verify(!dot.Accessible.ignored);
        dot.Accessible.pressAction();
        compare(server.count, 1);
    }
}
