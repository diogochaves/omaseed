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
Text {
    id: root

    property bool tracked: false
    property bool dim: false
    property bool bright: false
    property string size: "body"
    /// The ground the label is painted on; null is the window.
    property Ink ink: null

    readonly property int _pixelSize: Theme.font[root.tracked ? "caption" : root.size]

    color: root.ink !== null ? (root.bright ? root.ink.brightForeground : (root.dim || root.tracked)
                                              ? root.ink.dimForeground : root.ink.foreground)
                             : (root.bright ? Theme.brightForeground : (root.dim || root.tracked)
                                              ? Theme.dimForeground : Theme.foreground)
    font.capitalization: root.tracked ? Font.AllUppercase : Font.MixedCase
    font.family: Theme.fontFamily
    font.letterSpacing: root.tracked ? Math.max(1, Math.round(root._pixelSize * 0.08)) : 0
    font.pixelSize: root._pixelSize
    textFormat: Text.PlainText
}
