pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// A search result on the right (board 48 `resultAside()`, DECISIONS C26 SR3, SR9, SR11), in the
// details' place: the name and who found it, the fact (its seeds) with one sentence, then the
// copies folded into it and the trackers its magnet carries (or, for a link to a .torrent, why
// it has no hash yet), its facts as the plugin gave them, and add… · copy magnet · open its
// page. Nothing a plugin didn't send is shown; the page opens only on a click or `o`.
Rectangle {
    id: root

    /// SearchModel::details(key): the row's roles plus `copyList`, `trackers`, `hashV1`,
    /// `hashV2`, `published`.
    property var details: ({})
    /// Moves when the rows do, so the pane reads its row again.
    property int revision: 0
    property string serverName: ""

    signal addClicked
    signal copyClicked
    signal pageClicked
    signal closeClicked

    readonly property bool isLink: root.details.isLink === true
    readonly property var copies: root.details.copyList ?? []
    readonly property var trackers: root.details.trackers ?? []
    readonly property string _hash: root.details.hashV1 && root.details.hashV1.length > 0
                                    ? root.details.hashV1 : (root.details.hashV2 ?? "")
    readonly property string heroText: {
        if (root.isLink)
            return qsTr("A .torrent link from %1. Its hash is known only once %2 fetches it.").arg(
                        root.details.plugins ?? "").arg(root.serverName.length > 0 ? root.serverName :
                                                                                     qsTr("the server"));
        if (root.copies.length > 1)
            return qsTr(
                        "%1 copies of one torrent, from %2. The magnet carries all %3 of their trackers.").arg(
                        root.copies.length).arg(root.details.plugins ?? "").arg(
                        root.trackers.length);
        if ((root.details.privateHost ?? "").length > 0)
            return qsTr(
                        "On a tracker your library knows as private: kept as its own row, never given another copy's trackers.");
        return qsTr("From %1, with %2 in its magnet.").arg(root.details.plugins ?? "").arg(
                    root.trackers.length === 1 ? qsTr("1 tracker") : qsTr("%1 trackers").arg(
                                                     root.trackers.length));
    }
    readonly property color factColor: {
        switch (root.details.health) {
        case "good":
            return Theme.stateSeeding;
        case "few":
            return Theme.yellow;
        }
        return Theme.dimForeground;
    }

    /// The facts as the plugin gave them, label and value in turn.
    function facts(): var {
        const said = qsTr("as %1 says").arg(root.details.plugins ?? "");
        const size = root.details.sizeText ?? "";
        const published = root.details.published ?? "";
        const leechers = root.details.leechers ?? -1;
        const list = [qsTr("size"), size.length > 0 ? qsTr("%1, %2").arg(size).arg(said) : qsTr(
                                                          "not given"), qsTr("published"),
                      published.length > 0 ? qsTr("%1, %2").arg(published).arg(said) : qsTr(
                                                 "no date"), qsTr("leechers"), leechers < 0 ? qsTr(
                                                                                                  "not reported") :
                                                                                              String(leechers)];
        if (root._hash.length > 0)
            list.push(qsTr("info hash"), qsTr("%1…%2").arg(root._hash.slice(0, 8)).arg(root._hash.slice(
                                                                                           -4)));
        list.push(qsTr("files"), qsTr("the list comes at add"));
        return list;
    }

    Accessible.name: qsTr("result")
    color: Theme.background
    objectName: "searchResultPane"

    Hairline {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        vertical: true
    }

    Flickable {
        id: flick

        anchors.fill: parent
        anchors.leftMargin: 1
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        contentHeight: column.implicitHeight + 2 * Theme.searchPaneInset

        ColumnLayout {
            id: column

            spacing: Theme.searchPaneGap
            width: flick.width - 2 * Theme.searchPaneInset
            x: Theme.searchPaneInset
            y: Theme.searchPaneInset

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.searchPaneGap

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.libraryRowLineGap

                    Text {
                        Layout.fillWidth: true
                        color: Theme.brightForeground
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.libraryRowNameSize
                        font.weight: Theme.libraryRowNameWeight
                        objectName: "searchPaneName"
                        text: root.details.name ?? ""
                        textFormat: Text.PlainText
                    }

                    Text {
                        Layout.fillWidth: true
                        color: Theme.dimForeground
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.libraryRowMetaSize
                        text: [root.details.sizeText ?? "", root.details.plugins ?? ""].filter(part
                                                                                               => part.length
                                                                                                  > 0).join(
                            " · ")
                        textFormat: Text.PlainText
                    }
                }

                Button {
                    Layout.alignment: Qt.AlignTop
                    objectName: "searchPaneClose"
                    text: qsTr("esc")
                    tooltip: qsTr("close the result")

                    onClicked: root.closeClicked()
                }
            }

            Hairline {
                Layout.fillWidth: true
            }

            Row {
                spacing: Theme.searchPaneGap

                Text {
                    color: root.factColor
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.searchPaneFactSize
                    font.weight: Theme.libraryRowFactWeight
                    objectName: "searchPaneFact"
                    text: root.details.seedsText ?? ""
                    textFormat: Text.PlainText
                }

                Text {
                    anchors.baseline: parent.children[0].baseline
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.libraryRowSentenceSize
                    text: root.details.seedsCaption ?? ""
                    textFormat: Text.PlainText
                }
            }

            Text {
                Layout.fillWidth: true
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.libraryRowSentenceSize
                objectName: "searchPaneSentence"
                text: root.heroText
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }

            Hairline {
                Layout.fillWidth: true
            }

            // The copies (SR3), or a link's one line.
            Label {
                text: root.isLink ? qsTr("the link") : root.copies.length === 1 ? qsTr("one copy") :
                                                                                  qsTr("copies · %1 · one info hash").arg(
                                                                                      root.copies.length)
                tracked: true
            }

            Text {
                Layout.fillWidth: true
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.libraryRowSentenceSize
                text: qsTr(
                          "%1 gives a link to a .torrent, not a magnet, so its info hash is unknown until it is fetched. add… asks %2 to fetch it through %1, the add sheet reads it, and if it is a torrent already in your library it says so there.").arg(
                          root.details.plugins ?? "").arg(root.serverName.length > 0
                                                          ? root.serverName : qsTr("the server"))
                textFormat: Text.PlainText
                visible: root.isLink
                wrapMode: Text.Wrap
            }

            Repeater {
                model: root.isLink ? [] : root.copies

                delegate: ColumnLayout {
                    id: copy

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: 0

                    Text {
                        Layout.fillWidth: true
                        color: copy.modelData.naming ? Theme.brightForeground : Theme.foreground
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.libraryRowSentenceSize
                        text: qsTr("%1 · %2").arg(copy.modelData.plugin).arg(copy.modelData.name)
                        textFormat: Text.PlainText
                    }

                    Text {
                        Layout.fillWidth: true
                        color: Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.searchNoteSize
                        text: {
                            const trackers = copy.modelData.trackers === 1 ? qsTr("1 tracker") :
                                                                             qsTr("%1 trackers").arg(
                                                                                 copy.modelData.trackers);
                            const fresh = copy.modelData.newTrackers > 0 ? qsTr(", %1 new").arg(copy.modelData.newTrackers) :
                                                                           "";
                            const role = copy.modelData.naming ? qsTr(
                                                                     "names the row: the most seeds") :
                                                                 qsTr("folded in");
                            return qsTr("%1 · %2%3 · %4").arg(copy.modelData.seedsText).arg(
                                        trackers).arg(fresh).arg(role);
                        }
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                }
            }

            Label {
                text: qsTr("trackers in its magnet · %1").arg(root.trackers.length)
                tracked: true
                visible: !root.isLink && root.trackers.length > 0
            }

            Repeater {
                model: root.isLink ? [] : root.trackers

                delegate: Text {
                    required property var modelData

                    Layout.fillWidth: true
                    color: Theme.foreground
                    elide: Text.ElideMiddle
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.searchNoteSize
                    text: modelData.from.length > 0 ? qsTr("%1 · from %2").arg(modelData.url).arg(
                                                          modelData.from) : modelData.url
                    textFormat: Text.PlainText
                }
            }

            Text {
                Layout.fillWidth: true
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.searchNoteSize
                text: qsTr(
                          "A copy whose tracker your library knows as private is never folded and never gets another's trackers: it keeps its own row.")
                textFormat: Text.PlainText
                visible: !root.isLink
                wrapMode: Text.Wrap
            }

            Label {
                text: qsTr("facts")
                tracked: true
            }

            GridLayout {
                Layout.fillWidth: true
                columnSpacing: Theme.searchPaneGap
                columns: 2
                rowSpacing: Theme.libraryRowLineGap

                Repeater {
                    model: root.facts()

                    delegate: Text {
                        required property string modelData
                        required property int index

                        Layout.fillWidth: index % 2 === 1
                        color: index % 2 === 0 ? Theme.dimForeground : Theme.foreground
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.libraryRowSentenceSize
                        text: modelData
                        textFormat: Text.PlainText
                    }
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.searchPaneGap

                Button {
                    bordered: true
                    objectName: "searchPaneAdd"
                    primary: true
                    text: root.details.addWord ?? qsTr("add…")
                    tooltip: qsTr("the add sheet · enter")

                    onClicked: root.addClicked()
                }

                Button {
                    bordered: true
                    objectName: "searchPaneCopy"
                    text: root.isLink ? qsTr("copy link") : qsTr("copy magnet")
                    tooltip: qsTr("c")

                    onClicked: root.copyClicked()
                }

                Button {
                    bordered: true
                    objectName: "searchPanePage"
                    text: qsTr("open its page")
                    tooltip: qsTr("in the browser · o")
                    visible: (root.details.page ?? "").length > 0

                    onClicked: root.pageClicked()
                }
            }
        }
    }
}
