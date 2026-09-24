pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// "Speed limits" (docs/08 §3, from the hero's ⋯ menu): the server's global limits, the normal
// pair and the alternative pair that alt-speed switches to, in the two-by-two grid of the
// torrent Limits sheet (Fig. 8). Fields are KB/s; empty means no limit. The fields stay
// empty and `apply` stays a no-op until the server's current values have arrived, so an
// early `enter` can never lift a limit nobody saw.
//
// The ⋯ menu's "alt-speed limits…" opens the same sheet on the alternative pair, titled for
// it and saying when those limits apply (batch-2 D2): one sheet, but no surprise.
Sheet {
    id: root

    /// The server's values have arrived and are in the fields.
    property bool loaded: false
    /// Opened from "alt-speed limits…": the caret starts in the alternative pair and the
    /// head names it.
    property bool alt: false

    readonly property alias downloadField: dlField
    readonly property alias altDownloadField: altDlField

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": qsTr("apply"),
            "primary": true
        }
    ]
    glyph: root.alt ? Glyphs.altSpeed : Glyphs.limits
    sheetWidth: Theme.space(460)
    subtitle: !root.loaded ? qsTr("reading the server…") : root.alt ? qsTr("used while alt-speed is on · A switches it")
                                                                    : qsTr("the server's global limits")
    title: root.alt ? qsTr("Alt-speed limits") : qsTr("Speed limits")

    /// Opens with the caret in the normal pair, or in the alternative pair when `alt`.
    function openFor(alt: bool): void {
        root.alt = alt;
        root.loaded = false;
        dlField.text = "";
        ulField.text = "";
        altDlField.text = "";
        altUlField.text = "";
        root.open();
        App.requestGlobalLimits();
        (alt ? altDlField : dlField).focusInput();
    }

    /// KB/s text for a limit in bytes/s; no limit is an empty field.
    function _kb(bytes: real): string {
        return bytes > 0 ? String(Math.round(bytes / 1024)) : "";
    }

    /// Bytes/s for a KB/s field: empty or 0 lifts the limit, anything unreadable leaves it (-1).
    function _bytes(text: string): real {
        if (text.trim().length === 0)
            return 0;
        const kb = Number(text);
        return Number.isFinite(kb) ? Math.max(0, Math.round(kb * 1024)) : -1;
    }

    onAccepted: {
        if (root.loaded)
            App.setGlobalLimits(root._bytes(dlField.text), root._bytes(ulField.text),
                                root._bytes(altDlField.text), root._bytes(altUlField.text));
        root.close();
    }

    Connections {
        function onGlobalLimitsLoaded(dl, up, altDl, altUp) {
            if (!root.opened)
                return;
            dlField.text = root._kb(dl);
            ulField.text = root._kb(up);
            altDlField.text = root._kb(altDl);
            altUlField.text = root._kb(altUp);
            root.loaded = true;
        }

        target: App
    }

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
                placeholder: "∞"
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
            label: qsTr("Alt-speed download")

            Field {
                id: altDlField

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
            label: qsTr("Alt-speed upload")

            Field {
                id: altUlField

                Layout.fillWidth: true
                escClears: false
                placeholder: "∞"
                trailing: qsTr("KB/s")

                onAccepted: root.triggerPrimary()
            }
        }
    }
}
