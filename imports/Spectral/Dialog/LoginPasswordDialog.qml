import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

import Spectral.Component 2.0

Popup {
    property var conn
    property string userID

    property alias deviceName: deviceNameField.text
    property alias password: passwordField.text
    property bool busy: false
    property bool completed: false

    anchors.centerIn: parent

    id: root

    padding: 0

    contentItem: Control {
        padding: 16

        contentItem: RowLayout {
            AutoTextField {
                Layout.preferredWidth: 240

                id: deviceNameField

                placeholderText: "Device Name (Optional)"
            }

            AutoTextField {
                Layout.preferredWidth: 240

                id: passwordField

                placeholderText: "Password"
                echoMode: TextInput.Password

                onAccepted: login()
            }

            Button {
                highlighted: true
                text: "Login"

                onClicked: login()
            }
        }

        ProgressBar {
            anchors.top: parent.top
            width: parent.width

            visible: busy
            indeterminate: true
        }
    }

    function login() {
        busy = true

        if (deviceName == "") {
            deviceName = spectralController.generateDeviceName()
        }

        conn.loginWithPassword(userID, password, deviceName)
    }

    function handleConnected() {
        console.log("Login succeeded")
        busy = false
        spectralController.finishLogin(conn, deviceName)
        completed = true
        root.close()
    }

    function handleLoginError(message, details) {
        console.log("Login failed: " + message + " " + details)
        busy = false

        errorControl.show("Login failed: " + message + " " + details, 3000)
    }

    Component.onCompleted: {
        conn.connected.connect(handleConnected)
        conn.loginError.connect(handleLoginError)
    }

    onClosed: {
        if (!completed) {
            loginDialog.createObject(ApplicationWindow.overlay, {"conn": conn, "userID": userID}).open()
        }

        conn.connected.disconnect(handleConnected)
        conn.loginError.disconnect(handleLoginError)

        destroy()
    }
}
