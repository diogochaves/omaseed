import QtQuick

// Stand-in for the shell's PanelSectionHeader (no accessibility of its own on either kit).
Text {
    property color foreground: "white"
    property string fontFamily: "sans-serif"
    property real fontSize: 11
    textFormat: Text.PlainText
}
