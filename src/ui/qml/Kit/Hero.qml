pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The hero row (spec v2 §1 Fig. 1 `.hero`): the app name, a search `Field` with a `/` hint
// and live count, the filter chips and four glyph `IconButton`s, a hairline rule underneath.
// The mark and the view tabs moved to the rail (DECISIONS C24 H2); the server line, the speeds,
// their sparks, the alt-speed chip and the network's warning to the status line (H15, #167
// assumed 5). The title bar replaces what is left (frame step S4). Only `Theme` tokens; the
// caller owns state and reacts to the signals.
//
// Narrow windows (docs/10 §2, the ladder): nothing is ever cut off. As the hero narrows it
// takes steps (`level`; step 1 shortened what moved to the status line, and changes nothing
// here now): the gear and `?` move into ⋯ (2, `compact`), the filter folds into a
// `/` button and the chips into their glyph and ✕ (3), and the name goes (4). `+` and ⋯ never
// leave. On step 4 the filter chips leave the row: the `/` button carries them, lit with their
// count, and `filtersClicked` asks the caller for the menu that lists them (docs/15 Q1 A).
Item {
    id: root

    property string appName: "omaseed"
    // Exposed read-only so `tst_Hero.qml` can drive the inner Field directly instead of
    // simulating keystrokes.
    property alias searchField: searchField
    /// The step-4 filter button, so the caller can hang the filters' menu under it.
    readonly property alias filterButton: filterButton
    /// The ⋯ button, so the caller can hang its menu under it.
    readonly property alias moreButton: moreButton
    /// The `+`, so a test can see how it is drawn.
    readonly property alias addButton: addButton
    property int searchCount: 0
    /// What the field's trailing count prints. Fig. 1 shows the bare count, Fig. 24 shows
    /// "0 of 142"; the caller decides, and the plain count is the default.
    property string searchCountText: String(root.searchCount)
    property string searchText: ""

    /// docs/10 §2: the step the hero is on, from its own width (the window's). Thresholds are
    /// base-12 pixels through `Theme.space`, so they scale with the base size: 1500, 1150,
    /// 900 and 700 px at base 14.
    /// With filter chips up (`filterChips`, KIT-5) the step is also the first one whose row,
    /// measured part by part (`_rowWidth`), still holds the chips: whole for steps 0–2, folded
    /// to glyph and ✕ for 3–4, which is how they are drawn there. The chips use the slack the
    /// thresholds leave before anything folds (review VER-6: subtracting them from the
    /// window's width and then asking for the no-chip thresholds folded three steps early).
    /// Step 4 is where the ladder ends and the chips stop counting: there they go into the
    /// `/` button (`filtersInButton`, docs/15 Q1 A), so the row is as wide as with none.
    readonly property int level: {
        let step = 4;
        if (root.width >= Theme.space(1286))
            step = 0;
        else if (root.width >= Theme.space(986))
            step = 1;
        else if (root.width >= Theme.space(771))
            step = 2;
        else if (root.width >= Theme.space(600))
            step = 3;
        if (root.filterChips.length === 0)
            return step;
        const room = root.width - Theme.space(32);
        while (step < 4 && root._rowWidth(step) + root._chipsWidth(step >= 3) > room)
            ++step;
        return step;
    }

    /// The row's width at `step` without the chips: the name (gone at step 4), the filter
    /// (or its button from step 3) and the buttons, with the spacer: four items, three
    /// spacings; the chips bring their own (`_chipsWidth`).
    function _rowWidth(step: int): real {
        const buttons = step >= 2 ? 2 : 4;
        const nameWidth = step >= 4 ? 0 : appNameText.implicitWidth;
        const searchWidth = step >= 3 ? addButton.implicitWidth : Theme.space([220, 180, 140][step]);
        return nameWidth + searchWidth + buttons * addButton.implicitWidth
                + (buttons - 1) * Theme.space(2) + 3 * heroRow.spacing;
    }
    /// The filter chips fold to their glyph and ✕ from step 3.
    readonly property bool chipsFolded: root.level >= 3

    /// The room the filter chips take in the row, spacing included, drawn whole or folded.
    /// Measured on hidden copies (`chipMeasure`), so it does not depend on `level`.
    function _chipsWidth(folded: bool): real {
        let width = 0;
        for (let i = 0; i < chipMeasure.count; ++i) {
            // Each copy holds the whole chip (child 0) and the folded one (child 1).
            const measured = chipMeasure.itemAt(i);
            if (measured)
                width += measured.children[folded ? 1 : 0].implicitWidth + heroRow.spacing;
        }
        return width;
    }
    /// Step 4 with filter chips up: the chips leave the row and the `/` button carries them
    /// (docs/15 Q1 A). A name filter alone stays in the field the button opens.
    readonly property bool filtersInButton: root.level >= 4 && root.filterChips.length > 0
    /// Every filter that is up: the chips, and the name filter when it holds text.
    readonly property int filterCount: root.filterChips.length
                                       + (searchField.text.length > 0 ? 1 : 0)
    /// From step 2 the gear and `?` are not in the hero; the caller puts them in ⋯.
    readonly property bool compact: root.level >= 2
    /// From step 3 the filter is a button; this is true while it is open over the name.
    property bool searchOpen: false
    readonly property bool searchFolded: root.level >= 3 && !root.searchOpen
    /// `/`: focus the filter, opening it first when it is folded into its button. While the
    /// button carries the filters (step 4), it asks for their menu instead.
    function openSearch(): void {
        if (root.filtersInButton) {
            root.filtersClicked();
            return;
        }
        root.searchOpen = true;
        searchField.focusInput();
    }

    /// Filters to show as removable chips before the search field, `[{key, glyph, text}]`:
    /// the caller fills it while the rail that would show them is hidden (docs/08 §5).
    property var filterChips: []

    signal addClicked
    signal helpClicked
    signal moreClicked
    /// The filter button on step 4, or `/` there: the caller opens the filters' menu.
    signal filtersClicked
    /// The ✕ of one of `filterChips`.
    signal filterChipCleared(string key)
    signal searchChanged(string text)
    /// `enter` in the search field: the filter is already live, so this only says "done".
    signal searchAccepted(string text)
    signal searchCleared
    signal settingsClicked

    height: implicitHeight
    implicitHeight: heroRow.implicitHeight + Theme.space(24) + 1
    implicitWidth: Theme.space(600)
    width: implicitWidth

    RowLayout {
        id: heroRow

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.top: parent.top
        anchors.topMargin: Theme.space(12)
        spacing: Theme.space(14)

        Text {
            id: appNameText

            Layout.alignment: Qt.AlignVCenter
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.subtitle
            font.weight: Font.DemiBold
            objectName: "heroName"
            text: root.appName
            visible: root.level < 3 || (root.level === 3 && !root.searchOpen)
        }

        Item {
            id: spacer

            Layout.fillWidth: true
            objectName: "heroSpacer"
        }

        Repeater {
            id: chipRepeater

            model: root.filterChips

            Chip {
                id: filterChip

                required property var modelData

                Accessible.name: filterChip.modelData.text
                Layout.alignment: Qt.AlignVCenter
                glyph: filterChip.modelData.glyph
                objectName: "filterChip_" + filterChip.modelData.key
                text: root.chipsFolded ? Glyphs.close : filterChip.modelData.text + "  " + Glyphs.close
                // Folded to its ✕, the chip still says what it clears.
                tooltip: root.chipsFolded ? filterChip.modelData.text : ""
                visible: !root.filtersInButton

                onClicked: root.filterChipCleared(filterChip.modelData.key)
            }
        }

        // Step 3: the filter folded into a button; lit while a filter is set.
        IconButton {
            id: searchButton

            // #78: named by what it does; the filter's words and the key in the description.
            Accessible.description: searchField.text.length > 0
                                    ? qsTr("%1 · shortcut %2").arg(searchField.text).arg("/")
                                    : qsTr("shortcut %1").arg("/")
            Accessible.name: qsTr("filter")
            accent: searchField.text.length > 0
            glyph: Glyphs.search
            objectName: "searchButton"
            tooltip: searchField.text.length > 0 ? qsTr("filter: %1 · /").arg(searchField.text)
                                                 : qsTr("filter · /")
            visible: root.searchFolded && !root.filtersInButton

            onClicked: root.openSearch()
        }

        // Step 4 with filters up (docs/15 Q1 A): the search glyph and how many filters are
        // up, lit in the accent, drawn as a chip. It is never wider than a few digits.
        Rectangle {
            id: filterButton

            /// What its tooltip says: how many filters are up, and the key.
            readonly property string tip: qsTr("%n filter(s) · /", "", root.filterCount)

            // #78: named by what it does; how many are up, and the key, in the description.
            Accessible.description: qsTr("%n filter(s) up · shortcut %1", "",
                                         root.filterCount).arg("/")
            Accessible.name: qsTr("filters")
            Accessible.role: Accessible.Button
            // A screen reader's press is the click (#75); disabled, it does nothing, as the
            // pointer.
            Accessible.onPressAction: if (filterButton.enabled)
                                          root.filtersClicked()
            Layout.alignment: Qt.AlignVCenter
            activeFocusOnTab: true
            border.color: Theme.accent
            border.width: 1
            color: filterMouse.containsMouse ? Theme.fill(Theme.fillHover) : Theme.fill(
                                                   Theme.fillIdle)
            implicitHeight: Theme.space(22)
            implicitWidth: filterContent.implicitWidth + Theme.space(16)
            objectName: "filterButton"
            radius: Theme.cornerRadius
            visible: root.filtersInButton

            Row {
                id: filterContent

                anchors.centerIn: parent
                spacing: Theme.space(5)

                Glyph {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.accent
                    height: Theme.font.small
                    pixelSize: Theme.font.small
                    text: Glyphs.search
                    width: Theme.font.small
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    objectName: "filterCount"
                    text: String(root.filterCount)
                }
            }

            MouseArea {
                id: filterMouse

                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: root.filtersClicked()
            }

            // A button like its IconButton neighbours: reached by Tab, ringed while it holds
            // the focus (review Q6), and named in the kit's one tooltip (review Q8).
            FocusRing {}

            Tooltip {
                id: filterTip

                readonly property bool wanted: filterButton.visible && (filterMouse.containsMouse
                                                                        || filterButton.activeFocus)

                objectName: "filterButtonTip"
                parent: filterButton.Window.window ? filterButton.Window.window.contentItem : filterButton
                z: 1000

                onWantedChanged: filterTip.wanted ? filterTip.showFor(filterButton, filterButton.tip, false)
                                                  : filterTip.hide()
            }

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                        || event.key === Qt.Key_Space) {
                    root.filtersClicked();
                    event.accepted = true;
                }
            }
        }

        Field {
            id: searchField

            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: root.level >= 3
            Layout.preferredWidth: Theme.space([220, 180, 140, 140, 140][root.level])
            visible: !root.searchFolded
            // #91: the same name at every width; the placeholder shortens.
            accessibleName: qsTr("filter torrents")
            hint: Glyphs.search
            placeholder: root.level >= 2 ? qsTr("filter") : qsTr("filter torrents")
            text: root.searchText
            trailing: root.searchCountText
            // While it filters, the count and an `esc` cap clear it with a click, as `esc`
            // does; the cap only where the field has room for it beside the count.
            trailingClickable: searchField.text.length > 0
            trailingKey: searchField.text.length > 0 && root.level < 2 ? "esc" : ""
            // #78: the count is no name for what its click does.
            trailingName: qsTr("clear the filter")

            onAccepted: {
                root.searchChanged(searchField.text);
                root.searchAccepted(searchField.text);
            }
            onCleared: root.searchCleared()
            onEditingChanged: {
                if (!searchField.editing)
                    root.searchOpen = false;
            }
            onTextChanged: root.searchChanged(searchField.text)
            onTrailingClicked: {
                searchField.text = "";
                root.searchCleared();
            }
        }

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.space(2)

            // #78: each one named by what it does, the key in the description; the tooltip
            // keeps both, for the eye.
            IconButton {
                id: addButton

                Accessible.description: qsTr("shortcut %1").arg("a")
                Accessible.name: qsTr("add torrent")
                // Fig. 1 draws `.btn.icon.on`, the bare accent glyph; beside the other icons it
                // did not read as a button. The kit's primary outline does (docs/08 §3, REVIEW §9).
                glyph: Glyphs.add
                primary: true
                tooltip: qsTr("add · a")

                onClicked: root.addClicked()
            }


            IconButton {
                Accessible.description: qsTr("shortcut %1").arg(",")
                Accessible.name: qsTr("settings")
                glyph: Glyphs.settings
                objectName: "settingsButton"
                tooltip: qsTr("settings · ,")
                visible: !root.compact

                onClicked: root.settingsClicked()
            }

            // docs/08 §3: global, occasional actions (the limits, columns, select all).
            IconButton {
                id: moreButton

                Accessible.description: qsTr("shortcut %1").arg("ctrl+.")
                Accessible.name: qsTr("more")
                glyph: Glyphs.more
                tooltip: qsTr("more · ctrl+.")

                onClicked: root.moreClicked()
            }

            IconButton {
                Accessible.description: qsTr("shortcut %1").arg("?")
                Accessible.name: qsTr("help")
                glyph: Glyphs.help
                objectName: "helpButton"
                tooltip: qsTr("help · ?")
                visible: !root.compact

                onClicked: root.helpClicked()
            }
        }
    }

    // Hidden copies of the filter chips, whole and folded, so `level` can count their width
    // without reading the drawn chips, whose text follows `level`.
    Repeater {
        id: chipMeasure

        model: root.filterChips

        Item {
            id: measured

            required property var modelData

            // Measures, never seen: not for a screen reader either (#78).
            Accessible.ignored: true
            visible: false

            Chip {
                id: fullChip

                Accessible.ignored: true
                glyph: measured.modelData.glyph
                text: measured.modelData.text + "  " + Glyphs.close
            }

            Chip {
                id: foldedChip

                Accessible.ignored: true
                glyph: measured.modelData.glyph
                text: Glyphs.close
            }
        }
    }

    Hairline {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }
}
