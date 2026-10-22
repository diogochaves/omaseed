pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit
import Omaseed.Views

// `--demo firstrun` host (T4.6, spec v2 §5 Fig. 15). `FirstRunPage` normally runs the real
// `ServerDetector` against 127.0.0.1:8080 on load; for a deterministic screenshot this host
// turns that off (`autoDetect: false`) and hands the page a fake "found a Docker container"
// candidate instead. `OMASEED_DEMO_STEP=1|2|3|install` jumps straight to that step, the way
// `OMASEED_DEMO_STATE` does for `DemoStates.qml`:
//   1        (default) step 1, choose, with the fake local candidate: the install row is
//            the dimmed, captioned third one (Q20); `SHOT_KEYS="j j Return"` opens the
//            install page with its port-8080 warning from here.
//   2        step 2, the API-key page, as if "connect to a remote server" was chosen.
//   3        step 3, the magnet-handler prompt.
//   install  step 1 with no local candidate found, so the install row is at full weight,
//            then jumps straight to the install page (step 2's other body, no warning).
Window {
    id: root

    readonly property string _step: {
        const value = DemoEnv.value("OMASEED_DEMO_STEP");
        return value.length > 0 ? value : "1";
    }

    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.windowAlpha)
    height: Theme.space(760)
    title: qsTr("omaseed — first run demo")
    visible: true
    width: Theme.space(920)

    FirstRunPage {
        id: page

        anchors.fill: parent
        autoDetect: false
        candidates: root._step === "install" ? [] : [
            {
                "kind": "docker",
                "url": "http://127.0.0.1:8080",
                "label": qsTr("Use qBittorrent on this machine"),
                "detail": "127.0.0.1:8080 · Docker container qbittorrent"
            }
        ]

        Component.onCompleted: {
            if (root._step === "2") {
                page.openRow("remote", "");
                page.editUrl = "http://127.0.0.1:8081";
                page.editName = "playground";
            } else if (root._step === "3") {
                page.openRow("remote", "http://127.0.0.1:8081");
                page.testOk = true;
                page.tested = true;
                page.testResultText = qsTr("qBittorrent 5.2.3 · API 2.15.1 · 142 torrents");
                page.page = "handler";
                page.currentHandlerText = qsTr("chromium.desktop");
            } else if (root._step === "install") {
                page.page = "install";
            }
        }
    }
}
