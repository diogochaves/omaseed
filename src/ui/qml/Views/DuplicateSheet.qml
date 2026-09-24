pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// Fig. 10 — the add that never leaves (spec v2 §3, `.sheet` at 460). qBittorrent rejects an
// infohash it already holds, so omaseed finds that out locally (`App.findDuplicate`, which is
// why the SHA-1 of a `.torrent` is computed before the upload) and says which torrent it is
// instead of letting the server answer "Fails.". `show it` hands the hash back to the table.
//
//     DuplicateSheet { anchors.fill: parent; onShowRequested: hash => App.selectTorrent(hash) }
Sheet {
    id: sheet

    /// `App.torrentRow`'s map for the torrent already in the library, or null.
    property var row: null

    /// `show it`: move the table's cursor to this hash.
    signal showRequested(string hash)

    readonly property string _showText: qsTr("show it")

    /// "seeding since 12 Sep" — `torrentRow`'s `addedOnText` already reads "added <when>",
    /// and the figure's sentence supplies its own "since".
    readonly property string bodyText: {
        if (!sheet.row)
            return "";
        const since = String(sheet.row.addedOnText).replace(/^added /, "");
        return qsTr(
                    "This infohash is already in your list, %1 since %2. qBittorrent rejects duplicates.").arg(
                    sheet.row.stateText).arg(since);
    }

    /// Open on one `App.torrentRow` map.
    function openFor(duplicate: var): void {
    sheet.row = duplicate;
    // There is no form row here, so `open()` puts the keyboard on the primary action by
    // itself (Kit/Sheet, since T4.8): Fig. 10's default is `show it`.
    sheet.open();
}

    actions: [
        {
            "text": qsTr("close"),
            "cancel": true
        },
        {
            "text": sheet._showText,
            "primary": true
        }
    ]
    glyph: Glyphs.statusErrored
    sheetWidth: Theme.space(460)
    subtitle: sheet.row ? sheet.row.name : ""
    title: qsTr("Already on %1").arg(App.serverName)

    onRejected: sheet.close()
    onActionTriggered: text => {
        if (text !== sheet._showText)
            return;
        const hash = sheet.row ? sheet.row.hash : "";
        sheet.close();
        if (hash.length > 0)
            sheet.showRequested(hash);
    }

    // `.banner` — the figure's yellow notice. Kit's `Banner` is the one-line footer notice of
    // Fig. 23 and does not wrap; this one runs to two lines inside a 460-wide sheet, so it is
    // built here from the same parts (left rule, kind glyph, kind-coloured text).
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(8)

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.preferredHeight: bannerText.implicitHeight
            Layout.preferredWidth: 2
            color: Theme.yellow
        }

        Label {
            Layout.alignment: Qt.AlignTop
            color: Theme.yellow
            size: "small"
            text: Glyphs.statusErrored
        }

        Label {
            id: bannerText

            Layout.fillWidth: true
            color: Theme.yellow
            size: "small"
            text: sheet.bodyText
            wrapMode: Text.WordWrap
        }
    }
}
