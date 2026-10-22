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
        notice.width = Theme.space(560);
        verify(notice.stacked);
        verify(findChild(notice, "noticeActionsUnder").visible);
        verify(!findChild(notice, "noticeActionsBeside").visible);
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

    // #173: two hundred rows (a big move's record) stop at a third of the window and scroll
    // inside the block, clipped; what sits under the band moves down and is never drawn over.
    // The keys scroll it while it has them, and esc folds it.
    function test_long_details_stop_and_scroll_inside_the_band() {
        const rows = [];
        for (let i = 0; i < 200; ++i)
            rows.push({
                          "text": "moved · Torrent %1 · /mnt/slow/a → /mnt/fast/b".arg(i + 1)
                      });
        const column = createTemporaryQmlObject("import QtQuick; Column { width: 1100 }",
                                                testCase);
        const notice = noticeComponent.createObject(column, {
                                                        "detailRows": rows
                                                    });
        const below = createTemporaryQmlObject(
                        "import QtQuick; Rectangle { width: 1100; height: 40 }", column);
        const folded = notice.height;
        notice.toggleDetails();
        const block = findChild(notice, "noticeDetails");
        const scroll = findChild(notice, "noticeDetailsScroll");
        verify(block.visible);
        tryVerify(() => notice.height > folded, 1000);
        verify(notice.detailsMaxHeight >= testCase.height / 3 - 1);
        verify(block.height <= notice.detailsMaxHeight + 0.5,
               "the block is %1 tall, past %2".arg(block.height).arg(notice.detailsMaxHeight));
        verify(notice.height < testCase.height);
        verify(scroll.clip);
        verify(scroll.contentHeight > scroll.height * 3);
        tryVerify(() => below.y >= notice.y + notice.height, 1000);

        scroll.forceActiveFocus();
        verify(scroll.activeFocus);
        keyClick(Qt.Key_Down);
        verify(scroll.contentY > 0);
        keyClick(Qt.Key_End);
        fuzzyCompare(scroll.contentY, scroll.contentHeight - scroll.height, 1);
        keyClick(Qt.Key_Home);
        compare(scroll.contentY, 0);
        keyClick(Qt.Key_Escape);
        verify(!notice.detailsOpen);
        tryCompare(notice, "height", folded, 1000);
        notice.destroy();
    }

    // Short details take only their own height: nothing to scroll, Tab passes them by.
    function test_short_details_take_their_own_height() {
        const notice = createTemporaryObject(noticeComponent, testCase);
        notice.toggleDetails();
        const block = findChild(notice, "noticeDetails");
        const scroll = findChild(notice, "noticeDetailsScroll");
        // Once laid out: its width comes on a later polish than its height.
        tryVerify(() => scroll.width > 0 && scroll.height > 0, 1000);
        verify(block.height < notice.detailsMaxHeight, "%1 tall".arg(block.height));
        compare(scroll.contentY, 0);
        verify(!scroll.activeFocusOnTab, "content %1 in %2".arg(scroll.contentHeight).arg(
                   scroll.height));
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
