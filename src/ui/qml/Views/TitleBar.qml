pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The title bar (board 46 `titleBar()`, DECISIONS C24 H5, H9's hero half): one bar across the
// content's top, beside the rail's mark, in the hero's old place. Left to right: the title
// (where you are: Library, a question, a category, a folder, a face) with its one sentence
// under it (core/ScopeSentence, what the scope holds), then the tools: "back to the sheet" or
// "all torrents" while a move's torrents are on show (C15 M10, C18), "move this folder…" while
// a folder is picked (H5; shift+m), the filter (`/`, with its count) and "+ add ▾" (`a`; its
// menu has create and search, H17, #146). The hero's settings, ?, ⋯ and chips are gone (H9): `,`, `?`, the mark and
// ctrl+. reach them, and the title's sentence names the scope.
//
// Properties only: MainView feeds it the words and answers its signals. Its form (height,
// insets, gaps, the type, the tools' height, the filter's width, the steps) is `Theme.titleBar*`
// tokens, so a later skin can set them (frame plan, "form in tokens").
//
// Narrow (`level`, from its own width against `Theme.titleBarSteps`): the tools keep their
// glyph alone and say their words in the tooltip (1), and the filter folds into its `/` tool,
// opening over the title while it is used (2). The title and sentence elide before anything
// is pushed out.
Item {
    id: root

    /// Where the window is: "Library", "Arriving", a category's or folder's name, a face.
    property string title: ""
    /// What the title names, as the skin's `labels` know it ("library", "folders"): its kana
    /// word follows the title where the skin draws one (board 46 `.top h1 .kn`).
    property string titleId: ""
    /// The scope in one sentence.
    property string sentence: ""

    // ---- the filter tool -----------------------------------------------------------------
    /// The filter is offered (not on the log, whose lines have their own).
    property bool filterShown: true
    property string searchText: ""
    /// What the field's trailing count prints: the count, or "N of M" while it filters.
    property string searchCountText: ""
    /// The field, for the window's `/`, `esc` and the tests.
    readonly property alias searchField: filterField
    /// From step 2 the filter is a tool; this is true while it is open over the title.
    property bool searchOpen: false
    readonly property bool searchFolded: root.level >= 2 && !root.searchOpen
    /// Board 51 D18 a (F21): the field is in use, the caret in it or a filter set. It then
    /// takes the bar's free room: the title stays, its sentence gives way.
    readonly property bool filterOpen: root.filterShown && !root.searchFolded
                                       && (filterField.editing || filterField.text.length > 0)

    // ---- the speeds (DECISIONS C32 D2 a, D6 a) ---------------------------------------------
    /// The speeds are drawn here (Settings › Appearance › Speeds: the title bar). From step 2
    /// they give way (`speedsFit`), and the status line takes them back.
    property bool speedsShown: false
    readonly property bool speedsFit: root.level < 2
    /// Each speed as `App.speedParts` splits it: `{value, cap, unit}`, `cap` "" with none.
    property var downParts: ({
            "value": "—",
            "cap": "",
            "unit": ""
        })
    property var upParts: ({
            "value": "—",
            "cap": "",
            "unit": ""
        })
    /// The last polls' speeds, oldest first, for the line under each.
    property var downHistory: []
    property var upHistory: []
    /// D6 a: the caps read in the alternative set's colour (turtle yellow, burst green).
    property color capColor: Theme.yellow
    /// #24: ↑ opens the seed console here too, as on the status line; with what it adds for a
    /// screen reader and its tooltip ("18.4 GB today").
    property bool upOpensSeed: false
    property string upNote: ""
    /// The speeds' block, for the tests.
    readonly property alias speeds: speedsRow

    // ---- the other tools -----------------------------------------------------------------
    /// The display tool (board 46 `.tool.disp`, DECISIONS C24 H3–H4): the scope's display,
    /// "rows", "table" or "space" (board 51 D12 a, C32); "" hides it (the seed
    /// console, the log). No key (#168 assumed 8): `:` has its rows.
    property string display: ""
    /// Board 51 D13 a (DECISIONS C32): the list's sort in a word ("added"), said after the
    /// display ("rows · added"); "" says the display alone.
    property string sortWord: ""
    /// "+ add" (`a`).
    property bool addShown: true
    /// "+ add ▾": a click opens the add menu (create, search; C24 H17, #146), not the sheet.
    property bool addMenuShown: false
    /// "move this folder…" (shift+m): a folder is picked (H5).
    property bool moveFolderShown: false
    /// The way back from a move's torrents on show, its words and key ("back to the sheet",
    /// esc; or "all torrents"); "" for none.
    property string backText: ""
    property string backKey: ""

    /// DECISIONS C24 H10: alt is held: each tool, and the filter, shows its key over its
    /// top-right corner (board 46 `.tool .kb`).
    property bool keysShown: false

    /// The parts tests and the walks read.
    readonly property alias addTool: addTool
    readonly property alias displayTool: displayTool
    readonly property alias moveFolderTool: moveFolderTool
    readonly property alias backTool: backTool
    readonly property alias filterTool: filterTool
    readonly property alias tip: tip

    /// The step the bar is on, from its own width (see the top).
    readonly property int level: {
        const steps = Theme.titleBarSteps;
        for (let i = 0; i < steps.length; ++i) {
            if (root.width >= steps[i])
                return i;
        }
        return steps.length;
    }

    signal addClicked
    /// ↑, with `upOpensSeed`: the seed console.
    signal upClicked
    /// The display tool: the window opens its menu under it.
    signal displayClicked
    signal moveFolderClicked
    signal backClicked
    signal searchChanged(string text)
    /// `enter` in the field: the filter is already live, so this only says "done".
    signal searchAccepted(string text)
    signal searchCleared

    /// `/`: the caret in the filter, opening it over the title first when it is folded.
    function openSearch(): void {
        root.searchOpen = true;
        filterField.focusInput();
    }

    /// The display's word: rows, table, space.
    function displayWord(display: string): string {
        return display === "table" ? qsTr("table") : display === "space" ? qsTr("space") : qsTr("rows");
    }

    /// The display tool's word: the display, and the sort after it (board 51 D13 a).
    function displayToolWord(display: string): string {
        const word = root.displayWord(display);
        return root.sortWord.length > 0 ? qsTr("%1 · %2").arg(word).arg(root.sortWord) : word;
    }

    /// A tool's tooltip: its words (which narrow steps leave out) and its key.
    function toolTip(words: string, key: string): string {
        return key.length > 0 ? qsTr("%1 · %2").arg(words).arg(key) : words;
    }

    implicitHeight: Theme.titleBarHeight
    /// The tools' chevron is the skin's icon (Console's `ms:expand_more`), drawn after their
    /// words in a Glyph; Omarchy's ▾ stays in the words.
    readonly property bool chevronDrawn: Glyphs.chevronDownGlyph !== Glyphs.chevronDown
    implicitWidth: Theme.space(900)

    // One speed (board 51 `.tspd`): its arrow and figure, the cap after a slash while the
    // alternative limits hold it (D6 a), the unit small, and the last polls under them.
    component SpeedFigure: ColumnLayout {
        id: figure

        property string arrow: ""
        property color tone: Theme.foreground
        property var parts: ({})
        property var history: []
        /// "download" or "upload", for a screen reader.
        property string name: ""
        /// A button (↑ to the seed console, #24): its name, its tooltip.
        property bool clickable: false
        property string clickName: ""
        property string tipText: ""

        signal clicked
        readonly property string spoken: [figure.parts.value ?? "", figure.parts.cap ? "/ " + figure.parts.cap : "",
                                          figure.parts.unit ?? ""].filter(part => part.length > 0).join(" ")

        Accessible.description: figure.spoken
        Accessible.name: figure.clickable ? figure.clickName : figure.name
        Accessible.role: figure.clickable ? Accessible.Button : Accessible.StaticText
        Accessible.onPressAction: if (figure.clickable)
                                      figure.clicked()
        Layout.alignment: Qt.AlignVCenter
        spacing: 0

        MouseArea {
            id: figureMouse

            anchors.fill: parent
            cursorShape: figure.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
            enabled: figure.clickable
            hoverEnabled: true

            onClicked: figure.clicked()
            onContainsMouseChanged: figureMouse.containsMouse && figure.tipText.length > 0
                                    ? tip.showFor(figure, figure.tipText) : tip.hide()
        }

        Row {
            spacing: Theme.space(5)

            Text {
                anchors.baseline: value.baseline
                color: figureMouse.containsMouse ? Theme.accent : figure.tone
                font.family: Theme.valueFamily
                font.pixelSize: Theme.titleBarSpeedArrowSize
                text: figure.arrow
            }

            Text {
                id: value

                color: Theme.brightForeground
                font.family: Theme.valueFamily
                font.features: {
                    "tnum": 1
                }
                font.pixelSize: Theme.titleBarSpeedSize
                font.weight: Theme.titleBarSpeedWeight
                objectName: "speedValue"
                text: figure.parts.value ?? ""
            }

            Text {
                anchors.baseline: value.baseline
                color: root.capColor
                font.family: Theme.valueFamily
                font.pixelSize: Theme.titleBarSpeedUnitSize
                objectName: "speedCap"
                text: "/ " + (figure.parts.cap ?? "")
                visible: (figure.parts.cap ?? "").length > 0
            }

            Text {
                anchors.baseline: value.baseline
                color: Theme.dimForeground
                font.family: Theme.valueFamily
                font.pixelSize: Theme.titleBarSpeedUnitSize
                objectName: "speedUnit"
                text: figure.parts.unit ?? ""
                visible: text.length > 0
            }
        }

        Sparkline {
            Layout.preferredHeight: Theme.titleBarSparkHeight
            Layout.preferredWidth: Theme.titleBarSparkWidth
            color: figure.tone
            fillAlpha: 0
            lineAlpha: 1
            values: figure.history
        }
    }

    // The tools: a kit Button whose words go at step 1, with a tooltip that keeps them and
    // the key, and the key in a screen reader's description (#78).
    component Tool: Button {
        id: tool

        /// Its words, its key, and its glyph.
        property string words: ""
        property string key: ""

        Accessible.description: tool.key.length > 0 ? qsTr("shortcut %1").arg(tool.key) : ""
        Accessible.name: tool.words
        Layout.alignment: Qt.AlignVCenter
        bordered: !tool.primary
        height: Theme.titleBarToolHeight
        text: root.level >= 1 ? "" : tool.words

        // Over the corner, but inside the window: the bar is the window's top.
        KeyBadge {
            key: tool.key
            overhangY: Math.max(0, Math.min(Theme.keyBadgeOverhangY, tool.y))
            shown: root.keysShown && tool.visible
        }

        HoverHandler {
            id: toolHover

            onHoveredChanged: toolHover.hovered ? tip.showFor(tool, root.toolTip(tool.words,
                                                                                 tool.key)) :
                                                  tip.hide()
        }
    }

    RowLayout {
        id: row

        anchors.bottom: rule.top
        anchors.left: parent.left
        anchors.leftMargin: Theme.titleBarInsetStart
        anchors.right: parent.right
        anchors.rightMargin: Theme.titleBarInsetEnd
        anchors.top: parent.top
        spacing: Theme.titleBarGap

        // The title and its sentence. At step 2 the open filter takes their place.
        ColumnLayout {
            id: heading

            Layout.alignment: Qt.AlignVCenter
            // D18 a: while the filter is open the title keeps its own width, no more.
            Layout.fillWidth: !root.filterOpen
            Layout.maximumWidth: root.filterOpen ? Math.ceil(titleText.implicitWidth) : -1
            Layout.minimumWidth: root.filterOpen ? Math.ceil(titleText.implicitWidth) : 0
            Layout.preferredWidth: root.filterOpen ? Math.ceil(titleText.implicitWidth) : -1
            spacing: Theme.titleBarLineGap
            visible: !(root.level >= 2 && root.searchOpen)

            // Board 46 `.top h1`: the skin's heading voice, and its kana word.
            Label {
                id: titleText

                Accessible.name: titleText.text
                Accessible.role: Accessible.Heading
                Layout.fillWidth: true
                bright: true
                elide: Text.ElideRight
                font.pixelSize: Theme.titleBarTitleSize
                font.weight: Theme.titleBarTitleWeight
                kanaRise: 0.25
                labelId: root.titleId
                objectName: "titleText"
                size: "title"
                text: root.title
                voice: "title"
            }

            Text {
                id: sentenceText

                Accessible.name: sentenceText.text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                color: Theme.dimForeground
                elide: Text.ElideRight
                font.family: Theme.valueFamily
                font.pixelSize: Theme.titleBarSentenceSize
                objectName: "titleSentence"
                text: root.sentence
                textFormat: Text.PlainText
                visible: root.sentence.length > 0 && !root.filterOpen

                HoverHandler {
                    id: sentenceHover

                    // The whole sentence when the bar cut it.
                    onHoveredChanged: sentenceHover.hovered && sentenceText.truncated ? tip.showFor(
                                                                                            sentenceText,
                                                                                            sentenceText.text) :
                                                                                        tip.hide()
                }
            }
        }

        // D2 a: ↓ and ↑ after the title, a hairline before the tools.
        RowLayout {
            id: speedsRow

            Layout.alignment: Qt.AlignVCenter
            Layout.fillHeight: true
            objectName: "titleSpeeds"
            spacing: Theme.titleBarGap
            visible: root.speedsShown && root.speedsFit

            SpeedFigure {
                arrow: Glyphs.arrowDown
                history: root.downHistory
                name: qsTr("download")
                objectName: "titleDown"
                parts: root.downParts
                tone: Theme.stateDownloading
            }

            SpeedFigure {
                arrow: Glyphs.arrowUp
                clickName: qsTr("seed console")
                clickable: root.upOpensSeed
                history: root.upHistory
                name: qsTr("upload")
                tipText: root.upOpensSeed ? [qsTr("seed console · `"), root.upNote].filter(part => part.length > 0).join(
                                                "\n") : ""

                onClicked: root.upClicked()
                objectName: "titleUp"
                parts: root.upParts
                tone: Theme.stateSeeding
            }

            Hairline {
                Layout.bottomMargin: Theme.space(10)
                Layout.fillHeight: true
                Layout.preferredWidth: Theme.hairWidth
                Layout.topMargin: Theme.space(10)
                vertical: true
            }
        }

        Tool {
            id: backTool

            glyph: Glyphs.chevronLeft
            key: root.backKey
            objectName: "titleBack"
            visible: root.backText.length > 0
            words: root.backText

            onClicked: root.backClicked()
        }

        Tool {
            id: moveFolderTool

            glyph: Glyphs.location
            key: "shift+m"
            objectName: "titleMoveFolder"
            visible: root.moveFolderShown
            words: qsTr("move this folder…")

            onClicked: root.moveFolderClicked()
        }

        // Step 2: the filter folded into its tool; lit while a filter is set.
        Tool {
            id: filterTool

            Accessible.description: filterField.text.length > 0 ? qsTr("%1 · shortcut %2").arg(
                                                                      filterField.text).arg("/") :
                                                                  qsTr("shortcut %1").arg("/")
            glyph: Glyphs.search
            key: "/"
            objectName: "titleFilterTool"
            selected: filterField.text.length > 0
            text: ""
            visible: root.filterShown && root.searchFolded
            words: filterField.text.length > 0 ? qsTr("filter: %1").arg(filterField.text) : qsTr(
                                                     "filter")

            onClicked: root.openSearch()
        }

        Field {
            id: filterField

            Layout.alignment: Qt.AlignVCenter
            // F21, D18 a: at rest the tool's width; in use, the bar's free room.
            Layout.fillWidth: root.level >= 2 || root.filterOpen
            Layout.preferredWidth: Theme.titleBarFindWidth
            // #91: the same name at every width.
            accessibleName: qsTr("filter torrents")
            hint: Glyphs.search
            objectName: "titleFilter"
            // D18 a: open, the hint names what it takes.
            placeholder: root.filterOpen ? qsTr("words, or is:stalled, cat:films, tag:keep, tracker:…")
                                         : qsTr("filter")
            text: root.searchText
            tokenChips: true
            trailing: root.searchCountText
            // D18 a: "× esc" clears what is typed; "esc" alone leaves the empty field.
            trailingKey: !root.filterOpen ? "" : filterField.text.length > 0 ? qsTr("× esc") : qsTr("esc")
            // While it filters, the count clears it with a click, as `esc` does.
            trailingClickable: filterField.text.length > 0
            // #78: the count is no name for what its click does.
            trailingName: qsTr("clear the filter")
            visible: root.filterShown && !root.searchFolded

            onAccepted: {
                root.searchChanged(filterField.text);
                root.searchAccepted(filterField.text);
            }
            onCleared: root.searchCleared()
            onEditingChanged: {
                if (!filterField.editing)
                    root.searchOpen = false;
            }
            onTextChanged: root.searchChanged(filterField.text)
            onTrailingClicked: {
                filterField.text = "";
                root.searchCleared();
            }

            KeyBadge {
                key: "/"
                objectName: "titleFilterKey"
                overhangY: Math.max(0, Math.min(Theme.keyBadgeOverhangY, filterField.y))
                shown: root.keysShown
            }
        }

        Tool {
            id: displayTool

            glyph: root.display === "table" ? Glyphs.displayTable : root.display === "space" ? Glyphs.displaySpace
                                                                                             : Glyphs.displayRows
            objectName: "titleDisplay"
            text: root.level >= 1 ? "" : root.chevronDrawn ? root.displayToolWord(root.display)
                                                           : qsTr("%1 ▾").arg(root.displayToolWord(root.display))
            trailingGlyph: root.level >= 1 || !root.chevronDrawn ? "" : Glyphs.chevronDownGlyph
            visible: root.display.length > 0
            words: root.sortWord.length > 0
                   ? qsTr("display: %1, sorted by %2").arg(root.displayWord(root.display)).arg(root.sortWord)
                   : qsTr("display: %1").arg(root.displayWord(root.display))

            onClicked: root.displayClicked()
        }

        Tool {
            id: addTool

            Accessible.role: root.addMenuShown ? Accessible.ButtonMenu : Accessible.Button
            glyph: Glyphs.add
            key: "a"
            objectName: "titleAdd"
            primary: true
            text: root.level >= 1 ? "" : root.addMenuShown && !root.chevronDrawn ? qsTr("%1 ▾").arg(addTool.words)
                                                                               : addTool.words
            trailingGlyph: root.level >= 1 || !root.addMenuShown || !root.chevronDrawn ? "" : Glyphs.chevronDownGlyph
            visible: root.addShown
            words: qsTr("add")

            onClicked: root.addClicked()
        }
    }

    Hairline {
        id: rule

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }

    Tooltip {
        id: tip

        objectName: "titleTip"
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 1000
    }
}
