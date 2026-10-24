pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The round trip in the add sheet (board 49 §4 `roundTripSheet()`, DECISIONS C27 CR8, CR9): the
// `.torrent` being added is one omaseed made here, or the site's copy of it (App.madeHereFor).
//
//   same    ✓ "Made here today 14:20: the same torrent": the site only changed the announce;
//           saved at the folder it was made from, in seed mode, nothing downloads
//   files   ✓ "Made here today 14:20: the same 14 files, another hash": what the site changed;
//           saved there and checked, then it seeds. The made one already in the library: replace
//           it (files kept, the default) or keep both
//   made    ! "This is the .torrent made here for HOST": upload it first; add still goes
//
// Properties only: the add sheet applies the save path and the check, and reads `replace`.
ColumnLayout {
    id: root

    /// App.madeHereFor's map; {} draws nothing.
    property var made: ({})
    /// The made one in the library is replaced (CR9); false keeps both.
    readonly property bool replace: root.asking && choice.choice === 0

    readonly property string kind: root.made.kind ?? ""
    readonly property bool asking: root.kind === "files" && (root.made.madeHash ?? "").length > 0
    readonly property string madeText: {
        const text = root.made.madeText ?? "";
        return text.length > 0 ? text.charAt(0).toUpperCase() + text.slice(1) : "";
    }

    objectName: "addMadeLine"
    spacing: Theme.createFactsGap
    visible: root.kind.length > 0

    CreateFact {
        Layout.fillWidth: true
        detail: root.kind === "same"
                ? qsTr("same info hash; the site only changed the announce · it seeds from %1 at once, nothing downloads").arg(root.made.folder ?? "")
                : root.kind === "files"
                  ? [root.made.changedText ?? "", qsTr("every file matches by name and size in %1 · qBittorrent checks them there first, then it seeds").arg(root.made.folder ?? "")].filter(text => text.length > 0).join(" · ")
                  : qsTr("%1 only knows it once you upload it: added now, it would announce a torrent the tracker calls unregistered. Upload it on %1's site, then add the site's copy.").arg(root.made.host ?? "")
        kind: root.kind === "made" ? "warn" : "ok"
        objectName: "addMadeFact"
        title: root.kind === "same" ? qsTr("%1: the same torrent").arg(root.madeText)
                                    : root.kind === "files" ? qsTr("%1: the same %2 files, another hash").arg(root.madeText).arg(root.made.fileCount ?? 0)
                                                            : qsTr("This is the .torrent made here for %1").arg(root.made.host ?? "")
    }

    CreateFact {
        Layout.fillWidth: true
        detail: qsTr("%1 seeds the same folder under another hash, which %2 doesn't know.").arg(root.made.madeName ?? "").arg(root.made.host ?? "")
        kind: "warn"
        objectName: "addMadeInLibrary"
        title: qsTr("The one made here is already in your library")
        visible: root.asking
    }

    SharedChoice {
        id: choice

        objectName: "addMadeChoice"
        rows: [{
                "title": qsTr("Replace it with the site's copy"),
                "detail": qsTr("the made one is removed, its files kept; the site's copy is checked there and seeds")
            }, {
                "title": qsTr("Keep both"),
                "detail": qsTr("two torrents over one folder; the made one keeps failing to announce")
            }]
        visible: root.asking
    }
}
