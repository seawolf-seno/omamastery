import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The teacher's classes, in the order of the week, with the one happening
// now marked. The list comes from the folders in ~/Classes.
Rectangle {
    color: win.surface

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            text: "OmaMastery"
            font.pixelSize: 18
            font.weight: Font.DemiBold
            color: win.ink
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            text: backend.nowText
            color: win.dim
            font.pixelSize: 13
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: backend.classes

            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool selected: index === backend.currentIndex
                width: ListView.view.width
                height: Math.max(56, rowText.implicitHeight + 18)
                radius: Math.min(6, theme.radius)
                color: selected ? Qt.rgba(1, 1, 1, 0.08) : hover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: backend.selectClass(row.index) }
                // Right-click, or the ⋯ button, for archive and delete.
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: classMenu.popup() }

                Menu {
                    id: classMenu
                    MenuItem {
                        text: qsTr("Archive class…")
                        onTriggered: win.confirmClassAction("archive", row.modelData.id, row.modelData.name)
                    }
                    MenuItem {
                        text: qsTr("Delete class…")
                        onTriggered: win.confirmClassAction("delete", row.modelData.id, row.modelData.name)
                    }
                }

                ToolButton {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: 2
                    width: 28
                    height: 28
                    padding: 0
                    visible: hover.hovered || classMenu.visible
                    text: "⋯"
                    font.pixelSize: 16
                    onClicked: classMenu.popup()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Archive or delete this class")
                    ToolTip.delay: 400
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3
                    height: 28
                    radius: 2
                    color: theme.accent
                    visible: row.selected
                }

                Column {
                    id: rowText
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.right: badge.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Label {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: row.modelData.name
                        color: win.ink
                        font.weight: row.selected ? Font.DemiBold : Font.Normal
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: win.whenText(row.modelData)
                              + (row.modelData.sample ? qsTr(" · sample") : "")
                        color: win.dim
                        font.pixelSize: 12
                    }
                }

                Rectangle {
                    id: badge
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    visible: (row.modelData.isNow || row.modelData.isNext) && !hover.hovered && !classMenu.visible
                    width: badgeText.implicitWidth + 14
                    height: 22
                    radius: 11
                    color: row.modelData.isNow ? theme.accent : "transparent"
                    border.color: theme.accent
                    Label {
                        id: badgeText
                        anchors.centerIn: parent
                        text: row.modelData.isNow ? qsTr("Now") : qsTr("Next")
                        color: row.modelData.isNow ? theme.accentForeground : theme.accent
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        Button {
            Layout.fillWidth: true
            flat: true
            visible: backend.nowIndex >= 0 && backend.nowIndex !== backend.currentIndex
            text: qsTr("Go to the class now")
            onClicked: backend.goToNow()
        }

        Button {
            Layout.fillWidth: true
            flat: true
            text: qsTr("Update from Canvas")
            font.pixelSize: 12
            onClicked: win.openSetup()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("New term or new students: refresh rosters, or add classes")
        }
    }
}
