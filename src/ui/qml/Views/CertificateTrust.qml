import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// Trust on first use (issue #2, docs/DECISIONS.md Q27): what the server sheet and first run's
// sign-in step show when a test met a certificate the system does not trust. A warning line,
// one sentence on what trusting means, the certificate's SHA-256 in the colon form
// certificate tools print (two lines of 16 pairs, so the halves line up for comparing), and
// `trust this certificate`. When another certificate was trusted for the server before, the
// line turns into the error "the certificate changed" and the button stops being primary:
// that is the case where someone may be in the way.
//
// The caller owns what trusting does (save the pin, test again); this only asks for it.
ColumnLayout {
    id: root

    /// The certificate the server showed (CertificatePin form: 64 hex digits).
    property string sha256: ""
    /// The certificate trusted for this server so far; empty for none.
    property string pinned: ""
    readonly property bool changed: root.pinned.length > 0 && root.pinned !== root.sha256
    readonly property alias trustButton: trustButton
    /// False where the page cycles `tab` by hand (first run): the button leaves Qt's chain.
    property bool buttonInTabChain: true

    signal trustRequested

    readonly property var _pairs: App.certificateText(root.sha256).split(":")

    spacing: Theme.space(8)

    Banner {
        Layout.fillWidth: true
        kind: root.changed ? "error" : "warning"
        text: root.changed ? qsTr("the certificate changed") : qsTr("untrusted certificate")
    }

    Label {
        Layout.fillWidth: true
        dim: true
        size: "small"
        text: root.changed ? qsTr("This server now shows a different certificate from the one you trusted. "
                                  + "Trust it only if you changed the certificate yourself.") :
                             qsTr("The system does not trust this server's certificate, as with a "
                                  + "self-signed one. If the fingerprint matches the server's, trust it: "
                                  + "omaseed then accepts this certificate only.")
        wrapMode: Text.WordWrap
    }

    Rectangle {
        Layout.fillWidth: true
        border.color: Theme.fill(Theme.hairlineAlpha)
        border.width: 1
        color: Theme.fill(Theme.fillIdle)
        implicitHeight: fingerprintColumn.implicitHeight + Theme.space(16)
        radius: Theme.cornerRadius

        Column {
            id: fingerprintColumn

            anchors.left: parent.left
            anchors.leftMargin: Theme.space(10)
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(10)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space(2)

            Label {
                text: qsTr("SHA-256")
                tracked: true
            }

            Label {
                objectName: "fingerprintFirstHalf"
                size: "small"
                text: root._pairs.slice(0, 16).join(":")
            }

            Label {
                objectName: "fingerprintSecondHalf"
                size: "small"
                text: root._pairs.slice(16).join(":")
            }
        }
    }

    Button {
        id: trustButton

        activeFocusOnTab: root.buttonInTabChain
        bordered: root.changed
        primary: !root.changed
        text: root.changed ? qsTr("trust the new certificate") : qsTr("trust this certificate")

        onClicked: root.trustRequested()
    }
}
