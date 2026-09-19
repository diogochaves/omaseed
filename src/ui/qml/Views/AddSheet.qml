pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The add sheet (spec v2 §3 Fig. 5, `.sheet` at 600): everything that can start a torrent
// arrives here — `a` and the hero's `+`, the clipboard, a drop on the window, a `magnet:` or
// `.torrent` handed over by the browser, and `ctrl+o`'s portal file picker. What the user
// gave is parsed locally first (`App.parseAddable`), so the head already confirms the name,
// the size and where it came from instead of asking; the rows below only adjust the add.
//
// The sheet never talks to the server itself: it emits `addRequested(request)` with the map
// `App.addTorrent` takes, and listens for `App.addFinished` to close, to move on to the next
// queued source, or to show the server's refusal and stay open. An infohash the library
// already holds never leaves as an add at all — `duplicateFound(row)` is emitted instead and
// the host opens Fig. 10's duplicate sheet.
//
//     AddSheet { anchors.fill: parent; onAddRequested: request => App.addTorrent(request) }
Sheet {
    id: sheet

    /// `App.parseAddable`'s map for what is in the field, or null while it holds nothing
    /// recognisable. `valid`, `name`, `sizeText`, `infoHashV1/V2`, `kind`, `value`, `token`.
    property var addable: null
    /// Where the current text came from: "clipboard", "magnet", "file", "drop", "browser",
    /// or "" when it was typed. Only the head's subtitle reads it.
    property string source: ""
    /// The sources waiting behind this one (a drop or a hand-off of several links).
    property var queue: []
    /// Test seam: what `Xdg.detectClipboardMagnet()` would answer. Non-empty wins over the
    /// real clipboard, which an offscreen test run has no control over.
    property string clipboardAddable: ""
    /// The server's refusal, drawn as an error Banner above the rows; "" hides it.
    property string errorText: ""
    /// False once the save path has been typed into: the add then carries `autoTMM: false`.
    property bool autoTMM: true
    /// Names the category/tag palette answered with that the server does not list (yet):
    /// they join the chip rows so the choice is visible in the sheet.
    property var extraCategories: []
    property var extraTags: []

    /// The form, exposed for the host and the tests.
    readonly property alias sourceField: sourceField
    readonly property alias savePathField: savePathField
    readonly property alias upLimitField: upLimitField
    readonly property alias categoryChips: categoryChips
    readonly property alias tagChips: tagChips
    readonly property alias layoutChips: layoutChips
    readonly property alias sequentialToggle: sequentialToggle
    readonly property alias skipCheckToggle: skipCheckToggle

    /// The names in `App.categories` / `App.tags`, as plain string lists for the chip rows.
    /// Both models notify on `count` alone, so it is read first to make this re-evaluate
    /// when the server's category or tag list changes.
    readonly property var categoryNames: {
        const known = App.categories.count;
        const base = known > 0 ? App.categories.names() : [];
        return base.concat(sheet.extraCategories.filter(name => base.indexOf(name) < 0));
    }
    readonly property var tagNames: {
        const known = App.tags.count;
        const base = known > 0 ? App.tags.tags() : [];
        return base.concat(sheet.extraTags.filter(name => base.indexOf(name) < 0));
    }

    /// `add` / `add stopped`: the map `App.addTorrent` takes.
    signal addRequested(var request)
    /// This infohash is already on the server: `row` is `App.torrentRow`'s map. Fig. 10.
    signal duplicateFound(var row)
    /// The `…` chips: the host opens T4.4's palette in its `openForChoice` mode and hands the
    /// answer back through `applyChosenCategory` / `applyChosenTags`.
    signal categoryPaletteRequested
    signal tagPaletteRequested

    readonly property string _addText: qsTr("add")
    readonly property string _addStoppedText: qsTr("add stopped")

    /// Fig. 5's three layout values, in the order the figure lists them.
    readonly property var _layoutOptions: [qsTr("Original"), qsTr("Subfolder"), qsTr(
            "No subfolder")]
    readonly property var _layoutValues: ["Original", "Subfolder", "NoSubfolder"]

    /// "from clipboard" and its siblings, the third part of the head's subtitle.
    readonly property string sourceText: {
        switch (sheet.source) {
        case "clipboard":
            return qsTr("from clipboard");
        case "magnet":
            return qsTr("from magnet link");
        case "file":
            return qsTr("from file");
        case "drop":
            return qsTr("from drop");
        case "browser":
            return qsTr("from browser");
        }
        return "";
    }

    property bool _programmatic: false
    property string _pendingToken: ""
    property bool _awaitingPick: false
    property bool _savePathEdited: false

    /// What the clipboard offers, or "" — the fake first, so a test can drive it.
    function clipboardText(): string {
        if (sheet.clipboardAddable.length > 0)
            return sheet.clipboardAddable;
        const found = Xdg.detectClipboardMagnet();
        return found ? found.value : "";
    }

    /// `a`, the hero's `+`, the tray: an empty sheet, prefilled from the clipboard when it
    /// holds a magnet or a URL (and from `OMASEED_DEMO_ADD` under `--demo`, so Fig. 5 and
    /// Fig. 10 can be screenshot reproducibly).
    function openBlank(): void {
    sheet.reset();
    const demo = App.demoAddText();
    const prefill = demo.length > 0 ? demo : sheet.clipboardText();
    // Only something that really parses goes in: the clipboard usually holds prose, and the
    // sheet must not paste that over the field.
    const parsed = prefill.length > 0 ? App.parseAddable(prefill) : null;
    if (parsed && (parsed.valid || parsed.pending))
    sheet.loadText(prefill, "clipboard");
    sheet.open();
}

    /// One source, from a known place ("drop", "browser", …).
    function openWith(text: string, from: string): void {
    sheet.reset();
    sheet.loadText(text, from);
    sheet.open();
}

    /// Several: the first opens the sheet, the rest queue behind it.
    function openList(list: var, from: string): void {
    if (!list || list.length === 0)
    return;
    sheet.openWith(list[0], from);
    sheet.queue = list.slice(1);
}

    /// `ctrl+o`: the portal's file chooser. Its answer arrives as `App.addableParsed`.
    function browse(): void {
                           sheet._awaitingPick = true;
                           App.pickTorrentFile();
                       }

    /// Put `text` in the field and parse it, without treating it as typing.
    function loadText(text: string, from: string): void {
    sheet._programmatic = true;
    sourceField.text = text;
    sheet._programmatic = false;
    sheet.source = from;
    sheet.parseField();
}

    /// Re-read the field. Called on every keystroke, so the head follows a paste live.
    function parseField(): void {
                               const map = App.parseAddable(sourceField.text);
                               sheet._pendingToken = map.token;
                               sheet.addable = (map.valid || map.pending) ? map : null;
                               if (sheet.source.length === 0 && map.kind === "magnet")
                               sheet.source = "magnet";
                               else if (sheet.source.length === 0 && map.kind === "file")
                               sheet.source = "file";
                           }

    /// Everything `App.addTorrent` needs: the parse plus the rows.
    function request(stopped: bool): var {
    const map = {};
    for (const key in sheet.addable)
    map[key] = sheet.addable[key];
    map["category"] = sheet.chosenCategory();
    map["tags"] = sheet.chosenTags();
    map["savePath"] = savePathField.text;
    map["autoTMM"] = sheet.autoTMM;
    map["contentLayout"] = sheet.layoutValue();
    map["upLimitKb"] = parseInt(upLimitField.text, 10) || 0;
    map["sequentialDownload"] = sequentialToggle.checked;
    map["skipChecking"] = skipCheckToggle.checked;
    map["stopped"] = stopped;
    return map;
}

    /// The chip that is on, or "" for no category.
    function chosenCategory(): string {
        return categoryChips.current;
    }

        /// The chips that are on.
        function chosenTags(): var {
                                   return tagChips.selected.slice();
                               }

        /// The category palette's answer (`chosen(name)`): pick it, and keep it as a chip
        /// when the server's own list does not carry it yet.
        function applyChosenCategory(name: string): void {
        if (name.length > 0 && sheet.categoryNames.indexOf(name) < 0)
        sheet.extraCategories = sheet.extraCategories.concat([name]);
        categoryChips.current = name;
        categoryChips.cursorIndex = Math.max(0, sheet.categoryNames.indexOf(name));
        sheet._applyCategorySavePath();
    }

        /// The tag palette's answer (`chosen(tags)`).
        function applyChosenTags(tags: var): void {
                                                  const extra = [];
                                                  for (const tag of tags) {
                                                      if (sheet.tagNames.indexOf(tag) < 0)
                                                      extra.push(tag);
                                                  }
                                                  if (extra.length > 0)
                                                  sheet.extraTags = sheet.extraTags.concat(extra);
                                                  tagChips.selected = tags.slice();
                                              }

        /// "Original" | "Subfolder" | "NoSubfolder", whatever the chips are labelled.
        function layoutValue(): string {
            const at = sheet._layoutOptions.indexOf(layoutChips.current);
            return at >= 0 ? sheet._layoutValues[at] : "Original";
        }

        /// `enter` / `shift+enter` and the two buttons. A duplicate never leaves as an add.
        function submit(stopped: bool): void {
        if (!sheet.addable || sheet.addable.valid !== true)
        return;
        const duplicate = App.findDuplicate(sheet.addable.infoHashV1, sheet.addable.infoHashV2);
        if (duplicate && duplicate.hash) {
            sheet.duplicateFound(duplicate);
            return;
        }
            sheet.errorText = "";
            sheet.addRequested(sheet.request(stopped));
        }

            /// `App.addFinished`: move to the next queued source, close, or stay open in red.
            function finish(ok: bool, message: string): void {
            if (!ok) {
                sheet.errorText = message.length > 0 ? message : qsTr(
                                                                     "the server refused this torrent");
                return;
            }
            if (sheet.queue.length > 0) {
                const next = sheet.queue[0];
                sheet.queue = sheet.queue.slice(1);
                sheet.loadText(next, sheet.source);
                return;
            }
                sheet.dismiss();
            }

                /// `esc`, `cancel`, the scrim: drop what is queued and give the keyboard back.
                function dismiss(): void {
                                        sheet.queue = [];
                                        sheet.close();
                                    }

                /// Back to Fig. 5's empty state.
                function reset(): void {
                sheet._programmatic = true;
                sourceField.text = "";
                savePathField.text = "";
                upLimitField.text = "";
                sheet._programmatic = false;
                sheet.addable = null;
                sheet.source = "";
                sheet.queue = [];
                sheet.errorText = "";
                sheet.autoTMM = true;
                sheet._savePathEdited = false;
                sheet._pendingToken = "";
                sheet._awaitingPick = false;
                sheet.extraCategories = [];
                sheet.extraTags = [];
                categoryChips.current = "";
                categoryChips.cursorIndex = 0;
                tagChips.selected = [];
                tagChips.cursorIndex = 0;
                layoutChips.current = sheet._layoutOptions[0];
                layoutChips.cursorIndex = 0;
                sequentialToggle.checked = false;
                skipCheckToggle.checked = false;
            }

                /// The category's own save path, so `Save to` follows the chips until it is typed into.
                function _applyCategorySavePath(): void {
                                                       if (sheet._savePathEdited)
                                                       return;
                                                       sheet._programmatic = true;
                                                       savePathField.text
                                                       = categoryChips.current.length > 0
                                                       ? App.categories.savePathOf(
                                                             categoryChips.current) : "";
                                                       sheet._programmatic = false;
                                                   }

                actions: [
                    {
                        "text": qsTr("cancel"),
                        "cancel": true
                    },
                    {
                        "text": sheet._addStoppedText,
                        "secondary": true
                    },
                    {
                        "text": sheet._addText,
                        "primary": true
                    }
                ]
                footerRight: qsTr("server: %1").arg(App.serverName)
                glyph: Glyphs.add
                hints: [
                    {
                        "key": "tab",
                        "label": qsTr("fields")
                    },
                    {
                        "key": "enter",
                        "label": qsTr("add")
                    },
                    {
                        "key": "shift+enter",
                        "label": qsTr("add stopped")
                    }
                ]
                sheetWidth: Theme.space(600)
                subtitle: {
                    if (!sheet.addable)
                        return qsTr("paste a magnet link, a URL or pick a file");
                    if (sheet.addable.pending === true)
                        return qsTr("reading %1…").arg(sheet.addable.name);
                    const parts = [sheet.addable.name];
                    if (sheet.addable.sizeText.length > 0)
                        parts.push(sheet.addable.sizeText);
                    if (sheet.sourceText.length > 0)
                        parts.push(sheet.sourceText);
                    return parts.join(" · ");
                }
                title: qsTr("Add torrent")

                // Modal: nothing that lands here goes on to the window behind the scrim. A `Keys.onPressed`
                // in a derived component does not replace the base's — both handlers run on the same
                // attached object — so Sheet's own `esc`/`enter`/`tab` handling still happens and this one
                // only marks whatever is left as consumed, before MainView's `_route` could see it.
                Keys.onPressed: event => {
                    if (sheet.opened)
                        event.accepted = true;
                }

                onRejected: sheet.dismiss()
                onActionTriggered: text => {
                    if (text === sheet._addText)
                        sheet.submit(false);
                    else if (text === sheet._addStoppedText)
                        sheet.submit(true);
                }

                Connections {
                    function onAddFinished(ok: bool, message: string, hash: string): void {
                    if (sheet.opened)
                    sheet.finish(ok, message);
                }

                    // The second half of a `.torrent` parse, and the whole answer of the file picker.
                    function onAddableParsed(result: var): void {
                    if (!sheet.opened)
                    return;
                    if (sheet._awaitingPick) {
                        sheet._awaitingPick = false;
                        sheet.loadText(result.value, "file");
                        return;
                    }
                        if (result.token.length > 0 && result.token === sheet._pendingToken)
                        sheet.addable = result;
                    }

                        target: App
                    }

                    // The server's refusal (spec has no state for it in Fig. 5; the kit's error Banner is
                    // the one red surface a sheet already owns).
                    Banner {
                        Layout.fillWidth: true
                        kind: "error"
                        text: sheet.errorText
                        visible: sheet.errorText.length > 0
                    }

                    // `.row` — Magnet, URL or file
                    FormRow {
                        Layout.fillWidth: true
                        label: qsTr("Magnet, URL or file")

                        Field {
                            id: sourceField

                            Layout.fillWidth: true
                            placeholder: qsTr("magnet:?xt=urn:btih:…")
                            trailing: qsTr("ctrl+o browse")

                            onAccepted: sheet.triggerPrimary()
                            onTextChanged: {
                                if (sheet._programmatic)
                                    return;
                                sheet.source = "";
                                sheet.parseField();
                            }
                            // `ctrl+o` has to be taken inside the field: a TextInput with the caret swallows
                            // it before the sheet's own handler ever runs.
                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_O && (event.modifiers
                                                               & Qt.ControlModifier)) {
                                    sheet.browse();
                                    event.accepted = true;
                                }
                            }
                        }
                    }

                    // `.cols` — Category | Tags
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space(10)

                        FormRow {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            label: qsTr("Category")

                            ChipSelect {
                                id: categoryChips

                                Layout.fillWidth: true
                                more: true
                                options: sheet.categoryNames

                                onChanged: sheet._applyCategorySavePath()
                                onMoreRequested: sheet.categoryPaletteRequested()
                            }
                        }

                        FormRow {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            label: qsTr("Tags")

                            ChipSelect {
                                id: tagChips

                                Layout.fillWidth: true
                                more: true
                                multiple: true
                                options: sheet.tagNames

                                onMoreRequested: sheet.tagPaletteRequested()
                            }
                        }
                    }

                    // `.row` — Save to
                    FormRow {
                        Layout.fillWidth: true
                        label: qsTr("Save to")

                        Field {
                            id: savePathField

                            Layout.fillWidth: true
                            placeholder: qsTr("the server's default")
                            trailing: sheet.autoTMM && savePathField.text.length > 0 ? qsTr(
                                                                                           "from category") :
                                                                                       ""

                            onAccepted: sheet.triggerPrimary()
                            onTextChanged: {
                                if (sheet._programmatic)
                                    return;
                                // Typing a path of your own is what turns automatic torrent management off.
                                sheet._savePathEdited = true;
                                sheet.autoTMM = false;
                            }
                        }
                    }

                    // `.cols` — Layout | Upload limit
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space(10)

                        FormRow {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            label: qsTr("Layout")

                            ChipSelect {
                                id: layoutChips

                                Layout.fillWidth: true
                                current: sheet._layoutOptions[0]
                                options: sheet._layoutOptions
                            }
                        }

                        FormRow {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            label: qsTr("Upload limit")

                            Field {
                                id: upLimitField

                                Layout.fillWidth: true
                                placeholder: qsTr("∞")
                                trailing: qsTr("KB/s")

                                onAccepted: sheet.triggerPrimary()
                            }
                        }
                    }

                    // `.cols` — the two toggles
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space(10)

                        // A Kit `Toggle` sizes itself to its label, so each half of the `.cols` grid is an
                        // Item the layout can stretch instead.
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            implicitHeight: sequentialToggle.implicitHeight
                            implicitWidth: sequentialToggle.implicitWidth

                            Toggle {
                                id: sequentialToggle

                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("sequential download")
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            implicitHeight: skipCheckToggle.implicitHeight
                            implicitWidth: skipCheckToggle.implicitWidth

                            Toggle {
                                id: skipCheckToggle

                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("skip hash check")
                            }
                        }
                    }
                }
