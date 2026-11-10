import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The gradebook: students down the side, what's been taught so far across the
// top. Objectives are grouped as in Canvas (a unit or skill per group); each
// group shows one level, the lowest of its objectives, and opens to show them.
// In an objective's column, click to raise a level and right-click to lower
// it; arrow keys and N B D P E work too. Click an objective's heading to fill
// its empty cells at once. Click a student's name for their own page. Every
// change is a new line in mastery.csv, so nothing is lost, and hovering a cell
// shows where its level came from.
Item {
    id: view
    objectName: "masteryView"
    readonly property var taught: backend.objectives.filter(o => o.taught)
    readonly property bool grouped: taught.some(o => o.group)
    readonly property int nameWidth: 150
    readonly property int cellWidth: 118
    readonly property int cellHeight: 40
    // Open groups, by "class|group". Collapsed to start, so the grid fits.
    property var openGroups: ({})
    // The student whose page is open; "" shows the grid.
    property string student: ""
    property int studentClass: -1
    // The cell the keyboard works on; -1 when none.
    property int selRow: -1
    property int selCol: -1
    readonly property bool hasSelection: selRow >= 0 && selCol >= 0
    readonly property var levelKeys: ({ [Qt.Key_N]: "Not yet", [Qt.Key_B]: "Beginning", [Qt.Key_D]: "Developing",
                                        [Qt.Key_P]: "Proficient", [Qt.Key_E]: "Extending" })

    // One column per group (its objectives follow it when open), or one per
    // objective when the plan has no groups.
    readonly property var columns: {
        if (!grouped)
            return taught.map(o => ({ kind: "objective", objective: o, group: "" }))
        const order = []
        const members = {}
        for (const o of taught) {
            const key = o.group ? "g:" + o.group : "o:" + o.id
            if (!members[key]) { members[key] = []; order.push(key) }
            members[key].push(o)
        }
        const cols = []
        for (const key of order) {
            const list = members[key]
            if (key.startsWith("o:")) {
                cols.push({ kind: "objective", objective: list[0], group: "" })
                continue
            }
            const group = list[0].group
            const open = !!openGroups[backend.current.id + "|" + group]
            cols.push({ kind: "group", group: group, objectives: list, open: open })
            if (open)
                for (const o of list) cols.push({ kind: "objective", objective: o, group: group })
        }
        return cols
    }

    function objectiveCount(n) {
        return n === 1 ? qsTr("1 objective") : qsTr("%1 objectives").arg(n)
    }
    function toggleGroup(group) {
        const next = Object.assign({}, openGroups)
        const key = backend.current.id + "|" + group
        next[key] = !next[key]
        openGroups = next
    }
    function openStudent(name) {
        studentClass = backend.currentIndex
        student = name
    }
    function select(row, col) {
        if (backend.students.length === 0 || view.columns.length === 0) return
        selRow = Math.max(0, Math.min(row, backend.students.length - 1))
        selCol = Math.max(0, Math.min(col, view.columns.length - 1))
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
        if (!view.hasSelection || view.student) return false
        return key === Qt.Key_Left || key === Qt.Key_Right || key === Qt.Key_Up || key === Qt.Key_Down
            || key === Qt.Key_Escape || key === Qt.Key_Return || key === Qt.Key_Enter
            || view.levelKeys[key] !== undefined
    }
    // "Lowest of 3" and each objective's level, for a group cell's tooltip.
    function groupTip(studentName, column) {
        const lines = column.objectives.map(o => {
            const level = win.levelOf(studentName, o.id)
            return o.title + ": " + (level ? qsTr(level) : qsTr("not set"))
        })
        return qsTr("The lowest of:") + "\n" + lines.join("\n")
    }

    // A new class or plan: start without a selection or student page.
    Connections {
        target: backend
        function onChanged() {
            if (view.selRow >= backend.students.length || view.selCol >= view.columns.length) {
                view.selRow = -1
                view.selCol = -1
            }
            if (view.student && (backend.currentIndex !== view.studentClass || !backend.students.includes(view.student)))
                view.student = ""
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !view.student && (view.taught.length === 0 || backend.students.length === 0)
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
        Row {
            id: header
            x: -grid.contentX
            height: 72
            Repeater {
                model: view.columns
                delegate: Item {
                    id: heading
                    required property var modelData
                    required property int index
                    readonly property bool isGroup: modelData.kind === "group"
                    readonly property bool inGroup: !isGroup && modelData.group !== ""
                    width: view.cellWidth
                    height: header.height
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: Math.min(4, theme.radius)
                        color: headerHover.hovered || heading.isGroup ? win.surface : "transparent"
                        border.color: view.selCol === heading.index ? theme.accent : "transparent"
                    }
                    // Ties an open group's objectives to it.
                    Rectangle {
                        visible: heading.inGroup || (heading.isGroup && heading.modelData.open)
                        x: 2; width: parent.width - 4; y: 2; height: 2
                        color: theme.accent
                        opacity: 0.5
                    }
                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        Label {
                            width: parent.width
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: heading.isGroup
                                ? (heading.modelData.open ? "▾ " : "▸ ") + view.objectiveCount(heading.modelData.objectives.length)
                                : [heading.inGroup ? "" : heading.modelData.objective.idea, heading.modelData.objective.weekLabel].filter(t => t).join(" · ")
                            color: !heading.isGroup && heading.modelData.objective.thisWeek ? theme.accent : win.dim
                            font.pixelSize: 11
                        }
                        Label {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: heading.isGroup ? heading.modelData.group : heading.modelData.objective.title
                            color: win.ink
                            font.pixelSize: 12
                            font.weight: heading.isGroup ? Font.DemiBold : Font.Normal
                        }
                    }
                    HoverHandler { id: headerHover; cursorShape: Qt.PointingHandCursor }
                    ToolTip.visible: headerHover.hovered && !fillMenu.visible
                    ToolTip.text: heading.isGroup
                        ? heading.modelData.group + "\n" + (heading.modelData.open ? qsTr("Click to close") : qsTr("Click to show its objectives"))
                        : heading.modelData.objective.title + "\n" + qsTr("Click to fill its empty cells")
                    ToolTip.delay: 400
                    TapHandler {
                        onTapped: heading.isGroup ? view.toggleGroup(heading.modelData.group) : fillMenu.popup()
                    }

                    Menu {
                        id: fillMenu
                        MenuItem { text: qsTr("Fill empty cells with:"); enabled: false }
                        Repeater {
                            model: heading.isGroup ? [] : backend.levelNames
                            delegate: MenuItem {
                                required property string modelData
                                text: qsTr(modelData)
                                onTriggered: backend.fillObjective(heading.modelData.objective.id, modelData)
                            }
                        }
                    }
                }
            }
        }
    }

    // Names: click one for that student's page.
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
                    id: nameLabel
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
                    color: view.selRow === index || nameMouse.containsMouse ? theme.accent : win.ink
                    font.weight: view.selRow === index ? Font.DemiBold : Font.Normal
                    font.underline: nameMouse.containsMouse
                    ToolTip.visible: nameMouse.containsMouse
                    ToolTip.text: qsTr("Open %1's progress and next steps").arg(modelData)
                    ToolTip.delay: 600
                    MouseArea {
                        id: nameMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: view.openStudent(nameLabel.modelData)
                    }
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
        visible: !view.student && view.taught.length > 0 && backend.students.length > 0
        clip: true
        focus: true
        contentWidth: view.columns.length * view.cellWidth
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
            const column = view.columns[view.selCol]
            const level = view.levelKeys[event.key]
            if (level !== undefined) {
                if (column.kind !== "objective") return
                backend.setLevel(backend.students[view.selRow], column.objective.id, level)
                view.select(view.selRow + 1, view.selCol) // on to the next student
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (column.kind === "group") view.toggleGroup(column.group)
                else view.openStudent(backend.students[view.selRow])
            }
            else if (event.key === Qt.Key_Left) view.select(view.selRow, view.selCol - 1)
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
                        model: view.columns
                        delegate: Item {
                            id: cell
                            required property var modelData
                            required property int index
                            readonly property bool isGroup: modelData.kind === "group"
                            readonly property string level: isGroup
                                ? (backend.groupLevels[studentRow.modelData + "|" + modelData.group] || "")
                                : win.levelOf(studentRow.modelData, modelData.objective.id)
                            readonly property string note: isGroup ? ""
                                : backend.levelNotes[studentRow.modelData + "|" + modelData.objective.id] || ""
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
                                    font.weight: cell.isGroup ? Font.DemiBold : Font.Normal
                                }
                            }
                            ToolTip.visible: cellMouse.containsMouse && (cell.isGroup || cell.level !== "")
                            ToolTip.text: cell.isGroup ? view.groupTip(studentRow.modelData, cell.modelData)
                                                       : qsTr(cell.level) + (cell.note ? " — " + cell.note : "")
                            ToolTip.delay: 400
                            MouseArea {
                                id: cellMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: Qt.PointingHandCursor
                                onClicked: event => {
                                    view.select(studentRow.index, cell.index)
                                    if (cell.isGroup)
                                        view.toggleGroup(cell.modelData.group)
                                    else
                                        backend.cycleLevel(studentRow.modelData, cell.modelData.objective.id,
                                                           event.button === Qt.RightButton ? -1 : 1)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    StudentView {
        anchors.fill: parent
        visible: view.student !== ""
        student: view.student
        onBack: { view.student = ""; grid.forceActiveFocus() }
        onVisibleChanged: if (visible) forceActiveFocus()
    }
}
