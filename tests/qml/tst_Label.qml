import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Label"

    // A test that switches the skin leaves Omarchy's for the next, even when it fails.
    function cleanup() {
        TestTheme.setSkinChoice("omarchy"); // qmllint disable unqualified
    }

    Component {
        id: labelComponent

        Label {}
    }

    function test_default_colour_is_foreground() {
        const label = createTemporaryObject(labelComponent, testCase, {
                                                "text": "plain"
                                            });
        compare(label.color, Theme.foreground);
        compare(label.font.pixelSize, Theme.font.body);
    }

    function test_tracked_is_uppercase_caption_sized_and_dim() {
        const label = createTemporaryObject(labelComponent, testCase, {
                                                "text": "seeding",
                                                "tracked": true
                                            });
        compare(label.font.capitalization, Font.AllUppercase);
        compare(label.font.pixelSize, Theme.font.caption);
        compare(label.color, Theme.dimForeground);
        verify(label.font.letterSpacing > 0);
    }

    function test_bright_overrides_dim() {
        const label = createTemporaryObject(labelComponent, testCase, {
                                                "text": "title",
                                                "bright": true
                                            });
        compare(label.color, Theme.brightForeground);
    }

    function test_size_selects_a_theme_font_token() {
        const label = createTemporaryObject(labelComponent, testCase, {
                                                "text": "big",
                                                "size": "heading"
                                            });
        compare(label.font.pixelSize, Theme.font.heading);
    }

    // #171: a label that names itself carries the skin's kana word after its words, and a
    // tracked one takes the skin's voice; under Omarchy neither changes a thing.
    function test_a_named_label_carries_the_skins_kana() {
        TestTheme.setSkinChoice("console");
        const label = createTemporaryObject(labelComponent, testCase, {
                                                "text": "Library",
                                                "tracked": true,
                                                "labelId": "library"
                                            });
        const kana = findChild(label, "labelKana");
        compare(label.kana, "ライブラリ");
        compare(kana.text, "ライブラリ");
        compare(kana.font.family, Theme.kanaFamily);
        verify(label.rightPadding > 0);
        compare(label.implicitWidth, label.contentWidth + label.rightPadding);
        verify(kana.x >= label.contentWidth);
        compare(label.font.family, Theme.labelFamily);
        compare(label.font.weight, 600);
        compare(label.font.letterSpacing, Math.max(1, Math.round(Theme.font.caption * 0.16)));

        // Without an id, no kana, and no room kept for one.
        const plain = createTemporaryObject(labelComponent, testCase, {
                                                "text": "Library",
                                                "tracked": true
                                            });
        compare(plain.kana, "");
        compare(findChild(plain, "labelKana").text, "");
        compare(plain.rightPadding, 0);

        TestTheme.setSkinChoice("omarchy");
        if (Theme.skin.name !== "omarchy")
            return; // the suite's Console run pins its skin
        compare(label.kana, "");
        compare(kana.text, "");
        compare(label.rightPadding, 0);
        compare(label.font.weight, Font.Normal);
        compare(label.font.family, Theme.fontFamily);
        compare(label.font.letterSpacing, Math.max(1, Math.round(Theme.font.caption * 0.08)));
    }

    // #171 step 5: the skin's other voices. Console's heading is upper case, tracked by
    // `trackTitle` and bold; its control's words upper and tracked; its status line upper in
    // the value family. Omarchy's voices are the plain label's.
    function test_the_skins_voices() {
        TestTheme.setSkinChoice("console");
        const title = createTemporaryObject(labelComponent, testCase, {
                                                "text": "Library",
                                                "voice": "title"
                                            });
        const control = createTemporaryObject(labelComponent, testCase, {
                                                  "text": "add",
                                                  "voice": "control"
                                              });
        const status = createTemporaryObject(labelComponent, testCase, {
                                                 "text": "12 torrents",
                                                 "voice": "status"
                                             });
        compare(title.font.capitalization, Font.AllUppercase);
        compare(title.font.family, Theme.labelFamily);
        compare(title.font.weight, 700);
        fuzzyCompare(title.font.letterSpacing, Theme.font.body * 0.22, 0.05); // 26.6 fixed point
        compare(control.font.capitalization, Font.AllUppercase);
        compare(control.font.weight, 600);
        compare(status.font.capitalization, Font.AllUppercase);
        compare(status.font.family, Theme.valueFamily);
        fuzzyCompare(status.font.letterSpacing, Theme.font.body * 0.04, 0.05);

        TestTheme.setSkinChoice("omarchy");
        if (Theme.skin.name !== "omarchy")
            return; // the suite's Console run pins its skin
        for (const label of [title, control, status]) {
            compare(label.font.capitalization, Font.MixedCase);
            compare(label.font.family, Theme.fontFamily);
            compare(label.font.letterSpacing, 0);
        }
        compare(title.font.weight, Font.DemiBold);
        compare(control.font.weight, Font.Normal);
        compare(status.font.weight, Font.Normal);
    }
}
