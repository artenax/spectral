import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

import Spectral.Component 2.0

Popup {
    property var conn

    property alias userID: usernameField.text
    property bool busy: false
    property bool completed: false

    anchors.centerIn: parent

    id: root

    padding: 0
    closePolicy: spectralController.accountCount == 0 ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

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

                id: usernameField

                enabled: !busy
                placeholderText: "Username"

                onAccepted: next()
            }

            Button {
                highlighted: true
                enabled: !busy
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
        if (userID.startsWith("@") && userID.includes(":")) {
            console.log("Probably a complete userID")

            busy = true
            conn.resolveServer(userID)
        } else {
            completed = true
            loginHomeserverDialog.createObject(ApplicationWindow.overlay, {"conn": conn, "userID": userID}).open()
            root.close()
        }
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

        errorControl.show("Failed to detect homeserver, falling back to manual config: " + msg, 3000)

        completed = true
        loginHomeserverDialog.createObject(ApplicationWindow.overlay, {"conn": conn, "userID": userID}).open()
        root.close()
    }

    Component.onCompleted: {
        conn.loginFlowsChanged.connect(handleLoginFlowsChanged)

        conn.resolveError.connect(handleResolveError)
    }

    onClosed: {
        if (!completed) {
            console.log("Deleting useless connection")
            conn.destroy()
        } else {
            conn.loginFlowsChanged.disconnect(handleLoginFlowsChanged)
            conn.resolveError.disconnect(handleResolveError)
        }

        destroy()
    }
}
