import QtQuick
import QtTest
import Omaseed.Views

// #146 (board 49 §4, DECISIONS C27 CR8, CR9): the add sheet's round-trip line, one case at a
// time, as App.madeHereFor hands them over. Names and hosts are invented.
TestCase {
    id: testCase

    height: 400
    name: "AddMadeLine"
    visible: true
    when: windowShown
    width: 700

    Component {
        id: lineComponent

        AddMadeLine {
            width: 640
        }
    }

    function make(made) {
        return createTemporaryObject(lineComponent, testCase, {
                                         "made": made
                                     });
    }

    function base(kind) {
        return {
            "kind": kind,
            "name": "field-recordings",
            "folder": "/mnt/media/share/field-recordings",
            "host": "tracker-a.example",
            "madeText": "made here today 14:20",
            "fileCount": 14,
            "madeHash": "",
            "madeName": "",
            "changedText": ""
        };
    }

    function test_nothing() {
        const line = make({});
        compare(line.visible, false);
        compare(line.replace, false);
    }

    function test_the_same_hash() {
        const line = make(base("same"));
        verify(line.visible);
        const fact = findChild(line, "addMadeFact");
        compare(fact.title, "Made here today 14:20: the same torrent");
        verify(fact.detail.indexOf("nothing downloads") > 0);
        compare(fact.kind, "ok");
        compare(findChild(line, "addMadeChoice").visible, false);
    }

    function test_the_same_files() {
        const made = base("files");
        made.changedText = "its source tag TA became TA-site";
        const line = make(made);
        const fact = findChild(line, "addMadeFact");
        compare(fact.title, "Made here today 14:20: the same 14 files, another hash");
        verify(fact.detail.indexOf("its source tag TA became TA-site · every file matches") === 0, fact.detail);
        compare(line.replace, false);
        // CR9: the made one is in the library: replace it by default, or keep both.
        made.madeHash = "a".repeat(40);
        made.madeName = "field-recordings";
        const asking = make(made);
        verify(findChild(asking, "addMadeInLibrary").visible);
        verify(findChild(asking, "addMadeChoice").visible);
        compare(asking.replace, true);
        findChild(asking, "addMadeChoice").choice = 1;
        compare(asking.replace, false);
    }

    function test_the_made_file_itself() {
        const line = make(base("made"));
        const fact = findChild(line, "addMadeFact");
        compare(fact.title, "This is the .torrent made here for tracker-a.example");
        compare(fact.kind, "warn");
        verify(fact.detail.indexOf("upload it") > 0);
    }
}
