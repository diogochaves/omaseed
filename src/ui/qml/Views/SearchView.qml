pragma ComponentBehavior: Bound

import QtQuick
import Omaseed
import Omaseed.Kit

// The search face (board 48 §3 `search()`, DECISIONS C26, C24 H16): a view in the rail after
// log (ctrl+f), searching the current server's own search plugins. omaseed never names, ships
// or scrapes a site (SR11): every request goes through qBittorrent's search API.
//
// From the top: the bar (SearchBar: the field, plugins ▾, type ▾, hide dead, sort ▾; at
// 720 px the filters fold into "filters ▾", SR6), the plugins' line (what the search found so
// far, a pill per plugin as it answers, "1 of 3 plugins didn't answer ›", SR1–SR2), then the
// results in the rows display (SearchRow), sorted live with the cursor's row kept in place and
// a line when rows land above it (SR5). A result opens on the right (SearchResultPane) on a
// click or space. With nothing to search with (no plugins, all off, no Python on the server)
// the view stays and says so in one sentence, with a link to the pane (SR13).
//
// Keys (SR14): enter in the field asks; ↓ into the rows; tab through the filters; in the rows
// enter add…, space the result, c copy its magnet, o open its page, `/` filters the rows (the
// title bar's filter); esc backs out step by step: the pane, the rows, the field's words, then
// the face before search (`leaveRequested`).
FocusScope {
    id: root

    /// The server's name, for the sentences, while there are two servers or more (SR12).
    property string serverName: ""
    property bool multiServer: false
    /// C12: the server isn't answering; the rows stay, dimmed, and the search waits for it.
    property bool serverLost: false
    /// The window's width, for SR6's fold.
    property real windowWidth: root.width
    readonly property bool narrow: root.windowWidth < Theme.searchFoldWidth

    /// The cursor's row in the results, -1 while the keys are in the bar.
    property int currentIndex: -1
    /// The result on the right is open.
    property bool paneOpen: false
    /// SR6: the plugins asked (names), empty for every plugin that is on; and the type.
    property var pluginPick: []
    property string category: "all"
    /// The keys are in the rows (else in the bar).
    readonly property bool rowsHaveKeys: rowsKeys.activeFocus
    readonly property alias field: searchBar.field
    readonly property alias rows: list
    readonly property alias bar: searchBar
    readonly property alias answers: answersMenu

    readonly property SearchSession search: App.search
    readonly property SearchModel results: App.searchResults
    readonly property SearchPlugins plugins: App.searchPlugins

    /// SR13: why there is nothing to search with, or "".
    readonly property string emptyKind: {
        if (root.search.state === "nopython")
            return "nopython";
        if (!root.plugins.loaded)
            return "";
        if (root.plugins.count === 0)
            return "none";
        if (root.plugins.enabledCount === 0)
            return "off";
        return "";
    }
    readonly property string _serverWord: root.multiServer && root.serverName.length > 0 ? qsTr(
                                                                                               " on %1").arg(
                                                                                               root.serverName) :
                                                                                           ""
    readonly property string _serverOwn: root.multiServer && root.serverName.length > 0 ? qsTr(
                                                                                              "%1's ").arg(
                                                                                              root.serverName) :
                                                                                          ""

    /// The plugins' rows of this search (SearchSession): `{name, fullName, state, count, …}`.
    readonly property var pluginRows: root.search.plugins
    readonly property var _asked: root.pluginRows.filter(row => row.state !== "skipped")
    readonly property var _waiting: root._asked.filter(row => row.state === "asking" || row.state
                                                              === "queued")

    readonly property var _answered: root._asked.filter(row => row.state === "answered")

    /// The title's sentence (SR12: the server it searches, when there are two).
    readonly property string sentence: {
        if (root.serverLost)
            return qsTr("%1 isn't answering · search waits for it").arg(root.serverName);
        switch (root.emptyKind) {
        case "none":
            return qsTr("no search plugins%1").arg(root._serverWord);
        case "off":
            return root.plugins.count === 1 ? qsTr("%1the plugin is turned off").arg(
                                                  root._serverOwn || qsTr("the ")) : qsTr(
                                                  "%1%2 plugins are all turned off").arg(
                                                  root._serverOwn || qsTr("the ")).arg(
                                                  root.plugins.count);
        case "nopython":
            return qsTr("%1qBittorrent can't run plugins").arg(root._serverOwn);
        }
        const words = qsTr("“%1”").arg(root.search.pattern);
        if (root.search.state === "asking") {
            if (root._answered.length === 0)
                return qsTr("%1 · asking %2%3").arg(words).arg(root._pluginCount(
                                                                   root._asked.length)).arg(
                            root._serverWord);
            if (root._waiting.length === 1)
                return qsTr("%1 · %2 of %3 answered · %4 still looking").arg(words).arg(
                            root._answered.length).arg(root._asked.length).arg(
                            root._waiting[0].fullName);
            return qsTr("%1 · %2 answered · %3 still looking").arg(words).arg(root._names(
                                                                                  root._answered)).arg(
                        root._waiting.length);
        }
        if (root.search.state === "done" || root.search.state === "error")
            return qsTr("%1 · %2 from %3 of %4%5").arg(words).arg(root._torrentCount(
                                                                      root.results.torrentCount)).arg(
                        root._answered.length).arg(root._pluginCount(root._asked.length)).arg(
                        root._serverWord);
        const on = root.plugins.enabledCount;
        return on > 0 ? qsTr("asks the %1 that %2 on%3").arg(root._pluginCount(on)).arg(on === 1
                                                                                        ? qsTr("is") :
                                                                                          qsTr("are")).arg(
                            root._serverWord) : qsTr("search plugins%1").arg(root._serverWord);
    }

    /// add… on a row (SR9): the window opens the add sheet on `key`.
    signal addRequested(string key)
    /// The face before search again (esc on an empty field, SR14).
    signal leaveRequested
    /// Settings › qBittorrent › Search plugins (SR13's link).
    signal pluginsPaneRequested
    /// qBittorrent's log, for a plugin's failure (SR2's "the log line").
    signal logRequested
    signal copyRequested(string text, string note)
    /// A line for the status line's note slot.
    signal noteRequested(string text)

    function _pluginCount(n: int): string {
        return n === 1 ? qsTr("1 plugin") : qsTr("%1 plugins").arg(n);
    }

    function _torrentCount(n: int): string {
        return n === 1 ? qsTr("1 torrent") : qsTr("%1 torrents").arg(n);
    }

    function _names(rows: var): string {
        const names = rows.map(row => row.fullName);
        if (names.length <= 1)
            return names.join("");
        return qsTr("%1 and %2").arg(names.slice(0, -1).join(", ")).arg(names[names.length - 1]);
    }

    /// ctrl+f (SR14): the field focused, the last words selected to type over.
    function focusField(): void {
        root.paneOpen = false;
        searchBar.stop = 0;
        searchBar.field.focusInput();
        searchBar.field.selectAll();
    }

    /// Where the keys go back to: the rows when there is a cursor, else the field.
    function focusHome(): void {
        if (root.currentIndex >= 0 && list.count > 0)
            rowsKeys.forceActiveFocus();
        else
            searchBar.field.focusInput();
    }

    /// enter (SR14): every plugin that is on (or the pick) asks for the field's words.
    function ask(): void {
        const words = searchBar.field.text.trim();
        if (words.length === 0 || root.emptyKind.length > 0)
            return;
        root.currentIndex = -1;
        root.paneOpen = false;
        App.startSearch(words, root.category, root.pluginPick);
    }

    /// ↓ from the field: the first row.
    function enterRows(): void {
        if (list.count === 0)
            return;
        root.currentIndex = Math.max(0, Math.min(root.currentIndex, list.count - 1));
        list.positionViewAtIndex(root.currentIndex, ListView.Contain);
        rowsKeys.forceActiveFocus();
    }

    function moveCursor(delta: int): void {
        if (list.count === 0)
            return;
        root.currentIndex = Math.max(0, Math.min(list.count - 1, root.currentIndex + delta));
        list.positionViewAtIndex(root.currentIndex, ListView.Contain);
        if (root.currentIndex === 0)
            root.results.clearLanded();
    }

    function keyAt(index: int): string {
        return index >= 0 ? (root.results.get(index).key ?? "") : "";
    }

    function addAt(index: int): void {
        const key = root.keyAt(index);
        if (key.length > 0)
            root.addRequested(key);
    }

    function copyAt(index: int): void {
        const row = root.results.get(index);
        root.copyRequested(row.link ?? "", row.isLink ? qsTr("link copied") : qsTr(
                                                            "magnet copied"));
    }

    /// `o` (SR11): the result's page, in the browser, only when asked.
    function openPageAt(index: int): void {
        const page = root.results.get(index).page ?? "";
        if (page.length === 0) {
            root.noteRequested(qsTr("this plugin gives no page"));
            return;
        }
        Qt.openUrlExternally(page);
    }

    /// SR6: a change of plugins asks again.
    function pickPlugin(action: string): void {
        if (action === "all") {
            root.pluginPick = [];
        } else {
            const name = action.slice(2);
            let pick = root.pluginPick.slice();
            if (pick.length === 0)
                pick = [name];
            else if (pick.indexOf(name) >= 0)
                pick = pick.filter(each => each !== name);
            else
                pick.push(name);
            root.pluginPick = pick;
        }
        if (root.search.pattern.length > 0)
            root.ask();
    }

    function pickType(id: string): void {
        if (id === root.category)
            return;
        root.category = id;
        if (root.search.pattern.length > 0)
            root.ask();
    }

    /// esc, one step at a time (SR14).
    function backOut(): bool {
        if (root.paneOpen) {
            root.paneOpen = false;
            return true;
        }
        if (rowsKeys.activeFocus) {
            searchBar.field.focusInput();
            return true;
        }
        if (searchBar.stop > 0) {
            searchBar.stop = 0;
            searchBar.field.focusInput();
            return true;
        }
        root.leaveRequested();
        return true;
    }

    Keys.onEscapePressed: event => event.accepted = root.backOut()

    // SR5: the cursor's row stays where it is on screen while rows sort in above it.
    property string _keptKey: ""
    property real _keptOffset: 0

    Connections {
        target: root.results

        function onAboutToRefresh() {
            root._keptKey = root.keyAt(root.currentIndex);
            root._keptOffset = root.currentIndex * Theme.libraryRowHeight - list.contentY;
        }

        function onRefreshed() {
            if (root._keptKey.length === 0) {
                if (root.currentIndex >= list.count)
                    root.currentIndex = list.count - 1;
                return;
            }
            const at = root.results.indexOfKey(root._keptKey);
            if (at < 0) {
                root.currentIndex = Math.min(root.currentIndex, list.count - 1);
                return;
            }
            root.currentIndex = at;
            list.contentY = Math.max(0, at * Theme.libraryRowHeight - root._keptOffset);
        }
    }

    // What lands above the cursor counts only while the keys are on a row.
    Binding {
        property: "anchorKey"
        target: App.searchResults
        value: root.rowsHaveKeys ? root.keyAt(root.currentIndex) : ""
    }

    SearchBar {
        id: searchBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        answersShown: root.search.didntAnswer > 0 && root.search.state !== "asking"
        category: root.category
        hideDead: root.results.hideDead
        narrow: root.narrow
        order: root.results.order
        pluginPick: root.pluginPick
        plugins: root.plugins
        visible: root.emptyKind !== "none"

        onAccepted: root.ask()
        onDownPressed: root.enterRows()
        onEmptied: root.leaveRequested()
        onHideDeadToggled: root.results.hideDead = !root.results.hideDead
        onMenuRequested: (which, x, y) => {
            const point = searchBar.mapToItem(root, x, y);
            barMenu.which = which;
            barMenu.openAt(Math.round(point.x), Math.round(point.y));
        }
        onAnswersRequested: (x, y) => {
            const point = searchBar.mapToItem(root, x, y);
            answersMenu.openAt(Math.round(point.x), Math.round(point.y));
        }
    }

    // The plugins' line (`.sline`): what the search found so far, then a pill per plugin.
    SearchLine {
        id: searchLine

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: searchBar.bottom
        narrow: root.narrow
        order: root.results.order
        pluginRows: root.pluginRows
        results: root.results
        answersHaveCursor: searchBar.answersHaveCursor
        searchState: root.search.state
        visible: root.emptyKind.length === 0 && root.search.state !== "idle"

        onAnswersRequested: (x, y) => {
            const point = searchLine.mapToItem(root, x, y);
            answersMenu.openAt(Math.round(point.x), Math.round(point.y));
        }
    }

    // SR5: "3 new and 1 folded while you were here".
    Text {
        id: landedLine

        Accessible.name: landedLine.text
        Accessible.role: Accessible.Button
        Accessible.onPressAction: landedArea.clicked(null)
        anchors.left: parent.left
        anchors.leftMargin: Theme.searchBarInset
        anchors.top: searchLine.visible ? searchLine.bottom : searchBar.bottom
        color: Theme.accent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.searchNoteSize
        height: landedLine.visible ? implicitHeight + 2 * Theme.searchNoteInset : 0
        objectName: "searchLanded"
        text: {
            const landed = root.results.landedAbove;
            const folded = root.results.foldedAbove;
            const parts = [];
            if (landed > 0)
                parts.push(qsTr("%1 new").arg(landed));
            if (folded > 0)
                parts.push(qsTr("%1 folded").arg(folded));
            return qsTr("↑ %1 while you were here · the cursor row stays put").arg(parts.join(qsTr(
                                                                                                  " and ")));
        }
        verticalAlignment: Text.AlignVCenter
        visible: root.results.landedAbove + root.results.foldedAbove > 0

        MouseArea {
            id: landedArea

            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor

            onClicked: {
                list.positionViewAtBeginning();
                root.results.clearLanded();
            }
        }
    }

    Item {
        id: content

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: landedLine.visible ? landedLine.bottom : searchLine.visible
                                          ? searchLine.bottom : searchBar.visible
                                            ? searchBar.bottom : parent.top

        Hairline {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            visible: list.count > 0
        }

        // The rows: keys go here once ↓ leaves the field.
        FocusScope {
            id: rowsKeys

            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: pane.visible ? pane.left : parent.right
            anchors.top: parent.top
            opacity: root.serverLost ? Theme.staleOpacity : 1

            Keys.onPressed: event => {
                switch (event.key) {
                case Qt.Key_J:
                case Qt.Key_Down:
                    root.moveCursor(1);
                    break;
                case Qt.Key_K:
                case Qt.Key_Up:
                    if (root.currentIndex <= 0) {
                        searchBar.field.focusInput();
                        break;
                    }
                    root.moveCursor(-1);
                    break;
                case Qt.Key_G:
                    root.moveCursor((event.modifiers & Qt.ShiftModifier) ? list.count :
                                                                           -list.count);


                    break;
                case Qt.Key_PageDown:
                    root.moveCursor(Math.max(1, Math.floor(list.height / Theme.libraryRowHeight)));
                    break;
                case Qt.Key_PageUp:
                    root.moveCursor(-Math.max(1, Math.floor(list.height / Theme.libraryRowHeight)));
                    break;
                case Qt.Key_Return:
                case Qt.Key_Enter:
                    root.addAt(root.currentIndex);
                    break;
                case Qt.Key_Space:
                    root.paneOpen = !root.paneOpen;
                    break;
                case Qt.Key_C:
                    root.copyAt(root.currentIndex);
                    break;
                case Qt.Key_O:
                    root.openPageAt(root.currentIndex);
                    break;
                case Qt.Key_L:
                    if (!root.paneOpen) {
                        root.paneOpen = true;
                        break;
                    }
                    return;
                default:
                    return;
                }
                event.accepted = true;
            }

            ListView {
                id: list

                anchors.fill: parent
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                model: root.results
                objectName: "searchRows"
                visible: root.emptyKind.length === 0

                delegate: SearchRow {
                    cursorIndex: root.rowsHaveKeys || root.paneOpen ? root.currentIndex : -1
                    narrow: list.width < Theme.libraryRowsSteps[1] ? 2 : list.width
                                                                     < Theme.libraryRowsSteps[0]
                                                                     ? 1 : 0
                    width: ListView.view.width

                    onAddClicked: index => {
                        root.currentIndex = index;
                        rowsKeys.forceActiveFocus();
                        root.addAt(index);
                    }
                    onClicked: index => {
                        root.currentIndex = index;
                        rowsKeys.forceActiveFocus();
                        root.paneOpen = true;
                    }
                    onDoubleClicked: index => root.addAt(index)
                }
                // SR4: what hide dead hides, said under the rows.
                footer: Item {
                    height: hiddenLine.visible ? hiddenLine.implicitHeight + 2
                                                 * Theme.searchBarInsetBottom : 0
                    width: ListView.view ? ListView.view.width : 0

                    Text {
                        id: hiddenLine

                        anchors.left: parent.left
                        anchors.leftMargin: Theme.searchBarInset
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.searchLineSize
                        objectName: "searchHidden"
                        text: root.results.hiddenDead === 1 ? qsTr(
                                                                  "1 hidden: no seeds when the plugin looked · rows whose seeds aren't reported stay") :
                                                              qsTr("%1 hidden: no seeds when the plugins looked · rows whose seeds aren't reported stay").arg(
                                                                  root.results.hiddenDead)
                        visible: root.results.hideDead && root.results.hiddenDead > 0
                    }
                }

                ScrollBar {
                    flickable: list
                }
            }

            // Asked, nothing back yet: who was asked (board 48 `.asking`).
            Column {
                anchors.left: parent.left
                anchors.leftMargin: Theme.searchBarInset
                anchors.top: parent.top
                anchors.topMargin: Theme.searchBarInsetTop
                objectName: "searchAsking"
                spacing: Theme.searchEmptyGap
                visible: root.emptyKind.length === 0 && root.search.state === "asking"
                         && list.count === 0

                Text {
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.searchLineSize
                    text: qsTr("Rows appear as each plugin answers; nothing waits for the slowest.")
                }
            }

            // Asked and answered with nothing: "0 results", not a failure (SR2).
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Theme.searchEmptyTop
                objectName: "searchNothing"
                spacing: Theme.searchEmptyGap
                visible: root.emptyKind.length === 0 && root.search.state === "done"
                         && root.results.torrentCount === 0
                width: Math.min(Theme.searchEmptyWidth, parent.width - 2 * Theme.searchBarInset)

                Text {
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.searchEmptyTitleSize
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("0 results")
                    width: parent.width
                }

                Text {
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.searchEmptyWordsSize
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr(
                              "No plugin found “%1”. Other words, or another type, may find it.").arg(
                              root.search.pattern)
                    width: parent.width
                    wrapMode: Text.Wrap
                }
            }

            // Before the first search: what enter will do.
            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.searchBarInset
                anchors.right: parent.right
                anchors.rightMargin: Theme.searchBarInset
                anchors.top: parent.top
                anchors.topMargin: Theme.searchBarInsetTop
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.searchLineSize
                objectName: "searchIdle"
                text: qsTr(
                          "Type what you are looking for, then enter: each plugin that is on asks its own site, through %1's qBittorrent. Nothing is asked until then.").arg(
                          root.serverName.length > 0 ? root.serverName : qsTr("the server"))
                visible: root.emptyKind.length === 0 && root.search.state === "idle"
                wrapMode: Text.Wrap
            }
        }

        // SR13: nothing to search with; the view stays and says so.
        SearchEmpty {
            id: emptyState

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            kind: root.emptyKind
            pluginCount: root.plugins.count
            pluginNames: root._names(root.pluginRowsOf(root.plugins))
            serverName: root.serverName
            multiServer: root.multiServer
            visible: root.emptyKind.length > 0

            onPaneRequested: root.pluginsPaneRequested()
        }

        SearchResultPane {
            id: pane

            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.top: parent.top
            details: root.paneOpen && root.currentIndex >= 0 ? root.results.details(root.keyAt(
                                                                                        root.currentIndex)) :
                                                               ({})
            revision: root.results.count
            serverName: root.serverName
            visible: root.paneOpen && root.currentIndex >= 0 && root.emptyKind.length === 0
            width: Math.min(Theme.searchPaneWidth, Math.round(content.width * 0.45))

            onAddClicked: root.addAt(root.currentIndex)
            onCloseClicked: root.paneOpen = false
            onCopyClicked: root.copyAt(root.currentIndex)
            onPageClicked: root.openPageAt(root.currentIndex)
        }
    }

    /// The plugins model as `{fullName}` rows, for the empty states' words.
    function pluginRowsOf(model: var): var {
        const rows = [];
        for (let i = 0; i < model.count; ++i)
            rows.push({
                          "fullName": model.data(model.index(i, 0), SearchPlugins.FullNameRole)
                      });
        return rows;
    }

    // The bar's menus: plugins ▾, type ▾, sort ▾, filters ▾ (SR6).
    SearchBarMenu {
        id: barMenu

        anchors.fill: parent
        category: root.category
        hideDead: root.results.hideDead
        order: root.results.order
        pluginPick: root.pluginPick
        plugins: root.plugins
        z: 5

        onPicked: action => {
            if (action.startsWith("p:") || action === "all")
                root.pickPlugin(action);
            else if (action.startsWith("t:"))
                root.pickType(action.slice(2));
            else if (action.startsWith("s:"))
                root.results.order = action.slice(2);
            else if (action === "hideDead")
                root.results.hideDead = !root.results.hideDead;
            else if (action === "menu:plugins" || action === "menu:type") {
                barMenu.which = action.slice(5);
                barMenu.open();
            }
        }
    }

    // SR2: each plugin's answer, opened from "1 of 3 plugins didn't answer ›".
    SearchAnswers {
        id: answersMenu

        anchors.fill: parent
        pluginRows: root.pluginRows
        serverName: root.serverName
        z: 5

        onAskAgain: name => App.search.askAgain(name)
        onLogRequested: root.logRequested()
        onStopAsking: name => App.search.stopAsking(name)
        onTurnOff: name => {
            App.turnOffSearchPlugin(name);
            root.noteRequested(qsTr("turned off: %1").arg(answersMenu.nameOf(name)));
        }
    }
}
