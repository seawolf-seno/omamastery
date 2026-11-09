import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The gradebook: students down the side, objectives taught so far across the
// top, each cell a level in words. Click to raise a level, right-click to
// lower it; arrow keys and N B D P E work too. Click an objective's heading to
// fill its empty cells at once. Every change is a new line in mastery.csv, so
// nothing is lost, and hovering a cell shows where its level came from.
Item {
    id: view
    objectName: "masteryView"
    readonly property var taught: backend.objectives.filter(o => o.taught)
    readonly property int nameWidth: 150
    readonly property int cellWidth: 118
    readonly property int cellHeight: 40
    // The cell the keyboard works on; -1 when none.
    property int selRow: -1
    property int selCol: -1
    readonly property bool hasSelection: selRow >= 0 && selCol >= 0
    readonly property var levelKeys: ({ [Qt.Key_N]: "Not yet", [Qt.Key_B]: "Beginning", [Qt.Key_D]: "Developing",
                                        [Qt.Key_P]: "Proficient", [Qt.Key_E]: "Extending" })

    function select(row, col) {
        if (backend.students.length === 0 || view.taught.length === 0) return
        selRow = Math.max(0, Math.min(row, backend.students.length - 1))
        selCol = Math.max(0, Math.min(col, view.taught.length - 1))
        grid.forceActiveFocus()
        // Keep the selected cell on screen.
        const x = selCol * view.cellWidth
        const y = selRow * view.cellHeight
        if (x < grid.contentX) grid.contentX = x
        else if (x + view.cellWidth > grid.contentX + grid.width) grid.contentX = x + view.cellWidth - grid.width
        if (y < grid.contentY) grid.contentY = y
        else if (y + view.cellHeight > grid.contentY + grid.height) grid.contentY = y + view.cellHeight - grid.height
    }
    function isKeyForGrid(key) {
        if (!view.hasSelection) return false
        return key === Qt.Key_Left || key === Qt.Key_Right || key === Qt.Key_Up || key === Qt.Key_Down
            || key === Qt.Key_Escape || view.levelKeys[key] !== undefined
    }

    // A new class or plan: start without a selection.
    Connections {
        target: backend
        function onChanged() {
            if (view.selRow >= backend.students.length || view.selCol >= view.taught.length) {
                view.selRow = -1
                view.selCol = -1
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: view.taught.length === 0 || backend.students.length === 0
        textFormat: Text.PlainText
        text: backend.students.length === 0 ? qsTr("No students in this class yet.")
                                            : qsTr("Nothing taught yet. Objectives appear here from the week they're taught.")
        color: win.dim
    }

    // Headings stay at the top and names at the left while the cells scroll.
    Item {
        id: headings
        x: view.nameWidth
        width: parent.width - view.nameWidth
        height: 72
        clip: true
        visible: grid.visible
        // Objective headings: the core idea, then the objective itself. A
        // click opens "fill empty cells with…".
        Row {
            id: header
            x: -grid.contentX
            height: 72
            Repeater {
                model: view.taught
                delegate: Item {
                    id: heading
                    required property var modelData
                    required property int index
                    width: view.cellWidth
                    height: header.height
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: Math.min(4, theme.radius)
                        color: headerHover.hovered ? win.surface : "transparent"
                        border.color: view.selCol === heading.index ? theme.accent : "transparent"
                    }
                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        Label {
                            width: parent.width
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: [heading.modelData.idea, heading.modelData.weekLabel].filter(t => t).join(" · ")
                            color: heading.modelData.thisWeek ? theme.accent : win.dim
                            font.pixelSize: 11
                        }
                        Label {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: heading.modelData.title
                            color: win.ink
                            font.pixelSize: 12
                        }
                    }
                    HoverHandler { id: headerHover; cursorShape: Qt.PointingHandCursor }
                    ToolTip.visible: headerHover.hovered && !fillMenu.visible
                    ToolTip.text: heading.modelData.title + "\n" + qsTr("Click to fill its empty cells")
                    ToolTip.delay: 400
                    TapHandler { onTapped: fillMenu.popup() }

                    Menu {
                        id: fillMenu
                        MenuItem { text: qsTr("Fill empty cells with:"); enabled: false }
                        Repeater {
                            model: backend.levelNames
                            delegate: MenuItem {
                                required property string modelData
                                text: qsTr(modelData)
                                onTriggered: backend.fillObjective(heading.modelData.id, modelData)
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        id: names
        y: headings.height
        width: view.nameWidth
        height: parent.height - headings.height
        clip: true
        visible: grid.visible
        Column {
            y: -grid.contentY
            Repeater {
                model: backend.students
                delegate: Label {
                    required property string modelData
                    required property int index
                    width: view.nameWidth
                    height: view.cellHeight
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 4
                    rightPadding: 8
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: modelData
                    color: view.selRow === index ? theme.accent : win.ink
                    font.weight: view.selRow === index ? Font.DemiBold : Font.Normal
                }
            }
        }
    }

    Flickable {
        id: grid
        objectName: "masteryGrid"
        x: view.nameWidth
        y: headings.height
        width: parent.width - view.nameWidth
        height: parent.height - headings.height
        visible: view.taught.length > 0 && backend.students.length > 0
        clip: true
        focus: true
        contentWidth: view.taught.length * view.cellWidth
        contentHeight: backend.students.length * view.cellHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {}
        ScrollBar.vertical: ScrollBar {}

        // With a cell selected, the arrows and level letters belong to the
        // grid, not to the app's class shortcuts (J/K, arrows, N).
        Keys.onShortcutOverride: event => event.accepted = view.isKeyForGrid(event.key)
        Keys.onPressed: event => {
            if (!view.hasSelection) {
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                    view.select(0, 0)
                    event.accepted = true
                }
                return
            }
            const level = view.levelKeys[event.key]
            if (level !== undefined) {
                backend.setLevel(backend.students[view.selRow], view.taught[view.selCol].id, level)
                view.select(view.selRow + 1, view.selCol) // on to the next student
            } else if (event.key === Qt.Key_Left) view.select(view.selRow, view.selCol - 1)
            else if (event.key === Qt.Key_Right) view.select(view.selRow, view.selCol + 1)
            else if (event.key === Qt.Key_Up) view.select(view.selRow - 1, view.selCol)
            else if (event.key === Qt.Key_Down) view.select(view.selRow + 1, view.selCol)
            else if (event.key === Qt.Key_Escape) { view.selRow = -1; view.selCol = -1 }
            else return
            event.accepted = true
        }

        Column {
            Repeater {
                model: backend.students
                delegate: Row {
                    id: studentRow
                    required property string modelData
                    required property int index
                    height: view.cellHeight
                    Repeater {
                        model: view.taught
                        delegate: Item {
                            id: cell
                            required property var modelData
                            required property int index
                            readonly property string level: win.levelOf(studentRow.modelData, modelData.id)
                            readonly property string note: backend.levelNotes[studentRow.modelData + "|" + modelData.id] || ""
                            readonly property bool selected: view.selRow === studentRow.index && view.selCol === cell.index
                            width: view.cellWidth
                            height: view.cellHeight

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 3
                                radius: Math.min(4, theme.radius)
                                color: win.levelFill(cell.level)
                                border.width: cell.selected ? 2 : 1
                                border.color: cell.selected || cellMouse.containsMouse ? theme.accent
                                    : (cell.level === "" || cell.level === "Not yet" ? win.line : "transparent")
                                Label {
                                    anchors.centerIn: parent
                                    textFormat: Text.PlainText
                                    text: cell.level === "" ? "·" : qsTr(cell.level)
                                    color: cell.level === "" ? win.dim : win.levelInk(cell.level)
                                    font.pixelSize: 12
                                }
                            }
                            ToolTip.visible: cellMouse.containsMouse && cell.level !== ""
                            ToolTip.text: qsTr(cell.level) + (cell.note ? " — " + cell.note : "")
                            ToolTip.delay: 400
                            MouseArea {
                                id: cellMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: Qt.PointingHandCursor
                                onClicked: event => {
                                    view.select(studentRow.index, cell.index)
                                    backend.cycleLevel(studentRow.modelData, cell.modelData.id,
                                                       event.button === Qt.RightButton ? -1 : 1)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
