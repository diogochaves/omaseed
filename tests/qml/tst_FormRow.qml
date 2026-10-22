import QtQuick
import QtQuick.Layouts
import QtTest
import Omaseed.Kit

// T4.1. `Kit/FormRow.qml`: the tracked caption over a control of spec v2 §3 `.sheet .row`.
TestCase {
    id: testCase

    height: 200
    name: "FormRow"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: rowComponent

        FormRow {
            label: "New path on home"

            Field {
                Layout.fillWidth: true
            }
        }
    }

    function test_the_label_is_a_tracked_caption() {
        const row = createTemporaryObject(rowComponent, testCase);
        const label = row.children[0];
        verify(label.tracked, "the row's label is not tracked");
        compare(label.text, "New path on home");
        compare(label.font.pixelSize, Theme.font.caption);
        compare(label.color, Theme.dimForeground);
        // #79: a screen reader hears the caption's words, before the control.
        compare(label.Accessible.role, Accessible.StaticText);
        compare(label.Accessible.name, "New path on home");
        // #91: the field takes the caption as its name, so the caption is said once.
        verify(label.Accessible.ignored);
        row.children[1].accessibleName = "path";
        verify(!label.Accessible.ignored);
        // A control named by the caption already says it: the caption stays out.
        row.spokenCaption = false;
        verify(label.Accessible.ignored);
    }

    // #91: a field straight in a row, with no name of its own, is named by the caption; its
    // placeholder, an example, is the description.
    function test_the_caption_names_the_field() {
        const row = createTemporaryObject(rowComponent, testCase);
        const field = row.children[1];
        field.placeholder = "/mnt/media";
        const input = findChild(field, "fieldInput");
        verify(field.namedByRow);
        compare(input.Accessible.name, "New path on home");
        compare(input.Accessible.description, "/mnt/media");
        field.accessibleName = "path";
        verify(!field.namedByRow);
        compare(input.Accessible.name, "path");
        row.label = "";
        field.accessibleName = "";
        compare(input.Accessible.name, "/mnt/media");
    }

    function test_the_gap_between_label_and_control_is_four() {
        const row = createTemporaryObject(rowComponent, testCase);
        compare(row.spacing, Theme.space(4));
    }

    function test_the_control_is_the_default_content() {
        const row = createTemporaryObject(rowComponent, testCase);
        compare(row.children.length, 2);
        compare(row.children[1].height, Theme.controlHeight);
    }

    function test_an_empty_label_hides_the_caption() {
        const row = createTemporaryObject(rowComponent, testCase, {
                                              "label": ""
                                          });
        verify(!row.children[0].visible, "an empty label still draws");
    }
}
