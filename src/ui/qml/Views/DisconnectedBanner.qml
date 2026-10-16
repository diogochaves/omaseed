import QtQuick
import Omaseed.Kit

// The lost server's notice (spec v2 §7 Fig. 23, in docs/33 D8/D9's words and docs/35's look):
// a band across the window under the hero, the server named in plain words (core/ErrorWords,
// D23), what omaseed does next, and at most two actions: "retry now" while it retries on its
// own (yellow, docs/35 N3), "edit login" after a refused sign-in or "review certificate" for a
// certificate (red), then "switch server" with two servers or more; the technical text behind
// "details". The caller places it (full width, no margins: docs/35 N1) and says what each
// action does.
Notice {
    id: root

    /// `App.serverProblem(lastError, retryInSeconds)`: {headline, line, lineBase, retry,
    /// retryChars, details, kind}.
    property var problem: ({})
    /// Two servers or more: "switch server" (D8).
    property bool multiServer: false
    /// The last list is on screen, from `listTime` (Fig. 23: dimmed, read-only).
    property string listTime: ""

    readonly property string problemKind: root.problem.kind || ""

    signal retryRequested
    signal switchRequested
    signal loginRequested
    signal certificateRequested

    // docs/35 N3: yellow while omaseed retries by itself, red once it needs you.
    kind: root.problemKind === "down" || root.problemKind === "" ? "warning" : "error"
    title: root.problem.headline || ""
    body: {
        const base = root.problem.lineBase || "";
        if (root.listTime.length === 0)
            return base;
        const list = qsTr("The list below is from %1.").arg(root.listTime);
        return base.length > 0 ? base + " " + list : list;
    }
    bodyTail: root.problem.retry || ""
    bodyTailChars: root.problem.retryChars || 0
    details: root.problem.details || ""
    actions: {
        const first = root.problemKind === "refused" || root.problemKind === "banned" ? {
                                                                                            "label": qsTr("edit login"),
                                                                                            "key": "login"
                                                                                        } : root.problemKind === "certificate" ? {
                                                                                                                                     "label": qsTr("review certificate"),
                                                                                                                                     "key": "certificate"
                                                                                                                                 } : {
                                                                                                                                     "label": qsTr("retry now"),
                                                                                                                                     "key": "retry"
                                                                                                                                 };
        return root.multiServer ? [first, {
                                       "label": qsTr("switch server"),
                                       "key": "switch"
                                   }] : [first];
    }

    onActionTriggered: key => {
        if (key === "retry")
            root.retryRequested();
        else if (key === "switch")
            root.switchRequested();
        else if (key === "login")
            root.loginRequested();
        else if (key === "certificate")
            root.certificateRequested();
    }
}
