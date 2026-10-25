pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The first-run page of spec v2 §5 Fig. 15: a full-window page (not a sheet — there is
// nothing to dismiss, only a decision to make) that walks a fresh install through choosing a
// server, testing it, and offering to become the magnet/`.torrent` handler. `MainView` shows
// this instead of the body when `App.firstRunNeeded` (the store is empty) and there is no
// demo server standing in.
//
// docs/14 (approved 2026-09-23) made it one "setup surface": no library chrome (MainView
// hides its hero and footer while this shows), one 520-unit column centred in the window,
// the mark and the named steps above each step's question, a welcome before step 1 and a
// done screen after step 3. The page draws its own footer at the window's bottom edge.
//
//   welcome     shown first on a real first run: the name, the pitch and the server search
//               as it runs; `enter` sets up, `esc` quits.
// Three steps, named above the question ("server · sign in · links", the current one bright;
// no numbers, manual box M3):
//   1 choose    `App.detectServers()` on `Component.onCompleted`; rows for a detected local
//               server, "connect to a remote server", and "install qbittorrent-nox here" —
//               the last one dimmed and captioned "a local server was already found" when
//               step 1 found one (Q20: still selectable, since a second engine is possible
//               with its own profile and port, but the packaged service is one instance on
//               8080 and would not start next to a server already answering there).
//   2 details   either the API-key page (URL/name/auth/key, `test`, `continue`) for "local"
//               (M2: `test` is the primary action and `continue` waits, dimmed with "test
//               first", until a test passes; `enter` in a field does whichever is next, and
//               changing the address or the login clears an earlier pass)
//               and "remote", or the install page (copyable commands, `run with pkexec`) for
//               "install", which itself continues into the API-key page pointed at
//               127.0.0.1:8080.
//   3 handler   `Xdg.isDefaultMagnetHandler()`'s answer, "open them with omaseed"/`not now`.
//   4 bar       #17 (ruled 2026-10-02): "add to the bar"/`not now`, App.barWidget. Only when
//               the Omarchy shell and the installed widget are here and it is not in the bar
//               yet (`barStep`, decided once when the page opens, so the steps line holds).
//   done        the server's name, version and count, three keys, "open the library".
//
// `esc` quits from welcome and step 1, goes back to step 1 from the install and server
// pages, and does nothing once the server is saved. `finished()` fires from the done screen; `MainView` has nothing else to do — `App`'s
// `serverChanged`/`firstRunNeeded` already flip the moment `addServer`/`setDefaultServer` run,
// so the table appears underneath this page a moment before it closes.
Item {
    id: root

    signal finished
    /// `esc` on the welcome or step 1: leave the app, as the footer says.
    signal quitRequested

    /// Off for `--demo firstrun`: the demo host sets `candidates`/`page` itself with fake
    /// data, instead of this page running the real `ServerDetector` against this machine.
    property bool autoDetect: true

    /// Where an unreadable `servers.json` was kept (review Q9): first run then shows although
    /// the user had servers, and one plain line on the welcome and step 1 says why.
    property string serversKeptPath: App.serversKeptPath

    // ---- step 1: candidates ------------------------------------------------------------
    property var candidates: []
    /// True until `App.serversDetected` answers; the welcome shows the search while it runs.
    property bool detecting: root.autoDetect
    /// More qBittorrents run here than the one candidate step 1 offers (Q26): the done
    /// screen points at Settings › Servers for the rest.
    property bool anotherRunning: false
    readonly property bool hasLocal: root.candidates.length > 0
    readonly property var _localCandidate: root.hasLocal ? root.candidates[0] : ({})

    readonly property var _rows: {
        const rows = [];
        if (root.hasLocal)
            rows.push({
                          "kind": "local",
                          "glyph": Glyphs.serverLocal,
                          "title": qsTr("Use the server on this machine"),
                          "detail": root._localCandidate.detail || "",
                          "dimmed": false
                      });
        rows.push({
                      "kind": "remote",
                      "glyph": Glyphs.serverRemote,
                      "title": qsTr("Connect to a remote server"),
                      // docs/14's wording: an API key is only one of the ways to sign in (M1).
                      "detail": qsTr("A seedbox, a NAS, or qui's proxy."),
                      "dimmed": false
                  });
        rows.push({
                      "kind": "install",
                      "glyph": "+",
                      "title": qsTr("Install qbittorrent-nox here"),
                      "detail": root.hasLocal ? qsTr("a local server was already found") : qsTr(
                                                                                              "Adds the package and a user service. Offered because you can, not because you must."),
                      "dimmed": root.hasLocal
                  });
        return rows;
    }

    property int cursorIndex: 0

    // ---- the page's state machine --------------------------------------------------------
    /// "welcome" | "choose" | "install" | "apikey" | "handler" | "done"
    property string page: root.autoDetect ? "welcome" : "choose"

    // Each step puts the keyboard where typing starts: the page root's own `Keys` only
    // serve the choose step, and the window's tab chain starts at the hero, not here.
    onPageChanged: Qt.callLater(() => {
        if (root.page === "apikey")
            urlField.focusInput();
        else if (root.page === "handler")
            registerButton.forceActiveFocus();
        else if (root.page === "bar")
            addBarButton.forceActiveFocus();
        else if (root.page === "done")
            openLibraryButton.forceActiveFocus();
        else if (root.page === "install")
            commandsField.focusInput();
        else
            root.forceActiveFocus();
    })
    /// The footer's keys for this page; `esc` is pinned right, as in the library's footer, and
    /// takes a click too (#51 D3): the mouse's way back to step 1 and out, with no button added.
    /// A screen reader finds each hint named by its action, the key its description (#78);
    /// `onScreen` leaves one out where the same action is a button on the page (the welcome's
    /// "set up" and "quit", done's "open the library"), so it is not found twice.
    readonly property var _footerHints: {
        const hint = (key, label, onScreen) => ({
                                                    "key": key,
                                                    "label": label,
                                                    "onScreen": onScreen === true
                                                });
        switch (root.page) {
        case "welcome":
            return [hint("enter", qsTr("set up"), true)];
        case "choose":
            return [hint("j k", qsTr("choose")), hint("enter", qsTr("continue"))];
        case "apikey":
            return [hint("tab", qsTr("next field")), hint("enter", root.tested && root.testOk
                                                                  ? qsTr("continue") : qsTr("test"))];
        case "handler":
        case "bar":
            return [hint("tab", qsTr("switch")), hint("enter", qsTr("choose"))];
        case "done":
            return [hint("enter", qsTr("open the library"), true)];
        }
        return [];
    }
    readonly property var _footerPinned: root.page === "welcome" || root.page === "choose" ? {
                                                                                                 "key": "esc",
                                                                                                 "label": qsTr("quit"),
                                                                                                 "clickable": true,
                                                                                                 "onScreen": root.page === "welcome"
                                                                                             } : root.page === "install"
                                                                                                 || root.page === "apikey" ? {
                                                                                                                                 "key": "esc",
                                                                                                                                 "label": qsTr("back"),
                                                                                                                                 "clickable": true
                                                                                                                             } : null

    /// 1 server (choose, install), 2 sign in, 3 links, 4 bar; 0 on the welcome, 5 once done.
    readonly property int stepNumber: root.page === "welcome" ? 0 : root.page === "choose"
                                                                  || root.page === "install" ? 1 :
                                                                                              root.page === "apikey" ? 2 :
                                                                                                                       root.page === "handler" ? 3 :
                                                                                                                                                 root.page === "bar" ? 4 : 5
    /// Each step asks its own question; step 1's would read wrong above a server's details or
    /// the magnet handler.
    readonly property string heading: root.page === "install" ? qsTr("Install qBittorrent on this machine?") :
                                      root.page === "apikey" ? qsTr("Where is it, and how does omaseed sign in?") :
                                      root.page === "handler" ? (root.alreadyHandler ? qsTr("Magnet links and .torrent files already open with omaseed.") :
                                                                                  qsTr("Open magnet links and .torrent files with omaseed?")) :
                                      root.page === "bar" ? qsTr("Show omaseed in the Omarchy bar?") :
                                      root.page === "done" ? qsTr("You're connected to %1.").arg(root.connectedName) :
                                      qsTr("Which qBittorrent should omaseed talk to?")

    // ---- step 2 buffers --------------------------------------------------------------------
    property string chosenKind: ""
    property string editUrl: ""
    property string editName: ""
    property string editAuthMode: "none"
    property string editUsername: ""
    property string editSecret: ""

    // The secret is the visible field's: a key typed, then "cookie" chosen, must not go out
    // as the password.
    onEditAuthModeChanged: {
        root.editSecret = root.editAuthMode === "apiKey" ? keyField.text : root.editAuthMode
                                                                          === "cookie" ? passField.text : "";
        root._resetTest();
    }
    // M2: a pass is for the address and login it tested; changing either asks for a new test.
    // The name is only a label, so it keeps the pass.
    // A certificate trusted for one address is not trusted for another.
    onEditUrlChanged: {
        root.trustedCertificate = "";
        root._resetTest();
    }
    onEditUsernameChanged: root._resetTest()
    onEditSecretChanged: root._resetTest()

    property bool tested: false
    property bool testOk: false
    property string testResultText: ""
    property bool testing: false
    // the rest of the last answer, kept so `continue` can hand it to the row it creates
    property string testVersion: ""
    property string testApiVersion: ""
    property int testCount: -1
    property int testLatency: -1
    property string testError: ""
    /// The raw error under a friendly one ("wrong username or password"), drawn dim.
    property string testErrorDetail: ""
    /// Issue #2: the certificate the last test was refused (empty when it was not), the one
    /// trusted for it then, and the one `trust this certificate` chose (saved with the server).
    property string certificateSha256: ""
    property string certificatePinned: ""
    property string trustedCertificate: ""

    readonly property string _newServerId: ""
    /// The saved server's name for the done screen ("strict"); the URL's host when unnamed.
    property string connectedName: ""
    /// Whether step 3 made omaseed the handler, for the done screen's second line.
    property bool registered: false
    /// #17: whether first run offers the bar widget (step 4), and whether it was asked for.
    property bool barStep: false
    property bool barAsked: false

    function _fieldMap(): var {
    return {
    "id": "",
    "name": root.editName,
    "url": root.editUrl,
    "authMode": root.editAuthMode,
    "username": root.editAuthMode === "cookie" ? root.editUsername : "",
"certificateSha256": root.trustedCertificate
};
}

function _resetTest(): void {
    root.tested = false;
    root.testOk = false;
    root.testResultText = "";
    root.testing = false;
    root.testVersion = "";
    root.testApiVersion = "";
    root.testCount = -1;
    root.testLatency = -1;
    root.testError = "";
    root.testErrorDetail = "";
    root.certificateSha256 = "";
    root.certificatePinned = "";
}

function openRow(kind: string, url: string): void {
    root.chosenKind = kind;
    root._resetTest();
    if (kind === "install") {
        root.page = "install";
        return;
    }
    root.editUrl = url;
    root.editName = kind === "local" ? qsTr("home") : "";
    root.editAuthMode = "none";
    root.editUsername = "";
    root.editSecret = "";
    root.trustedCertificate = "";
    // Written imperatively, not bound: a Kit `Field`'s `text` is a plain alias over its
    // TextInput (see the component's own header comment), so a declarative `text:
    // root.editUrl` binding would fight every keystroke. These buffers are filled *from*
    // the fields (`onTextChanged`) and pushed *into* them only here.
    urlField.text = root.editUrl;
    nameField.text = root.editName;
    keyField.text = "";
    userField.text = "";
    passField.text = "";
    root.page = "apikey";
}

function chooseCursor(): void {
    const row = root._rows[root.cursorIndex];
    if (!row)
        return;
    root.openRow(row.kind, row.kind === "remote" ? "" : root._localCandidate.url || "");
}

function moveCursor(delta: int): void {
    if (root._rows.length === 0)
        return;
    root.cursorIndex = Math.max(0, Math.min(root._rows.length - 1, root.cursorIndex + delta));
}

function runTest(): void {
    root.certificateSha256 = "";
    root.certificatePinned = "";
    root.testing = true;
    App.testServer(root._fieldMap(), root.editSecret);
}

/// `trust this certificate` (issue #2): kept for the server `continue` saves, then tested.
function trustCertificate(): void {
    root.trustedCertificate = root.certificateSha256;
    root.runTest();
}

/// `enter` on the sign-in page (M2): test until a test has passed, then continue.
function enterOnApiKey(): void {
    if (root.testing)
        return;
    if (root.tested && root.testOk)
        root.continueFromApiKey();
    else
        root.runTest();
}

function continueFromApiKey(): void {
    const id = App.addServer(root._fieldMap(), root.editSecret);
    if (id.length === 0)
        return;
    // The test ran before the server had an id: give its answer to the row that now exists,
    // so Settings → Servers shows the version and count straight away (docs/DECISIONS.md Q23).
    if (root.tested)
        App.serverList.setProbeResult(id, root.testOk, root.testVersion, root.testApiVersion,
                                      root.testCount, root.testLatency, root.testError);
    App.setDefaultServer(id);
    root.connectedName = root.editName.length > 0 ? root.editName : App.serverName;
    root._openHandlerStep();
}

function _openHandlerStep(): void {
    root.page = "handler";
    root.alreadyHandler = false;
    Xdg.isDefaultMagnetHandler();
}

property string currentHandlerText: ""
/// True when omaseed already answers for magnet: links, often only because nothing else on
/// the machine claims them: the step then offers to keep it rather than asking to switch.
property bool alreadyHandler: false

function registerHandler(): void {
    Xdg.registerAsDefaultHandler();
    Xdg.setFirstRunPromptAsked();
    root.registered = true;
    root._afterHandler();
}

function skipHandler(): void {
    Xdg.setFirstRunPromptAsked();
    root.registered = false;
    root._afterHandler();
}

function _afterHandler(): void {
    root.page = root.barStep ? "bar" : "done";
}

/// #17: "add to the bar". The done screen follows the install as it runs.
function addToBar(): void {
    root.barAsked = true;
    App.barWidget.install();
    root.page = "done";
}

function skipBar(): void {
    root.barAsked = false;
    root.page = "done";
}

function openLibrary(): void {
    root.finished();
}

/// Step 1's choice is still open to change: the install and sign-in pages, where `esc` (and a
/// click on the "server" step) goes back to it.
readonly property bool canGoBack: root.page === "install" || root.page === "apikey"

/// `esc`: quit before anything is chosen, back to step 1 while filling one in, and nothing
/// once the server is saved (steps 3, 4 and done).
function goBack(): void {
    if (root.page === "welcome" || root.page === "choose")
        root.quitRequested();
    else if (root.page === "install" || root.page === "apikey")
        root.page = "choose";
}

Component.onCompleted: {
    root.barStep = App.barWidget.available && !App.barWidget.inBar;
    if (root.autoDetect)
    App.detectServers();
    // The page has no sheet/scrim wrapping it to grab focus for it (unlike Kit/Sheet's
    // overlays): it must ask for it itself, or `j`/`k`/`enter` land on whatever the
    // window focused before first run showed (nothing, on a fresh launch).
    root.forceActiveFocus();
}

Connections {
    function onServersDetected(list: var, anotherRunning: bool): void {
        root.candidates = list;
        root.anotherRunning = anotherRunning;
        root.cursorIndex = 0;
        root.detecting = false;
    }

    target: App
}

Connections {
    function onServerCertificateRefused(id: string, sha256: string, pinned: string): void {
        if (id.length > 0 || !root.testing)
        return; // a saved server's own test, or an answer for fields changed since
        root.certificateSha256 = sha256;
        root.certificatePinned = pinned;
    }

    function onServerTested(id: string, ok: bool, version: string, apiVersion: string, torrentCount: int,
                            latencyMs: int, error: string, errorDetail: string): void {
        if (id.length > 0)
        return; // a saved server's own test, not first run's throwaway probe
        if (!root.testing)
        return; // an answer for fields changed since (M2): `_resetTest()` dropped it
        root.testing = false;
        root.tested = true;
        root.testOk = ok;
        root.testVersion = version;
        root.testApiVersion = apiVersion;
        root.testCount = torrentCount;
        root.testLatency = latencyMs;
        root.testError = error;
        root.testErrorDetail = errorDetail;
        if (ok) {
            root.certificateSha256 = "";
            root.certificatePinned = "";
        }
        root.testResultText = ok ? qsTr("qBittorrent %1 · API %2 · %3").arg(version.replace(
                                                                                /^v/, "")).arg(apiVersion).arg(
                                       torrentCount === 1 ? qsTr("1 torrent") : qsTr("%1 torrents").arg(
                                                                torrentCount)) : error;
    }

    target: App
}

Connections {
    function onIsDefaultMagnetHandlerResult(isDefault: bool, currentHandler: string): void {
        root.currentHandlerText = isDefault ? qsTr("omaseed") : currentHandler;
        root.alreadyHandler = isDefault;
    }

    target: Xdg
}

/// The api-key step's tab order. Kit `Field`s are not in Qt's own tab chain (the sheets
/// cycle by hand too), and the window's chain would wander off to the hero.
function _apiKeyTabOrder(): list<Item> {
    const order = [urlField, nameField];
    for (let i = 0; i < authChips.count; ++i)
        order.push(authChips.itemAt(i));
    if (root.editAuthMode === "apiKey")
        order.push(keyField);
    else if (root.editAuthMode === "cookie")
        order.push(userField, passField);
    if (certificateTrust.visible)
        order.push(certificateTrust.trustButton);
    order.push(testButton, openWebUiButton);
    if (saveAnywayButton.visible)
        order.push(saveAnywayButton);
    if (continueButton.enabled)
        order.push(continueButton);
    return order;
}

/// #201 F1: a credential picked (the chip clicked, or space on it) puts the caret in its
/// first field, the key or the username; "none" has none and leaves the keyboard on the chip.
/// Later, once the field the choice shows is laid out.
function _pickAuth(mode: string): void {
    root.editAuthMode = mode;
    const field = mode === "apiKey" ? keyField : mode === "cookie" ? userField : null;
    if (field)
        Qt.callLater(() => {
                         field.focusInput();
                         root._scrollIntoView(field);
                     });
}

/// Only matters when the column scrolls (a big font on a small screen): keep what the
/// keyboard just reached on screen.
function _scrollIntoView(item: Item): void {
    if (!scroller.interactive)
        return;
    const top = item.mapToItem(scroller.contentItem, 0, 0).y - Theme.space(8);
    const bottom = top + item.height + Theme.space(16);
    if (top < scroller.contentY)
        scroller.contentY = Math.max(0, top);
    else if (bottom > scroller.contentY + scroller.height)
        scroller.contentY = Math.min(bottom - scroller.height, scroller.contentHeight - scroller.height);
}

function _cycleApiKeyFocus(delta: int): void {
    const order = root.page === "handler" ? [registerButton, notNowButton] : root.page === "bar" ? [addBarButton, skipBarButton] :
                                                                                                   root._apiKeyTabOrder();
    let current = -1;
    for (let i = 0; i < order.length; ++i) {
        const item = order[i];
        if (item.activeFocus || item.editing === true)
            current = i;
    }
    const next = order[(current + delta + order.length) % order.length];
    if (typeof next.focusInput === "function")
        next.focusInput();
    else
        next.forceActiveFocus();
    root._scrollIntoView(next);
}

Keys.onPressed: event => {
    if (event.key === Qt.Key_Escape) {
        root.goBack();
        event.accepted = true;
        return;
    }
    if ((root.page === "welcome" || root.page === "done") && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
        if (root.page === "welcome")
            root.page = "choose";
        else
            root.openLibrary();
        event.accepted = true;
        return;
    }
    if (root.page === "apikey" && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
        root.enterOnApiKey();
        event.accepted = true;
        return;
    }
    if ((root.page === "apikey" || root.page === "handler" || root.page === "bar") && (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)) {
        root._cycleApiKeyFocus(event.key === Qt.Key_Tab ? 1 : -1);
        event.accepted = true;
        return;
    }
    if (root.page !== "choose")
        return;
    if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
        root.moveCursor(1);
        event.accepted = true;
    } else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
        root.moveCursor(-1);
        event.accepted = true;
    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
        root.chooseCursor();
        event.accepted = true;
    }
}

focus: true

// ---- layout -----------------------------------------------------------------------
// docs/14 C1: one column, centred both ways in the space above the footer, its text
// left-aligned. Each step is a different height, so the column re-centres between steps.
// The window grows with the font (Main.qml), so the column normally fits; when it cannot
// (a big font on a small screen), it starts at the top and scrolls, and never runs into the
// footer.
Flickable {
    id: scroller

    readonly property int margin: Theme.space(24)

    anchors.bottom: setupFooter.top
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: Math.max(scroller.height, column.height + 2 * scroller.margin)
    contentWidth: scroller.width
    interactive: column.height + 2 * scroller.margin > scroller.height
    objectName: "setupScroller"

    // A wheel notch moves three rows and quick notches add up, as in the library
    // (Kit/WheelScroll), while the column is taller than the room.
    WheelScroll {
        anchors.fill: parent
        enabled: scroller.interactive
        flickable: scroller
        objectName: "setupWheel"
        parent: scroller
        z: 2
    }

    ScrollBar {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.top: parent.top
        flickable: scroller
        vertical: true
    }

    ColumnLayout {
        id: column

        objectName: "setupColumn"
        spacing: Theme.space(18)
        width: Math.min(Theme.space(520), scroller.width - Theme.space(48))
        x: Math.round((scroller.width - column.width) / 2)
        y: Math.max(scroller.margin, Math.round((scroller.height - column.height) / 2))

        // ---- welcome (docs/14 screen 0) -----------------------------------------------------
        RowLayout {
            spacing: Theme.space(14)
            visible: root.page === "welcome"

            Icon {
                color: Theme.accent
                implicitHeight: Theme.font.display * 2
                implicitWidth: Theme.font.display * 2
                name: "mark"
            }

            // #79: the welcome's heading, then its intro and what the search found as text; a
            // plain Text publishes nothing to a screen reader otherwise.
            Label {
                Accessible.name: text
                Accessible.role: Accessible.Heading
                bright: true
                font.weight: Font.DemiBold
                objectName: "welcomeHeading"
                size: "display"
                text: qsTr("omaseed")
            }
        }

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            dim: true
            objectName: "welcomeIntro"
            text: qsTr("Torrents for Omarchy, powered by qBittorrent.")
            visible: root.page === "welcome"
            wrapMode: Text.WordWrap
        }

        // The server search, as it runs: what step 1 will offer.
        ColumnLayout {
            Layout.fillWidth: true
            objectName: "detectLines"
            spacing: Theme.space(4)
            visible: root.page === "welcome"

            // Each line by its words, without the leading · or ✓.
            Label {
                Accessible.name: qsTr("looking for qBittorrent on this machine…")
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                dim: true
                size: "small"
                text: qsTr("· looking for qBittorrent on this machine…")
                visible: root.detecting
            }

            Repeater {
                model: root.detecting ? [] : root.candidates

                Label {
                    required property var modelData

                    Accessible.name: qsTr("found %1").arg(modelData.detail || modelData.url || "")
                    Accessible.role: Accessible.StaticText
                    Layout.fillWidth: true
                    color: Theme.green
                    objectName: "detectFound"
                    size: "small"
                    text: qsTr("✓ found %1").arg(modelData.detail || modelData.url || "")
                    wrapMode: Text.WordWrap
                }
            }

            Label {
                Accessible.name: qsTr("no qBittorrent on this machine yet")
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                dim: true
                objectName: "detectNone"
                size: "small"
                text: qsTr("· no qBittorrent on this machine yet")
                visible: !root.detecting && !root.hasLocal
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            spacing: Theme.space(8)
            visible: root.page === "welcome"

            Button {
                id: setUpButton

                primary: true
                text: qsTr("set up")

                onClicked: root.page = "choose"
            }

            Button {
                text: qsTr("quit")

                onClicked: root.quitRequested()
            }
        }

        // ---- every other page: the mark, the named steps, the question -----------------------
        RowLayout {
            spacing: Theme.space(8)
            visible: root.page !== "welcome"

            Icon {
                color: Theme.accent
                implicitHeight: Theme.font.title
                implicitWidth: Theme.font.title
                name: "mark"
            }

            Label {
                bright: true
                font.weight: Font.DemiBold
                text: qsTr("omaseed")
            }
        }

        // docs/14 D1: done steps in green with a tick, the current one bright, the rest dim.
        // "server" takes a click back to step 1 while `esc` would go there (the install and
        // sign-in pages): the mouse's way back, with no button added to the page.
        Row {
            objectName: "setupSteps"
            spacing: Theme.space(18)
            visible: root.page !== "welcome"

            Repeater {
                model: root.barStep ? [qsTr("server"), qsTr("sign in"), qsTr("links"), qsTr("bar")] : [qsTr("server"), qsTr("sign in"), qsTr("links")]

                Label {
                    id: stepName

                    required property int index
                    required property string modelData

                    readonly property int number: index + 1
                    readonly property bool goesBack: stepName.number === 1 && root.canGoBack

                    // #77: a step that goes back is a link to it; its press is the click.
                    Accessible.ignored: !stepName.goesBack
                    Accessible.name: stepName.modelData
                    Accessible.role: Accessible.Link
                    Accessible.onPressAction: if (stepName.enabled && stepName.goesBack)
                                                  root.goBack()
                    bright: number === root.stepNumber
                    color: stepName.goesBack && stepHover.hovered ? Theme.accent
                                                                   : number < root.stepNumber ? Theme.green
                                                                                              : number === root.stepNumber
                                                                                                ? Theme.brightForeground
                                                                                                : Theme.dimForeground
                    objectName: "setupStep" + stepName.number
                    size: "small"
                    text: number < root.stepNumber ? "✓ " + modelData : modelData

                    HoverHandler {
                        id: stepHover

                        cursorShape: stepName.goesBack ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }

                    TapHandler {
                        enabled: stepName.goesBack

                        onTapped: root.goBack()
                    }
                }
            }
        }

        // #79: each step's question is its page's heading.
        Label {
            Accessible.name: text
            Accessible.role: Accessible.Heading
            Layout.fillWidth: true
            size: "heading"
            bright: true
            objectName: "heading"
            text: root.heading
            visible: root.page !== "welcome"
            wrapMode: Text.WordWrap
        }

        // Review Q9: under the welcome's buttons, or step 1's question.
        Label {
            // #79: a warning that comes on its own.
            Accessible.name: text
            Accessible.role: Accessible.AlertMessage
            Layout.fillWidth: true
            color: Theme.yellow
            objectName: "serversKeptLine"
            size: "small"
            text: qsTr("Your servers.json could not be read. It was kept as %1.").arg(
                      root.serversKeptPath)
            visible: root.serversKeptPath.length > 0
                     && (root.page === "welcome" || root.page === "choose")
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }

        // ---- step 1: choose --------------------------------------------------------------
        ColumnLayout {
            id: chooseColumn

            Layout.fillWidth: true
            spacing: Theme.space(6)
            visible: root.page === "choose"

            Repeater {
                model: root._rows

                Rectangle {
                    id: optionRow

                    required property int index
                    required property var modelData

                    readonly property bool hasCursor: optionRow.index === root.cursorIndex
                    readonly property bool hot: optionRow.hasCursor || hoverHandler.hovered
                    // Q20: the install row under a found server. Only the ink dims; cursor,
                    // hover, enter and click work as on the other rows.
                    readonly property bool dimmed: optionRow.modelData.dimmed
                    /// The row's words on its ground, the hover fill while it is hot
                    /// (docs/DECISIONS.md C30 F1).
                    readonly property Ink ink: Ink {
                        animated: false
                        ground: Theme.ground(false, false, optionRow.hot, false)
                    }

                    /// Choose this row: a click, or a screen reader's press.
                    function choose(): void {
                        root.cursorIndex = optionRow.index;
                        root.chooseCursor();
                    }

                    // #77: each way to start is a button named by its title, the detail its
                    // description; its press is the click, and does nothing while disabled.
                    Accessible.description: optionRow.modelData.detail
                    Accessible.name: optionRow.modelData.title
                    Accessible.role: Accessible.Button
                    Accessible.onPressAction: if (optionRow.enabled)
                                                  optionRow.choose()
                    Layout.fillWidth: true
                    objectName: "setupOption" + optionRow.index
                    color: optionRow.hot ? optionRow.ink.fill : "transparent"
                    implicitHeight: rowLayout.implicitHeight + Theme.space(16)
                    radius: Theme.cornerRadius

                    HoverHandler {
                        id: hoverHandler

                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: optionRow.choose()
                    }

                    RowLayout {
                        id: rowLayout

                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(12)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space(12)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.space(12)

                        Rectangle {
                            color: "transparent"
                            border.color: Theme.fill(Theme.borderIdle)
                            border.width: Theme.hairWidth
                            implicitHeight: Theme.space(28)
                            implicitWidth: Theme.space(28)
                            radius: Theme.cornerRadius

                            Text {
                                anchors.centerIn: parent
                                color: optionRow.dimmed ? optionRow.ink.dimForeground : optionRow.ink.accent
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.body
                                text: optionRow.modelData.glyph
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space(2)

                            Label {
                                Layout.fillWidth: true
                                bright: !optionRow.dimmed
                                dim: optionRow.dimmed
                                ink: optionRow.ink
                                text: optionRow.modelData.title
                            }

                            Label {
                                Layout.fillWidth: true
                                dim: true
                                ink: optionRow.ink
                                size: "small"
                                text: optionRow.modelData.detail
                                wrapMode: Text.WordWrap
                            }
                        }

                        Label {
                            // Spec Fig. 15 `.choice .k`: a dim caption in the key's own case —
                            // case is meaningful in every key chip (T3.5/T3.6), and `tracked`
                            // would print it as "ENTER".
                            dim: true
                            ink: optionRow.ink
                            size: "caption"
                            text: qsTr("enter")
                            visible: optionRow.hasCursor
                        }
                    }
                }
            }
        }

        // ---- step 2: install ---------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(12)
            visible: root.page === "install"

            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                text: qsTr(
                          "Two commands: install the package, then enable the user service so it starts with your session.")
                wrapMode: Text.WordWrap
            }

            // Q20: the port clash, only when step 1 found a server. The kit's `Banner` is one
            // line; this wraps, so it borrows `DuplicateSheet`'s rule-glyph-text shape instead.
            RowLayout {
                id: portWarning

                Layout.fillWidth: true
                objectName: "portWarning"
                spacing: Theme.space(8)
                visible: root.hasLocal

                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredHeight: portWarningText.implicitHeight
                    Layout.preferredWidth: 2
                    color: Theme.yellow
                }

                Label {
                    Layout.alignment: Qt.AlignTop
                    color: Theme.yellow
                    size: "small"
                    text: Glyphs.statusStalled
                }

                Label {
                    id: portWarningText

                    // #79: the clash is a warning: an alert by its words, the glyph left out.
                    Accessible.name: text
                    Accessible.role: Accessible.AlertMessage
                    Layout.fillWidth: true
                    color: Theme.yellow
                    size: "small"
                    text: qsTr(
                              "A server already answers on port 8080. The packaged service also uses 8080, so it will not start until one of them moves. To run two engines, give the new one its own profile and port (qbittorrent-nox --profile=… --webui-port=…).")
                    wrapMode: Text.WordWrap
                }
            }

            Field {
                id: commandsField

                Layout.fillWidth: true
                accessibleName: qsTr("install commands")
                objectName: "commandsField"
                text: App.installCommandsText()
            }

            RowLayout {
                spacing: Theme.space(8)

                Button {
                    text: qsTr("copy")

                    onClicked: App.copyToClipboard(commandsField.text)
                }

                Button {
                    primary: true
                    text: qsTr("run with pkexec")

                    onClicked: App.runInstallWithPkexec()
                }

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("continue")

                    onClicked: root.openRow("local", "http://127.0.0.1:8080")
                }
            }

            Connections {
                function onInstallFinished(ok: bool, message: string): void {
                    installResultLabel.text = ok ? qsTr("installed") : message;
                }

                target: App
            }

            Label {
                id: installResultLabel

                // #79: what the install came to ("installed", or why not) comes on its own.
                Accessible.ignored: text.length === 0
                Accessible.name: text
                Accessible.role: Accessible.AlertMessage
                Layout.fillWidth: true
                color: Theme.red
                objectName: "installResult"
                wrapMode: Text.WordWrap
            }
        }

        // ---- step 2: api key -----------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(12)
            visible: root.page === "apikey"

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space(10)

                FormRow {
                    Layout.fillWidth: true
                    Layout.horizontalStretchFactor: 3
                    label: qsTr("URL")

                    Field {
                        id: urlField

                        Layout.fillWidth: true
                        placeholder: qsTr("http://127.0.0.1:8080")

                        onAccepted: root.enterOnApiKey()
                        onTextChanged: root.editUrl = text
                    }
                }

                FormRow {
                    Layout.fillWidth: true
                    Layout.horizontalStretchFactor: 2
                    label: qsTr("Server name")

                    Field {
                        id: nameField

                        Layout.fillWidth: true
                        placeholder: qsTr("home")

                        onAccepted: root.enterOnApiKey()
                        onTextChanged: root.editName = text
                    }
                }
            }

            FormRow {
                Layout.fillWidth: true
                label: qsTr("Auth")

                RowLayout {
                    spacing: Theme.space(6)

                    Repeater {
                        id: authChips

                        model: [
                            {
                                "mode": "apiKey",
                                "text": qsTr("API key")
                            },
                            {
                                "mode": "cookie",
                                // M1: a cookie login is a username and a password; say so.
                                "text": qsTr("username and password")
                            },
                            {
                                "mode": "none",
                                "text": qsTr("none")
                            }
                        ]

                        Chip {
                            id: authChip

                            activeFocusOnTab: false

                            required property var modelData

                            focusable: true
                            checkable: true
                            // #201 F2: the choice made reads as chosen, with an accent edge; the fill alone
                            // (no border) read as a dimmed, disabled button beside the outlined others.
                            accent: authChip.selected
                            selected: root.editAuthMode === authChip.modelData.mode
                            text: authChip.modelData.text

                            onClicked: root._pickAuth(authChip.modelData.mode)
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }
            }

            FormRow {
                Layout.fillWidth: true
                label: qsTr("Key")
                visible: root.editAuthMode === "apiKey"

                Field {
                    id: keyField

                    Layout.fillWidth: true
                    objectName: "keyField"
                    echoMode: TextInput.Password
                    placeholder: qsTr("paste the API key")

                    onAccepted: root.enterOnApiKey()
                    onTextChanged: root.editSecret = text
                }
            }

            // Cookie login, as Settings' server sheet asks for it: a user and a password.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space(10)
                visible: root.editAuthMode === "cookie"

                FormRow {
                    Layout.fillWidth: true
                    label: qsTr("Username")

                    Field {
                        id: userField

                        Layout.fillWidth: true
                        objectName: "userField"

                        onAccepted: root.enterOnApiKey()
                        onTextChanged: root.editUsername = text
                    }
                }

                FormRow {
                    Layout.fillWidth: true
                    label: qsTr("Password")

                    Field {
                        id: passField

                        Layout.fillWidth: true
                        objectName: "passField"
                        echoMode: TextInput.Password

                        onAccepted: root.enterOnApiKey()
                        onTextChanged: root.editSecret = text
                    }
                }
            }

            // docs/14 screen 2: the answer sits above the buttons it decides, with a dot for
            // its state; a refused credential keeps the raw error dim underneath.
            RowLayout {
                Layout.fillWidth: true
                objectName: "testResult"
                spacing: Theme.space(8)
                visible: root.testResultText.length > 0

                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredHeight: Theme.space(7)
                    Layout.preferredWidth: Theme.space(7)
                    Layout.topMargin: Math.round((resultText.font.pixelSize * 1.4 - Theme.space(7)) / 2)
                    color: root.testOk ? Theme.green : Theme.red
                    radius: Theme.space(7) / 2
                }

                Label {
                    id: resultText

                    // #79: the test's answer comes on its own: an alert by its words (the dot
                    // beside it says the same in colour).
                    Accessible.name: text
                    Accessible.role: Accessible.AlertMessage
                    Layout.fillWidth: true
                    objectName: "testResultText"
                    color: root.tested && !root.testOk ? Theme.red : Theme.foreground
                    text: root.testOk ? qsTr("%1 · %2").arg(root.editAuthMode === "none" ? qsTr("reached") :
                                                                                           qsTr("signed in")).arg(
                                            root.testResultText) : root.testResultText
                    wrapMode: Text.WordWrap
                }
            }

            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space(15)
                Layout.topMargin: -Theme.space(8)
                dim: true
                objectName: "testErrorDetail"
                size: "small"
                text: root.testErrorDetail
                visible: root.tested && !root.testOk && root.testErrorDetail.length > 0
                wrapMode: Text.WordWrap
            }

            // Issue #2: a certificate the system does not trust, offered to be trusted.
            CertificateTrust {
                id: certificateTrust

                Layout.fillWidth: true
                buttonInTabChain: false // the page cycles tab itself (see _cycleApiKeyFocus)
                objectName: "certificateTrust"
                pinned: root.certificatePinned
                sha256: root.certificateSha256
                visible: root.certificateSha256.length > 0 && !root.testing

                onTrustRequested: root.trustCertificate()
            }

            RowLayout {
                spacing: Theme.space(8)

                Button {
                    id: testButton

                    activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                    objectName: "testButton"
                    // M2: the next thing to do is the highlighted one: test, then continue.
                    primary: !(root.tested && root.testOk)
                    text: root.testing ? qsTr("testing…") : qsTr("test")

                    onClicked: root.runTest()
                }

                Button {
                    id: openWebUiButton

                    activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                    text: qsTr("open web UI")

                    onClicked: Qt.openUrlExternally(root.editUrl)
                }

                Item {
                    Layout.fillWidth: true
                }

                // docs/14: "skip test" left the row. A server that is down right now can still be
                // saved, but only once a test has said so.
                Button {
                    id: saveAnywayButton

                    activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                    objectName: "saveAnywayButton"
                    text: qsTr("save anyway")
                    visible: root.tested && !root.testOk

                    onClicked: root.continueFromApiKey()
                }

                // M2: why `continue` is dimmed, until a test has said anything.
                Label {
                    Accessible.name: text
                    Accessible.role: Accessible.StaticText
                    dim: true
                    objectName: "continueHint"
                    size: "small"
                    text: qsTr("test first")
                    visible: !root.tested
                }

                Button {
                    id: continueButton

                    activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                    enabled: root.tested && root.testOk
                    objectName: "continueButton"
                    primary: root.tested && root.testOk
                    text: qsTr("continue")

                    onClicked: root.continueFromApiKey()
                }
            }
        }

        // ---- step 3: magnet handler -----------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(12)
            visible: root.page === "handler"

            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                dim: true
                objectName: "handlerText"
                text: root.alreadyHandler ? qsTr("Keep it that way, so a torrent app installed later does not take them.") :
                                            qsTr("Magnet links you click in a browser and .torrent files you open go straight to omaseed.")
                wrapMode: Text.WordWrap
            }

            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                dim: true
                text: root.currentHandlerText.length > 0 ? qsTr("currently: %1").arg(
                                                               root.currentHandlerText) : ""
                visible: root.currentHandlerText.length > 0 && !root.alreadyHandler
            }

            RowLayout {
                spacing: Theme.space(8)

                Button {
                    id: registerButton

                    activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                    objectName: "registerButton"
                    primary: true
                    text: root.alreadyHandler ? qsTr("keep omaseed") : qsTr("open them with omaseed")

                    onClicked: root.registerHandler()
                }

                Button {
                    id: notNowButton

                    activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                    text: qsTr("not now")

                    onClicked: root.skipHandler()
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }

        // ---- step 4: the bar widget (#17) -----------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(12)
            visible: root.page === "bar"

            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                dim: true
                objectName: "barText"
                text: qsTr("The omaseed mark in your bar shows what moves. Its popup switches alternative speeds, adds what you copied and shows what finished, without opening the window.")
                wrapMode: Text.WordWrap
            }

            RowLayout {
                spacing: Theme.space(8)

                Button {
                    id: addBarButton

                    activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                    objectName: "addBarButton"
                    primary: true
                    text: qsTr("add to the bar")

                    onClicked: root.addToBar()
                }

                Button {
                    id: skipBarButton

                    activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                    objectName: "skipBarButton"
                    text: qsTr("not now")

                    onClicked: root.skipBar()
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }

        // ---- done (docs/14 screen 4) -------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            objectName: "doneColumn"
            spacing: Theme.space(12)
            visible: root.page === "done"

            // #79: what setup ended with, line by line, as text.
            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                objectName: "doneFacts"
                text: {
                    const parts = [];
                    const version = (root.testVersion || App.globalState.serverVersion).replace(/^v/, "");
                    if (version.length > 0)
                        parts.push(qsTr("qBittorrent %1").arg(version));
                    const count = App.torrents.count > 0 ? App.torrents.count : root.testCount;
                    if (count >= 0)
                        parts.push(count === 1 ? qsTr("1 torrent") : qsTr("%1 torrents").arg(count));
                    if (App.globalState.freeSpaceOnDisk > 0)
                        parts.push(qsTr("%1 free").arg(App.globalState.freeSpaceText));
                    return parts.join(" · ");
                }
                wrapMode: Text.WordWrap
            }

            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                dim: true
                objectName: "doneHandler"
                text: root.registered || root.alreadyHandler ? qsTr("Magnet links and .torrent files open here.") :
                                        qsTr("Magnet links still open elsewhere; Settings can change that.")
                wrapMode: Text.WordWrap
            }

            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                dim: true
                objectName: "doneBar"
                text: App.barWidget.busy ? qsTr("Adding omaseed to your bar…") : App.barWidget.inBar ? qsTr("omaseed is in your bar; click its mark for the popup.") :
                                                                                     App.barWidget.lastOk ? qsTr("The bar widget is installed; Settings › Appearance puts it in the bar.") :
                                                                                                            qsTr("The bar widget could not be added: %1").arg(App.barWidget.message)
                visible: root.barAsked
                wrapMode: Text.WordWrap
            }

            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                dim: true
                objectName: "doneAnother"
                text: qsTr("Another qBittorrent is running here; add it later in Settings › Servers.")
                visible: root.anotherRunning
                wrapMode: Text.WordWrap
            }

            KeyHints {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space(4)
                model: [
                    {
                        "key": "a",
                        "label": qsTr("add a torrent")
                    },
                    {
                        "key": ",",
                        "label": qsTr("settings and servers")
                    },
                    {
                        "key": "?",
                        "label": qsTr("every shortcut")
                    }
                ]
            }

            RowLayout {
                spacing: Theme.space(8)

                Button {
                    id: openLibraryButton

                    objectName: "openLibraryButton"
                    primary: true
                    text: qsTr("open the library")

                    onClicked: root.openLibrary()
                }
            }
        }
    }
}

// ---- footer: the window's bottom edge, as the library's footer is ----------------------
Item {
    id: setupFooter

    anchors.bottom: parent.bottom
    anchors.left: parent.left
    anchors.right: parent.right
    height: Math.round(Theme.font.baseSize * 2.2)

    Hairline {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space(16)
        anchors.rightMargin: Theme.space(16)
        spacing: Theme.space(16)

        Label {
            dim: true
            objectName: "setupFacts"
            size: "small"
            // M3: no "step N of 3"; the steps line above the question says where you are.
            text: root.page === "done" ? qsTr("setup done") : App.versionText
        }

        KeyHints {
            Layout.fillWidth: true
            model: root._footerHints
            objectName: "setupFooterHints"
            pinned: root._footerPinned
            size: "small"

            onPinnedClicked: root.goBack()
        }
    }
}
}
