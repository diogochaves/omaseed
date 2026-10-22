import QtQuick
import QtTest
import Omaseed.Kit

// docs/35: the error notice's band. Its kinds, the actions beside the headline or under the
// line, a height that holds while the countdown counts and the kind changes, details that
// unfold inside it, and the grow it comes in with.
TestCase {
    id: testCase

    height: 600
    name: "Notice"
    visible: true
    when: windowShown
    width: 1200

    Component {
        id: noticeComponent

        Notice {
            animated: false
            body: "Nothing is listening at 127.0.0.1:8091."
            bodyTail: "omaseed tries again in 7 s."
            bodyTailChars: 28
            details: "network error · GET sync/maindata · Connection refused · 127.0.0.1:8091"
            kind: "warning"
            title: "private didn't answer"
            width: 1100
            actions: [
                {
                    "label": "retry now",
                    "key": "retry"
                },
                {
                    "label": "switch server",
                    "key": "switch"
                }
            ]
        }
    }

    Component {
        id: triggeredSpy

        SignalSpy {
            signalName: "actionTriggered"
        }
    }

    function test_kinds_take_their_glyph_and_colour() {
        const notice = createTemporaryObject(noticeComponent, testCase);
        compare(findChild(notice, "noticeGlyph").text, "!");
        compare(findChild(notice, "noticeRule").color, Theme.yellow);
        notice.kind = "error";
        compare(findChild(notice, "noticeGlyph").text, Glyphs.close);
        compare(findChild(notice, "noticeRule").color, Theme.red);
    }

    // Board 42 (DECISIONS C15 M10, M16): a folder's move uses the band too.
    function test_a_move_draws_its_sign_progress_and_failures() {
        const notice = createTemporaryObject(noticeComponent, testCase);
        notice.kind = "info";
        notice.glyph = "↻";
        compare(findChild(notice, "noticeGlyph").text, "↻");
        compare(findChild(notice, "noticeRule").color, Theme.accent);
        const line = findChild(notice, "noticeProgress");
        verify(!line.visible);
        notice.progress = 0.5;
        verify(line.visible);
        fuzzyCompare(line.width, notice.width / 2, 1);
        compare(line.color, Theme.accent);
        notice.bodyAlert = "2 failed";
        const text = findChild(notice, "noticeLine");
        compare(text.textFormat, Text.StyledText);
        verify(text.text.indexOf("2 failed") > 0);
        notice.bodyAlert = "";
        compare(text.textFormat, Text.PlainText);
        notice.kind = "success";
        notice.glyph = "";
        compare(findChild(notice, "noticeGlyph").text, Glyphs.check);
        compare(findChild(notice, "noticeRule").color, Theme.green);
    }

    function test_actions_beside_then_under_when_narrow() {
        const notice = createTemporaryObject(noticeComponent, testCase);
        verify(!notice.stacked);
        verify(findChild(notice, "noticeActionsBeside").visible);
        verify(!findChild(notice, "noticeActionsUnder").visible);
        const actions = findChild(notice, "noticeActions");
        const at = name => findChild(notice, name).mapToItem(notice, 0, 0);
        compare(actions.x, at("noticeActionsBeside").x);
        compare(actions.y, at("noticeActionsBeside").y);
        notice.width = Theme.space(560);
        verify(notice.stacked);
        verify(findChild(notice, "noticeActionsUnder").visible);
        verify(!findChild(notice, "noticeActionsBeside").visible);
        tryCompare(actions, "x", at("noticeActionsUnder").x);
        tryCompare(actions, "y", at("noticeActionsUnder").y);
        // #91: a screen reader reads the band's children in declaration order: the headline
        // and its line (inside the content column) before the actions.
        verify(actions.parent === notice);
        const children = notice.children;
        let content = -1;
        let links = -1;
        for (let i = 0; i < children.length; ++i) {
            if (children[i] === findChild(notice, "noticeLine").parent)
                content = i;
            if (children[i] === actions)
                links = i;
        }
        verify(content >= 0 && content < links, "the actions come before the line");
    }

    function test_height_holds_while_the_countdown_counts_and_the_kind_changes() {
        const notice = createTemporaryObject(noticeComponent, testCase, {
                                                 "width": Theme.space(560)
                                             });
        const before = notice.height;
        verify(before > 0);
        notice.bodyTail = "omaseed tries again in 10 s.";
        compare(notice.height, before);
        notice.bodyTail = "omaseed tries again now.";
        compare(notice.height, before);
        notice.kind = "error";
        compare(notice.height, before);
        // The slot keeps its width whatever the tail says.
        compare(findChild(notice, "noticeLine").text.length,
                "Nothing is listening at 127.0.0.1:8091. ".length + 28);
    }

    function test_details_unfold_inside_the_band() {
        const notice = createTemporaryObject(noticeComponent, testCase);
        const folded = notice.height;
        verify(!findChild(notice, "noticeDetails").visible);
        notice.toggleDetails();
        verify(findChild(notice, "noticeDetails").visible);
        tryVerify(() => notice.height > folded, 1000);
        compare(findChild(notice, "noticeDetailsText").text, notice.details);
        let copied = "";
        notice.copyRequested.connect(text => copied = text);
        // Offscreen, the test runner delivers no mouse events: the click's own handler.
        findChild(notice, "noticeCopy").children[0].clicked(null);
        compare(copied, notice.details);
        notice.toggleDetails();
        tryCompare(notice, "height", folded, 1000);
    }

    // #77: the details' "copy" is a link whose press is the click, once; disabled, nothing.
    function test_the_details_copy_is_a_link_a_screen_reader_presses() {
        const notice = createTemporaryObject(noticeComponent, testCase);
        notice.toggleDetails();
        const copy = findChild(notice, "noticeCopy");
        compare(copy.Accessible.role, Accessible.Link);
        compare(copy.Accessible.name, "copy");
        const copied = [];
        notice.copyRequested.connect(text => copied.push(text));
        copy.Accessible.pressAction();
        compare(copied, [notice.details]);
        notice.enabled = false;
        copy.Accessible.pressAction();
        compare(copied.length, 1, "a disabled notice copied");
    }

    // #79: the band is an alert named by its headline; the line under it is text named by
    // the body, its countdown the description (it ticks); the details are text once open, and
    // "details" is named without its chevron.
    function test_a_screen_reader_hears_the_headline_and_the_line() {
        const notice = createTemporaryObject(noticeComponent, testCase);
        compare(notice.Accessible.role, Accessible.AlertMessage);
        compare(notice.Accessible.name, "private didn't answer");
        const line = findChild(notice, "noticeLine");
        compare(line.Accessible.role, Accessible.StaticText);
        compare(line.Accessible.name, "Nothing is listening at 127.0.0.1:8091.");
        compare(line.Accessible.description, "omaseed tries again in 7 s.");
        notice.bodyTail = "omaseed tries again in 6 s.";
        compare(line.Accessible.name, "Nothing is listening at 127.0.0.1:8091.");
        const toggle = findChild(notice, "noticeDetailsToggle");
        compare(toggle.Accessible.name, "details");
        compare(toggle.Accessible.description, "folded");
        notice.toggleDetails();
        compare(toggle.Accessible.description, "unfolded");
        const details = findChild(notice, "noticeDetailsText");
        compare(details.Accessible.role, Accessible.StaticText);
        compare(details.Accessible.name, notice.details);
    }

    function test_an_action_says_its_key() {
        const notice = createTemporaryObject(noticeComponent, testCase);
        let key = "";
        notice.actionTriggered.connect(k => key = k);
        findChild(notice, "noticeAction-switch").children[0].clicked(null);
        compare(key, "switch");
    }

    // Board 43 J3 (DECISIONS C16 L12): the moves waiting behind this one, each with its link.
    function test_detail_rows_list_above_the_text_each_with_its_link() {
        const notice = createTemporaryObject(noticeComponent, testCase, {
                                                 "detailRows": [
                                                     {
                                                         "text": "2 torrents to /mnt/fast/films",
                                                         "link": "cancel"
                                                     },
                                                     {
                                                         "text": "Big Film to /mnt/fast/films",
                                                         "link": "cancel"
                                                     }
                                                 ]
                                             });
        notice.toggleDetails();
        verify(findChild(notice, "noticeDetails").visible);
        let picked = -1;
        notice.detailRowTriggered.connect(index => picked = index);
        const rows = [];
        const walk = item => {
            for (let i = 0; i < item.children.length; ++i) {
                if (item.children[i].objectName === "noticeDetailRowLink")
                    rows.push(item.children[i]);
                walk(item.children[i]);
            }
        };
        walk(notice);
        compare(rows.length, 2);
        compare(rows[1].text, "cancel");
        rows[1].children[0].clicked(null);
        compare(picked, 1);
        // Rows alone unfold too, without the text and its copy.
        notice.details = "";
        verify(findChild(notice, "noticeDetails").visible);
        verify(!findChild(notice, "noticeCopy").visible);
    }

    // Board 44 F1: "Why?" unfolds a short note, which has nothing to copy.
    function test_a_note_takes_its_own_link_and_no_copy() {
        const notice = createTemporaryObject(noticeComponent, testCase, {
                                                 "copyable": false,
                                                 "detailsLabel": "Why?"
                                             });
        const toggle = findChild(notice, "noticeDetailsToggle");
        verify(toggle.text.startsWith("Why?"));
        compare(toggle.Accessible.name, "Why?");
        notice.toggleDetails();
        verify(findChild(notice, "noticeDetailsText").visible);
        verify(!findChild(notice, "noticeCopy").visible);
    }

    function test_no_details_no_toggle() {
        const notice = createTemporaryObject(noticeComponent, testCase, {
                                                 "details": ""
                                             });
        verify(!findChild(notice, "noticeDetailsToggle").visible);
    }

    function test_shown_grows_from_nothing() {
        const notice = createTemporaryObject(noticeComponent, testCase, {
                                                 "shown": false
                                             });
        compare(notice.height, 0);
        verify(!notice.visible);
        notice.shown = true;
        verify(notice.height > 0);
        compare(notice.height, notice.fullHeight);
        // Animated, it takes 120 ms to get there.
        notice.shown = false;
        notice.animated = true;
        notice.shown = true;
        verify(notice.height < notice.fullHeight);
        tryCompare(notice, "height", notice.fullHeight, 1000);
    }

    // #75: a screen reader's press on an action is its click (the action's key, once), and
    // on "details" it unfolds them, as a click does.
    function test_the_accessible_press_on_a_link_is_the_click() {
        const notice = createTemporaryObject(noticeComponent, testCase);
        const triggered = createTemporaryObject(triggeredSpy, testCase, {
                                                    "target": notice
                                                });
        findChild(notice, "noticeAction-switch").Accessible.pressAction();
        compare(triggered.count, 1);
        compare(triggered.signalArguments[0][0], "switch");
        findChild(notice, "noticeDetailsToggle").Accessible.pressAction();
        verify(notice.detailsOpen);
    }

    // #75: disabled, the notice ignores a screen reader's press, as it ignores the pointer.
    function test_a_disabled_notice_ignores_the_accessible_press() {
        const notice = createTemporaryObject(noticeComponent, testCase, {
                                                 "enabled": false
                                             });
        const triggered = createTemporaryObject(triggeredSpy, testCase, {
                                                    "target": notice
                                                });
        findChild(notice, "noticeAction-switch").Accessible.pressAction();
        findChild(notice, "noticeDetailsToggle").Accessible.pressAction();
        compare(triggered.count, 0);
        verify(!notice.detailsOpen);
    }
}
