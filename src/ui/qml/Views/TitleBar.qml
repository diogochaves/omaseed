pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The title bar (board 46 `titleBar()`, DECISIONS C24 H5, H9's hero half): one bar across the
// content's top, beside the rail's mark, in the hero's old place. Left to right: the title
// (where you are: Library, a question, a category, a folder, a face) with its one sentence
// under it (core/ScopeSentence, what the scope holds), then the tools: "back to the sheet" or
// "all torrents" while a move's torrents are on show (C15 M10, C18), "move this folder…" while
// a folder is picked (H5; shift+m), the filter (`/`, with its count) and "+ add" (`a`; no ▾
// until #146). The hero's settings, ?, ⋯ and chips are gone (H9): `,`, `?`, the mark and
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

    // ---- the other tools -----------------------------------------------------------------
    /// "+ add" (`a`).
    property bool addShown: true
    /// "move this folder…" (shift+m): a folder is picked (H5).
    property bool moveFolderShown: false
    /// The way back from a move's torrents on show, its words and key ("back to the sheet",
    /// esc; or "all torrents"); "" for none.
    property string backText: ""
    property string backKey: ""

    /// The parts tests and the walks read.
    readonly property alias addTool: addTool
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

    /// A tool's tooltip: its words (which narrow steps leave out) and its key.
    function toolTip(words: string, key: string): string {
        return key.length > 0 ? qsTr("%1 · %2").arg(words).arg(key) : words;
    }

    implicitHeight: Theme.titleBarHeight
    implicitWidth: Theme.space(900)

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
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.titleBarLineGap
            visible: !(root.level >= 2 && root.searchOpen)

            Text {
                id: titleText

                Accessible.name: titleText.text
                Accessible.role: Accessible.Heading
                Layout.fillWidth: true
                color: Theme.brightForeground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.titleBarTitleSize
                font.weight: Theme.titleBarTitleWeight
                objectName: "titleText"
                text: root.title
                textFormat: Text.PlainText
            }

            Text {
                id: sentenceText

                Accessible.name: sentenceText.text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                color: Theme.dimForeground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.titleBarSentenceSize
                objectName: "titleSentence"
                text: root.sentence
                textFormat: Text.PlainText
                visible: root.sentence.length > 0

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
            Layout.fillWidth: root.level >= 2
            Layout.preferredWidth: Theme.titleBarFindWidth
            // #91: the same name at every width.
            accessibleName: qsTr("filter torrents")
            hint: Glyphs.search
            objectName: "titleFilter"
            placeholder: qsTr("filter")
            text: root.searchText
            trailing: root.searchCountText
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
        }

        Tool {
            id: addTool

            glyph: Glyphs.add
            key: "a"
            objectName: "titleAdd"
            primary: true
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
