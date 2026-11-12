pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The teaching empty state (spec v2 §7 Fig. 22): a centred column with the headline, the
// `Drop a file here, or click a magnet link anywhere.` note, and the ways in as word buttons,
// one under another, each as wide as its words. Keys are taught on demand only
// (docs/DECISIONS.md C24 H10, board 46's law 4, as #170 did for the sheets' feet): Fig. 22's
// key column and its `Press a` chip are not drawn; each button's key is its tooltip ("add a
// torrent · a") and, while alt is held (`keysShown`), its badge. Every button emits its own
// signal; the caller (MainView) owns what happens next: the add dialog, settings, or the `?`
// sheet.
//
// docs/33 D6 (DECISIONS C12): with two servers or more the headline names the server ("No
// torrents on public yet") and a button "switch server · private has 4" (ctrl+s) leads to the
// busiest other one; with one server, Fig. 22 as built. D7: a server lost before it gave a
// list ("noList") or that refused the sign-in ("refused") shows the same place with a centred
// line saying so, and no buttons.
Item {
    id: root

    /// The server the app is on.
    property string serverName: ""
    /// Two servers or more (D6).
    property bool multiServer: false
    /// The busiest other server, for the switch button; "" when none has a count yet.
    property string otherName: ""
    property int otherCount: -1
    /// "empty" (Fig. 22), "noList" (O3), "refused" (O4) or "engine" (DECISIONS C28: a server
    /// of another engine, in `problem`'s words).
    property string variant: "empty"
    /// `App.serverProblem(…)`, read for "engine": {headline, lineBase, …}.
    property var problem: ({})
    /// The window's `keysShown`: alt is held, so each button shows its key's badge (C24 H10).
    property bool keysShown: false

    signal addRequested
    signal helpRequested
    signal settingsRequested
    /// The switch server button (ctrl+s): the server palette (D2).
    signal switchRequested

    readonly property string headline: {
        if (root.variant === "noList")
            return qsTr("Nothing from %1 yet").arg(root.serverName);
        if (root.variant === "refused")
            return qsTr("%1 refused the sign-in").arg(root.serverName);
        if (root.variant === "engine")
            return root.problem.headline || "";
        return root.multiServer && root.serverName.length > 0 ? qsTr("No torrents on %1 yet").arg(root.serverName)
                                                              : qsTr("No torrents yet");
    }
    /// The centred line of O3 and O4; "" for Fig. 22, whose own line is `emptyTeach`.
    readonly property string note: root.variant === "noList" ? qsTr("omaseed hasn't reached it since it started. It tries again on its own.") : root.variant === "refused" ? qsTr("Fix the login in Settings › Servers; omaseed won't keep trying a password it knows is wrong.") : root.variant === "engine" ? (root.problem.lineBase || "") : ""

    implicitHeight: Theme.space(280)
    implicitWidth: Theme.space(400)

    readonly property var _rows: {
        if (root.variant !== "empty")
            return [];
        const rows = [
                  {
                      "key": "a",
                      "label": qsTr("add a torrent"),
                      "signal": "add"
                  }];
        if (root.multiServer)
            rows.push({
                          "key": "ctrl+s",
                          "label": root.otherName.length > 0 && root.otherCount >= 0 ? qsTr("switch server · %1 has %2").arg(root.otherName).arg(root.otherCount) : qsTr("switch server"),
                          "signal": "switch"
                      });
        rows.push({
                      "key": ",",
                      "label": qsTr("settings and servers"),
                      "signal": "settings"
                  }, {
                      "key": "?",
                      "label": qsTr("every shortcut"),
                      "signal": "help"
                  });
        return rows;
    }

    /// What a click on `_rows[index]` does — exposed so `tst_States.qml` can exercise the
    /// button's outcome directly (`mouseClick()` never delivers under the offscreen
    /// qmltestrunner, the same limitation T3.2's Rail report found for RailRow).
    function triggerRow(index) {
        const kind = root._rows[index].signal;
        if (kind === "add")
            root.addRequested();
        else if (kind === "switch")
            root.switchRequested();
        else if (kind === "settings")
            root.settingsRequested();
        else
            root.helpRequested();
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.space(10)

        // #79: the headline is the place's heading, the lines under it text; a plain Text
        // publishes nothing to a screen reader otherwise.
        Text {
            Accessible.name: text
            Accessible.role: Accessible.Heading
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.subtitle
            font.weight: Font.DemiBold
            objectName: "emptyHeadline"
            text: root.headline
        }

        Text {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            horizontalAlignment: Text.AlignHCenter
            objectName: "emptyNote"
            text: root.note
            visible: root.note.length > 0
            width: Math.min(implicitWidth, root.width - Theme.space(32))
            wrapMode: Text.Wrap
        }

        // Fig. 22's line without its `Press a` (docs/DECISIONS.md C24 H10): the add button
        // under it carries the key.
        Text {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            horizontalAlignment: Text.AlignHCenter
            objectName: "emptyTeach"
            text: qsTr("Drop a file here, or click a magnet link anywhere.")
            visible: root.variant === "empty"
            width: Math.min(implicitWidth, root.width - Theme.space(32))
            wrapMode: Text.Wrap
        }

        Column {
            id: rowsColumn

            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space(6)
            topPadding: Theme.space(6)

            Repeater {
                model: root._rows

                Button {
                    id: row

                    required property int index
                    required property var modelData

                    // #77: each way in is a button named by what it does (Button's name is its
                    // words), its key the description; its press is the click.
                    Accessible.description: qsTr("shortcut %1").arg(row.modelData.key)
                    objectName: "emptyRow_" + row.index
                    // Centred in the column; not an anchor, which a delegate the Repeater
                    // drops reads on a null parent.
                    x: Math.round((rowsColumn.width - row.width) / 2)
                    text: row.modelData.label
                    // C24 H10: the key on demand, in the tooltip as on the action bar's words.
                    tooltip: qsTr("%1 · %2").arg(row.modelData.label).arg(row.modelData.key)

                    onClicked: root.triggerRow(row.index)

                    KeyBadge {
                        key: row.modelData.key
                        objectName: "emptyKey_" + row.index
                        shown: root.keysShown
                    }
                }
            }
        }
    }
}
