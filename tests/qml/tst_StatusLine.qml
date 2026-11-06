import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// The status line (board 46 `statusLine()`, DECISIONS C24 H15): the server lane, ↓ ↑, alt
// speeds and the network's warning, the facts, the note slot and "? keys", in that order, its
// form from `Theme.statusLine*` tokens.
TestCase {
    id: testCase

    height: 200
    name: "StatusLine"
    visible: true
    when: windowShown
    width: 1500

    Component {
        id: lineComponent

        // Wide enough for step 0 at base 12.
        StatusLine {
            downSpeed: "4.21 MB/s"
            downSpeedShort: "4.21M"
            facts: [
                {
                    "value": "16",
                    "label": "torrents"
                },
                {
                    "value": "1.80 TB",
                    "label": "free"
                },
                {
                    "value": "",
                    "label": "synced now"
                }
            ]
            serverShortText: "home"
            serverText: "home · qBittorrent 5.2.3"
            upSpeed: "812 KB/s"
            upSpeedShort: "812K"
            width: 1400
        }
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }

    function make(props) {
        const line = createTemporaryObject(lineComponent, testCase, props || {});
        waitForItemPolished(line.children[1]);
        return line;
    }

    function spy(target, signalName) {
        return createTemporaryObject(spyComponent, testCase, {
                                         "target": target,
                                         "signalName": signalName
                                     });
    }

    // Board 46's order, left to right: lane, ↓, ↑, alt, facts, then "? keys" at the right edge.
    function test_the_parts_sit_in_the_boards_order() {
        const line = make();
        compare(line.height, Theme.statusLineHeight);
        const xOf = name => findChild(line, name).mapToItem(line, 0, 0).x;
        compare(xOf("statusServer"), Theme.statusLineInsetStart);
        verify(xOf("statusServer") < xOf("statusDown"));
        verify(xOf("statusDown") < xOf("statusUp"));
        verify(xOf("statusUp") < xOf("statusAlt"));
        verify(xOf("statusAlt") < xOf("statusFacts"));
        const keys = findChild(line, "statusKeys");
        compare(Math.round(xOf("statusKeys") + keys.width), line.width - Theme.statusLineInsetEnd);
        compare(keys.text, "? keys");
        compare(findChild(line, "statusDown").text, "↓ 4.21 MB/s");
        compare(findChild(line, "statusUp").text, "↑ 812 KB/s");
        compare(findChild(line, "statusDown").color, Theme.stateDownloading);
        compare(findChild(line, "statusUp").color, Theme.stateSeeding);
        compare(findChild(line, "statusServerName").font.weight, Theme.statusLineNameWeight);
    }

    // DECISIONS C24 H15, docs/33 D19: one server is named, with no ▾ and no click.
    function test_one_server_is_named_with_no_switch() {
        const line = make();
        const clicks = spy(line, "serverClicked");
        compare(findChild(line, "statusServerName").text, "home · qBittorrent 5.2.3");
        verify(!findChild(line, "statusServerChevron").visible);
        compare(line.serverLane.Accessible.role, Accessible.StaticText);
        compare(line.serverLane.Accessible.name, "home · qBittorrent 5.2.3");
        mouseClick(line.serverLane);
        line.serverLane.Accessible.pressAction();
        compare(clicks.count, 0);
        compare(line.serverTipText, "home · qBittorrent 5.2.3");
    }

    // docs/33 D3: two servers, the lane is a "switch server" button with a ▾; its click and a
    // screen reader's press ask for the palette, once each; disabled, neither.
    function test_two_servers_switch_from_the_lane() {
        const line = make({
                              "serverSwitchable": true
                          });
        const clicks = spy(line, "serverClicked");
        verify(findChild(line, "statusServerChevron").visible);
        compare(line.serverLane.Accessible.role, Accessible.Button);
        compare(line.serverLane.Accessible.name, "switch server");
        compare(line.serverLane.Accessible.description, "home · qBittorrent 5.2.3");
        compare(line.serverTipText, "home · qBittorrent 5.2.3\nswitch server · ctrl+s");
        mouseClick(line.serverLane);
        compare(clicks.count, 1);
        line.serverLane.Accessible.pressAction();
        compare(clicks.count, 2);
        line.enabled = false;
        line.serverLane.Accessible.pressAction();
        compare(clicks.count, 2, "a disabled line's lane ran");
    }

    // The lane's tooltip: the whole line, opened above the lane (the line is the window's
    // foot, KIT-8).
    function test_the_lane_tip_opens_above_it() {
        const line = make({
                              "serverSwitchable": true
                          });
        mouseMove(line.serverLane, 4, line.serverLane.height / 2);
        tryVerify(() => line.tip.shown, 2000, "the tip never opened");
        compare(line.tip.text, "home · qBittorrent 5.2.3\nswitch server · ctrl+s");
        mouseMove(testCase, testCase.width - 1, 0);
        tryVerify(() => !line.tip.shown);
    }

    // T5.4, docs/35 N3: the dot follows the server: none dim, first sync yellow, lost red,
    // retried yellow, live green.
    function test_the_dot_follows_the_server() {
        const line = make();
        const dot = findChild(line, "statusDot");
        compare(dot.color, Theme.green);
        line.connected = false;
        compare(dot.color, Theme.red);
        line.retrying = true;
        compare(dot.color, Theme.yellow);
        line.syncing = true;
        compare(dot.color, Theme.yellow);
        line.serverConfigured = false;
        compare(dot.color, Theme.dimForeground);
        line.connected = true;
        line.syncing = false;
        compare(dot.color, Theme.dimForeground, "no server still drew a live dot");
    }

    // #201 F4: on the way to a server, after a switch's reset (qBittorrent's network reads
    // "disconnected" then), the dot is neutral and no warning is read; red only on a loss.
    function test_a_switch_under_way_is_neutral() {
        const line = make();
        const dot = findChild(line, "statusDot");
        line.connectionStatus = "disconnected";
        line.connecting = true;
        verify(Qt.colorEqual(dot.color, Theme.fill(Theme.faintAlpha)), "a switch drew " + dot.color);
        compare(line.networkWarning, "");
        line.connecting = false;
        compare(dot.color, Theme.red);
        line.connectionStatus = "connected";
        compare(dot.color, Theme.green);
    }

    // docs/08 §3, docs/16 §5 (#167 assumed 5): silent while connected; firewalled and
    // disconnected are named after the speeds in colour, anonymous mode too, with the advice
    // as a screen reader's description; never while the API itself is down.
    function test_the_network_warning_follows_the_speeds() {
        const line = make();
        verify(!line.networkLabel.visible);
        line.connectionStatus = "firewalled";
        verify(line.networkLabel.visible);
        compare(line.networkLabel.text, "firewalled");
        compare(line.networkLabel.color, Theme.yellow);
        compare(findChild(line, "statusDot").color, Theme.yellow);
        line.listenPort = 26999;
        compare(line.networkLabel.text, "port 26999 not reachable");
        compare(line.networkLabel.Accessible.role, Accessible.AlertMessage);
        verify(line.networkLabel.Accessible.description.indexOf("UPnP") >= 0);
        waitForItemPolished(line.children[1]);
        verify(line.networkLabel.mapToItem(line, 0, 0).x > findChild(line, "statusAlt").mapToItem(line, 0, 0).x);
        line.anonymousWarning = true;
        compare(line.networkText, "port 26999 not reachable · anonymous mode");
        verify(line.networkAdvice.indexOf("private trackers") >= 0);
        verify(line.serverTipText.indexOf("home · qBittorrent 5.2.3 · port 26999 not reachable") === 0);
        line.connectionStatus = "disconnected";
        compare(line.networkLabel.color, Theme.red);
        compare(findChild(line, "statusDot").color, Theme.red);
        line.connected = false;
        compare(line.networkText, "");
        line.connected = true;
        line.syncing = true;
        compare(line.networkWarning, "");
    }

    // docs/31, manual box M14, #78 (#167 assumed 5): off, the glyph alone, named by its set
    // with its tooltip as the description; on, a chip tinted 18 % in the set's colour (C32
    // D6 a); a click opens the presets (D8 a).
    function test_the_alt_speed_chip_moved_here() {
        const line = make({});
        const chip = line.altChip;
        const clicks = spy(line, "altSpeedClicked");
        compare(chip.text, "");
        compare(chip.tooltip, "alt-speed · A");
        compare(chip.Accessible.name, "alt-speed");
        compare(chip.Accessible.description, "alt-speed · A");
        verify(!chip.warn);
        verify(!findChild(chip, "statusAltTint").visible);
        line.altSpeedActive = true;
        compare(chip.text, "alt-speed");
        compare(chip.tooltip, "alt-speed on · A");
        compare(findChild(chip, "statusAltText").color, Theme.on.turtle.yellow);
        const tint = findChild(chip, "statusAltTint");
        verify(tint.visible);
        compare(tint.color, Theme.on.turtle.fill);
        mouseClick(chip);
        compare(clicks.count, 1);
        chip.Accessible.pressAction();
        compare(clicks.count, 2);
    }

    // docs/31 H1: burst names itself and its end in green; from step 2 the word, from 3 the
    // glyph. H2: turtle keeps the yellow with its word. Mixed keeps the limits.
    function test_burst_turtle_and_mixed_sets() {
        const line = make({
                              "altKind": "burst",
                              "altUntil": "07:00",
                              "altSpeedActive": true,
                              "altTooltip": "burst until 07:00, by the schedule · A"
                          });
        const chip = line.altChip;
        compare(chip.glyph, Glyphs.burst);
        compare(chip.text, "burst");
        compare(chip.suffix, "until 07:00");
        compare(chip.tooltip, "burst until 07:00, by the schedule · A");
        compare(chip.Accessible.name, "burst");
        compare(findChild(chip, "statusAltText").color, Theme.on.burst.green);
        compare(findChild(chip, "statusAltTint").color, Theme.on.burst.fill);
        compare(line.altText(2), "burst");
        compare(line.altSuffix(2), "until 07:00");
        compare(line.altText(3), "");
        compare(line.altSuffix(3), "");
        line.altKind = "turtle";
        compare(chip.glyph, Glyphs.altSpeed);
        compare(chip.text, "turtle");
        compare(findChild(chip, "statusAltText").color, Theme.on.turtle.yellow);
        compare(findChild(chip, "statusAltTint").color, Theme.on.turtle.fill);
        line.altKind = "alt";
        compare(chip.text, "alt-speed");
        compare(chip.suffix, "until 07:00");
    }

    // C32 D6 a: while the alternative limits hold them, each speed reads against its cap in
    // the set's colour; off, or with no cap, the speed alone. D2 a: the title bar can take them.
    function test_speeds_against_their_caps() {
        const line = make({
                              "downSpeed": "290 KB/s",
                              "upSpeed": "48 KB/s",
                              "downParts": {
                                  "value": "290",
                                  "cap": "300",
                                  "unit": "KB/s"
                              },
                              "upParts": {
                                  "value": "48",
                                  "cap": "",
                                  "unit": "KB/s"
                              }
                          });
        const down = findChild(line, "statusDown");
        compare(down.text, "↓ 290 KB/s");
        line.altSpeedActive = true;
        line.altKind = "turtle";
        compare(down.text, "↓ 290 <font color=\"" + Theme.yellow + "\">/ 300</font> KB/s");
        compare(findChild(line, "statusUp").text, "↑ 48 KB/s");
        line.altKind = "burst";
        compare(down.text, "↓ 290 <font color=\"" + Theme.green + "\">/ 300</font> KB/s");
        line.speedsShown = false;
        verify(!down.visible);
        verify(!findChild(line, "statusUp").visible);
    }

    // The steps (Theme.statusLineSteps): the lane keeps the name alone from 1, the speeds go
    // short and "? keys" is "?" from 3, and nothing leaves the line.
    function test_the_steps_follow_the_width() {
        const line = make({
                              "altSpeedActive": true
                          });
        const steps = Theme.statusLineSteps;
        for (let i = 0; i < steps.length; ++i) {
            line.width = steps[i];
            compare(line.level, i, "at " + steps[i]);
            line.width = steps[i] - 1;
            compare(line.level, i + 1, "just under " + steps[i]);
        }
        line.width = steps[0];
        compare(findChild(line, "statusServerName").text, "home · qBittorrent 5.2.3");
        line.width = steps[1];
        compare(findChild(line, "statusServerName").text, "home");
        compare(line.altChip.text, "alt-speed");
        compare(findChild(line, "statusDown").text, "↓ 4.21 MB/s");
        line.width = steps[3];
        compare(findChild(line, "statusDown").text, "↓4.21M");
        compare(findChild(line, "statusUp").text, "↑812K");
        compare(findChild(line, "statusKeys").text, "?");
        compare(line.altChip.text, "");
        // A lost server keeps its words at every step: the caller leaves the short form empty.
        line.serverShortText = "";
        line.serverText = "home · isn't answering";
        compare(findChild(line, "statusServerName").text, "home · isn't answering");
    }

    // Narrow, with a long note, the keys stay on the line: the lane and the note elide first.
    function test_nothing_leaves_a_narrow_line() {
        const line = make({
                              "serverSwitchable": true,
                              "noteText": "Field Recordings wasn't moved: qBittorrent can't create or write the folder",
                              "noteMark": "✕",
                              "noteHasDetails": true
                          });
        // Down to the window's floor with the rail open (560 px at base 14, 280 at base 12).
        for (const w of [1400, 1000, 800, 600, 400, 280]) {
            line.width = Theme.space(w);
            waitForItemPolished(line.children[1]);
            const keys = findChild(line, "statusKeys");
            const right = keys.mapToItem(line, 0, 0).x + keys.width;
            verify(right <= line.width - Theme.statusLineInsetEnd + 0.5, "at " + w + " the keys end at "
                   + right + " of " + line.width);
            verify(findChild(line, "statusDot").visible);
        }
    }

    // #79: the figures by their words; #77: ↑ is the seed console's button while it opens it.
    function test_a_screen_reader_hears_the_figures() {
        const line = make({
                              "upNote": "18.4 GB today"
                          });
        const down = findChild(line, "statusDown");
        compare(down.Accessible.role, Accessible.StaticText);
        compare(down.Accessible.name, "download");
        compare(down.Accessible.description, "4.21 MB/s");
        const up = findChild(line, "statusUp");
        compare(up.Accessible.role, Accessible.StaticText);
        compare(up.Accessible.name, "upload");
        compare(up.Accessible.description, "812 KB/s · 18.4 GB today");
        const ups = spy(line, "upClicked");
        up.Accessible.pressAction();
        compare(ups.count, 0);
        line.upOpensSeed = true;
        compare(up.Accessible.role, Accessible.Button);
        compare(up.Accessible.name, "seed console");
        up.Accessible.pressAction();
        compare(ups.count, 1);
        mouseClick(up);
        compare(ups.count, 2);
    }

    // The note slot (M16, docs/35 N8, docs/31 F1, docs/32 N1): the mark's colour, the note's
    // own (accent, sticky foreground, a logged failure yellow), "details" and "· i details".
    function test_the_note_slot() {
        const line = make();
        const note = findChild(line, "footerNote");
        const mark = findChild(line, "footerNoteMark");
        const details = findChild(line, "footerFailureDetails");
        const hint = findChild(line, "footerNoteHint");
        verify(!note.visible);
        line.noteText = "copied";
        verify(note.visible);
        verify(!mark.visible);
        compare(note.color, Theme.accent);
        compare(note.Accessible.role, Accessible.AlertMessage);
        compare(note.Accessible.name, "copied");
        waitForItemPolished(line.children[1]);
        verify(note.mapToItem(line, 0, 0).x > findChild(line, "statusFacts").mapToItem(line, 0, 0).x);
        // With room to spare the note is whole, not elided, whatever fraction of a pixel its
        // words end on.
        verify(!note.truncated);
        for (const words of ["copied", "copied name", "reannounced", "nothing to copy"]) {
            line.noteText = words;
            waitForItemPolished(line.children[1]);
            verify(!note.truncated, words + " elided at " + note.width + " of " + note.implicitWidth);
        }
        line.noteText = "copied";
        line.noteSticky = true;
        line.noteMark = "▲";
        verify(mark.visible);
        compare(mark.color, Theme.green);
        compare(note.color, Theme.foreground);
        line.noteMark = "✕";
        compare(mark.color, Theme.red);
        line.noteHasDetails = true;
        verify(details.visible);
        const toggles = spy(line, "detailsToggled");
        mouseClick(details);
        compare(toggles.count, 1);
        line.noteDetailsOpen = true;
        compare(details.text, "details " + Glyphs.chevronDown);
        line.noteWarn = true;
        compare(note.color, Theme.yellow);
        line.noteHintShown = true;
        verify(hint.visible);
        compare(hint.Accessible.name, "show details");
        const hints = spy(line, "noteHintClicked");
        hint.Accessible.pressAction();
        compare(hints.count, 1);
        line.noteText = "";
        verify(!note.visible && !details.visible && !hint.visible);
    }

    // "? keys" (C24 H10): a button to the `?` sheet, its key in the description.
    function test_keys_open_the_help() {
        const line = make();
        const keys = findChild(line, "statusKeys");
        compare(keys.Accessible.role, Accessible.Button);
        compare(keys.Accessible.name, "keys");
        compare(keys.Accessible.description, "shortcut ?");
        const clicks = spy(line, "keysClicked");
        mouseClick(keys);
        compare(clicks.count, 1);
    }

    // docs/32 D12: the log's face says whether it follows, before "? keys".
    function test_the_log_follow_state() {
        const line = make();
        const follow = findChild(line, "logFollowState");
        verify(!follow.visible);
        line.logFollowShown = true;
        verify(follow.visible);
        compare(follow.text, "● following");
        compare(follow.color, Theme.green);
        line.logFollowing = false;
        compare(follow.text, "○ paused");
        compare(follow.Accessible.name, "paused");
    }

    // The facts: value in the foreground, label dim, each after a `·`; one label for a screen
    // reader.
    function test_the_facts_are_one_label() {
        const line = make({
                              "factsSpoken": "16 torrents, 1.80 TB free, synced now"
                          });
        const facts = findChild(line, "statusFacts");
        compare(facts.Accessible.role, Accessible.StaticText);
        compare(facts.Accessible.name, "16 torrents, 1.80 TB free, synced now");
        line.facts = [];
        verify(!facts.visible);
    }

    // DECISIONS C24 H10: alt held, the server lane shows ctrl+s, with two servers only (D19).
    function test_alt_badges_the_server_lane_with_two_servers() {
        const line = make({
                              "serverSwitchable": false,
                              "keysShown": true
                          });
        const badge = findChild(line, "statusServerKey");
        verify(!badge.visible);
        const width = line.serverLane.width;
        line.serverSwitchable = true;
        verify(badge.visible);
        compare(badge.key, "ctrl+s");
        line.keysShown = false;
        verify(!badge.visible);
        line.keysShown = true;
        waitForItemPolished(line);
        verify(line.serverLane.width >= width);
    }
}
