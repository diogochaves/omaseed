import QtQuick

// The read-only guard Main.qml places over the table when disconnected (spec v2 §7 Fig. 23:
// the last list stays, dimmed — Main applies the 0.45 opacity to the table itself, so this is
// only the interaction blocker on top of it). Transparent, eats every mouse click, and
// swallows every key except the ones the figure's footer hints still promise (`R` retry, `,`
// settings, `?` help, `q` hide to tray, `ctrl+q` quit), which it forwards as signals instead
// of letting them reach the dimmed table underneath.
Item {
    id: root

    signal helpRequested
    signal hideRequested
    signal quitRequested
    signal retryRequested
    signal settingsRequested

    anchors.fill: parent

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_R && !(event.modifiers & Qt.ControlModifier)) {
            root.retryRequested();
        } else if (event.key === Qt.Key_Comma) {
            root.settingsRequested();
        } else if (event.key === Qt.Key_Question) {
            root.helpRequested();
        } else if (event.key === Qt.Key_Q && (event.modifiers & Qt.ControlModifier)) {
            root.quitRequested();
        } else if (event.key === Qt.Key_Q) {
            root.hideRequested();
        }
        event.accepted = true;
    }
}
