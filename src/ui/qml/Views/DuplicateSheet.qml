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
// docs/25 §2 (#26): the source may list trackers the torrent lacks. The sheet asks the
// server for the torrent's own (`App.loadDuplicateTrackers`), lists the new ones and offers
// `merge N trackers` as the primary action (D2 a); the host adds them and moves to the
// torrent's Trackers tab (D3 a). A private torrent, on either side, is never merged: one dim
// line says why, since another site's tracker would announce it outside its own.
//
// Hand-formatted since #26 (qmlformat rewrites typed function bodies).
//
//     DuplicateSheet { anchors.fill: parent; onShowRequested: hash => App.selectTorrent(hash) }
Sheet {
    id: sheet

    /// `App.torrentRow`'s map for the torrent already in the library, or null.
    property var row: null
    /// The duplicate source's trackers (`parseAddable`'s `trackers`), and whether it is
    /// private (`isPrivate`).
    property var sourceTrackers: []
    property bool sourcePrivate: false
    /// `App.duplicateTrackersLoaded`' answer for `row`: what a merge would add, and the
    /// torrent's own trackers (dimmed under the new ones).
    property var missingTrackers: []
    property var existingTrackers: []
    property bool trackersLoaded: false

    /// `show it`: move the table's cursor to this hash.
    signal showRequested(string hash)
    /// `merge N trackers`: add `urls` to `hash` (the host calls `App.mergeTrackers`).
    signal mergeRequested(string hash, var urls)

    readonly property string _showText: qsTr("show it")
    readonly property string _mergeText: sheet.missingTrackers.length === 1
                                         ? qsTr("merge 1 tracker")
                                         : qsTr("merge %1 trackers").arg(sheet.missingTrackers.length)

    /// Either side carries the private flag: nothing is merged.
    readonly property bool isPrivate: sheet.sourcePrivate || (sheet.row !== null && sheet.row.isPrivate === true)
    /// A magnet still fetching its metadata reads as public whatever it is: no merge yet.
    readonly property bool metadataPending: sheet.row !== null && sheet.row.hasMetadata === false
    /// Nothing may be merged, and the dim line says why.
    readonly property bool mergeBlocked: sheet.isPrivate || sheet.metadataPending
    readonly property bool canMerge: !sheet.mergeBlocked && sheet.missingTrackers.length > 0

    /// "seeding since 12 Sep" — `torrentRow`'s `addedOnText` already reads "added <when>",
    /// and the figure's sentence supplies its own "since".
    readonly property string bodyText: {
        if (!sheet.row)
            return "";
        const since = String(sheet.row.addedOnText).replace(/^added /, "");
        const known = qsTr("This infohash is already in your list, %1 since %2.").arg(
                    sheet.row.stateText).arg(since);
        if (!sheet.mergeBlocked && sheet.trackersLoaded && sheet.missingTrackers.length === 0
                && sheet.sourceTrackers.length > 0)
            return known + " " + qsTr("Its trackers are the same as this source's.");
        if (sheet.canMerge)
            return known;
        return known + " " + qsTr("qBittorrent rejects duplicates.");
    }

    /// "This source lists 2 trackers it doesn't have."
    readonly property string mergeHeadText: sheet.missingTrackers.length === 1
                                            ? qsTr("This source lists 1 tracker it doesn't have.")
                                            : qsTr("This source lists %1 trackers it doesn't have.").arg(
                                                  sheet.missingTrackers.length)
    readonly property string privateText: sheet.isPrivate
                                          ? qsTr("Private torrent: its trackers stay as they are. Adding another site's tracker would announce it outside its own tracker.")
                                          : qsTr("Its metadata hasn't arrived, so whether it is private isn't known yet: its trackers stay as they are.")

    /// Open on one `App.torrentRow` map. `source` is the add sheet's parse (`trackers`,
    /// `isPrivate`), or nothing when only the row is known.
    function openFor(duplicate, source) {
        sheet.row = duplicate;
        sheet.sourceTrackers = source && source.trackers ? source.trackers : [];
        sheet.sourcePrivate = source ? source.isPrivate === true : false;
        sheet.missingTrackers = [];
        sheet.existingTrackers = [];
        sheet.trackersLoaded = false;
        if (!sheet.mergeBlocked && sheet.sourceTrackers.length > 0 && duplicate && duplicate.hash)
            App.loadDuplicateTrackers(duplicate.hash, sheet.sourceTrackers);
        // There is no form row here, so `open()` puts the keyboard on the primary action by
        // itself (Kit/Sheet, since T4.8): Fig. 10's default is `show it`, or `merge` (D2 a).
        sheet.open();
    }

    /// "udp://tracker.example:1337/announce" → "tracker.example:1337".
    function hostOf(url) {
        const match = /^[a-z][a-z0-9+.-]*:\/\/([^/?#]+)/i.exec(String(url));
        return match ? match[1] : String(url);
    }

    actions: sheet.canMerge ? [
        {
            "text": qsTr("close"),
            "cancel": true
        },
        {
            "text": sheet._showText,
            "secondary": true
        },
        {
            "text": sheet._mergeText,
            "primary": true
        }
    ] : [
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
        const hash = sheet.row ? sheet.row.hash : "";
        if (text === sheet._showText) {
            sheet.close();
            if (hash.length > 0)
                sheet.showRequested(hash);
        } else if (sheet.canMerge && text === sheet._mergeText) {
            const urls = sheet.missingTrackers.slice();
            sheet.close();
            if (hash.length > 0)
                sheet.mergeRequested(hash, urls);
        }
    }

    Connections {
        function onDuplicateTrackersLoaded(hash, missing, existing) {
            if (!sheet.row || hash !== sheet.row.hash)
                return;
            sheet.missingTrackers = missing;
            sheet.existingTrackers = existing;
            sheet.trackersLoaded = true;
            // The merge button arrives late: put the keyboard on it, as opening would have.
            // The buttons are rebuilt from `actions` first, hence the later call.
            if (sheet.opened && sheet.canMerge)
                Qt.callLater(sheet.focusFirst);
        }

        target: App
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

    // docs/25 M1: the trackers a merge would add (green), then the ones already there (dim).
    RowLayout {
        objectName: "duplicateMerge"
        Layout.fillWidth: true
        spacing: Theme.space(8)
        visible: sheet.canMerge

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            color: Theme.accent
        }

        Label {
            Layout.alignment: Qt.AlignTop
            color: Theme.accent
            size: "small"
            text: "+"
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(2)

            Label {
                Layout.fillWidth: true
                bright: true
                size: "small"
                text: sheet.mergeHeadText
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: sheet.missingTrackers.slice(0, 5)

                delegate: Label {
                    required property string modelData

                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    size: "caption"
                    text: "<font color='" + Theme.green + "'>" + qsTr("+ new") + "</font>  "
                          + sheet.hostOf(modelData)
                    textFormat: Text.StyledText
                }
            }

            Label {
                Layout.fillWidth: true
                dim: true
                elide: Text.ElideRight
                size: "caption"
                text: sheet.missingTrackers.length > 5
                      ? qsTr("+ %1 more new").arg(sheet.missingTrackers.length - 5) : ""
                visible: text.length > 0
            }

            Label {
                Layout.fillWidth: true
                dim: true
                elide: Text.ElideRight
                size: "caption"
                text: sheet.existingTrackers.length === 0 ? ""
                      : sheet.existingTrackers.length === 1
                        ? qsTr("has  %1").arg(sheet.hostOf(sheet.existingTrackers[0]))
                        : qsTr("has  %1 (and %2 more)").arg(sheet.hostOf(sheet.existingTrackers[0])).arg(
                              sheet.existingTrackers.length - 1)
                visible: text.length > 0
            }
        }
    }

    // docs/25 M2: private on either side, so nothing to offer.
    RowLayout {
        objectName: "duplicatePrivate"
        Layout.fillWidth: true
        spacing: Theme.space(8)
        // Said only when the source brings trackers at all, or when it is private.
        visible: sheet.row !== null && (sheet.isPrivate
                                        || (sheet.metadataPending && sheet.sourceTrackers.length > 0))

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            color: Theme.fill(Theme.hairlineAlpha)
        }

        Label {
            Layout.alignment: Qt.AlignTop
            dim: true
            size: "small"
            text: "◆"
        }

        Label {
            Layout.fillWidth: true
            dim: true
            size: "small"
            text: sheet.privateText
            wrapMode: Text.WordWrap
        }
    }
}
