// STUB — replaced by T1.2 (ThemeReader + Theme singleton exposed from omaseed-integration).
// It exists so the shell can be built and linted before the real reader lands. The values are
// Omarchy's Tokyo Night at base size 12, hardcoded. This is the ONLY QML file in omaseed that
// is allowed to contain colour literals; everything else uses these roles.
pragma Singleton

import QtQuick

QtObject {
    id: root

    // ---- sizes -------------------------------------------------------------------------
    // Real value comes from shell.toml [font] base-size; 12 is the spec's reference size.
    readonly property real baseSize: 12
    readonly property real sizeScale: root.baseSize / 12

    /// Spacing in spec pixels, scaled by the base size.
    function space(px: real): real {
        return Math.round(px * root.sizeScale);
    }

    /// A foreground fill at the given alpha: 0.04 idle, 0.08 hover, 0.18 selected, 0.22 pressed.
    function fill(alpha: real): color {
        return Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, alpha);
    }

    readonly property real hairlineAlpha: 0.12
    readonly property real windowAlpha: 0.90
    readonly property real cornerRadius: 0
    readonly property string fontFamily: "monospace"

    // ---- colour roles ------------------------------------------------------------------
    readonly property color background: "#1a1b26"
    readonly property color foreground: "#a9b1d6"
    readonly property color dimForeground: "#565f89"
    readonly property color brightForeground: "#c0caf5"
    readonly property color accent: "#7aa2f7"
    readonly property color muted: "#414868"
    readonly property color red: "#f7768e"
    readonly property color yellow: "#e0af68"
    readonly property color orange: "#eb927b"
    readonly property color green: "#9ece6a"
    readonly property color cyan: "#449dab"
    readonly property color blue: "#7aa2f7"
    readonly property color magenta: "#ad8ee6"

    // ---- semantic state roles ----------------------------------------------------------
    readonly property color stateDownloading: root.blue
    readonly property color stateSeeding: root.green
    readonly property color stateStopped: root.dimForeground
    readonly property color stateError: root.red
    readonly property color stateChecking: root.yellow
    readonly property color stateQueued: root.magenta
    readonly property color stateStalled: root.orange

    // ---- font sizes --------------------------------------------------------------------
    component FontTokens: QtObject {
        id: tokens

        required property real base

        readonly property int caption: Math.round(tokens.base * 0.833)
        readonly property int small: Math.round(tokens.base * 0.917)
        readonly property int body: Math.round(tokens.base)
        readonly property int subtitle: Math.round(tokens.base * 1.083)
        readonly property int title: Math.round(tokens.base * 1.167)
        readonly property int heading: Math.round(tokens.base * 1.333)
        readonly property int display: Math.round(tokens.base * 2)
        readonly property int iconSmall: tokens.small
        readonly property int icon: tokens.subtitle
    }

    readonly property FontTokens font: FontTokens {
        base: root.baseSize
    }
}
