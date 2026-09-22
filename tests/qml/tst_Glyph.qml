import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Glyph"

    Component {
        id: glyphComponent

        Glyph {}
    }

    // docs/08 §2: the symbols the UI font lacks are SVG icons, not characters.
    function test_the_font_gaps_are_icons() {
        const names = ["settings", "keyboard", "altSpeed", "jump", "statusChecking", "category",
                       "folder", "appearance", "server", "more", "columns", "sidebar", "limits",
                       "selectAll"];
        for (const name of names) {
            const glyph = createTemporaryObject(glyphComponent, testCase, {
                                                    "text": Glyphs[name]
                                                });
            verify(glyph.isIcon, name + " should draw as an icon");
            const icon = glyph.children[1]; // [0] is the Text, [1] the Icon
            verify(icon.known, name + " names an icon Icon.qml does not have");
        }
    }

    function test_a_symbol_stays_text() {
        const glyph = createTemporaryObject(glyphComponent, testCase, {
                                                "text": Glyphs.arrowDown
                                            });
        verify(!glyph.isIcon);
        verify(glyph.implicitWidth > 0);
    }

    function test_an_icon_is_square_at_its_pixel_size() {
        const glyph = createTemporaryObject(glyphComponent, testCase, {
                                                "text": Glyphs.settings,
                                                "pixelSize": 18
                                            });
        compare(glyph.implicitWidth, 18);
        compare(glyph.implicitHeight, 18);
    }
}
