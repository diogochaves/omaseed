pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// Views/CheckSheet, `omaseed check`'s picture (DECISIONS C37 U5): every part is there, by its
// objectName, drawn from the demo library. The suite runs once per skin (tst_qml,
// tst_qml_console), so both skins draw it.
TestCase {
    id: testCase

    height: 960
    name: "CheckSheet"
    visible: true
    when: windowShown
    width: 1440

    Component {
        id: sheetComponent

        CheckSheet {}
    }

    function initTestCase() {
        if (App.torrents.count === 0)
            App.loadDemo(16);
    }

    readonly property var parts: ["checkRows", "checkTable", "checkMenu", "checkConfirm",
        "checkNotice", "checkSeams", "checkPlate", "checkSelectionMark", "checkMeter", "checkLamp"]

    function test_every_part_is_there_data() {
        const rows = [];
        for (const part of testCase.parts)
            rows.push({
                          "tag": part,
                          "part": part
                      });
        return rows;
    }

    function test_every_part_is_there(data) {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        verify(sheet !== null);
        compare(sheet.objectName, "checkSheet");
        const part = findChild(sheet, data.part);
        verify(part !== null, data.part);
        verify(part.visible, data.part);
        verify(part.width > 0 && part.height > 0, data.part);
    }

    function test_the_rows_show_the_demo_library() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        tryVerify(() => findChild(sheet, "libraryRowCursor_0") !== null);
        verify(App.filtered.count > 8);
    }
}
