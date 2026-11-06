import QtQuick
import Omaseed.Kit

// A move (a folder's, board 42, or a selection's, board 43), under the hero, for as long as it
// runs and until its summary is closed (board 42 §4–§6 J1–J4, T1–T4, E1, L1; DECISIONS C15
// M10, M14, M16; a selection's only after 3 s, C16 L6, which App.move.shown holds back): the
// box of board 35 in the job's kind (accent while it runs, yellow when it waited on you, red
// when the first torrent failed, green at the end), with a 2 px progress line on its bottom
// edge. Its words are App.move.band's; "details" unfolds the job's record (copy copies it).
// The caller places it like the lost server's notice and does what an action's key says.
Notice {
    id: root

    /// App.move.band: {kind, glyph, headline, line, failedText, progress, actions [{id, text}]}.
    property var band: ({})
    /// The record "details" unfolds (App.move.detailsText()).
    property string record: ""
    /// Board 43 J3 (C16 L12): the moves waiting behind this one (App.move.queued), listed in
    /// "details" each with "cancel" (`dropRequested`).
    property var queued: []
    /// The band in a word, where the skin names kinds by words (board 46 `bands()`: "moving",
    /// "moved"): while it runs, and once it has landed.
    property string doingTag: qsTr("moving")
    property string doneTag: qsTr("moved")

    /// "cancel" on the `index`th move waiting.
    signal dropRequested(int index)

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
    detailRows: root.queued.map(title => ({
        "text": qsTr("waiting: %1").arg(title),
        "link": qsTr("cancel")
    }))
    details: root.record
    glyph: root.band.glyph ?? ""
    banded: true
    objectName: "moveBand"
    progress: root.band.progress ?? -1
    tag: root.progress >= 0 ? root.doingTag : root.bandKind === "green" ? root.doneTag : ""
    title: root.band.headline ?? ""

    onDetailRowTriggered: index => root.dropRequested(index)
}
