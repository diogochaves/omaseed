pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// "Limits" (spec v2 §3 Fig. 8, `.sheet`): download/upload speed, share ratio (with a "use the
// global default" chip) and seeding time, prefilled from the cursor torrent (the first of
// `hashes`). `apply` converts every field to the units `App.setLimits` wants: 0 means
// unlimited for a speed field left empty, `global` on the ratio chip is -2, an empty seeding
// time is -1 (no limit).
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
        seedingField.text = row.seedingTimeLimit !== undefined && row.seedingTimeLimit >= 0 ?
                                                                       String(Math.round(
                                                                                  row.seedingTimeLimit
                                                                                  / 1440)) : "";
        root._setAction(row.shareLimitAction, hashes);
        root.open();
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

    /// Bytes/s for a speed field: empty means unchanged (-1), "0" or blank-with-placeholder
    /// means unlimited (0), otherwise KB/s converted to bytes/s.
    function _speedValue(text: string): real {
        if (text.length === 0)
            return -1;
        const kb = Number(text);
        return Number.isFinite(kb) ? Math.max(0, Math.round(kb * 1024)) : -1;
    }

    property bool _ratioGlobal: true

    onActionTriggered: text => {
        if (text !== qsTr("reset"))
            return;
        dlField.text = "";
        ulField.text = "";
        root._setRatio(-2);
        seedingField.text = "";
        root._setAction("Default", []);
        App.setLimits(root.hashes, 0, 0, -2, -2, "Default");
        root.close();
    }
    onAccepted: {
        const ratio = root._ratioGlobal ? -2 : (ratioField.text.length > 0 ? Number(
                                                    ratioField.text) : -1);
        const seedingMinutes = seedingField.text.length > 0 ? Math.round(
                                   Number(seedingField.text) * 1440) : -1;
        App.setLimits(root.hashes, root._speedValue(dlField.text), root._speedValue(
                          ulField.text), ratio, seedingMinutes, root.actionValue());
        root.close();
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
