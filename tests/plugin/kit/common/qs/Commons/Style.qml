pragma Singleton
import QtQuick

// The shell's sizes, as far as the widget's rows read them (tests/plugin/kit/README.md).
QtObject {
    readonly property real cornerRadius: 0
    readonly property QtObject font: QtObject {
        readonly property string family: "sans-serif"
        readonly property real body: 14
        readonly property real bodySmall: 12
        readonly property real caption: 11
        readonly property real title: 16
        readonly property real heading: 20
        readonly property real display: 28
        readonly property real icon: 16
    }
    readonly property QtObject spacing: QtObject {
        readonly property real popupRowHeight: 28
        readonly property real controlHeight: 32
        readonly property real controlPaddingX: 8
        readonly property real controlPaddingY: 4
    }

    function space(n) {
        return n;
    }
}
