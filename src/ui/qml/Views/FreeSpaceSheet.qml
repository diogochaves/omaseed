pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// "Free the space…" (board 44 §2 F2, DECISIONS C17 D3, #118): a sheet that says each step
// before it runs. Stop the torrent (when it runs), delete the skipped files that hold space and
// no downloaded piece (only inside its folder, none another torrent uses, none a program has
// open), start it again; no recheck, but on a v1-only or v2-only torrent whose files with
// downloaded pieces go too, which is named before it runs. On a hybrid those files stay, the
// sheet says why (a recheck there hits libtorrent's bug, #115), and "add it again" frees them,
// its cost named first (AddAgainConfirm).
//
// The plan is App.planFreeSkipped()'s, read when the sheet opens; App.freeSkippedSpace() reads
// the files again at delete time, so a file wanted or opened since stays.
Sheet {
    id: root

    property string hash: ""
    /// App's plan (`freeSkippedPlanned`); `{}` while it is read.
    property var plan: ({})
    readonly property bool ready: root.plan.hash !== undefined
    readonly property int deletes: root.plan.deletes || 0
    readonly property var items: root.plan.items || []
    readonly property bool anyOpen: root.items.some(item => item.verdict === "open")

    /// The hybrid's "add it again", with what it costs.
    signal addAgainRequested(string hash, string name, int files)
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

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": qsTr("show in folder")
        },
        {
            "text": root.deletes > 0 ? qsTr("free %1").arg(root.plan.freedText) : qsTr("nothing to free"),
            "primary": true,
            "disabled": !root.ready || root.deletes === 0 || App.skippedOnDisk.busy === true
        }
    ]
    glyph: Glyphs.trash
    sheetWidth: Theme.space(600)
    subtitle: root.ready ? (root.plan.name || "") + " · " + (root.plan.folder || "") : ""
    title: !root.ready ? qsTr("Free the space") : root.deletes > 0 ? qsTr("Free %1").arg(root.plan.freedText)
                                                                   : qsTr("Nothing to free")

    onAccepted: {
        if (!root.ready || root.deletes === 0)
            return;
        App.freeSkippedSpace(root.hash);
        root.close();
    }
    onActionTriggered: text => {
        if (text === qsTr("show in folder"))
            root.showFolderRequested(root.hash);
    }

    Connections {
        function onFreeSkippedPlanned(plan: var): void {
            if (plan.hash !== root.hash || !root.opened)
                return;
            root.plan = plan;
            // `enter` frees, as the sheet's primary: the keyboard on it once it can.
            const primary = root.primaryButton();
            if (primary && root.deletes > 0)
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
            steps.push(root.plan.recheck
                       ? qsTr("Delete %n skipped files from the disk, some holding downloaded pieces. Only skipped files, only inside this torrent's folder, and none another torrent also uses.", "", root.deletes)
                       : qsTr("Delete %n skipped files from the disk: those that hold no downloaded piece. Only skipped files, only inside this torrent's folder, and none another torrent also uses.", "", root.deletes));
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

    // C17 D3: a hybrid's files with downloaded pieces stay; adding it again frees them.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space(4)
        visible: (root.plan.leftWithPieces || 0) > 0

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            dim: true
            objectName: "freeSpaceHybridNote"
            size: "small"
            text: qsTr("%n files stay (%1) because this torrent is hybrid and pieces of them were downloaded: deleting them safely needs a recheck, and a recheck there hits a libtorrent bug.", "",
                       root.plan.leftWithPieces || 0).arg(root.plan.leftText || "")
            wrapMode: Text.Wrap
        }

        Button {
            objectName: "freeSpaceAddAgain"
            visible: App.features.addAgain !== false // qBittorrent's (C14 E4)
            text: qsTr("Free them too by adding the torrent again…")

            onClicked: {
                root.close();
                root.addAgainRequested(root.hash, root.plan.name || "",
                                       (root.plan.leftWithPieces || 0) + root.deletes);
            }
        }
    }
}
