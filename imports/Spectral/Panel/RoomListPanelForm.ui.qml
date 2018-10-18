import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtGraphicalEffects 1.0
import QtQuick.Controls.Material 2.2
import QtQml.Models 2.3

import Spectral.Component 2.0
import Spectral.Menu 2.0
import Spectral.Effect 2.0

import Spectral 0.1
import Spectral.Setting 0.1
import SortFilterProxyModel 0.2

import "qrc:/js/util.js" as Util

Rectangle {
    property var listModel
    property int filter: 0
    property var enteredRoom: null

    property alias searchField: searchField
    property alias model: listView.model

    color: MSettings.darkTheme ? "#323232" : "#f3f3f3"

    Label {
        text: MSettings.miniMode ? "Empty" : "Here? No, not here."
        anchors.centerIn: parent
        visible: listView.count === 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AutoTextField {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Layout.margins: 12

            id: searchField

            leftPadding: MSettings.miniMode ? 4 : 32
            topPadding: 0
            bottomPadding: 0
            placeholderText: "Search..."

            background: Rectangle {
                color: MSettings.darkTheme ? "#303030" : "#fafafa"
                layer.enabled: true
                layer.effect: ElevationEffect {
                    elevation: searchField.focus ? 2 : 1
                }
            }
        }

        AutoListView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            id: listView

            spacing: 1
            clip: true

            boundsBehavior: Flickable.DragOverBounds

            ScrollBar.vertical: ScrollBar {}

            delegate: RoomListDelegate {
                width: parent.width
                height: 64
            }

            section.property: "display"
            section.criteria: ViewSection.FullString
            section.delegate: Label {
                width: parent.width
                height: 24

                text: section
                color: "grey"
                leftPadding: MSettings.miniMode ? undefined : 16
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: MSettings.miniMode ? Text.AlignHCenter : undefined
            }

            RoomContextMenu { id: roomContextMenu }
        }
    }
}
