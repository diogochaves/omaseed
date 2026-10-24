import QtQuick
import Omaseed.Kit

// The text roles on the ground a row or control is painted on (docs/DECISIONS.md C30 F1–F4):
// `ground` is one of `Theme.on`'s, usually `Theme.ground(selected, cursor, hovered, pressed)`,
// and each role here is that ground's, lifted where it sits on it and nowhere else. When the
// ground changes, the colours move with the fill's own colour change (`Theme.fillChangeDuration`,
// 120 ms), so a hovered or pressed row's text never reads short on its way. A row keeps one and
// paints every word in it: `ink.dimForeground` on a selected row is the caption lifted against
// the selected fill, `ink[row.stateRole]` its state colour.
QtObject {
    id: root

    /// The ground the text sits on: `Theme.on.window` until the caller says otherwise.
    property ThemeInk ground: Theme.on.window
    /// Whether a change of ground eases over the fill's change; off, the colours jump.
    property bool animated: true

    /// What the ground paints over the window.
    readonly property color fill: root.ground.fill

    property color foreground: root.ground.foreground
    property color dimForeground: root.ground.dimForeground
    property color brightForeground: root.ground.brightForeground
    property color accent: root.ground.accent
    property color red: root.ground.red
    property color yellow: root.ground.yellow
    property color orange: root.ground.orange
    property color green: root.ground.green
    property color cyan: root.ground.cyan
    property color blue: root.ground.blue
    property color magenta: root.ground.magenta

    // The state roles read the palette roles, as Theme's do.
    readonly property color stateDownloading: root.blue
    readonly property color stateSeeding: root.green
    readonly property color stateStopped: root.dimForeground
    readonly property color stateError: root.red
    readonly property color stateChecking: root.yellow
    readonly property color stateQueued: root.magenta
    readonly property color stateStalled: root.orange

    /// `colour`, one of the window's text roles (a caller's `tone`, `Theme.yellow`), as it
    /// reads on this ground (`Theme.inkOf`, eased): a colour that is no role comes back as it is.
    function of(colour: color): color {
        const w = Theme.on.window;
        if (Qt.colorEqual(colour, w.foreground))
            return root.foreground;
        if (Qt.colorEqual(colour, w.brightForeground))
            return root.brightForeground;
        if (Qt.colorEqual(colour, w.dimForeground))
            return root.dimForeground;
        if (Qt.colorEqual(colour, w.accent))
            return root.accent;
        if (Qt.colorEqual(colour, w.red))
            return root.red;
        if (Qt.colorEqual(colour, w.yellow))
            return root.yellow;
        if (Qt.colorEqual(colour, w.orange))
            return root.orange;
        if (Qt.colorEqual(colour, w.green))
            return root.green;
        if (Qt.colorEqual(colour, w.cyan))
            return root.cyan;
        if (Qt.colorEqual(colour, w.blue))
            return root.blue;
        if (Qt.colorEqual(colour, w.magenta))
            return root.magenta;
        return colour;
    }

    Behavior on foreground {
        enabled: root.animated

        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }
    Behavior on dimForeground {
        enabled: root.animated

        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }
    Behavior on brightForeground {
        enabled: root.animated

        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }
    Behavior on accent {
        enabled: root.animated

        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }
    Behavior on red {
        enabled: root.animated

        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }
    Behavior on yellow {
        enabled: root.animated

        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }
    Behavior on orange {
        enabled: root.animated

        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }
    Behavior on green {
        enabled: root.animated

        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }
    Behavior on cyan {
        enabled: root.animated

        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }
    Behavior on blue {
        enabled: root.animated

        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }
    Behavior on magenta {
        enabled: root.animated

        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }
}
