pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The server sheet of spec v2 §3 Fig. 9, opened from a row of the settings modal's Servers
// pane. One `Kit/Sheet` with two bodies:
//
//   view mode   a `Kit/KeyValue` block — Auth, Key, Path map, Torrents, Reachable — and the
//               actions `test` / `remove` (danger) / `edit` (primary).
//   edit mode   "Edit opens the fields in place": Name, URL, the three auth chips, the key
//               (or username + password), and one path mapping; `cancel` / `test` / `save`.
//
// `test` runs `App.testServer()` in both modes and writes what comes back into the subtitle
// and into the Torrents / Reachable rows. A saved server's test lands on its list row by
// itself (App stores the probe under the id it was given); a new server has no id until
// `save`, so `_save()` hands the sheet's fresh result to the new row itself, or the Servers
// pane would show no version or count until the next test (docs/QUESTIONS.md Q23). Nothing
// here ever reads a stored secret back: the `Key` row shows a fixed mask, and an empty key
// field on save means "keep the stored one".
Sheet {
    id: root

    /// The row this sheet is showing, as `App.serverList.serverMap(id)` returns it. An empty
    /// map is a server that does not exist yet.
    property var server: ({})
    /// Fig. 9's second half. A new server opens straight into it.
    property bool editing: false

    /// The last `App.testServer()` answer for this sheet.
    property bool tested: false
    property bool testOk: false
    property string testVersion: ""
    property string testApiVersion: ""
    property int testCount: -1
    property int testLatency: -1
    property string testError: ""
    /// The raw error under a friendly one ("wrong username or password"), drawn dim.
    property string testErrorDetail: ""
    readonly property bool _credentialRefused: !root.testing && root.tested && !root.testOk
                                               && root.testErrorDetail.length > 0
    property bool testing: false

    // the edit buffers; `save` turns them back into a field map
    property string editName: ""
    property string editUrl: ""
    /// Why `save` refused, over the fields (review RUN-15); empty hides it.
    property string errorText: ""
    property string editAuthMode: "none"
    property string editUsername: ""
    property string editSecret: ""
    property string editRemote: ""
    property string editLocal: ""

    readonly property string serverId: root.server && root.server.id ? String(root.server.id) : ""
    readonly property bool isNew: root.serverId.length === 0

    /// The caller saved the sheet; `id` is the server that now exists.
    signal saved(string id)
    /// `remove` was pressed. The caller confirms and removes; the sheet is already closed.
    signal removeRequested(string id)

    /// Show an existing server (view mode).
    function openFor(map: var): void {
        root.server = map;
        root.editing = false;
        root.errorText = "";
        root._resetTest();
        root._loadBuffers();
        root.open();
    }

    /// Show an empty sheet in edit mode (Fig. 16's "+ add a server").
    function openNew(): void {
        root.server = {};
        root.editing = true;
        root.errorText = "";
        root._resetTest();
        root._loadBuffers();
        root.open();
    }

    /// The field map `App.addServer` / `App.updateServer` / `App.testServer` take.
    function fieldMap(): var {
        return {
            "id": root.serverId,
            "name": root.editName,
            "url": root.editUrl,
            "authMode": root.editAuthMode,
            "username": root.editUsername,
            "pathMapRemote": root.editRemote,
            "pathMapLocal": root.editLocal
        };
    }

    function _resetTest(): void {
        root.tested = false;
        root.testOk = false;
        root.testVersion = "";
        root.testApiVersion = "";
        root.testCount = -1;
        root.testLatency = -1;
        root.testError = "";
        root.testErrorDetail = "";
        root.testing = false;
    }

    // The fields are written, not bound: a Kit `Field`'s `text` is a plain alias over its
    // TextInput, so typing into it breaks any binding the sheet had put there (the component's
    // own header comment says so). The buffers below are therefore filled *from* the fields
    // (`onTextChanged`) and pushed *into* them only here, when a server is loaded.
    function _loadBuffers(): void {
        const map = root.server ? root.server : {};
        root.editAuthMode = map.authMode ? String(map.authMode) : "none";
        root.editSecret = "";
        nameField.text = map.name ? String(map.name) : "";
        urlField.text = map.url ? String(map.url) : "";
        userField.text = map.username ? String(map.username) : "";
        keyField.text = "";
        passField.text = "";
        remoteField.text = map.pathMapRemote ? String(map.pathMapRemote) : "";
        localField.text = map.pathMapLocal ? String(map.pathMapLocal) : "";
    }

    function _runTest(): void {
        root.testing = true;
        App.testServer(root.editing ? root.fieldMap() : root.server, root.editSecret);
    }

    /// Q23: the result of a `test` pressed before `save` belongs to the row that now exists.
    function _carryTestResult(id: string): void {
        if (!root.tested)
            return;
        App.serverList.setProbeResult(id, root.testOk, root.testVersion, root.testApiVersion,
                                      root.testCount, root.testLatency, root.testError);
    }

    function _save(): void {
        const map = root.fieldMap();
        if (root.editName.trim().length === 0 || root.editUrl.trim().length === 0) {
            root.errorText = qsTr("name and URL are required");
            return;
        }
        root.errorText = "";
        if (root.isNew) {
            const id = App.addServer(map, root.editSecret);
            if (id.length === 0) {
                // No usable URL: the sheet stays open with the fields as typed.
                root.errorText = qsTr("that URL cannot be used");
                return;
            }
            root._carryTestResult(id);
            root.server = App.serverList.serverMap(id);
            root.editing = false;
            root.saved(id);
        } else {
            if (!App.updateServer(map, root.editSecret)) {
                root.errorText = qsTr("that URL cannot be used");
                return;
            }
            root.server = App.serverList.serverMap(root.serverId);
            root.editing = false;
            root.saved(root.serverId);
        }
        root.close();
    }

    /// Fig. 9's `Key` row. The secret is never read back out of libsecret to be drawn, so the
    /// mask is a fixed width rather than the key's real length.
    readonly property string _maskedKey: "•".repeat(20)

    readonly property string _authValueText: {
        if (root.editAuthMode === "apiKey")
            return qsTr("API key · stored in %1").arg(qsTr("libsecret"));
        if (root.editAuthMode === "cookie")
            return qsTr("username and password · %1").arg(root.editUsername.length > 0 ? root.editUsername :
                                                                                         qsTr("no user"));
        return qsTr("none");
    }

    readonly property string _torrentsText: {
        if (root.testCount >= 0) {
            const live = !root.isNew && App.hasServer && App.serverName === root.editName
                && App.globalState.connected;
            return live ? qsTr("%1 · %2 free").arg(root.testCount).arg(
                              App.globalState.freeSpaceText) : String(root.testCount);
        }
        const count = root.server && root.server.torrentCount !== undefined ? Number(
                          root.server.torrentCount) : -1;
        return count >= 0 ? String(count) : qsTr("not tested yet");
    }

    readonly property string _reachableText: {
        if (root.testing)
            return qsTr("testing…");
        if (root.tested) {
            if (root.testOk)
                return root.testLatency >= 0 ? qsTr("yes · %1 ms").arg(root.testLatency) : qsTr("yes");
            return qsTr("no · %1").arg(root.testError);
        }
        const text = root.server && root.server.reachableText ? String(root.server.reachableText) : "";
        return text.length > 0 ? text : qsTr("not tested yet");
    }

    readonly property color _reachableColor: {
        if (root.testing || (!root.tested && (!root.server || !root.server.reachableText)))
            return Theme.dimForeground;
        const ok = root.tested ? root.testOk : (root.server && root.server.reachable === true);
        return ok ? Theme.green : Theme.red;
    }

    actions: root.editing ? [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": qsTr("test")
        },
        {
            "text": qsTr("save"),
            "primary": true
        }
    ] : [
        {
            "text": qsTr("test")
        },
        {
            "text": qsTr("remove"),
            "danger": true
        },
        {
            "text": qsTr("edit"),
            "primary": true
        }
    ]
    glyph: Glyphs.server
    hints: root.editing ? [
        {
            "key": "tab",
            "label": qsTr("fields")
        },
        {
            "key": "enter",
            "label": root._enterLabel
        }
    ] : [
        {
            "key": "enter",
            "label": root._enterLabel
        }
    ]
    /// What `enter` runs: the focused button, else the primary one (review RUN-9: with the
    /// keyboard on `test` the footer still said "enter edit").
    readonly property string _enterLabel: {
        // Only while open: a closed sheet asking actionButtons() (which claims its buttons)
        // whenever another overlay's button takes the focus was a binding loop.
        const item = root.opened ? root.Window.activeFocusItem as Button : null;
        if (item && root.actionButtons().indexOf(item) >= 0)
            return String(item.text);
        return root.editing ? qsTr("save") : qsTr("edit");
    }
    sheetWidth: Theme.space(460)
    subtitle: {
        const parts = [];
        if (root.editUrl.length > 0)
            parts.push(root.editUrl);
        if (root.testVersion.length > 0)
            parts.push(qsTr("qBittorrent %1").arg(root.testVersion.replace(/^v/, "")));
        if (root.testApiVersion.length > 0)
            parts.push(qsTr("API %1").arg(root.testApiVersion));
        return parts.join(" · ");
    }
    title: root.editName.length > 0 ? root.editName : qsTr("New server")

    onActionTriggered: text => {
        if (text === qsTr("test")) {
            root._runTest();
            return;
        }
        if (text === qsTr("edit")) {
            root.editing = true;
            root.focusFirst();
            return;
        }
        if (text === qsTr("remove")) {
            root.close();
            root.removeRequested(root.serverId);
            return;
        }
        if (text === qsTr("save")) {
            root._save();
            return;
        }
        if (text === qsTr("cancel")) {
            root.close();
        }
    }

    Connections {
        function onServerTested(id: string, ok: bool, version: string, apiVersion: string, torrentCount: int, latencyMs: int, error: string, errorDetail: string): void {
            if (id !== root.serverId)
                return;
            root.testing = false;
            root.tested = true;
            root.testOk = ok;
            root.testVersion = version;
            root.testApiVersion = apiVersion;
            root.testCount = torrentCount;
            root.testLatency = latencyMs;
            root.testError = error;
            root.testErrorDetail = errorDetail;
        }

        target: App
    }

    // ---- view mode (Fig. 9's `.kv` block) -------------------------------------------------
    KeyValue {
        Layout.fillWidth: true
        items: [
            {
                "key": qsTr("Auth"),
                "value": root._authValueText
            },
            {
                "key": qsTr("Key"),
                "value": root.editAuthMode === "none" ? qsTr("—") : root._maskedKey
            },
            {
                "key": qsTr("Path map"),
                "value": root.editRemote.length > 0 && root.editLocal.length
                > 0 ? qsTr("%1 → %2").arg(root.editRemote).arg(root.editLocal) : qsTr("none")
            },
            {
                "key": qsTr("Torrents"),
                "value": root._torrentsText
            },
            {
                "key": qsTr("Reachable"),
                "value": root._reachableText,
                "color": root._reachableColor
            }
        ]
        visible: !root.editing
    }

    // A refused save: the kit's error Banner, as the add and limits sheets show theirs.
    Banner {
        Layout.fillWidth: true
        kind: "error"
        text: root.errorText
        visible: root.errorText.length > 0
    }

    // ---- edit mode ("Edit opens the fields in place") --------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space(12)
        visible: root.editing

        FormRow {
            Layout.fillWidth: true
            label: qsTr("Name")

            Field {
                id: nameField

                Layout.fillWidth: true
                escClears: false
                placeholder: qsTr("home")

                onAccepted: root.triggerPrimary()
                onTextChanged: root.editName = text
            }
        }

        FormRow {
            Layout.fillWidth: true
            label: qsTr("URL")

            Field {
                id: urlField

                Layout.fillWidth: true
                escClears: false
                // An example, not a value: it must not read as a prefilled address.
                placeholder: qsTr("http://host:8080")

                onAccepted: root.triggerPrimary()
                onTextChanged: root.editUrl = text
            }
        }

        FormRow {
            Layout.fillWidth: true
            label: qsTr("Auth")

            RowLayout {
                spacing: Theme.space(6)

                Repeater {
                    model: [
                        {
                            "mode": "apiKey",
                            "text": qsTr("API key")
                        },
                        {
                            "mode": "cookie",
                            // M1: as first run says it; a cookie login is a username and password.
                            "text": qsTr("username and password")
                        },
                        {
                            "mode": "none",
                            "text": qsTr("none")
                        }
                    ]

                    Chip {
                        id: authChip

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
            visible: root.editAuthMode === "apiKey"

            Field {
                id: keyField

                Layout.fillWidth: true
                escClears: false
                echoMode: TextInput.Password
                placeholder: root.isNew ? qsTr("paste the API key") : qsTr("unchanged")

                onAccepted: root.triggerPrimary()
                onTextChanged: root.editSecret = text
            }
        }

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
                    escClears: false

                    onAccepted: root.triggerPrimary()
                    onTextChanged: root.editUsername = text
                }
            }

            FormRow {
                Layout.fillWidth: true
                label: qsTr("Password")

                Field {
                    id: passField

                    Layout.fillWidth: true
                    escClears: false
                    echoMode: TextInput.Password
                    placeholder: root.isNew ? "" : qsTr("unchanged")

                    onAccepted: root.triggerPrimary()
                    onTextChanged: root.editSecret = text
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space(10)

            FormRow {
                Layout.fillWidth: true
                label: qsTr("Path map · remote")

                Field {
                    id: remoteField

                    Layout.fillWidth: true
                    escClears: false
                    placeholder: qsTr("/downloads")

                    onAccepted: root.triggerPrimary()
                    onTextChanged: root.editRemote = text
                }
            }

            FormRow {
                Layout.fillWidth: true
                label: qsTr("local")

                Field {
                    id: localField

                    Layout.fillWidth: true
                    escClears: false
                    placeholder: qsTr("/data/torrents")

                    onAccepted: root.triggerPrimary()
                    onTextChanged: root.editLocal = text
                }
            }
        }

        Label {
            Layout.fillWidth: true
            dim: true
            size: "small"
            text: root.testing ? qsTr("testing…") : root.tested ? (root.testOk ? qsTr(
                                                                                    "reached %1 · qBittorrent %2 · API %3 · %4 torrents").arg(
                                                                                    root.editUrl).arg(root.testVersion.replace(
                                                                                                          /^v/, "")).arg(
                                                                                    root.testApiVersion).arg(
                                                                                    root.testCount) : root._credentialRefused
                                                                                   ? root.testError : qsTr(
                                                                                    "not reachable · %1").arg(
                                                                                    root.testError)) : ""
            color: root._credentialRefused ? Theme.red : Theme.dimForeground
            visible: root.testing || root.tested
            wrapMode: Text.WordWrap
        }

        // The server was reached and turned the credential down: the raw error, dim, under
        // the words above.
        Label {
            Layout.fillWidth: true
            dim: true
            objectName: "testErrorDetail"
            size: "small"
            text: root.testErrorDetail
            visible: root._credentialRefused
            wrapMode: Text.WordWrap
        }
    }
}
