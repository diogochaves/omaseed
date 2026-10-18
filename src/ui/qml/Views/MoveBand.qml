import QtQuick
import Omaseed.Kit

// A folder's move, under the hero, for as long as it runs and until its summary is closed
// (board 42 §4–§6 J1–J4, T1–T4, E1, L1; DECISIONS C15 M10, M14, M16): board 35's box in the
// job's kind (accent while it runs, yellow when it waited on you, red when the first torrent
// failed, green at the end), with a 2 px progress line on its bottom edge. Its words are
// App.move.band's; "details" unfolds the job's record (copy copies it). The caller places it
// like the lost server's notice and does what an action's key says.
Notice {
    id: root

    /// App.move.band: {kind, glyph, headline, line, failedText, progress, actions [{id, text}]}.
    property var band: ({})
    /// The record "details" unfolds (App.move.detailsText()).
    property string record: ""

    readonly property string bandKind: root.band.kind ?? ""

    kind: root.bandKind === "red" ? "error" : root.bandKind === "yellow" ? "warning" :
                                                                           root.bandKind
                                                                           === "green" ? "success" :
                                                                                         "info"
    actions: (root.band.actions ?? []).filter(item => item.id !== "details").map(item => ({
        "label": item.text,
        "key": item.id
    }))
    body: root.band.line ?? ""
    bodyAlert: root.band.failedText ?? ""
    details: root.record
    glyph: root.band.glyph ?? ""
    objectName: "moveBand"
    progress: root.band.progress ?? -1
    title: root.band.headline ?? ""
}
