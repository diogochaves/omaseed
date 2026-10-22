import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// One labelled row inside a Sheet (spec v2 §3 `.sheet .row`: a tracked caption over the
// control, 4 px apart). The control is the default content, so a row reads exactly like the
// figure's markup:
//
//     FormRow { label: qsTr("New path on home"); Field { Layout.fillWidth: true } }
//
// Children are the `ColumnLayout`'s own default content, so they take `Layout.*` attached
// properties and stack under the label; a row with two controls side by side puts a
// `RowLayout`/`GridLayout` of its own inside.
ColumnLayout {
    id: root

    /// The tracked caption above the control. Empty hides the label entirely.
    property string label: ""
    /// False when the control's own accessible name is the caption (#78's limit fields).
    property bool spokenCaption: true
    /// #91: a Field straight in the row that has no `accessibleName` of its own takes the
    /// caption as its name (a placeholder is an example, not a name). The caption is then
    /// said once, as the field's name, and the label stays out of the tree.
    readonly property bool captionNamesField: {
        for (const child of root.children) {
            if ((child as Field)?.namedByRow)
                return true;
        }
        return false;
    }

    spacing: Theme.space(4)

    // #79: the caption is text, just before its control in reading order, so a field that
    // names itself by its value still has its row's words before it. A control that already
    // carries the caption as its own name sets `spokenCaption: false`, so it is said once;
    // a Field named by the row (#91) does the same by itself.
    Label {
        Accessible.ignored: !root.spokenCaption || root.captionNamesField
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        elide: Text.ElideRight
        text: root.label
        tracked: true
        visible: root.label.length > 0
    }
}
