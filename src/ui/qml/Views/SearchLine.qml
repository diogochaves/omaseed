pragma ComponentBehavior: Bound

import QtQuick
import Omaseed
import Omaseed.Kit

// The plugins' line under search's bar (board 48 `.sline`, `pills()`, DECISIONS C26 SR1–SR2):
// at the left what the search found so far and its order, at the right one pill per plugin as
// it answers (its count, "…" while asked, "failed", "still looking", "skipped: no TYPE"), then
// "1 of 3 plugins didn't answer ›", which opens each plugin's answer (SearchAnswers). Narrow,
// the pills fold into "2 of 3 answered" and the link into "1 didn't ›".
Item {
    id: root

    property var pluginRows: []
    property var results: null
    /// SearchSession's state.
    property string searchState: "idle"
    property string order: "seeds"
    property bool narrow: false

    /// The keyboard is on "didn't answer ›" (SearchBar's last tab stop).
    property bool answersHaveCursor: false

    signal answersRequested(real x, real y)

    readonly property var _asked: root.pluginRows.filter(row => row.state !== "skipped")
    readonly property int _answered: root._asked.filter(row => row.state === "answered").length
    readonly property int _missing: root._asked.filter(row => row.state === "failed" || row.state === "slow"
                                                              || row.state === "stopped").length
    readonly property string orderWords: {
        switch (root.order) {
        case "newest":
            return qsTr("newest first");
        case "size":
            return qsTr("biggest first");
        case "name":
            return qsTr("by name");
        }
        return root.narrow ? qsTr("most seeds") : qsTr("most seeds, then newest");
    }
    readonly property string leftText: {
        const rows = root.results ? root.results.torrentCount : 0;
        const folded = root.results ? root.results.foldedCount : 0;
        if (root.searchState === "asking" && rows === 0) {
            const names = root._asked.map(row => row.fullName);
            if (root.narrow || names.length === 0)
                return qsTr("asking…");
            const list = names.length === 1 ? names[0] : qsTr("%1 and %2").arg(names.slice(0,
                                                                                           -1).join(", ")).arg(
                                                  names[names.length - 1]);
            return qsTr("asking %1…").arg(list);
        }
        if (root.searchState === "asking") {
            const parts = [qsTr("%1 so far").arg(rows)];
            if (folded > 0 && !root.narrow)
                parts.push(folded === 1 ? qsTr("1 copy folded") : qsTr("%1 copies folded").arg(
                                              folded));
            if (!root.narrow)
                parts.push(root.orderWords);
            return parts.join(" · ");
        }
        if (rows === 0)
            return qsTr("0 results");
        const torrents = rows === 1 ? qsTr("1 torrent") : qsTr("%1 torrents").arg(rows);
        if (root.narrow)
            return qsTr("%1 · %2").arg(torrents).arg(root.orderWords);
        const all = rows + folded;
        return qsTr("%1 from %2 · %3").arg(torrents).arg(all === 1 ? qsTr("1 result") : qsTr(
                                                                         "%1 results").arg(all)).arg(
                    root.orderWords);
    }

    function pillText(row: var): string {
        switch (row.state) {
        case "answered":
            return qsTr("%1 %2").arg(row.fullName).arg(row.count);
        case "failed":
            return qsTr("%1 failed").arg(row.fullName);
        case "slow":
            return qsTr("%1 still looking").arg(row.fullName);
        case "stopped":
            return qsTr("%1 stopped").arg(row.fullName);
        case "skipped":
            return qsTr("%1 skipped: not this type").arg(row.fullName);
        case "queued":
            return qsTr("%1 waits").arg(row.fullName);
        }
        return qsTr("%1 …").arg(row.fullName);
    }

    function pillColor(row: var): color {
        switch (row.state) {
        case "answered":
            return Theme.green;
        case "failed":
        case "slow":
            return Theme.yellow;
        case "asking":
            return Theme.accent;
        }
        return Theme.dimForeground;
    }

    implicitHeight: flow.implicitHeight + Theme.searchLineInsetBottom
    height: implicitHeight

    Text {
        id: left

        anchors.left: parent.left
        anchors.leftMargin: Theme.searchBarInset
        anchors.top: parent.top
        color: Theme.dimForeground
        elide: Text.ElideRight
        font.family: Theme.fontFamily
        font.pixelSize: Theme.searchLineSize
        objectName: "searchLineLeft"
        text: root.leftText
        textFormat: Text.PlainText
        width: Math.min(implicitWidth, parent.width - 2 * Theme.searchBarInset - flow.implicitWidth
                        - Theme.searchLineGap)
    }

    Row {
        id: flow

        anchors.right: parent.right
        anchors.rightMargin: Theme.searchBarInset
        anchors.top: parent.top
        spacing: Theme.searchLineGap

        Repeater {
            model: root.narrow ? [] : root.pluginRows

            delegate: Row {
                id: pill

                required property var modelData

                Accessible.name: root.pillText(pill.modelData)
                Accessible.role: Accessible.StaticText
                objectName: "searchPill_" + pill.modelData.name
                spacing: Theme.searchPillGap

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.pillColor(pill.modelData)
                    height: Theme.searchPillDot
                    radius: Theme.searchPillDot / 2
                    width: Theme.searchPillDot

                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        running: (pill.modelData.state === "asking" || pill.modelData.state
                                  === "slow") && !App.reducedMotion

                        NumberAnimation {
                            duration: Theme.searchPulseDuration / 2
                            to: Theme.searchPulseLow
                        }

                        NumberAnimation {
                            duration: Theme.searchPulseDuration / 2
                            to: 1
                        }
                    }
                }

                Text {
                    color: pill.modelData.state === "slow" || pill.modelData.state === "failed"
                           ? Theme.yellow : pill.modelData.state === "answered" ? Theme.foreground :
                                                                                  Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.searchLineSize
                    text: root.pillText(pill.modelData)
                    textFormat: Text.PlainText
                }
            }
        }

        // Narrow: one pill for all of them.
        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.searchLineSize
            objectName: "searchPillsFolded"
            text: qsTr("%1 of %2 answered").arg(root._answered).arg(root._asked.length)
            visible: root.narrow && root._asked.length > 0
        }

        // SR2: who didn't answer, opened plugin by plugin.
        Text {
            id: missingLink

            Accessible.name: missingLink.text
            Accessible.role: Accessible.Button
            Accessible.onPressAction: root.answersRequested(missingLink.x, missingLink.y
                                                            + missingLink.height)
            color: Theme.yellow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.searchLineSize
            font.underline: true

            FocusRing {
                shown: root.answersHaveCursor
            }
            objectName: "searchDidntAnswer"
            text: root.narrow ? qsTr("%1 didn't ›").arg(root._missing) : qsTr(
                                    "%1 of %2 plugins didn't answer ›").arg(root._missing).arg(
                                    root._asked.length)
            visible: root._missing > 0 && root.searchState !== "asking"

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor

                onClicked: {
                    const point = missingLink.mapToItem(root, 0, missingLink.height);
                    root.answersRequested(point.x, point.y);
                }
            }
        }
    }
}
