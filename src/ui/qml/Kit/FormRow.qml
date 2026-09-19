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

    spacing: Theme.space(4)

    Label {
        Layout.fillWidth: true
        elide: Text.ElideRight
        text: root.label
        tracked: true
        visible: root.label.length > 0
    }
}
