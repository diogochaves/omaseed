import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// Every token the Theme singleton exposes, on one scrollable page (spec v2 §8). This is the
// proof that the theme follows Omarchy: run `omaseed --demo gallery`, switch the theme with
// `omarchy-theme-set`, and every swatch, size and alpha below repaints without a restart.
Window {
    id: root

    // Colour roles, in the order the spec lists them. The bindings read Theme directly, so
    // the whole list is rebuilt when the theme changes.
    readonly property var colourRoles: [
        {
            "name": "background",
            "value": Theme.background
        },
        {
            "name": "darkBackground",
            "value": Theme.darkBackground
        },
        {
            "name": "darkerBackground",
            "value": Theme.darkerBackground
        },
        {
            "name": "lighterBackground",
            "value": Theme.lighterBackground
        },
        {
            "name": "foreground",
            "value": Theme.foreground
        },
        {
            "name": "dimForeground",
            "value": Theme.dimForeground
        },
        {
            "name": "brightForeground",
            "value": Theme.brightForeground
        },
        {
            "name": "accent",
            "value": Theme.accent
        },
        {
            "name": "muted",
            "value": Theme.muted
        },
        {
            "name": "selection",
            "value": Theme.selection
        },
        {
            "name": "red",
            "value": Theme.red
        },
        {
            "name": "yellow",
            "value": Theme.yellow
        },
        {
            "name": "orange",
            "value": Theme.orange
        },
        {
            "name": "green",
            "value": Theme.green
        },
        {
            "name": "cyan",
            "value": Theme.cyan
        },
        {
            "name": "blue",
            "value": Theme.blue
        },
        {
            "name": "magenta",
            "value": Theme.magenta
        },
        {
            "name": "brown",
            "value": Theme.brown
        }
    ]
    readonly property var stateRoles: [
        {
            "name": "stateDownloading",
            "value": Theme.stateDownloading
        },
        {
            "name": "stateSeeding",
            "value": Theme.stateSeeding
        },
        {
            "name": "stateStopped",
            "value": Theme.stateStopped
        },
        {
            "name": "stateError",
            "value": Theme.stateError
        },
        {
            "name": "stateChecking",
            "value": Theme.stateChecking
        },
        {
            "name": "stateQueued",
            "value": Theme.stateQueued
        },
        {
            "name": "stateStalled",
            "value": Theme.stateStalled
        }
    ]
    readonly property var typeScale: [
        {
            "name": "caption ×0.833",
            "size": Theme.font.caption
        },
        {
            "name": "small ×0.917",
            "size": Theme.font.small
        },
        {
            "name": "body ×1",
            "size": Theme.font.body
        },
        {
            "name": "subtitle ×1.083",
            "size": Theme.font.subtitle
        },
        {
            "name": "title ×1.167",
            "size": Theme.font.title
        },
        {
            "name": "heading ×1.333",
            "size": Theme.font.heading
        },
        {
            "name": "display ×2",
            "size": Theme.font.display
        },
        {
            "name": "iconSmall",
            "size": Theme.font.iconSmall
        },
        {
            "name": "icon",
            "size": Theme.font.icon
        }
    ]
    readonly property var surfaceStates: [
        {
            "name": "idle",
            "fill": Theme.fillIdle,
            "border": Theme.borderIdle
        },
        {
            "name": "hover / cursor",
            "fill": Theme.fillHover,
            "border": Theme.borderHover
        },
        {
            "name": "selected",
            "fill": Theme.fillSelected,
            "border": 0
        },
        {
            "name": "pressed",
            "fill": Theme.fillPressed,
            "border": 0
        },
        {
            "name": "hairline",
            "fill": Theme.hairlineAlpha,
            "border": 0
        }
    ]
    readonly property var spacingSteps: [2, 4, 6, 8, 12, 16, 24, 32]

    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.windowAlpha)
    height: 820
    title: qsTr("omaseed — theme gallery")
    visible: true
    width: 1180

    // The 1 px window edge, as spec v2 §1: accent while active, muted otherwise.
    Rectangle {
        anchors.fill: parent
        border.color: root.active ? Theme.accent : Theme.muted
        border.width: 1
        color: "transparent"
        radius: Theme.cornerRadius
        z: 1
    }

    Flickable {
        id: page

        anchors.fill: parent
        anchors.margins: Theme.space(18)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        contentHeight: body.implicitHeight
        contentWidth: width

        Column {
            id: body

            spacing: Theme.space(22)
            width: page.width

            // ---- head ----------------------------------------------------------------
            Column {
                spacing: Theme.space(4)
                width: parent.width

                Text {
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.heading
                    font.weight: Font.DemiBold
                    text: Theme.themeName
                }

                Text {
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: qsTr("%1 · base %2 px · radius %3 px · window alpha %4 · %5").arg(
                              Theme.mode).arg(Theme.font.baseSize).arg(Theme.cornerRadius).arg(
                              Theme.windowAlpha.toFixed(2)).arg(Theme.fontFamily)
                }
            }

            // ---- kit (T1.3) ------------------------------------------------------------
            // Every Kit/*.qml component in every state, side by side, matching spec v2 §8's
            // control-states table: idle (no chrome unless bordered), bordered, hover/cursor,
            // focus, selected, pressed, primary, danger, disabled.
            Column {
                spacing: Theme.space(14)
                width: parent.width

                GalleryLabel {
                    text: qsTr("kit")
                }

                // Glyphs (Kit/Glyphs.qml): every stand-in the spec's figures use.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("glyphs")
                    }

                    Flow {
                        spacing: Theme.space(10)
                        width: parent.width

                        Repeater {
                            model: [
                                {
                                    "name": "mark",
                                    "glyph": Glyphs.mark
                                },
                                {
                                    "name": "arrowDown",
                                    "glyph": Glyphs.arrowDown
                                },
                                {
                                    "name": "arrowUp",
                                    "glyph": Glyphs.arrowUp
                                },
                                {
                                    "name": "add",
                                    "glyph": Glyphs.add
                                },
                                {
                                    "name": "altSpeed",
                                    "glyph": Glyphs.altSpeed
                                },
                                {
                                    "name": "burst",
                                    "glyph": Glyphs.burst
                                },
                                {
                                    "name": "settings",
                                    "glyph": Glyphs.settings
                                },
                                {
                                    "name": "help",
                                    "glyph": Glyphs.help
                                },
                                {
                                    "name": "search",
                                    "glyph": Glyphs.search
                                },
                                {
                                    "name": "folder",
                                    "glyph": Glyphs.folder
                                },
                                {
                                    "name": "trayIdle",
                                    "glyph": Glyphs.trayIdle
                                },
                                {
                                    "name": "trayDownloading",
                                    "glyph": Glyphs.trayDownloading
                                },
                                {
                                    "name": "trayAltSpeed",
                                    "glyph": Glyphs.trayAltSpeed
                                },
                                {
                                    "name": "trayError",
                                    "glyph": Glyphs.trayError
                                },
                                {
                                    "name": "trayDisconnected",
                                    "glyph": Glyphs.trayDisconnected
                                },
                                {
                                    "name": "statusAll",
                                    "glyph": Glyphs.statusAll
                                },
                                {
                                    "name": "statusDownloading",
                                    "glyph": Glyphs.statusDownloading
                                },
                                {
                                    "name": "statusSeeding",
                                    "glyph": Glyphs.statusSeeding
                                },
                                {
                                    "name": "statusCompleted",
                                    "glyph": Glyphs.statusCompleted
                                },
                                {
                                    "name": "statusStopped",
                                    "glyph": Glyphs.statusStopped
                                },
                                {
                                    "name": "statusActive",
                                    "glyph": Glyphs.statusActive
                                },
                                {
                                    "name": "statusStalled",
                                    "glyph": Glyphs.statusStalled
                                },
                                {
                                    "name": "statusChecking",
                                    "glyph": Glyphs.statusChecking
                                },
                                {
                                    "name": "statusErrored",
                                    "glyph": Glyphs.statusErrored
                                },
                                {
                                    "name": "statusQueued",
                                    "glyph": Glyphs.statusQueued
                                },
                                {
                                    "name": "category",
                                    "glyph": Glyphs.category
                                },
                                {
                                    "name": "tag",
                                    "glyph": Glyphs.tag
                                },
                                {
                                    "name": "tracker",
                                    "glyph": Glyphs.tracker
                                },
                                {
                                    "name": "chevronRight",
                                    "glyph": Glyphs.chevronRight
                                },
                                {
                                    "name": "chevronDown",
                                    "glyph": Glyphs.chevronDown
                                },
                                {
                                    "name": "check",
                                    "glyph": Glyphs.check
                                },
                                {
                                    "name": "close",
                                    "glyph": Glyphs.close
                                },
                                {
                                    "name": "dot",
                                    "glyph": Glyphs.dot
                                }
                            ]

                            Column {
                                id: glyphSample

                                required property var modelData

                                spacing: Theme.space(2)
                                width: Theme.space(72)

                                Glyph {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    color: Theme.foreground
                                    pixelSize: Theme.font.icon
                                    text: glyphSample.modelData.glyph
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    color: Theme.dimForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.font.caption
                                    text: glyphSample.modelData.name
                                }
                            }
                        }
                    }
                }

                // Button + IconButton: idle, bordered, hover/cursor, focus, selected, pressed,
                // primary, danger, disabled.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("button")
                    }

                    Flow {
                        spacing: Theme.space(10)
                        width: parent.width

                        Captioned {
                            caption: qsTr("idle")

                            Button {
                                text: qsTr("Button")
                            }
                        }

                        Captioned {
                            caption: qsTr("bordered")

                            Button {
                                bordered: true
                                text: qsTr("Button")
                            }
                        }

                        Captioned {
                            caption: qsTr("hover / cursor")

                            Button {
                                hasCursor: true
                                text: qsTr("Button")
                            }
                        }

                        Captioned {
                            caption: qsTr("focus")

                            Button {
                                text: qsTr("Button")

                                Component.onCompleted: forceActiveFocus()
                            }
                        }

                        Captioned {
                            caption: qsTr("selected")

                            Button {
                                selected: true
                                text: qsTr("Button")
                            }
                        }

                        Captioned {
                            caption: qsTr("pressed")

                            Button {
                                pressed: true
                                text: qsTr("Button")
                            }
                        }

                        Captioned {
                            caption: qsTr("primary")

                            Button {
                                primary: true
                                text: qsTr("Button")
                            }
                        }

                        Captioned {
                            caption: qsTr("danger")

                            Button {
                                danger: true
                                text: qsTr("Button")
                            }
                        }

                        Captioned {
                            caption: qsTr("danger selected")

                            Button {
                                danger: true
                                selected: true
                                text: qsTr("Button")
                            }
                        }

                        Captioned {
                            caption: qsTr("disabled")

                            Button {
                                enabled: false
                                text: qsTr("Button")
                            }
                        }
                    }

                    Flow {
                        spacing: Theme.space(10)
                        width: parent.width

                        Captioned {
                            caption: qsTr("idle")

                            IconButton {
                                Accessible.name: qsTr("add")
                                glyph: Glyphs.add
                            }
                        }

                        Captioned {
                            caption: qsTr("bordered")

                            IconButton {
                                Accessible.name: qsTr("add")
                                bordered: true
                                glyph: Glyphs.add
                            }
                        }

                        Captioned {
                            caption: qsTr("hover / cursor")

                            IconButton {
                                Accessible.name: qsTr("add")
                                glyph: Glyphs.add
                                hasCursor: true
                            }
                        }

                        Captioned {
                            caption: qsTr("selected")

                            IconButton {
                                Accessible.name: qsTr("settings")
                                glyph: Glyphs.settings
                                selected: true
                            }
                        }

                        Captioned {
                            caption: qsTr("pressed")

                            IconButton {
                                Accessible.name: qsTr("settings")
                                glyph: Glyphs.settings
                                pressed: true
                            }
                        }

                        Captioned {
                            caption: qsTr("primary")

                            IconButton {
                                Accessible.name: qsTr("add")
                                glyph: Glyphs.add
                                primary: true
                            }
                        }

                        Captioned {
                            caption: qsTr("danger")

                            IconButton {
                                Accessible.name: qsTr("close")
                                danger: true
                                glyph: Glyphs.close
                            }
                        }

                        Captioned {
                            caption: qsTr("disabled")

                            IconButton {
                                Accessible.name: qsTr("settings")
                                enabled: false
                                glyph: Glyphs.settings
                            }
                        }
                    }
                }

                // Field: empty, with text, with a live count.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("field")
                    }

                    Flow {
                        spacing: Theme.space(10)
                        width: parent.width

                        Captioned {
                            caption: qsTr("empty")

                            Field {
                                hint: Glyphs.search
                                placeholder: qsTr("filter torrents")
                            }
                        }

                        Captioned {
                            caption: qsTr("with text")

                            Field {
                                accessibleName: qsTr("filter torrents")
                                hint: Glyphs.search
                                text: qsTr("fedora 45")
                            }
                        }

                        Captioned {
                            caption: qsTr("with count")

                            Field {
                                accessibleName: qsTr("filter torrents")
                                hint: Glyphs.search
                                text: qsTr("fedora 45")
                                trailing: qsTr("0 of 142")
                            }
                        }
                    }
                }

                // Chip: idle, hover/cursor, selected.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("chip")
                    }

                    Flow {
                        spacing: Theme.space(10)
                        width: parent.width

                        Captioned {
                            caption: qsTr("idle")

                            Chip {
                                text: qsTr("linux")
                            }
                        }

                        Captioned {
                            caption: qsTr("hover / cursor")

                            Chip {
                                hasCursor: true
                                text: qsTr("linux")
                            }
                        }

                        Captioned {
                            caption: qsTr("selected")

                            Chip {
                                checkable: true
                                selected: true
                                text: qsTr("linux")
                            }
                        }
                    }
                }

                // Toggle: off, on.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("toggle")
                    }

                    Row {
                        spacing: Theme.space(18)

                        Toggle {
                            checked: false
                            text: qsTr("off")
                        }

                        Toggle {
                            checked: true
                            text: qsTr("on")
                        }
                    }
                }

                // Stat: the two hero headline numbers.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("stat")
                    }

                    Row {
                        spacing: Theme.space(24)

                        Stat {
                            glyph: Glyphs.arrowDown
                            glyphColor: Theme.stateDownloading
                            label: qsTr("download")
                            value: qsTr("4.21 MB/s")
                        }

                        Stat {
                            glyph: Glyphs.arrowUp
                            glyphColor: Theme.stateSeeding
                            label: qsTr("upload")
                            value: qsTr("812 KB/s")
                        }
                    }
                }

                // KeyHints: unboxed, pinned help hint pushed right.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("key hints")
                    }

                    KeyHints {
                        model: [
                            {
                                "key": "j k",
                                "label": qsTr("move")
                            },
                            {
                                "key": "space",
                                "label": qsTr("select")
                            },
                            {
                                "key": "enter",
                                "label": qsTr("actions")
                            },
                            {
                                "key": "/",
                                "label": qsTr("filter")
                            }
                        ]
                        pinned: ({
                                     "key": "?",
                                     "label": qsTr("help")
                                 })
                        width: parent.width
                    }
                }

                // Banner: error, warning, info.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("banner")
                    }

                    Column {
                        spacing: Theme.space(4)
                        width: parent.width

                        Banner {
                            actions: [
                                {
                                    "label": qsTr("retry now"),
                                    "key": "retry"
                                },
                                {
                                    "label": qsTr("settings"),
                                    "key": "settings"
                                }
                            ]
                            kind: "error"
                            text: qsTr("connection refused · retrying in 7 s")
                        }

                        Banner {
                            actions: [
                                {
                                    "label": qsTr("settings"),
                                    "key": "settings"
                                }
                            ]
                            kind: "warning"
                            text: qsTr("home is unreachable")
                        }

                        Banner {
                            kind: "info"
                            text: qsTr("theme switched to %1").arg(Theme.themeName)
                        }
                    }
                }

                // Hero: the full row, sample data (spec v2 §1 Fig. 1).
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("hero")
                    }

                    Rectangle {
                        border.color: Theme.accent
                        border.width: 1
                        color: Theme.background
                        height: heroSample.implicitHeight
                        width: parent.width

                        Hero {
                            id: heroSample

                            appName: "omaseed"
                            searchCount: 142
                            width: parent.width
                        }
                    }
                }

                // RailRow (T3.1): idle, hover/cursor, selected, both together.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("rail row")
                    }

                    Flow {
                        spacing: Theme.space(10)
                        width: parent.width

                        Captioned {
                            caption: qsTr("idle")

                            Rectangle {
                                border.color: Theme.fill(Theme.hairlineAlpha)
                                border.width: 1
                                color: Theme.background
                                height: railIdle.implicitHeight
                                width: Theme.space(180)

                                RailRow {
                                    id: railIdle

                                    count: 12
                                    glyph: Glyphs.statusDownloading
                                    text: qsTr("downloading")
                                    width: parent.width
                                }
                            }
                        }

                        Captioned {
                            caption: qsTr("hover / cursor")

                            Rectangle {
                                border.color: Theme.fill(Theme.hairlineAlpha)
                                border.width: 1
                                color: Theme.background
                                height: railCursor.implicitHeight
                                width: Theme.space(180)

                                RailRow {
                                    id: railCursor

                                    count: 3
                                    glyph: Glyphs.statusSeeding
                                    hasCursor: true
                                    text: qsTr("seeding")
                                    width: parent.width
                                }
                            }
                        }

                        Captioned {
                            caption: qsTr("selected")

                            Rectangle {
                                border.color: Theme.fill(Theme.hairlineAlpha)
                                border.width: 1
                                color: Theme.background
                                height: railSelected.implicitHeight
                                width: Theme.space(180)

                                RailRow {
                                    id: railSelected

                                    count: 142
                                    glyph: Glyphs.statusAll
                                    selected: true
                                    text: qsTr("all")
                                    width: parent.width
                                }
                            }
                        }

                        Captioned {
                            caption: qsTr("selected + cursor")

                            Rectangle {
                                border.color: Theme.fill(Theme.hairlineAlpha)
                                border.width: 1
                                color: Theme.background
                                height: railBoth.implicitHeight
                                width: Theme.space(180)

                                RailRow {
                                    id: railBoth

                                    count: -1
                                    glyph: Glyphs.category
                                    hasCursor: true
                                    selected: true
                                    text: qsTr("linux")
                                    width: parent.width
                                }
                            }
                        }
                    }
                }

                // TableHeader (T3.1): unsorted, sorted ascending, sorted descending, num column.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("table header")
                    }

                    Row {
                        spacing: 0
                        width: parent.width

                        TableHeader {
                            text: qsTr("name")
                            width: Theme.space(260)
                        }

                        TableHeader {
                            alignRight: true
                            sorted: true
                            descending: true
                            text: qsTr("size")
                            width: Theme.space(100)
                        }

                        TableHeader {
                            alignRight: true
                            sorted: true
                            descending: false
                            text: qsTr("eta")
                            width: Theme.space(100)
                        }
                    }
                }

                // TableRow (T3.1): idle, hover/cursor, selected, both together — two text
                // cells stand in for the columns TorrentTable (T3.3) will place there.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("table row")
                    }

                    Column {
                        spacing: 0
                        width: Theme.space(360)

                        Repeater {
                            model: [
                                {
                                    "caption": "idle",
                                    "selected": false,
                                    "hasCursor": false,
                                    "name": "ubuntu-24.04.iso"
                                },
                                {
                                    "caption": "hover / cursor",
                                    "selected": false,
                                    "hasCursor": true,
                                    "name": "fedora-42-workstation.iso"
                                },
                                {
                                    "caption": "selected",
                                    "selected": true,
                                    "hasCursor": false,
                                    "name": "debian-13-netinst.iso"
                                },
                                {
                                    "caption": "selected + cursor",
                                    "selected": true,
                                    "hasCursor": true,
                                    "name": "archlinux-2026.09.01.iso"
                                }
                            ]

                            TableRow {
                                id: tableRowSample

                                required property var modelData

                                // #77: a TableRow is a list item, named by its use.
                                Accessible.name: tableRowSample.modelData.name
                                hasCursor: tableRowSample.modelData.hasCursor
                                selected: tableRowSample.modelData.selected
                                width: Theme.space(360)

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.space(8)
                                    spacing: Theme.space(8)

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: tableRowSample.textColor
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.font.body
                                        text: tableRowSample.modelData.name
                                    }

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: Theme.dimForeground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.font.caption
                                        text: tableRowSample.modelData.caption
                                    }
                                }
                            }
                        }
                    }
                }

                // ProgressLine (T3.1): 0 %, 48.9 %, 100 %.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("progress line")
                    }

                    Column {
                        spacing: Theme.space(8)
                        width: Theme.space(200)

                        ProgressLine {
                            color: Theme.stateStalled
                            value: 0
                            width: parent.width
                        }

                        ProgressLine {
                            color: Theme.stateDownloading
                            value: 0.489
                            width: parent.width
                        }

                        ProgressLine {
                            color: Theme.stateSeeding
                            value: 1
                            width: parent.width
                        }
                    }
                }

                // Strip (T3.1): the details head's four-up fact row.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("strip")
                    }

                    Rectangle {
                        border.color: Theme.fill(Theme.hairlineAlpha)
                        border.width: 1
                        color: Theme.background
                        height: stripSample.implicitHeight
                        width: Theme.space(420)

                        Strip {
                            id: stripSample

                            items: [
                                {
                                    "label": qsTr("size"),
                                    "value": "4.21 GB"
                                },
                                {
                                    "label": qsTr("ratio"),
                                    "value": "1.42"
                                },
                                {
                                    "label": qsTr("added"),
                                    "value": qsTr("2 days ago")
                                },
                                {
                                    "label": qsTr("eta"),
                                    "value": "12m"
                                }
                            ]
                            width: parent.width
                        }
                    }
                }

                // KeyValue (T3.1): a status row coloured by state, plain rows beneath.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("key value")
                    }

                    Rectangle {
                        border.color: Theme.fill(Theme.hairlineAlpha)
                        border.width: 1
                        color: Theme.background
                        height: keyValueSample.implicitHeight
                        width: Theme.space(340)

                        KeyValue {
                            id: keyValueSample

                            items: [
                                {
                                    "key": qsTr("status"),
                                    "value": qsTr("downloading"),
                                    "color": Theme.stateDownloading
                                },
                                {
                                    "key": qsTr("size"),
                                    "value": "4.21 GB"
                                },
                                {
                                    "key": qsTr("save path"),
                                    "value": "/home/diogo/downloads/linux-isos"
                                }
                            ]
                            width: parent.width
                        }
                    }
                }

                // Tabs (T3.1): the details tab strip, current tab and a tracker-error badge.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("tabs")
                    }

                    Rectangle {
                        border.color: Theme.fill(Theme.hairlineAlpha)
                        border.width: 1
                        color: Theme.background
                        height: tabsSample.implicitHeight
                        width: Theme.space(340)

                        Tabs {
                            id: tabsSample

                            currentIndex: 2
                            model: [
                                {
                                    "text": qsTr("General"),
                                    "key": "1"
                                },
                                {
                                    "text": qsTr("Files"),
                                    "key": "2"
                                },
                                {
                                    "text": qsTr("Trackers"),
                                    "key": "3",
                                    "badge": true
                                },
                                {
                                    "text": qsTr("Peers"),
                                    "key": "4"
                                }
                            ]
                            width: parent.width
                        }
                    }
                }

                // SplitLayout (T3.1): docked (right column) above the breakpoint, dropped
                // (side under main) below it.
                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    KitGroupLabel {
                        text: qsTr("split layout")
                    }

                    Column {
                        spacing: Theme.space(14)

                        Captioned {
                            caption: qsTr("wide · docked (>= breakpoint %1 px)").arg(Theme.space(
                                                                                         1100))

                            Rectangle {
                                border.color: Theme.accent
                                border.width: 1
                                color: Theme.background
                                height: Theme.space(160)
                                width: Theme.space(1105)

                                SplitLayout {
                                    id: splitWide

                                    anchors.fill: parent
                                    main: splitWideMain
                                    side: splitWideSide

                                    Rectangle {
                                        id: splitWideMain

                                        color: Theme.fill(Theme.fillIdle)

                                        Text {
                                            anchors.centerIn: parent
                                            color: Theme.dimForeground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.font.small
                                            text: splitWide.docked ? qsTr("main · docked") : qsTr(
                                                                         "main")
                                        }
                                    }

                                    Rectangle {
                                        id: splitWideSide

                                        color: Theme.fill(Theme.fillHover)

                                        Text {
                                            anchors.centerIn: parent
                                            color: Theme.dimForeground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.font.small
                                            text: qsTr("side")
                                        }
                                    }
                                }
                            }
                        }

                        Captioned {
                            caption: qsTr("narrow · dropped (< breakpoint)")

                            Rectangle {
                                border.color: Theme.accent
                                border.width: 1
                                color: Theme.background
                                height: Theme.space(260)
                                width: Theme.space(480)

                                SplitLayout {
                                    id: splitNarrow

                                    anchors.fill: parent
                                    main: splitNarrowMain
                                    side: splitNarrowSide

                                    Rectangle {
                                        id: splitNarrowMain

                                        color: Theme.fill(Theme.fillIdle)

                                        Text {
                                            anchors.centerIn: parent
                                            color: Theme.dimForeground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.font.small
                                            text: splitNarrow.docked ? qsTr("main · docked") : qsTr(
                                                                           "main")
                                        }
                                    }

                                    Rectangle {
                                        id: splitNarrowSide

                                        color: Theme.fill(Theme.fillHover)

                                        Text {
                                            anchors.centerIn: parent
                                            color: Theme.dimForeground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.font.small
                                            text: qsTr("side")
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- overlays (T4.1) -------------------------------------------------------
            // The four overlay components of spec v2 §3-§4, drawn inline (`inline: true`
            // drops the scrim and the centring) so they can be compared with Fig. 6, 7, 11
            // and 12 side by side instead of one at a time over the window.
            Column {
                spacing: Theme.space(14)
                width: parent.width

                GalleryLabel {
                    text: qsTr("overlays")
                }

                Flow {
                    spacing: Theme.space(22)
                    width: parent.width

                    Captioned {
                        caption: qsTr("Sheet \u00b7 Fig. 7")

                        Sheet {
                            actions: [
                                {
                                    "text": qsTr("cancel"),
                                    "cancel": true
                                },
                                {
                                    "text": qsTr("move"),
                                    "primary": true
                                }
                            ]
                            footerRight: qsTr("server: home")
                            glyph: Glyphs.folder
                            hints: [
                                {
                                    "key": "tab",
                                    "label": qsTr("fields")
                                },
                                {
                                    "key": "enter",
                                    "label": qsTr("move")
                                }
                            ]
                            inline: true
                            sheetWidth: Theme.space(420)
                            subtitle: qsTr("1 torrent \u00b7 1.31 GB moves with it")
                            title: qsTr("Set location")

                            FormRow {
                                Layout.fillWidth: true
                                label: qsTr("New path on home")

                                Field {
                                    Layout.fillWidth: true
                                    text: "/data/torrents/archive/"
                                    trailing: "ctrl+o"
                                }
                            }
                        }
                    }

                    Captioned {
                        caption: qsTr("ConfirmDialog \u00b7 Fig. 6")

                        ConfirmDialog {
                            cancelText: qsTr("Cancel")
                            confirmText: qsTr("Delete")
                            cursor: 1
                            destructive: true
                            inline: true
                            message: qsTr("Delete 2 torrents and their files?")
                            subtitle: qsTr(
                                          "archlinux-2026.09.01-x86_64.iso, debian-13.1.0-amd64-DVD-1.iso \u00b7 5.29 GB on disk")

                            Toggle {
                                checked: true
                                text: qsTr("also delete files from disk")
                            }
                        }
                    }

                    Captioned {
                        caption: qsTr("Palette \u00b7 Fig. 12")

                        Palette {
                            accessibleName: qsTr("jump to a torrent")
                            counter: qsTr("2 of 142")
                            footerRight: qsTr("also matches hash and tracker")
                            glyph: Glyphs.search
                            hints: [
                                {
                                    "key": "\u2191\u2193",
                                    "label": qsTr("move")
                                },
                                {
                                    "key": "enter",
                                    "label": qsTr("jump")
                                },
                                {
                                    "key": "tab",
                                    "label": qsTr("actions")
                                }
                            ]
                            inline: true
                            items: [
                                {
                                    "title": "Fedora-Workstation-Live-44-1.6.x86_64.iso",
                                    "meta": qsTr("downloading"),
                                    "detail": "linux \u00b7 2.34 GB \u00b7 12.4 %"
                                },
                                {
                                    "title": "Fedora-Server-44-1.6.x86_64.iso",
                                    "meta": qsTr("seeding"),
                                    "detail": "linux \u00b7 2.9 GB \u00b7 ratio 4.1"
                                }
                            ]
                            query: "fed"
                        }
                    }

                    Captioned {
                        caption: qsTr("Menu \u00b7 Fig. 11")

                        Menu {
                            cursorIndex: 4
                            inline: true
                            items: [
                                {
                                    "text": qsTr("start"),
                                    "key": "p"
                                },
                                {
                                    "text": qsTr("force start"),
                                    "key": "P"
                                },
                                {
                                    "separator": true
                                },
                                {
                                    "text": qsTr("recheck"),
                                    "key": "c"
                                },
                                {
                                    "text": qsTr("reannounce"),
                                    "key": "R"
                                },
                                {
                                    "separator": true
                                },
                                {
                                    "text": qsTr("category"),
                                    "key": "C"
                                },
                                {
                                    "text": qsTr("set location"),
                                    "key": "m"
                                },
                                {
                                    "text": qsTr("rename"),
                                    "key": "r",
                                    "disabled": true
                                },
                                {
                                    "separator": true
                                },
                                {
                                    "text": qsTr("delete"),
                                    "key": "d",
                                    "danger": true
                                }
                            ]
                        }
                    }
                }
            }

            // ---- colour roles --------------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("colour roles")
                }

                Flow {
                    spacing: Theme.space(10)
                    width: parent.width

                    Repeater {
                        model: root.colourRoles

                        Swatch {
                            required property var modelData

                            role: modelData.name
                            value: modelData.value
                        }
                    }
                }
            }

            // ---- state roles ---------------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("state roles")
                }

                Flow {
                    spacing: Theme.space(10)
                    width: parent.width

                    Repeater {
                        model: root.stateRoles

                        Swatch {
                            required property var modelData

                            role: modelData.name
                            value: modelData.value
                        }
                    }
                }
            }

            // ---- type scale ----------------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("type scale")
                }

                Column {
                    spacing: Theme.space(6)
                    width: parent.width

                    Repeater {
                        model: root.typeScale

                        Row {
                            id: typeRow

                            required property var modelData

                            spacing: Theme.space(14)

                            Text {
                                color: Theme.dimForeground
                                font.capitalization: Font.AllUppercase
                                font.family: Theme.fontFamily
                                font.letterSpacing: Theme.font.caption * 0.1
                                font.pixelSize: Theme.font.caption
                                horizontalAlignment: Text.AlignRight
                                text: typeRow.modelData.name
                                width: Theme.space(130)
                                y: Theme.space(4)
                            }

                            Text {
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: typeRow.modelData.size
                                text: qsTr("%1 px · seeding · ↓ 4.21 MB/s").arg(
                                          typeRow.modelData.size)
                            }
                        }
                    }
                }
            }

            // ---- fills and borders ---------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("fills and borders")
                }

                Row {
                    spacing: Theme.space(10)
                    width: parent.width

                    Repeater {
                        model: root.surfaceStates

                        Rectangle {
                            id: stateBox

                            required property var modelData

                            border.color: Theme.fill(stateBox.modelData.border)
                            border.width: stateBox.modelData.border > 0 ? 1 : 0
                            color: Theme.fill(stateBox.modelData.fill)
                            height: Theme.controlHeight
                            radius: Theme.cornerRadius
                            width: Theme.space(180)

                            Text {
                                anchors.centerIn: parent
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.small
                                text: qsTr("%1 · %2 %").arg(stateBox.modelData.name).arg(Math.round(
                                                                                             stateBox.modelData.fill
                                                                                             * 100))
                            }
                        }
                    }
                }
            }

            // ---- rows ----------------------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("row heights")
                }

                Row {
                    spacing: Theme.space(10)

                    GalleryBox {
                        boxHeight: Theme.rowHeight
                        caption: qsTr("rowHeight %1").arg(Theme.rowHeight)
                    }

                    GalleryBox {
                        boxHeight: Theme.controlHeight
                        caption: qsTr("controlHeight %1").arg(Theme.controlHeight)
                    }

                    GalleryBox {
                        boxHeight: Theme.railRowHeight
                        caption: qsTr("railRowHeight %1").arg(Theme.railRowHeight)
                    }
                }
            }

            // ---- spacing -------------------------------------------------------------
            Column {
                spacing: Theme.space(10)
                width: parent.width

                GalleryLabel {
                    text: qsTr("space()")
                }

                Row {
                    spacing: Theme.space(10)

                    Repeater {
                        model: root.spacingSteps

                        Column {
                            id: stepColumn

                            required property int modelData

                            spacing: Theme.space(4)

                            Rectangle {
                                color: Theme.accent
                                height: Theme.space(24)
                                width: Math.max(1, Theme.space(stepColumn.modelData))
                            }

                            Text {
                                color: Theme.dimForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.caption
                                text: qsTr("%1→%2").arg(stepColumn.modelData).arg(Theme.space(
                                                                                      stepColumn.modelData))
                            }
                        }
                    }
                }
            }
        }
    }

    // A small dim sub-heading inside the Kit section (e.g. "button", "field"), lighter than
    // GalleryLabel's hairline-ruled section heading.
    component KitGroupLabel: Text {
        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
    }

    // Wraps one Kit component sample with a caption beneath it, so every state in the Kit
    // section reads like a small labelled swatch.
    component Captioned: Column {
        id: captioned

        default property alias content: inner.data
        property string caption: ""

        spacing: Theme.space(4)

        Item {
            id: inner

            height: childrenRect.height
            width: childrenRect.width
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: captioned.caption
        }
    }

    // A tracked caption label, the spec's section heading.
    component GalleryLabel: Item {
        id: label

        property alias text: labelText.text

        implicitHeight: labelText.implicitHeight + Theme.space(8)
        implicitWidth: labelText.implicitWidth

        Text {
            id: labelText

            color: Theme.dimForeground
            font.capitalization: Font.AllUppercase
            font.family: Theme.fontFamily
            font.letterSpacing: Theme.font.caption * 0.1
            font.pixelSize: Theme.font.caption
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            color: Theme.fill(Theme.hairlineAlpha)
            height: 1
        }
    }

    // One colour role: the colour itself, its name and its hex.
    component Swatch: Column {
        id: swatch

        required property string role
        required property color value

        spacing: Theme.space(4)

        Rectangle {
            border.color: Theme.fill(Theme.hairlineAlpha)
            border.width: 1
            color: swatch.value
            height: Theme.space(46)
            radius: Theme.cornerRadius
            width: Theme.space(130)
        }

        Text {
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: swatch.role
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: swatch.value.toString()
        }
    }

    // A box whose height is one of the height tokens, with the number under it.
    component GalleryBox: Column {
        id: box

        required property int boxHeight
        required property string caption

        spacing: Theme.space(4)

        Rectangle {
            border.color: Theme.fill(Theme.borderIdle)
            border.width: 1
            color: Theme.fill(Theme.fillIdle)
            height: box.boxHeight
            radius: Theme.cornerRadius
            width: Theme.space(200)
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: box.caption
        }
    }
}
