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
// Three steps (the footer's "step N of 3"):
//   1 choose    `App.detectServers()` on `Component.onCompleted`; rows for a detected local
//               server, "connect to a remote server", and "install qbittorrent-nox here" —
//               the last one dimmed and captioned "a local server was already found" when
//               step 1 found one (Q20: still selectable, since a second engine is possible
//               with its own profile and port, but the packaged service is one instance on
//               8080 and would not start next to a server already answering there).
//   2 details   either the API-key page (URL/name/auth/key, `test`, `continue`) for "local"
//               and "remote", or the install page (copyable commands, `run with pkexec`) for
//               "install", which itself continues into the API-key page pointed at
//               127.0.0.1:8080.
//   3 handler   `Xdg.isDefaultMagnetHandler()`'s answer, `register`/`not now`.
//
// `finished()` fires once step 3 is answered; `MainView` has nothing else to do — `App`'s
// `serverChanged`/`firstRunNeeded` already flip the moment `addServer`/`setDefaultServer` run,
// so the table appears underneath this page a moment before it closes.
Item {
    id: root

    signal finished

    /// Off for `--demo firstrun`: the demo host sets `candidates`/`page` itself with fake
    /// data, instead of this page running the real `ServerDetector` against this machine.
    property bool autoDetect: true

    // ---- step 1: candidates ------------------------------------------------------------
    property var candidates: []
    readonly property bool hasLocal: root.candidates.length > 0
    readonly property var _localCandidate: root.hasLocal ? root.candidates[0] : ({})

    readonly property var _rows: {
        const rows = [];
        if (root.hasLocal)
            rows.push({
                          "kind": "local",
                          "glyph": "⛁",
                          "title": qsTr("Use the server on this machine"),
                          "detail": root._localCandidate.detail || "",
                          "dimmed": false
                      });
        rows.push({
                      "kind": "remote",
                      "glyph": "⇢",
                      "title": qsTr("Connect to a remote server"),
                      "detail": qsTr(
                                    "A seedbox, a NAS, or qui's proxy. You will enter a URL and an API key."),
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
    /// "choose" | "install" | "apikey" | "handler"
    property string page: "choose"

    // Each step puts the keyboard where typing starts: the page root's own `Keys` only
    // serve the choose step, and the window's tab chain starts at the hero, not here.
    onPageChanged: Qt.callLater(() => {
        if (root.page === "apikey")
            urlField.focusInput();
        else if (root.page === "handler")
            registerButton.forceActiveFocus();
        else if (root.page === "install")
            commandsField.focusInput();
        else
            root.forceActiveFocus();
    })
    readonly property int stepNumber: root.page === "choose" ? 1 : root.page === "handler" ? 3 : 2

    // ---- step 2 buffers --------------------------------------------------------------------
    property string chosenKind: ""
    property string editUrl: ""
    property string editName: ""
    property string editAuthMode: "none"
    property string editSecret: ""

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

    readonly property string _newServerId: ""

    function _fieldMap(): var {
    return {
    "id": "",
    "name": root.editName,
    "url": root.editUrl,
    "authMode": root.editAuthMode,
    "username": ""
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
    root.editSecret = "";
    // Written imperatively, not bound: a Kit `Field`'s `text` is a plain alias over its
    // TextInput (see the component's own header comment), so a declarative `text:
    // root.editUrl` binding would fight every keystroke. These buffers are filled *from*
    // the fields (`onTextChanged`) and pushed *into* them only here.
    urlField.text = root.editUrl;
    nameField.text = root.editName;
    keyField.text = "";
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
    root.testing = true;
    App.testServer(root._fieldMap(), root.editSecret);
}

function continueFromApiKey(): void {
    const id = App.addServer(root._fieldMap(), root.editSecret);
    if (id.length === 0)
        return;
    // The test ran before the server had an id: give its answer to the row that now exists,
    // so Settings → Servers shows the version and count straight away (QUESTIONS.md Q23).
    if (root.tested)
        App.serverList.setProbeResult(id, root.testOk, root.testVersion, root.testApiVersion,
                                      root.testCount, root.testLatency, root.testError);
    App.setDefaultServer(id);
    root._openHandlerStep();
}

function _openHandlerStep(): void {
    root.page = "handler";
    Xdg.isDefaultMagnetHandler();
}

property string currentHandlerText: ""

function registerHandler(): void {
    Xdg.registerAsDefaultHandler();
    Xdg.setFirstRunPromptAsked();
    root.finished();
}

function skipHandler(): void {
    Xdg.setFirstRunPromptAsked();
    root.finished();
}

Component.onCompleted: {
    if (root.autoDetect)
    App.detectServers();
    // The page has no sheet/scrim wrapping it to grab focus for it (unlike Kit/Sheet's
    // overlays): it must ask for it itself, or `j`/`k`/`enter` land on whatever the
    // window focused before first run showed (nothing, on a fresh launch).
    root.forceActiveFocus();
}

Connections {
    function onServersDetected(list: var): void {
        root.candidates = list;
        root.cursorIndex = 0;
    }

    target: App
}

Connections {
    function onServerTested(id: string, ok: bool, version: string, apiVersion: string, torrentCount: int,
                            latencyMs: int, error: string): void {
        if (id.length > 0)
        return; // a saved server's own test, not first run's throwaway probe
        root.testing = false;
        root.tested = true;
        root.testOk = ok;
        root.testVersion = version;
        root.testApiVersion = apiVersion;
        root.testCount = torrentCount;
        root.testLatency = latencyMs;
        root.testError = error;
        root.testResultText = ok ? qsTr("qBittorrent %1 · API %2 · %3 torrents").arg(version.replace(
                                                                                         /^v/, "")).arg(
                                       apiVersion).arg(torrentCount) : error;
    }

    target: App
}

Connections {
    function onIsDefaultMagnetHandlerResult(isDefault: bool, currentHandler: string): void {
        root.currentHandlerText = isDefault ? qsTr("omaseed") : currentHandler;
    }

    target: Xdg
}

/// The api-key step's tab order. Kit `Field`s are not in Qt's own tab chain (the sheets
/// cycle by hand too), and the window's chain would wander off to the hero.
function _apiKeyTabOrder(): list<Item> {
    const order = [urlField, nameField];
    for (let i = 0; i < authChips.count; ++i)
        order.push(authChips.itemAt(i));
    if (keyField.visible)
        order.push(keyField);
    order.push(openWebUiButton, testButton, skipTestButton);
    if (continueButton.enabled)
        order.push(continueButton);
    return order;
}

function _cycleApiKeyFocus(delta: int): void {
    const order = root.page === "handler" ? [notNowButton, registerButton] : root._apiKeyTabOrder();
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
}

Keys.onPressed: event => {
    if ((root.page === "apikey" || root.page === "handler") && (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)) {
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
ColumnLayout {
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.top: parent.top
    anchors.topMargin: Theme.space(72)
    spacing: Theme.space(18)
    width: Theme.space(520)

    Label {
        text: qsTr("first run")
        tracked: true
    }

    Label {
        Layout.fillWidth: true
        size: "heading"
        bright: true
        text: qsTr("Which qBittorrent should omaseed talk to?")
        wrapMode: Text.WordWrap
    }

    Label {
        Layout.fillWidth: true
        dim: true
        text: root.hasLocal ? qsTr("omaseed is a client. It found one server on this machine.") :
                              qsTr("omaseed is a client. It found no server on this machine.")
        visible: root.page === "choose"
        wrapMode: Text.WordWrap
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

                Layout.fillWidth: true
                color: optionRow.hot ? Theme.fill(Theme.fillHover) : "transparent"
                implicitHeight: rowLayout.implicitHeight + Theme.space(16)
                radius: Theme.cornerRadius

                HoverHandler {
                    id: hoverHandler
                }

                TapHandler {
                    onTapped: {
                        root.cursorIndex = optionRow.index;
                        root.chooseCursor();
                    }
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
                        border.width: 1
                        implicitHeight: Theme.space(28)
                        implicitWidth: Theme.space(28)
                        radius: Theme.cornerRadius

                        Text {
                            anchors.centerIn: parent
                            color: optionRow.dimmed ? Theme.dimForeground : Theme.accent
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
                            text: optionRow.modelData.title
                        }

                        Label {
                            Layout.fillWidth: true
                            dim: true
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

            Layout.fillWidth: true
            color: Theme.red
            wrapMode: Text.WordWrap
        }
    }

    // ---- step 2: api key -----------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space(12)
        visible: root.page === "apikey"

        FormRow {
            Layout.fillWidth: true
            label: qsTr("URL")

            Field {
                id: urlField

                Layout.fillWidth: true
                placeholder: qsTr("http://127.0.0.1:8080")

                onTextChanged: root.editUrl = text
            }
        }

        FormRow {
            Layout.fillWidth: true
            label: qsTr("Server name")

            Field {
                id: nameField

                Layout.fillWidth: true
                placeholder: qsTr("home")

                onTextChanged: root.editName = text
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
                            "text": qsTr("cookie")
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
                        selected: root.editAuthMode === authChip.modelData.mode
                        text: authChip.modelData.text

                        onClicked: root.editAuthMode = authChip.modelData.mode
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
            visible: root.editAuthMode !== "none"

            Field {
                id: keyField

                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholder: qsTr("paste the API key")

                onTextChanged: root.editSecret = text
            }
        }

        RowLayout {
            spacing: Theme.space(8)

            Button {
                id: openWebUiButton

                activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                text: qsTr("open web UI")

                onClicked: Qt.openUrlExternally(root.editUrl)
            }

            Button {
                id: testButton

                activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                text: root.testing ? qsTr("testing…") : qsTr("test")

                onClicked: root.runTest()
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                id: skipTestButton

                activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                text: qsTr("skip test")

                onClicked: root.continueFromApiKey()
            }

            Button {
                id: continueButton

                activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                enabled: root.tested && root.testOk
                objectName: "continueButton"
                primary: true
                text: qsTr("continue")

                onClicked: root.continueFromApiKey()
            }
        }

        Label {
            Layout.fillWidth: true
            color: root.tested && !root.testOk ? Theme.red : Theme.foreground
            text: root.testResultText
            visible: root.testResultText.length > 0
            wrapMode: Text.WordWrap
        }
    }

    // ---- step 3: magnet handler -----------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space(12)
        visible: root.page === "handler"

        Label {
            Layout.fillWidth: true
            bright: true
            text: qsTr("Make omaseed the handler for magnet links and .torrent files?")
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            dim: true
            text: root.currentHandlerText.length > 0 ? qsTr("currently: %1").arg(
                                                           root.currentHandlerText) : ""
            visible: root.currentHandlerText.length > 0
        }

        RowLayout {
            spacing: Theme.space(8)

            Button {
                id: notNowButton

                activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                text: qsTr("not now")

                onClicked: root.skipHandler()
            }

            Button {
                id: registerButton

                activeFocusOnTab: false // the page cycles tab itself (see _cycleApiKeyFocus)

                primary: true
                text: qsTr("register")

                onClicked: root.registerHandler()
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }

    KeyHints {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space(18)
        model: [
            {
                "key": "",
                "label": qsTr("step %1 of 3").arg(root.stepNumber)
            }
        ].concat(root.page === "choose" ? [
                                              {
                                                  "key": "j k",
                                                  "label": qsTr("choose")
                                              },
                                              {
                                                  "key": "enter",
                                                  "label": qsTr("continue")
                                              }
                                          ] : [])
        pinned: {
            "key": "esc",
            "label": qsTr("quit")
        }
        size: "caption"
    }
}
}
