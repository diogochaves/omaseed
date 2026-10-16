pragma Singleton
import QtQuick

// The shell's colours, as far as the widget's rows read them (tests/plugin/kit/README.md).
QtObject {
    readonly property color foreground: "white"
    readonly property color background: "black"
    readonly property color accent: "steelblue"
    readonly property color urgent: "red"
}
