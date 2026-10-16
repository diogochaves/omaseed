pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The teaching empty state (spec v2 §7 Fig. 22): a centred column with the headline, the
// `Press a, drop a file here, or click a magnet link anywhere.` note (its `a` set as a key
// chip, the same look `KeyHints` gives a key: `foreground`, tracked, `font.caption`), and a
// left-aligned three-row key/description list (`.helpsheet`: key column right-aligned dim
// bright, description column dim, `Theme.space(6)`/`Theme.space(14)` gaps — reused here as a
// vertical `Theme.space(10)` gap between rows per the task brief). Every row is clickable
// (hover fill, like a `RailRow`) and emits its own signal; the caller (Main.qml) owns what
// happens next — opening the add dialog, settings, or the `?` help sheet.
//
// docs/33 D6 (DECISIONS C12): with two servers or more the headline names the server ("No
// torrents on public yet") and a row "ctrl+s switch server · private has 4" leads to the
// busiest other one; with one server, Fig. 22 as built. D7: a server lost before it gave a
// list ("noList") or that refused the sign-in ("refused") shows the same place with a centred
// line saying so, and no rows.
Item {
    id: root

    /// The server the app is on.
    property string serverName: ""
    /// Two servers or more (D6).
    property bool multiServer: false
    /// The busiest other server, for the ctrl+s row; "" when none has a count yet.
    property string otherName: ""
    property int otherCount: -1
    /// "empty" (Fig. 22), "noList" (O3) or "refused" (O4).
    property string variant: "empty"

    signal addRequested
    signal helpRequested
    signal settingsRequested
    /// The ctrl+s row: the server palette (D2).
    signal switchRequested

    readonly property string headline: {
        if (root.variant === "noList")
            return qsTr("Nothing from %1 yet").arg(root.serverName);
        if (root.variant === "refused")
            return qsTr("%1 refused the sign-in").arg(root.serverName);
        return root.multiServer && root.serverName.length > 0 ? qsTr("No torrents on %1 yet").arg(root.serverName)
                                                              : qsTr("No torrents yet");
    }
    /// The centred line of O3 and O4; "" for Fig. 22, whose line teaches `a`.
    readonly property string note: root.variant === "noList" ? qsTr("omaseed hasn't reached it since it started. It tries again on its own.") : root.variant === "refused" ? qsTr("Fix the login in Settings › Servers; omaseed won't keep trying a password it knows is wrong.") : ""

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
    /// row's outcome directly (`mouseClick()` never delivers under the offscreen
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

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.subtitle
            font.weight: Font.DemiBold
            objectName: "emptyHeadline"
            text: root.headline
        }

        Text {
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

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space(4)
            visible: root.variant === "empty"

            Text {
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: qsTr("Press")
            }

            Text {
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.letterSpacing: Math.max(1, Math.round(Theme.font.caption * 0.08))
                font.pixelSize: Theme.font.caption
                text: "a"
            }

            Text {
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: qsTr(", drop a file here, or click a magnet link anywhere.")
            }
        }

        Column {
            id: rowsColumn

            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space(10)
            topPadding: Theme.space(6)

            Repeater {
                model: root._rows

                Rectangle {
                    id: row

                    required property int index
                    required property var modelData

                    readonly property bool hot: mouseArea.containsMouse

                    // #77: each way in is a button named by what it does, its key the
                    // description; its press is the click.
                    Accessible.description: qsTr("shortcut %1").arg(row.modelData.key)
                    Accessible.name: row.modelData.label
                    Accessible.role: Accessible.Button
                    Accessible.onPressAction: if (row.enabled)
                                                  root.triggerRow(row.index)
                    objectName: "emptyRow_" + row.index
                    color: row.hot ? Theme.fill(Theme.fillHover) : "transparent"
                    height: keyValueRow.implicitHeight + Theme.space(6)
                    width: keyValueRow.implicitWidth + Theme.space(16)

                    Row {
                        id: keyValueRow

                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.space(14)
                        x: Theme.space(8)

                        Text {
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.small
                            horizontalAlignment: Text.AlignRight
                            text: row.modelData.key
                            width: Theme.space(root.multiServer ? 44 : 20)
                        }

                        Text {
                            color: Theme.dimForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.small
                            text: row.modelData.label
                        }
                    }

                    MouseArea {
                        id: mouseArea

                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true

                        onClicked: root.triggerRow(row.index)
                    }
                }
            }
        }
    }
}
