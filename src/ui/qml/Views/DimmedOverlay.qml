import QtQuick

// The read-only guard Main.qml places over the table when disconnected (spec v2 §7 Fig. 23:
// the last list stays, dimmed — Main applies the 0.45 opacity to the table itself, so this is
// only the interaction blocker on top of it). Transparent, eats every mouse click, and
// swallows every key except the ones the figure's footer hints still promise (`R` retry, `,`
// settings, `?` help, `q` hide to tray, `ctrl+q` quit) and the command palette's (`:`,
// `ctrl+shift+p`, docs/30 P6), which it forwards as signals instead of letting them reach the
// dimmed table underneath. docs/33 D4: `ctrl+s` goes through too (switching away is what the
// guard should let pass), and D23: any other key it stops says so (`blocked`), so the
// footer can tell why nothing happened.
Item {
    id: root

    /// `:` or `ctrl+shift+p`: the command palette, which shows the rest dim (docs/30 P6).
    signal commandsRequested
    signal helpRequested
    signal hideRequested
    signal quitRequested
    signal retryRequested
    signal settingsRequested
    /// `ctrl+s`: the server palette (docs/33 D2, D4).
    signal serversRequested
    /// A key it stopped, as typed ("p", "d"); the footer says it waits (docs/33 D23).
    signal blocked(string key)

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
        } else if (event.key === Qt.Key_Colon || (event.key === Qt.Key_P
                                                   && (event.modifiers & Qt.ControlModifier)
                                                   && (event.modifiers & Qt.ShiftModifier))) {
            root.commandsRequested();
        } else if (event.key === Qt.Key_Q && (event.modifiers & Qt.ControlModifier)) {
            root.quitRequested();
        } else if (event.key === Qt.Key_Q) {
            root.hideRequested();
        } else if (event.key === Qt.Key_S && (event.modifiers & Qt.ControlModifier)) {
            root.serversRequested();
        } else if (event.text.length > 0 && event.text.trim().length > 0
                   && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
            root.blocked(event.text);
        }
        event.accepted = true;
    }
}
