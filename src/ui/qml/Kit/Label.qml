import QtQuick
import Omaseed.Kit

// Plain themed text (spec v2 §8 tokens page type scale, and the `.lbl`/`.eyebrow` caption
// class used throughout §1–7). `tracked` renders the spec's tracked-caption look: uppercase,
// ~0.08em letter-spacing, `font.caption`, dimmed — the same look as `.lbl`/`.eyebrow` in the
// spec's CSS. `size` picks a `Theme.font.*` token by name ("caption", "small", "body",
// "subtitle", "title", "heading", "display"); `tracked` overrides it to "caption" regardless,
// since every tracked caption in the spec is caption-sized. `dim`/`bright` opt into
// `dimForeground`/`brightForeground`; the plain default is `Theme.foreground`. On a fill, `ink`
// is the fill's (`Kit/Ink`, docs/DECISIONS.md C30 F1): each of the three is then that ground's,
// lifted where the label sits on it.
//
// The skin's voice (#171, the design note's `font` and `label`): a tracked caption takes the
// skin's label family, case, tracking and weight (Omarchy's are the `.lbl` above), any other
// label its value family. A label that names itself with `labelId` carries the skin's kana
// word after its words while the skin draws kana (board 46 `.kn`); nothing else gets one.
// `voice` gives a label one of the skin's other voices instead (Theme's `title*`, `control*`,
// `status*`, `nameTrack`): Omarchy's are the plain label's, so under Omarchy a voice changes
// nothing.
Text {
    id: root

    property bool tracked: false
    property bool dim: false
    property bool bright: false
    property string size: "body"
    /// The ground the label is painted on; null is the window.
    property Ink ink: null
    /// What the label names, as the skin's `labels` know it ("library", "appearance"): ""
    /// names nothing, and draws no kana.
    property string labelId: ""
    /// "title" (a heading: the title bar's, a sheet's, a menu's head), "control" (a
    /// control's words), "status" (the status line) or "name" (a thing's name: a rail row, a
    /// strip's words; `Theme.nameTrack`); "" is the plain label's, or the tracked caption's.
    property string voice: ""
    /// How far the kana rises over the words' baseline, in ems of its own size (board 46
    /// `.top h1 .kn { vertical-align: .25em }`); 0 sits it on the baseline.
    property real kanaRise: 0

    readonly property int _pixelSize: Theme.font[root.tracked ? "caption" : root.size]
    /// The skin's kana word for `labelId`, "" when it draws none.
    readonly property string kana: Theme.kana(root.labelId)

    /// The kana's colour: the skin's signal role, as it reads on the label's ground.
    function _kanaColor(): color {
        const ground = root.ink !== null ? root.ink : Theme.on.window;
        const role = ground[Theme.kanaRole];
        return role !== undefined ? role : Theme.roleColor(Theme.kanaRole);
    }

    color: root.ink !== null ? (root.bright ? root.ink.brightForeground : (root.dim || root.tracked)
                                              ? root.ink.dimForeground : root.ink.foreground)
                             : (root.bright ? Theme.brightForeground : (root.dim || root.tracked)
                                              ? Theme.dimForeground : Theme.foreground)
    font.capitalization: (root.tracked ? Theme.labelUpper : root.voice === "title" ? Theme.titleUpper
                          : root.voice === "control" ? Theme.controlUpper : root.voice === "status"
                                                       && Theme.statusUpper) ? Font.AllUppercase
                                                                             : Font.MixedCase
    font.family: root.tracked || root.voice === "title" || root.voice === "control" || root.voice === "name"
                 ? Theme.labelFamily : Theme.valueFamily
    font.letterSpacing: root.tracked ? Math.max(1, Math.round(root._pixelSize * Theme.labelTrack))
                                     : root._pixelSize * (root.voice === "title" ? Theme.labelTrackTitle
                                                          : root.voice === "control" ? Theme.controlTrack
                                                          : root.voice === "status" ? Theme.statusTrack
                                                          : root.voice === "name" ? Theme.nameTrack : 0)
    font.pixelSize: root._pixelSize
    font.weight: root.tracked ? Theme.labelWeight : root.voice === "title" ? Theme.titleWeight
                 : root.voice === "control" ? Theme.controlWeight : root.voice === "name" ? Theme.nameWeight
                 : Font.Normal
    // The kana's room is the label's own, so a layout gives it its width.
    rightPadding: root.kana.length > 0 ? kanaText.gap + kanaText.implicitWidth : 0
    textFormat: Text.PlainText

    // Board 46 `.kn`: 500, at most the caption's size, 0.06 em apart, 0.8 em after the words,
    // never upper case, on the words' baseline.
    Text {
        id: kanaText

        readonly property int gap: Math.round(kanaText.font.pixelSize * 0.8)

        anchors.baseline: root.baseline
        anchors.baselineOffset: -Math.round(kanaText.font.pixelSize * root.kanaRise)
        color: root._kanaColor()
        font.family: Theme.kanaFamily
        font.letterSpacing: kanaText.font.pixelSize * 0.06
        font.pixelSize: Math.min(root._pixelSize, Theme.font.caption)
        font.weight: Font.Medium
        objectName: "labelKana"
        text: root.kana
        textFormat: Text.PlainText
        visible: root.kana.length > 0
        x: Math.min(root.leftPadding + root.contentWidth, root.width - root.rightPadding)
           + kanaText.gap
    }
}
