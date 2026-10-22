import QtQuick
import QtTest
import "../../plugin"

// #111: a speed in a horizontal bar centres its figures' height, as Omarchy 4.5's labels do,
// where the widget sees that kit (`centerFigures`); otherwise, and in a vertical bar, the
// line box, as before and as 4.0.x's labels do.
Item {
    id: stage
    width: 200
    height: 100

    FontMetrics {
        id: metrics
        font.family: "sans-serif"
        font.pixelSize: 14 // the kit's Style.font.body
    }

    Component {
        id: field

        SpeedField {
            height: 26
            value: "8.8M"
            fontFamily: "sans-serif"
            valueSlot: 40
            arrowSlot: 10
        }
    }

    TestCase {
        name: "PluginSpeedField"
        when: windowShown

        function textsOf(item) {
            var out = [];
            for (var i = 0; i < item.children.length; i++)
                if (item.children[i].text !== undefined)
                    out.push(item.children[i]);
            return out;
        }

        function test_lineBoxByDefault() {
            const f = createTemporaryObject(field, stage);
            compare(f.figureOffset, 0);
            const texts = textsOf(f);
            compare(texts.length, 2);
            for (var i = 0; i < texts.length; i++)
                compare(texts[i].y, Math.round((f.height - texts[i].height) / 2));
        }

        function test_figuresCentredWhereTheKitDoes() {
            const f = createTemporaryObject(field, stage, {
                centerFigures: true
            });
            const expected = Math.round((metrics.descent - metrics.ascent + metrics.tightBoundingRect("0").height) / 2);
            compare(f.figureOffset, expected);
            // The digits' middle is the field's middle, to the pixel.
            const value = textsOf(f)[1];
            const figureMiddle = value.y + metrics.ascent - metrics.tightBoundingRect("0").height / 2;
            verify(Math.abs(figureMiddle - f.height / 2) <= 1, "figures centred: " + figureMiddle);
            // The arrow moves with them, so the two stay on one line.
            compare(textsOf(f)[0].y - value.y, Math.round((f.height - textsOf(f)[0].height) / 2) - Math.round((f.height - value.height) / 2));
        }

        function test_aVerticalBarStacksAsBefore() {
            const f = createTemporaryObject(field, stage, {
                centerFigures: true,
                vertical: true
            });
            compare(f.figureOffset, 0);
            compare(textsOf(f)[0].y, 0);
        }
    }
}
