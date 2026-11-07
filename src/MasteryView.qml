import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The gradebook: students down the side, objectives taught so far across the
// top, each cell a level in words. Click to raise a level, right-click to
// lower it; every change is a new line in mastery.csv, so nothing is lost.
Item {
    id: view
    readonly property var taught: backend.objectives.filter(o => o.taught)
    readonly property int nameWidth: 150
    readonly property int cellWidth: 118
    readonly property int cellHeight: 40

    Label {
        anchors.centerIn: parent
        visible: view.taught.length === 0 || backend.students.length === 0
        textFormat: Text.PlainText
        text: backend.students.length === 0 ? qsTr("No students in class.toml yet.")
                                            : qsTr("Nothing taught yet. Objectives appear here from the week they're taught.")
        color: win.dim
    }

    Flickable {
        id: grid
        anchors.fill: parent
        visible: view.taught.length > 0 && backend.students.length > 0
        clip: true
        contentWidth: view.nameWidth + view.taught.length * view.cellWidth
        contentHeight: header.height + backend.students.length * view.cellHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {}
        ScrollBar.vertical: ScrollBar {}

        // Objective headings: the core idea, then the objective itself.
        Row {
            id: header
            x: view.nameWidth
            height: 72
            Repeater {
                model: view.taught
                delegate: Item {
                    required property var modelData
                    width: view.cellWidth
                    height: header.height
                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        Label {
                            width: parent.width
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: modelData.idea + " · " + modelData.weekLabel
                            color: modelData.thisWeek ? theme.accent : win.dim
                            font.pixelSize: 11
                        }
                        Label {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: modelData.title
                            color: win.ink
                            font.pixelSize: 12
                        }
                    }
                    ToolTip.visible: headerHover.hovered
                    ToolTip.text: modelData.title
                    HoverHandler { id: headerHover }
                }
            }
        }

        Column {
            y: header.height
            Repeater {
                model: backend.students
                delegate: Row {
                    id: studentRow
                    required property string modelData
                    required property int index
                    height: view.cellHeight

                    Label {
                        width: view.nameWidth
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 4
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: studentRow.modelData
                        color: win.ink
                    }

                    Repeater {
                        model: view.taught
                        delegate: Item {
                            id: cell
                            required property var modelData
                            readonly property string level: win.levelOf(studentRow.modelData, modelData.id)
                            width: view.cellWidth
                            height: view.cellHeight

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 3
                                radius: Math.min(4, theme.radius)
                                color: win.levelFill(cell.level)
                                border.color: cellMouse.containsMouse ? theme.accent : (cell.level === "" || cell.level === "Not yet" ? win.line : "transparent")
                                Label {
                                    anchors.centerIn: parent
                                    textFormat: Text.PlainText
                                    text: cell.level === "" ? "·" : qsTr(cell.level)
                                    color: cell.level === "" ? win.dim : win.levelInk(cell.level)
                                    font.pixelSize: 12
                                }
                            }
                            MouseArea {
                                id: cellMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: Qt.PointingHandCursor
                                onClicked: event => backend.cycleLevel(studentRow.modelData, cell.modelData.id,
                                                                       event.button === Qt.RightButton ? -1 : 1)
                            }
                        }
                    }
                }
            }
        }
    }
}
