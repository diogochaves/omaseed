pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// "Limits" (spec v2 §3 Fig. 8, `.sheet`): download/upload speed, share ratio (with a "use the
// global default" chip) and seeding time, prefilled from the cursor torrent (the first of
// `hashes`). `apply` converts every field to the units `App.setLimits` wants, and only the
// fields the user changed travel: an untouched speed is -1 (unchanged), an untouched ratio,
// seeding time or action is each torrent's own value (so a `global` seeding time stays -2 and
// a multi-selection keeps its differences). A changed field that is empty means no limit
// (0 for a speed, -1 for ratio and seeding time), as the `∞` placeholder says and as the
// global Speed limits sheet reads it; `global` on the ratio chip is -2. Seeding time is in
// days and may be fractional (90 minutes reads 0.06 and, untouched, stays 90 minutes). A
// field that is not a number stops `apply` with a red line instead of sending NaN.
//
// qBittorrent's inactive seeding time limit has no field here (the spec has none), but it
// travels in the same `torrents/setShareLimits` call, so each torrent's own value
// (`inactiveSeedingTimeLimit`) is sent back unchanged; only `reset` puts it back on global.
//
// T5.5 Q14 adds qBittorrent's own fifth row, "when the limit is reached" (qui's share-limit
// action row): one chip per `shareLimitAction` the server knows, shown only on a Web API
// >= 2.15.1 (`App.supportsShareLimitAction`), prefilled from the cursor torrent's own value.
// `default` leaves the choice to the server's global setting. Several selected torrents
// with different actions prefill `default` and flag the row `mixed`, like qui.
Sheet {
    id: root

    property var hashes: []
    /// The fifth row and its chips, for the tests.
    readonly property alias actionRow: actionRow
    readonly property alias actionChips: actionChips

    /// The wire names behind `actionChips.options`, in the same order (qui's order).
    readonly property var actionNames: ["Default", "Stop", "Remove", "RemoveWithContent",
                                        "EnableSuperSeeding"]
    readonly property var actionTexts: [qsTr("default"), qsTr("stop"), qsTr("remove"),
                                        qsTr("remove with content"), qsTr("super seed")]
    /// True when `hashes` disagree on their action, until the row is touched.
    property bool _actionMixed: false
    /// The fields as `openFor` filled them, to tell what the user changed.
    property var _initial: ({})
    /// Why `apply` refused, shown over the fields; empty hides it.
    property string errorText: ""

    readonly property alias downloadField: dlField
    readonly property alias uploadField: ulField
    readonly property alias ratioField: ratioField
    readonly property alias seedingField: seedingField

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": qsTr("reset"),
            "secondary": true
        },
        {
            "text": qsTr("apply"),
            "primary": true
        }
    ]
    glyph: "⇅" // ⇅
    // 460, not Fig. 8's 420: the five action chips of T5.5 Q14 fit on one line at base 14.
    sheetWidth: Theme.space(460)
    subtitle: root.hashes.length === 1 ? qsTr("1 torrent") : qsTr("%1 torrents").arg(
                                             root.hashes.length)
    title: qsTr("Limits")

    /// Opens on `hashes`, prefilling from the cursor torrent (`hashes[0]`)'s own limits.
    function openFor(hashes: var): void {
        root.hashes = hashes;
        const row = hashes.length > 0 ? App.torrentRow(hashes[0]) : {};
        dlField.text = row.dlLimit > 0 ? String(Math.round(row.dlLimit / 1024)) : "";
        ulField.text = row.upLimit > 0 ? String(Math.round(row.upLimit / 1024)) : "";
        root._setRatio(row.ratioLimit !== undefined ? row.ratioLimit : -2);
        seedingField.text = row.seedingTimeLimit !== undefined && row.seedingTimeLimit >= 0
                ? root._daysText(row.seedingTimeLimit) : "";
        root._setAction(row.shareLimitAction, hashes);
        root.errorText = "";
        root._initial = {
            "dl": dlField.text,
            "ul": ulField.text,
            "ratio": ratioField.text,
            "ratioGlobal": root._ratioGlobal,
            "seeding": seedingField.text
        };
        root.open();
    }

    /// Minutes as days for the field: whole days plainly, anything else to two decimals.
    function _daysText(minutes: real): string {
        return minutes % 1440 === 0 ? String(minutes / 1440) : String(Number((minutes / 1440).toFixed(2)));
    }

    /// A number the user typed, or NaN when it is not one (empty is handled by the callers).
    function _number(text: string): real {
        const value = Number(text.trim());
        return Number.isFinite(value) && value >= 0 ? value : NaN;
    }

    /// What `apply` sends, or `{error}` naming the first field that is not a number.
    /// `speed` values are bytes/s with -1 for unchanged; `ratio`/`seeding` are null when
    /// untouched, meaning "each torrent keeps its own".
    function _formValues(): var {
        const out = {
            "dl": root._speedValue(dlField.text, root._initial.dl),
            "ul": root._speedValue(ulField.text, root._initial.ul),
            "ratio": null,
            "seeding": null
        };
        if (Number.isNaN(out.dl))
            return {"error": qsTr("Download is not a number")};
        if (Number.isNaN(out.ul))
            return {"error": qsTr("Upload is not a number")};
        // qBittorrent keeps a speed limit in an int of bytes/s; past it the server wraps the
        // value to "no limit" without a word (review RUN-2 (e)).
        if (out.dl > root.maxSpeedBytes)
            return {"error": qsTr("Download is over the largest limit qBittorrent takes")};
        if (out.ul > root.maxSpeedBytes)
            return {"error": qsTr("Upload is over the largest limit qBittorrent takes")};
        if (root._ratioGlobal !== root._initial.ratioGlobal
                || (!root._ratioGlobal && ratioField.text.trim() !== String(root._initial.ratio).trim())) {
            if (root._ratioGlobal) {
                out.ratio = -2;
            } else if (ratioField.text.trim().length === 0) {
                out.ratio = -1;
            } else {
                out.ratio = root._number(ratioField.text);
                if (Number.isNaN(out.ratio))
                    return {"error": qsTr("Share ratio is not a number")};
            }
        }
        if (seedingField.text.trim() !== String(root._initial.seeding).trim()) {
            if (seedingField.text.trim().length === 0) {
                out.seeding = -1;
            } else {
                const days = root._number(seedingField.text);
                if (Number.isNaN(days))
                    return {"error": qsTr("Seeding time is not a number")};
                out.seeding = Math.round(days * 1440);
            }
        }
        return out;
    }

    /// Puts the chip on `name`'s text (unknown or empty reads as `default`); when the other
    /// `hashes` do not all agree with it the row falls back to `default` and shows `mixed`.
    function _setAction(name: var, hashes: var): void {
        const action = root._actionText(name);
        let mixed = false;
        for (let i = 1; i < hashes.length; ++i) {
            if (root._actionText(App.torrentRow(hashes[i]).shareLimitAction) !== action) {
                mixed = true;
                break;
            }
        }
        actionChips.current = mixed ? root.actionTexts[0] : action;
        root._actionMixed = mixed;
    }

    /// The chip text for a wire name; anything the row does not know is `default`.
    function _actionText(name: var): string {
        const at = root.actionNames.indexOf(name);
        return root.actionTexts[at >= 0 ? at : 0];
    }

    /// The wire name of the chip that is on (`default` and "nothing" both read as Default).
    function actionValue(): string {
        const at = root.actionTexts.indexOf(actionChips.current);
        return root.actionNames[at >= 0 ? at : 0];
    }

    /// -2 shows the "global" chip pressed and blanks the field; anything else fills it in.
    function _setRatio(value: real): void {
        if (value === -2) {
            root._ratioGlobal = true;
            ratioField.text = "";
        } else {
            root._ratioGlobal = false;
            ratioField.text = value >= 0 ? String(value) : "";
        }
    }

    /// Bytes/s for a speed field: -1 while it still reads what it opened with (unchanged),
    /// 0 (unlimited) when emptied or "0", otherwise KB/s converted to bytes/s; NaN when it is
    /// not a number.
    function _speedValue(text: string, initial: var): real {
        const typed = text.trim();
        if (initial !== undefined && typed === String(initial).trim())
            return -1;
        if (typed.length === 0)
            return 0;
        const kb = root._number(typed);
        return Number.isNaN(kb) ? NaN : Math.round(kb * 1024);
    }

    property bool _ratioGlobal: true
    /// The largest speed limit the Web API stores (a signed 32-bit bytes/s).
    readonly property real maxSpeedBytes: 2147483647

    onActionTriggered: text => {
        if (text !== qsTr("reset"))
            return;
        dlField.text = "";
        ulField.text = "";
        root._setRatio(-2);
        seedingField.text = "";
        root._setAction("Default", []);
        root.errorText = "";
        App.setLimits(root.hashes, 0, 0, -2, -2, "Default", -2);
        root.close();
    }
    // `torrents/setShareLimits` always takes ratio, seeding time and action together, so the
    // torrents are grouped by what they end up with: a field left alone keeps each torrent's
    // own value instead of the cursor torrent's (or -1) overwriting all of them.
    onAccepted: {
        const values = root._formValues();
        if (values.error !== undefined) {
            root.errorText = values.error; // and the sheet stays up, like the add sheet's refusal
            return;
        }
        root.errorText = "";
        const groups = [];
        const byKey = {};
        for (let i = 0; i < root.hashes.length; ++i) {
            const row = App.torrentRow(root.hashes[i]);
            const ratio = values.ratio !== null ? values.ratio
                                                : (row.ratioLimit !== undefined ? row.ratioLimit : -2);
            const seeding = values.seeding !== null ? values.seeding
                                                    : (row.seedingTimeLimit !== undefined ? row.seedingTimeLimit : -2);
            const action = root._actionMixed ? root.actionNames[Math.max(0, root.actionNames.indexOf(
                                                                             row.shareLimitAction))]
                                             : root.actionValue();
            const inactive = row.inactiveSeedingTimeLimit !== undefined ? row.inactiveSeedingTimeLimit
                                                                        : -2;
            const key = ratio + "|" + seeding + "|" + action + "|" + inactive;
            if (byKey[key] === undefined) {
                byKey[key] = groups.length;
                groups.push({
                                "hashes": [],
                                "ratio": ratio,
                                "seeding": seeding,
                                "action": action,
                                "inactive": inactive
                            });
            }
            groups[byKey[key]].hashes.push(root.hashes[i]);
        }
        for (let g = 0; g < groups.length; ++g)
            App.setLimits(groups[g].hashes, values.dl, values.ul, groups[g].ratio,
                          groups[g].seeding, groups[g].action, groups[g].inactive);
        root.close();
    }

    // A field that is not a number: the kit's error Banner, the red surface the add sheet
    // already uses for a refusal.
    Banner {
        Layout.fillWidth: true
        kind: "error"
        text: root.errorText
        visible: root.errorText.length > 0
    }

    // Spec §3 Fig. 8 lays the four rows out as `.sheet .cols`
    // (`grid-template-columns: 1fr 1fr`), two by two, not as one column.
    GridLayout {
        Layout.fillWidth: true
        columnSpacing: Theme.space(10)
        columns: 2
        rowSpacing: Theme.space(10)

        FormRow {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            label: qsTr("Download")

            Field {
                id: dlField

                Layout.fillWidth: true
                escClears: false
                placeholder: "∞" // ∞
                trailing: qsTr("KB/s")

                onAccepted: root.triggerPrimary()
            }
        }

        FormRow {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            label: qsTr("Upload")

            Field {
                id: ulField

                Layout.fillWidth: true
                escClears: false
                placeholder: "∞"
                trailing: qsTr("KB/s")

                onAccepted: root.triggerPrimary()
            }
        }

        FormRow {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            label: qsTr("Share ratio")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space(8)

                Field {
                    id: ratioField

                    Layout.fillWidth: true
                    escClears: false
                    enabled: !root._ratioGlobal
                    // Empty is no limit, as in the speed fields; under `global` the chip
                    // says it all and the field has nothing to add (batch-2 J6).
                    placeholder: root._ratioGlobal ? "" : "∞"

                    onAccepted: root.triggerPrimary()
                }

                Chip {
                    selected: root._ratioGlobal
                    text: qsTr("global ▾")

                    onClicked: root._ratioGlobal = !root._ratioGlobal
                }
            }
        }

        FormRow {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            label: qsTr("Seeding time")

            Field {
                id: seedingField

                Layout.fillWidth: true
                escClears: false
                placeholder: "∞"
                trailing: qsTr("days")

                onAccepted: root.triggerPrimary()
            }
        }

        // T5.5 Q14 — the fifth row spans both columns, like qui's select under its three
        // limit rows. Only a server that knows the field gets to show it.
        FormRow {
            id: actionRow

            Layout.columnSpan: 2
            Layout.fillWidth: true
            label: qsTr("When the limit is reached")
            visible: App.supportsShareLimitAction

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space(8)

                ChipSelect {
                    id: actionChips

                    Layout.fillWidth: true
                    allowNone: false
                    current: root.actionTexts[0]
                    options: root.actionTexts

                    onChanged: root._actionMixed = false
                }

                // qui's "mixed" hint: the selection disagrees, so `default` is a stand-in
                // until the row is touched.
                Label {
                    dim: true
                    size: "small"
                    text: qsTr("mixed")
                    visible: root._actionMixed
                }
            }
        }
    }
}
