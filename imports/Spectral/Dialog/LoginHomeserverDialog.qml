import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

import Spectral.Component 2.0

Popup {
    property var conn
    property string userID

    property alias homeserver: homeserverField.text
    property bool busy: false
    property bool completed: false

    anchors.centerIn: parent

    id: root

    padding: 0

    contentItem: Control {
        padding: 16

        ProgressBar {
            anchors.top: parent.top
            width: parent.width

            visible: busy
            indeterminate: true
        }

        contentItem: RowLayout {
            AutoTextField {
                Layout.preferredWidth: 240

                id: homeserverField

                enabled: !busy
                placeholderText: "Homeserver"
                text: "https://matrix.org"

                onAccepted: next()
            }

            Button {
                highlighted: true
                enabled: !busy && homeserver.startsWith("http")
                text: "Next"

                onClicked: next()
            }
        }

        ProgressBar {
            anchors.top: parent.top
            width: parent.width

            visible: busy
            indeterminate: true
        }
    }

    function next() {
        if (!homeserver.startsWith("http")) {
            return
        }

        busy = true
        conn.homeserver = homeserver
    }

    function handleLoginFlowsChanged() {
        console.log("Login flow changed")
        busy = false

        if (conn.supportsPasswordAuth) {
            console.log("Homeserver supports password login")
            completed = true
            loginPasswordDialog.createObject(ApplicationWindow.overlay, {"conn": conn, "userID": userID}).open()
            root.close()
            return
        }

        errorControl.show("Homeserver does not support password login", 3000)
    }

    function handleResolveError(msg) {
        console.log("Resolving homeserver failed: " + msg)
        busy = false

        errorControl.show("Failed to resolve homeserver: " + msg, 3000)
    }

    Component.onCompleted: {
        conn.loginFlowsChanged.connect(handleLoginFlowsChanged)
        conn.resolveError.connect(handleResolveError)
    }

    onClosed: {
        if (!completed) {
            loginDialog.createObject(ApplicationWindow.overlay, {"conn": conn, "userID": userID}).open()
        }

        conn.loginFlowsChanged.disconnect(handleLoginFlowsChanged)
        conn.resolveError.disconnect(handleResolveError)

        destroy()
    }
}
