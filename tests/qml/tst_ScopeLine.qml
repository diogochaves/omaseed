pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// `Views/ScopeLine.qml` (docs/34 D1 a, D2 a; #70): a Backup pane's first line. The info
// banner's accent rule on the idle fill, the text wrapped to the width, a link in accent with
// its key in dim before it; a click on the link sends `linkActivated`. Without a link, only
// the text, escaped.
TestCase {
    id: testCase

    height: 400
    name: "ScopeLine"
    visible: true
    when: windowShown
    width: 700

    Component {
        id: lineComponent

        ScopeLine {
            width: 400
        }
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }

    function test_the_banner_look_and_a_link() {
        const line = createTemporaryObject(lineComponent, testCase, {
                                               "text": "omaseed's own: its settings, your 2 servers (no passwords) and their seed history, with a copy of each server's qBittorrent settings.",
                                               "linkKey": "t",
                                               "linkText": "qBittorrent › Backup ›"
                                           });
        compare(line.color, Theme.fill(Theme.fillIdle));
        compare(line.children[0].color, Theme.accent); // the rule
        compare(line.children[0].width, 1);
        const text = findChild(line, "scopeText");
        verify(text.lineCount > 1, "wraps to the width");
        verify(line.implicitHeight > text.implicitHeight);
        verify(text.text.indexOf("<a href='scope'>") > 0, text.text);
        verify(text.text.indexOf("qBittorrent › Backup ›") > 0);
        // #79: a screen reader hears the plain words, not the markup; #91: through stand-ins,
        // the label itself out of the tree, and the link one it can press.
        verify(text.Accessible.ignored);
        const words = findChild(line, "scopeTextName");
        compare(words.Accessible.role, Accessible.StaticText);
        compare(words.Accessible.name, line.text);
        const link = findChild(line, "scopeLinkName");
        verify(link.visible);
        compare(link.Accessible.role, Accessible.Link);
        compare(link.Accessible.name, "qBittorrent › Backup ›");
        compare(link.Accessible.description, "shortcut t");
        const spy = createTemporaryObject(spyComponent, testCase, {
                                              "target": line,
                                              "signalName": "linkActivated"
                                          });
        text.linkActivated("scope");
        compare(spy.count, 1);
        link.Accessible.pressAction();
        compare(spy.count, 2);
        line.linkText = "";
        verify(!link.visible, "a link with no words");
    }

    // #98: a folder's line stays one line; the text is elided, the key and link show whole.
    function test_one_line_elides_the_text_not_the_link() {
        const line = createTemporaryObject(lineComponent, testCase, {
                                               "text": "games · /home/sbx/Torrents/library/games · 5 torrents · 268 MB on disk · 2 save paths · 3 files no torrent owns",
                                               "linkKey": "shift+m",
                                               "linkText": "move this folder…",
                                               "oneLine": true
                                           });
        const text = findChild(line, "scopeText");
        const link = findChild(line, "scopeLink");
        compare(text.lineCount, 1);
        verify(text.truncated, "the text gives way");
        verify(link.visible);
        verify(link.text.indexOf("move this folder…") > 0, link.text);
        verify(link.width >= link.implicitWidth - 1, "the link is whole");
        compare(link.Accessible.role, Accessible.Link);
        compare(link.Accessible.name, "move this folder…");
        const spy = createTemporaryObject(spyComponent, testCase, {
                                              "target": line,
                                              "signalName": "linkActivated"
                                          });
        link.linkActivated("scope");
        compare(spy.count, 1);
    }

    function test_no_link_is_plain_and_escaped() {
        const line = createTemporaryObject(lineComponent, testCase, {
                                               "text": "a<b's own & more"
                                           });
        const text = findChild(line, "scopeText");
        compare(text.text, "a&lt;b's own &amp; more");
        verify(text.text.indexOf("<a ") < 0);
    }
}
