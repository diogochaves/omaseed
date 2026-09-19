import QtQuick
import Omaseed.Kit

// A 1 px rule at the theme's hairline alpha (spec v2 token `--hairline`, 12 % — plan §3 rule
// 2). Horizontal by default (a footer or section rule, e.g. spec §1 `.hero` bottom border);
// `vertical` makes it a column divider instead (e.g. spec §1 `.details` left border). The
// caller positions it: anchor both ends across the axis it does not fix itself (this
// component only fixes the 1 px thickness on its own axis and the colour), or give it an
// explicit length on the other axis.
Rectangle {
    id: root

    property bool vertical: false

    color: Theme.fill(Theme.hairlineAlpha)
    height: root.vertical ? undefined : 1
    width: root.vertical ? 1 : undefined
}
