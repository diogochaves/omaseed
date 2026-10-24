pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// Unboxed key hints row (spec v2 §1 Fig. 1 footer `.hints`, and Fig. 22 empty-state teaching
// hints). `model` is a list of `{key, label}` objects, keys in `foreground`/`font.small`,
// labels dim, both at `size`. Keys are printed exactly as given — never upper-cased, because case is what
// separates `p` from `P` and `d` from `D` in the keymap. `pinned` is one more `{key, label}` pair pushed to the right edge
// (the `?` help hint) — matching `.hints .help { margin-left:auto }`. The caller must give
// this component a width (it is a `RowLayout`, so the pinned item's push only works with a
// bounded width).
//
// Too narrow for every hint (docs/10 §3), the row shows as many as fit, dropping from the
// right; the pinned hint always keeps its place. A hint with `keep: true` is dropped last: the
// others go from the right first, so a line that ends in the key that matters (the add sheet's
// `enter add`, #8) loses the extras before it.
//
// A hint with `clickable: true` names a safe action the mouse may run too: it shows a pointing
// hand and turns its key and label to the accent under the pointer (as the console's
// "esc clear" and Field's clickable trailing do), and a click sends `hintClicked`. A clickable
// `pinned` hint does the same and sends `pinnedClicked`. The row only reports it; the caller
// runs what that key runs.
//
// `pinnedLead` is one more pair kept at the right edge, just before `pinned` (docs/30 D10's
// `: commands` beside `? help`). It is never dropped either.
//
// A screen reader (#78) finds each hint named by what it does (its label), with the key as its
// description ("shortcut enter"): a button when it is clickable, a label when it is not. A hint
// with `onScreen: true` is left out, because the same action is a button on screen and the
// screen reader finds that one (first run's `enter set up` beside its "set up").
RowLayout {
    id: root

    property var model: []
    property var pinned: null
    property var pinnedLead: null
    /// Which `Theme.font.*` token the row prints at. The window footer is `small` (spec
    /// `.hints`); a sheet's or a palette's own hint line is `caption` (spec `.sheet .ft`,
    /// `.palette .ft`).
    property string size: "small"
    /// The ground the row is painted on (docs/DECISIONS.md C30 F1); null is the window.
    property Ink ink: null

    /// A click on a `clickable` hint: its index in `model` and the hint itself.
    signal hintClicked(int index, var hint)
    /// A click on the `pinned` hint when it is `clickable`.
    signal pinnedClicked(var hint)

    readonly property bool hasPinned: root.pinned !== null
    readonly property bool hasPinnedLead: root.pinnedLead !== null
    readonly property int _pixelSize: Theme.font[root.size]
    readonly property color _foreground: root.ink !== null ? root.ink.foreground : Theme.foreground
    readonly property color _dim: root.ink !== null ? root.ink.dimForeground : Theme.dimForeground
    readonly property color _accent: root.ink !== null ? root.ink.accent : Theme.accent
    /// Which of `model`'s hints are shown, by index: every `keep` hint that fits (left to
    /// right), then the others from the left while there is room.
    readonly property var _shown: {
        const hints = root.model; // re-evaluate when the hints change
        const count = hintRepeater.count;
        const shown = [];
        for (let i = 0; i < count; ++i)
            shown.push(false);
        let room = root.width - (root.hasPinned ? pinnedRow.implicitWidth + root.spacing : 0)
                - (root.hasPinnedLead ? pinnedLeadRow.implicitWidth + root.spacing : 0);
        // Each shown hint takes its width and one gap; the first gap is given back.
        room += hintsRow.spacing;
        const need = i => {
            const item = hintRepeater.itemAt(i);
            return item === null ? Infinity : item.implicitWidth + hintsRow.spacing;
        };
        const kept = i => hints !== null && hints[i] !== undefined && hints[i].keep === true;
        for (let i = 0; i < count; ++i) {
            if (kept(i) && need(i) <= room) {
                shown[i] = true;
                room -= need(i);
            }
        }
        for (let i = 0; i < count; ++i) {
            if (kept(i))
                continue;
            if (need(i) > room)
                break;
            shown[i] = true;
            room -= need(i);
        }
        return shown;
    }
    /// How many of `model`'s hints are shown.
    readonly property int shownCount: root._shown.filter(shown => shown).length

    /// A key as a screen reader's description: "shortcut enter"; nothing for no key.
    function keyDescription(key: string): string {
        return key.length > 0 ? qsTr("shortcut %1").arg(key) : "";
    }

    spacing: Theme.space(14)

    Row {
        id: hintsRow

        spacing: Theme.space(14)

        Repeater {
            id: hintRepeater

            model: root.model

            Item {
                id: hintRow

                required property int index
                required property var modelData

                implicitHeight: hintPair.implicitHeight
                implicitWidth: hintPair.implicitWidth
                objectName: "keyHint_" + hintRow.modelData.key
                visible: root._shown[hintRow.index] === true

                Row {
                    id: hintPair

                    spacing: Theme.space(6)

                    Text {
                        color: hintArea.containsMouse ? root._accent : root._foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: root._pixelSize
                        text: hintRow.modelData.key
                    }

                    Text {
                        color: hintArea.containsMouse ? root._accent : root._dim
                        font.family: Theme.fontFamily
                        font.pixelSize: root._pixelSize
                        text: hintRow.modelData.label
                    }
                }

                MouseArea {
                    id: hintArea

                    Accessible.description: root.keyDescription(hintRow.modelData.key ?? "")
                    Accessible.ignored: hintRow.modelData.onScreen === true
                    Accessible.name: String(hintRow.modelData.label ?? "").trim()
                    Accessible.role: hintArea.enabled ? Accessible.Button : Accessible.StaticText
                    // A screen reader's press is the click (#75): only on a clickable hint,
                    // and not while disabled. A MouseArea's own `enabled` is its flag alone,
                    // so the row's says whether something above it is disabled.
                    Accessible.onPressAction: if (hintRow.enabled && hintArea.enabled)
                                                  root.hintClicked(hintRow.index, hintRow.modelData)
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    enabled: hintRow.modelData.clickable === true
                    hoverEnabled: true
                    objectName: "keyHintArea"

                    onClicked: root.hintClicked(hintRow.index, hintRow.modelData)
                }
            }
        }
    }

    Item {
        Layout.fillWidth: true
    }

    Row {
        id: pinnedLeadRow

        objectName: "keyHintPinnedLead"
        visible: root.hasPinnedLead

        Row {
            spacing: Theme.space(6)

            Text {
                color: root._foreground
                font.family: Theme.fontFamily
                font.pixelSize: root._pixelSize
                text: root.pinnedLead ? root.pinnedLead.key : ""
            }

            Text {
                color: root._dim
                font.family: Theme.fontFamily
                font.pixelSize: root._pixelSize
                text: root.pinnedLead ? root.pinnedLead.label : ""
            }
        }
    }

    Item {
        id: pinnedRow

        implicitHeight: pinnedPair.implicitHeight
        implicitWidth: pinnedPair.implicitWidth
        objectName: "keyHintPinned"
        visible: root.hasPinned

        Row {
            id: pinnedPair

            spacing: Theme.space(6)

            Text {
                color: pinnedArea.containsMouse ? root._accent : root._foreground
                font.family: Theme.fontFamily
                font.pixelSize: root._pixelSize
                text: root.pinned ? root.pinned.key : ""
            }

            Text {
                color: pinnedArea.containsMouse ? root._accent : root._dim
                font.family: Theme.fontFamily
                font.pixelSize: root._pixelSize
                text: root.pinned ? root.pinned.label : ""
            }
        }

        MouseArea {
            id: pinnedArea

            Accessible.description: root.hasPinned ? root.keyDescription(root.pinned.key ?? "") : ""
            // No pinned hint, nothing to find; one whose action is a button on screen, as a hint.
            Accessible.ignored: !root.hasPinned || root.pinned.onScreen === true
            Accessible.name: root.hasPinned ? String(root.pinned.label ?? "").trim() : ""
            Accessible.role: pinnedArea.enabled ? Accessible.Button : Accessible.StaticText
            // As a hint's (#75).
            Accessible.onPressAction: if (pinnedRow.enabled && pinnedArea.enabled)
                                          root.pinnedClicked(root.pinned)
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            enabled: root.hasPinned && root.pinned.clickable === true
            hoverEnabled: true
            objectName: "keyHintPinnedArea"

            onClicked: root.pinnedClicked(root.pinned)
        }
    }
}
