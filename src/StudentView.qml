import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One student: what to work on next, then each group with its level and the
// objectives under it. Levels can be changed here too: click to raise,
// right-click to lower. Esc or "All students" goes back to the grid.
FocusScope {
    id: page
    objectName: "studentView"
    property string student: ""
    signal back()
    // Read again whenever a level changes.
    readonly property var report: { backend.levels; return student ? backend.studentReport(student) : ({}) }

    Keys.onEscapePressed: page.back()

    component LevelPill: Rectangle {
        id: pill
        property string level: ""
        property string objective: ""
        implicitWidth: 104
        implicitHeight: 28
        radius: Math.min(4, theme.radius)
        color: win.levelFill(level)
        border.width: 1
        border.color: pillMouse.containsMouse && objective ? theme.accent
            : (level === "" || level === "Not yet" ? win.line : "transparent")
        Label {
            anchors.centerIn: parent
            textFormat: Text.PlainText
            text: pill.level === "" ? qsTr("Not set") : qsTr(pill.level)
            color: pill.level === "" ? win.dim : win.levelInk(pill.level)
            font.pixelSize: 12
        }
        MouseArea {
            id: pillMouse
            anchors.fill: parent
            enabled: pill.objective !== ""
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: event => backend.cycleLevel(page.student, pill.objective, event.button === Qt.RightButton ? -1 : 1)
        }
    }

    // A titled list in the next-steps card.
    component StepList: ColumnLayout {
        id: steps
        property string heading: ""
        property var items: []
        property bool withTarget: false
        visible: items.length > 0
        spacing: 6
        Layout.fillWidth: true
        Label { text: steps.heading; color: win.dim; font.pixelSize: 12; textFormat: Text.PlainText }
        Repeater {
            model: steps.items
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        text: modelData.title
                        color: win.ink
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: [modelData.group, modelData.weekLabel].filter(t => t).join(" · ")
                        color: win.dim
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                }
                Label {
                    visible: steps.withTarget
                    text: steps.withTarget ? qsTr(modelData.level) + "  →  " + qsTr(modelData.target) : ""
                    color: win.dim
                    font.pixelSize: 12
                    textFormat: Text.PlainText
                }
            }
        }
    }

    Flickable {
        id: scroller
        objectName: "studentScroll"
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: body
            x: 4
            width: Math.min(scroller.width - 24, 760)
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("‹ All students")
                    flat: true
                    onClicked: page.back()
                }
                Item { Layout.fillWidth: true }
            }

            Label {
                text: page.student
                color: win.ink
                font.pixelSize: 22
                font.weight: Font.DemiBold
                textFormat: Text.PlainText
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: win.dim
                text: (page.report.taught || 0) === 0 ? qsTr("Nothing taught yet.")
                    : qsTr("%1 of %2 objectives taught so far have a level · %3 at Proficient or above")
                        .arg(page.report.assessed).arg(page.report.taught).arg(page.report.proficient)
            }

            // ---- Next steps ----
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: next.implicitHeight + 28
                radius: theme.radius
                color: win.surface
                border.color: win.line
                ColumnLayout {
                    id: next
                    x: 14; y: 14
                    width: parent.width - 28
                    spacing: 14
                    Label {
                        text: qsTr("Next steps")
                        color: win.ink
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    StepList {
                        heading: qsTr("Work on first: the lowest levels, oldest first")
                        items: page.report.focus || []
                        withTarget: true
                    }
                    StepList {
                        heading: qsTr("Everything taught is Proficient or above. A stretch:")
                        items: page.report.stretch || []
                        withTarget: true
                    }
                    StepList {
                        heading: qsTr("No level yet: check these")
                        items: page.report.unassessed || []
                    }
                    StepList {
                        heading: qsTr("Coming up")
                        items: page.report.comingUp || []
                    }
                    Label {
                        visible: (page.report.focus || []).length + (page.report.stretch || []).length
                                 + (page.report.unassessed || []).length + (page.report.comingUp || []).length === 0
                        text: qsTr("Nothing to suggest yet.")
                        color: win.dim
                    }
                }
            }

            // ---- Progress, by group ----
            Label {
                text: qsTr("Progress")
                color: win.ink
                font.pixelSize: 15
                font.weight: Font.DemiBold
                visible: (page.report.groups || []).length > 0
            }
            Repeater {
                model: page.report.groups || []
                delegate: ColumnLayout {
                    id: group
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: group.modelData.name || qsTr("Other objectives")
                            color: win.ink
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                        LevelPill {
                            level: group.modelData.level
                            ToolTip.visible: groupHover.hovered
                            ToolTip.text: qsTr("The lowest of the objectives below")
                            ToolTip.delay: 400
                            HoverHandler { id: groupHover }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: win.line }
                    Repeater {
                        model: group.modelData.objectives
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    color: win.ink
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.note || qsTr("Taught %1").arg(modelData.weekLabel)
                                    color: win.dim
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                }
                            }
                            LevelPill { level: modelData.level; objective: modelData.id }
                        }
                    }
                    Item { implicitHeight: 6 }
                }
            }
        }
    }
}
