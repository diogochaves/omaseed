pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// #41 (docs/27 L2/L3): the line under a field a folder was browsed into.
TestCase {
    id: testCase

    height: 200
    name: "BrowsedPathLine"
    visible: true
    when: windowShown
    width: 500

    Component {
        id: lineComponent

        BrowsedPathLine {
            width: 480
        }
    }

    function test_nothing_without_a_pick_or_a_map() {
        const line = createTemporaryObject(lineComponent, testCase);
        compare(line.visible, false);
        line.pick = {
            "path": "/home/me/Videos",
            "local": "/home/me/Videos",
            "mapped": false,
            "outside": false,
            "mappings": ""
        };
        compare(line.visible, false);
        compare(line.text, "");
    }

    function test_a_mapped_pick_says_what_the_server_sees() {
        const line = createTemporaryObject(lineComponent, testCase);
        line.pick = {
            "path": "/downloads/movies",
            "local": "/mnt/media/downloads/movies",
            "mapped": true,
            "outside": false,
            "mappings": "/mnt/media/downloads → /downloads"
        };
        compare(line.visible, true);
        compare(line.outside, false);
        compare(line.text, "picked /mnt/media/downloads/movies · the server sees it as /downloads/movies");
    }

    function test_a_pick_outside_every_mapping_warns() {
        const line = createTemporaryObject(lineComponent, testCase);
        line.serverName = "home";
        line.pick = {
            "path": "/home/me/Videos",
            "local": "/home/me/Videos",
            "mapped": false,
            "outside": true,
            "mappings": "/mnt/media/downloads → /downloads"
        };
        compare(line.visible, true);
        verify(line.text.indexOf("/home/me/Videos is outside the folders mapped for home.") >= 0);
        verify(line.text.indexOf("qBittorrent may not see it. Mapped: /mnt/media/downloads → /downloads.") >= 0);
        verify(line.text.indexOf(String(Theme.on.warning.yellow)) >= 0, "the headline is not yellow");
        // DECISIONS C30 F5: on the yellow band, its dim words are lifted against the tint.
        verify(line.text.indexOf(String(Theme.on.warning.dimForeground)) >= 0,
               "the dim words are not the warning ground's");
        verify(!Qt.colorEqual(Theme.on.warning.dimForeground, Theme.dimForeground));
        compare(findChild(line, "browsedPathText").color, Theme.on.warning.dimForeground);
    }
}
