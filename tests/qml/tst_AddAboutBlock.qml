import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// Board 64 (DECISIONS C47 I15). `Views/AddAboutBlock.qml`: About this torrent under the add
// sheet's files, six rows in I15's order, a missing fact's row left out.
TestCase {
    id: testCase

    name: "AddAboutBlock"
    visible: true
    when: windowShown

    Component {
        id: blockComponent

        AddAboutBlock {
            width: 520
        }
    }

    /// A .torrent's whole about, as App::torrentFileMap makes it.
    function film() {
        return {
            "trackers": ["tracker.example.org", "open.example.net", "announce.example.com"],
            "trackerCount": 3,
            "isPrivate": true,
            "createdBy": "qBittorrent v5.0.1",
            "creationDate": 1730505600,
            "createdOn": "2 Nov 2024 00:00",
            "comment": "Remux of the festival cut.",
            "pieceCount": 296,
            "pieceLength": 8388608,
            "piecesText": "296 × 8 MiB",
            "hashKind": "hybrid",
            "infoHash": "9f2c00000000000000000000000000000000041ab"
        };
    }

    function make(about) {
        return createTemporaryObject(blockComponent, testCase, {
                                         "about": about
                                     });
    }

    function keys(block) {
        return block.rows.map(row => row[0]);
    }

    function value(block, key) {
        const row = block.rows.find(row => row[0] === key);
        return row ? row[1] : undefined;
    }

    function test_the_six_rows_in_order() {
        const block = testCase.make(testCase.film());
        compare(testCase.keys(block), ["trackers", "private", "created", "comment", "pieces",
                                       "hash"]);
        compare(testCase.value(block, "trackers"),
                "3 · tracker.example.org, open.example.net, announce.example.com");
        compare(testCase.value(block, "created"), "2 Nov 2024 00:00 by qBittorrent v5.0.1");
        compare(testCase.value(block, "comment"), "Remux of the festival cut.");
        compare(testCase.value(block, "pieces"), "296 × 8 MiB");
        verify(block.visible);
        // Drawn: a key and a value for each row.
        tryVerify(() => findChildrenNamed(block, "addAboutValue").length === 6);
        compare(findChildrenNamed(block, "addAboutKey").length, 6);
    }

    function test_a_missing_facts_row_is_left_out() {
        // A magnet before its list: the link's trackers and hash, nothing else.
        const block = testCase.make({
                                        "trackers": ["tracker.example.org"],
                                        "trackerCount": 1,
                                        "hashKind": "v1",
                                        "infoHash": "5c1e0000000000000000000000000000000007d2"
                                    });
        compare(testCase.keys(block), ["trackers", "hash"]);
        compare(testCase.value(block, "hash"), "v1 · 5c1e…07d2");
        // No tracker at all: no trackers row either.
        const bare = testCase.make({
                                       "trackers": [],
                                       "trackerCount": 0,
                                       "hashKind": "v2",
                                       "infoHash": "abcd" + "0".repeat(56) + "ef01"
                                   });
        compare(testCase.keys(bare), ["hash"]);
        compare(testCase.value(bare, "hash"), "v2 · abcd…ef01");
        // A date with no program, and nothing at all.
        const about = testCase.film();
        delete about.createdBy;
        compare(testCase.value(testCase.make(about), "created"), "2 Nov 2024 00:00");
        const none = testCase.make(null);
        compare(none.rows.length, 0);
        verify(!none.visible);
    }

    function test_private_words() {
        const about = testCase.film();
        compare(testCase.value(testCase.make(about), "private"), "yes: no DHT, no peer exchange");
        about.isPrivate = false;
        compare(testCase.value(testCase.make(about), "private"), "no");
    }

    function test_hybrids_hash_line() {
        compare(testCase.value(testCase.make(testCase.film()), "hash"),
                "v1 + v2 (hybrid) · 9f2c…41ab");
    }

    function test_an_empty_comment_says_none_dim() {
        const about = testCase.film();
        about.comment = "";
        const block = testCase.make(about);
        const row = block.rows.find(row => row[0] === "comment");
        compare(row[1], "none");
        compare(row[2], true);
        tryVerify(() => findChildrenNamed(block, "addAboutValue").some(label => label.text === "none"
                                                                                && label.dim));
        // A comment that is there is not dim.
        compare(testCase.make(testCase.film()).rows.find(row => row[0] === "comment")[2], false);
    }

    function findChildrenNamed(item, name) {
        let found = [];
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i];
            if (child.objectName === name)
                found.push(child);
            found = found.concat(testCase.findChildrenNamed(child, name));
        }
        return found;
    }
}
