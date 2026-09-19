import QtQuick
import Omaseed.Kit

// The disconnected banner (spec v2 §7 Fig. 23 `.banner.err`): "✕ connection refused ·
// retrying in 7 s · retry now · settings", `retry now` and `settings` as accent-coloured
// links. Built on the Kit `Banner` in its "error" kind, whose `actions` list already renders
// inline accent links and emits `actionTriggered(key)` on click — no Kit change needed. The
// caller (Main.qml) is responsible for the figure's `Theme.space(10)` top / `Theme.space(16)`
// side margins; this component fills whatever width it is given.
Banner {
    id: root

    property string errorText: qsTr("connection refused")
    property int retryInSeconds: 0

    signal retryRequested
    signal settingsRequested

    actions: [
        {
            "label": qsTr("retry now"),
            "key": "retry"
        },
        {
            "label": qsTr("settings"),
            "key": "settings"
        }
    ]
    kind: "error"
    text: root.retryInSeconds > 0 ? qsTr("%1 · retrying in %2 s").arg(root.errorText).arg(
                                        root.retryInSeconds) : qsTr("%1 · retrying now").arg(
                                        root.errorText)

    onActionTriggered: key => {
        if (key === "retry")
            root.retryRequested();
        else if (key === "settings")
            root.settingsRequested();
    }
}
