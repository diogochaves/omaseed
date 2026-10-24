pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// "Free the space…" (board 44 §2 F2, DECISIONS C17 D3, #118): a sheet that says each step
// before it runs. Stop the torrent (when it runs), delete the skipped files that hold space and
// no downloaded piece (only inside its folder, none another torrent uses, none a program has
// open), start it again; no recheck, but on a v1-only torrent whose files with downloaded
// pieces go too, which is named before it runs. On a hybrid those files stay, the sheet says
// why (a recheck there hits libtorrent's bug, #115), and "add it again" frees them, its cost
// named first (AddAgainConfirm). A v2-only torrent is treated like a hybrid, in words of its
// own: a recheck there leaves it short of 100 % for good, and adding it again doesn't
// (DECISIONS C29, #182).
//
// The plan is App.planFreeSkipped()'s, read when the sheet opens; App.freeSkippedSpace() reads
// the files again at delete time, so a file wanted or opened since stays.
//
// DECISIONS C30 F14–F15: when the plan finds that only adding it again frees anything (shift+f
// before the Files tab's reading, or a reading that changed), the sheet is titled "Only adding
// it again frees this" and its main button is the re-add ("Re-add to delete them…"); when some
// can go and some only that way, the main button frees what it can and "re-add to free all
// SIZE…" sits beside it.
Sheet {
    id: root

    property string hash: ""
    /// App's plan (`freeSkippedPlanned`); `{}` while it is read.
    property var plan: ({})
    readonly property bool ready: root.plan.hash !== undefined
    readonly property int deletes: root.plan.deletes || 0
    readonly property var items: root.plan.items || []
    readonly property bool anyOpen: root.items.some(item => item.verdict === "open")
    /// C30 F14–F15: the files only adding it again frees (a hybrid's or a v2-only torrent's
    /// with downloaded pieces).
    readonly property int readdFiles: root.plan.leftWithPieces || 0
    /// F14: nothing to free but by adding it again.
    readonly property bool readdOnly: root.ready && root.deletes === 0 && root.readdFiles > 0
    /// F15: some to free, some only by adding it again.
    readonly property bool mixed: root.ready && root.deletes > 0 && root.readdFiles > 0
    readonly property string readdText: root.readdOnly ? (root.readdFiles === 1 ? qsTr("Re-add to delete it…")
                                                                               : qsTr("Re-add to delete them…"))
                                                       : qsTr("re-add to free all %1…").arg(root.plan.allText || "")

    /// A hybrid's or a v2-only torrent's "add it again" (C29), with what it costs: the files
    /// and what they hold, and the version for the confirm's one line why (C30 F13).
    signal addAgainRequested(string hash, string name, int files, string bytesText, string version)
    /// "Show in folder": the torrent's folder.
    signal showFolderRequested(string hash)

    function openFor(hash: string): void {
        root.hash = hash;
        root.plan = ({});
        root.open();
        App.planFreeSkipped(hash);
    }

    function _verdictText(item: var): string {
        if (item.verdict === "delete")
            return qsTr("delete");
        if (item.verdict === "open")
            return item.openBy.length > 0 ? qsTr("open in %1 · stays").arg(item.openBy)
                                          : qsTr("open · stays");
        if (item.verdict === "pieces")
            return qsTr("has downloaded pieces · stays");
        if (item.verdict === "shared")
            return qsTr("another torrent uses it · stays");
        return qsTr("outside its folder · stays");
    }

    actions: {
        const list = [
            {
                "text": qsTr("cancel"),
                "cancel": true
            },
            {
                "text": qsTr("show in folder")
            }
        ];
        // F15: beside the main button, the re-add that frees all of it.
        if (root.mixed)
            list.push({
                          "text": root.readdText,
                          "disabled": App.skippedOnDisk.busy === true
                      });
        list.push({
                      "text": root.readdOnly ? root.readdText : root.deletes > 0 ? qsTr("free %1").arg(root.plan.freedText)
                                                                                : qsTr("nothing to free"),
                      "primary": true,
                      "disabled": !root.ready || (root.deletes === 0 && !root.readdOnly) || App.skippedOnDisk.busy === true
                  });
        return list;
    }
    glyph: Glyphs.trash
    // C30 F15: four buttons, the re-add's words among them; each as wide as its words
    // (Kit/Sheet, board 46), they fit 680 where four equal ones needed 880 (#169, #170), and
    // wrap onto two lines on a window narrower than that.
    sheetWidth: root.mixed ? Theme.space(680) : Theme.space(600)
    subtitle: root.ready ? (root.plan.name || "") + " · " + (root.plan.folder || "") : ""
    title: !root.ready ? qsTr("Free the space") : root.deletes > 0 ? qsTr("Free %1").arg(root.plan.freedText)
                                                                   : root.readdOnly ? qsTr("Only adding it again frees this")
                                                                                    : qsTr("Nothing to free")

    /// The re-add, with what it costs named first (AddAgainConfirm).
    function readd(): void {
        root.close();
        root.addAgainRequested(root.hash, root.plan.name || "", root.readdFiles + root.deletes,
                               root.plan.allText || "", root.plan.version === "v1" ? "" : (root.plan.version || ""));
    }

    onAccepted: {
        if (root.readdOnly) {
            root.readd();
            return;
        }
        if (!root.ready || root.deletes === 0)
            return;
        App.freeSkippedSpace(root.hash);
        root.close();
    }
    onActionTriggered: text => {
        if (text === qsTr("show in folder"))
            root.showFolderRequested(root.hash);
        else if (text === root.readdText)
            root.readd();
    }

    Connections {
        function onFreeSkippedPlanned(plan: var): void {
            if (plan.hash !== root.hash || !root.opened)
                return;
            root.plan = plan;
            // `enter` frees, as the sheet's primary: the keyboard on it once it can.
            const primary = root.primaryButton();
            if (primary && (root.deletes > 0 || root.readdOnly))
                Qt.callLater(() => primary.forceActiveFocus());
        }

        target: App
    }

    Label {
        Layout.fillWidth: true
        dim: true
        objectName: "freeSpaceReading"
        size: "small"
        text: (root.plan.error || "").length > 0 ? root.plan.error : qsTr("Reading the disk…")
        visible: !root.ready || (root.plan.error || "").length > 0
    }

    // The steps, in the order they run (docs/44 F2).
    Repeater {
        model: {
            if (!root.ready || root.deletes === 0)
                return [];
            const steps = [];
            if (root.plan.running)
                steps.push(qsTr("Stop the torrent so nothing writes while files go."));
            const only = qsTr("Only skipped files, only inside this torrent's folder, and none another torrent also uses.");
            if (root.deletes === 1)
                steps.push((root.plan.recheck ? qsTr("Delete 1 skipped file from the disk, one holding downloaded pieces.")
                                              : qsTr("Delete 1 skipped file from the disk: one that holds no downloaded piece."))
                           + " " + only);
            else
                steps.push((root.plan.recheck
                            ? qsTr("Delete %1 skipped files from the disk, some holding downloaded pieces.").arg(root.deletes)
                            : qsTr("Delete %1 skipped files from the disk: those that hold no downloaded piece.").arg(root.deletes))
                           + " " + only);
            if (root.plan.running)
                steps.push(root.plan.recheck ? qsTr("Start it again, then recheck it, so it downloads those pieces again when you tick them.")
                                             : qsTr("Start it again. The wanted files carry on. No recheck."));
            else if (root.plan.recheck)
                steps.push(qsTr("Recheck it, so it downloads those pieces again when you tick them. It stays stopped."));
            else
                steps.push(qsTr("It stays stopped. No recheck."));
            return steps;
        }

        RowLayout {
            id: stepRow

            required property int index
            required property string modelData

            Layout.fillWidth: true
            objectName: "freeSpaceStep-" + stepRow.index
            spacing: Theme.space(10)

            Label {
                Layout.alignment: Qt.AlignTop
                color: Theme.accent
                size: "small"
                text: String(stepRow.index + 1)
            }

            Label {
                Accessible.name: stepRow.modelData
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                size: "small"
                text: stepRow.modelData
                wrapMode: Text.Wrap
            }
        }
    }

    Hairline {
        Layout.fillWidth: true
        visible: root.items.length > 0
    }

    Repeater {
        model: root.items

        RowLayout {
            id: fileRow

            required property var modelData

            Accessible.name: qsTr("%1, %2, %3").arg(fileRow.modelData.name).arg(
                                 fileRow.modelData.holdsText).arg(root._verdictText(fileRow.modelData))
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            objectName: "freeSpaceFile"
            spacing: Theme.space(10)

            Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                size: "small"
                text: fileRow.modelData.name
            }

            Label {
                Layout.preferredWidth: Theme.space(70)
                dim: true
                horizontalAlignment: Text.AlignRight
                size: "small"
                text: fileRow.modelData.holdsText
            }

            Label {
                Layout.preferredWidth: Theme.space(200)
                color: fileRow.modelData.verdict === "delete" ? Theme.foreground : Theme.dimForeground
                elide: Text.ElideRight
                size: "small"
                text: root._verdictText(fileRow.modelData)
            }
        }
    }

    Label {
        Layout.fillWidth: true
        dim: true
        size: "small"
        text: qsTr("… %n more", "", root.plan.more || 0)
        visible: (root.plan.more || 0) > 0
    }

    Label {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.fillWidth: true
        dim: true
        objectName: "freeSpaceOpenNote"
        size: "small"
        text: qsTr("A file another program has open stays: on Linux its space would only come back when that program closes it.")
        visible: root.anyOpen
        wrapMode: Text.Wrap
    }

    // C17 D3: a hybrid's files with downloaded pieces stay; adding it again frees them. C29: a
    // v2-only torrent's too, said in its own words.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space(4)
        visible: (root.plan.leftWithPieces || 0) > 0

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            dim: true
            objectName: "freeSpaceStaysNote"
            size: "small"
            text: {
                const left = root.plan.leftWithPieces || 0;
                const bytes = root.plan.leftText || "";
                if (root.plan.version === "v2")
                    return left === 1
                           ? qsTr("1 file stays (%1) because this torrent is v2 and pieces of it were downloaded: deleting it leaves the torrent short of 100 % for good, a libtorrent bug.").arg(bytes)
                           : qsTr("%1 files stay (%2) because this torrent is v2 and pieces of them were downloaded: deleting them leaves the torrent short of 100 % for good, a libtorrent bug.").arg(left).arg(bytes);
                return left === 1
                       ? qsTr("1 file stays (%1) because this torrent is hybrid and pieces of it were downloaded: deleting it safely needs a recheck, and a recheck there hits a libtorrent bug.").arg(bytes)
                       : qsTr("%1 files stay (%2) because this torrent is hybrid and pieces of them were downloaded: deleting them safely needs a recheck, and a recheck there hits a libtorrent bug.").arg(left).arg(bytes);
            }
            wrapMode: Text.Wrap
        }

        // F14: what the re-add does, before its confirm says it again.
        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            dim: true
            objectName: "freeSpaceReaddNote"
            size: "small"
            text: root.readdFiles === 1
                  ? qsTr("It is removed keeping its files, the file is deleted, and it comes back from its own .torrent with the same files ticked, names, category, tags and limits. It loses its ratio, seeding time and added date.")
                  : qsTr("It is removed keeping its files, the %1 files are deleted, and it comes back from its own .torrent with the same files ticked, names, category, tags and limits. It loses its ratio, seeding time and added date.").arg(root.readdFiles)
            visible: root.readdOnly
            wrapMode: Text.Wrap
        }
    }
}
