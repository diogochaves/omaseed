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
Sheet {
    id: root

    property var hashes: []

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
    sheetWidth: Theme.space(420)
    subtitle: qsTr("%1 torrents").arg(root.hashes.length)
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
        root.open();
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
        App.setLimits(root.hashes, 0, 0, -2, -2);
        root.close();
    }
    onAccepted: {
        const ratio = root._ratioGlobal ? -2 : (ratioField.text.length > 0 ? Number(
                                                    ratioField.text) : -1);
        const seedingMinutes = seedingField.text.length > 0 ? Math.round(
                                   Number(seedingField.text) * 1440) : -1;
        App.setLimits(root.hashes, root._speedValue(dlField.text), root._speedValue(
                          ulField.text), ratio, seedingMinutes);
        root.close();
    }

    FormRow {
        Layout.fillWidth: true
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
        label: qsTr("Share ratio")

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space(8)

            Field {
                id: ratioField

                Layout.fillWidth: true
                enabled: !root._ratioGlobal

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
        label: qsTr("Seeding time")

        Field {
            id: seedingField

            Layout.fillWidth: true
            placeholder: "∞"
            trailing: qsTr("days")

            onAccepted: root.triggerPrimary()
        }
    }
}
