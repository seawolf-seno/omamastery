import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// What matters this week: the objectives being taught, where the class is on
// each, and the planning notes the teacher and their agent keep together.
ScrollView {
    id: view
    contentWidth: availableWidth
    clip: true

    readonly property var focusObjectives: {
        const all = backend.objectives
        const now = all.filter(o => o.thisWeek)
        const recent = all.filter(o => o.taught && !o.thisWeek).slice(-1)
        const upcoming = all.filter(o => !o.taught).slice(0, now.length ? 0 : 1)
        return recent.concat(now).concat(upcoming)
    }

    function counts(objectiveId) {
        const names = backend.levelNames
        let result = names.map(() => 0)
        let unchecked = 0
        for (const student of backend.students) {
            const i = names.indexOf(win.levelOf(student, objectiveId))
            if (i < 0) unchecked++
            else result[i]++
        }
        return { levels: result, unchecked: unchecked }
    }

    ColumnLayout {
        width: view.availableWidth
        spacing: 16

        // ---- Objectives in focus ----
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: focusColumn.implicitHeight + 40
            radius: theme.radius
            color: win.surface
            border.color: win.line

            ColumnLayout {
                id: focusColumn
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                Label { text: qsTr("This week"); font.pixelSize: 18; font.weight: Font.DemiBold; color: win.ink }

                Label {
                    visible: view.focusObjectives.length === 0
                    textFormat: Text.PlainText
                    text: qsTr("No objectives planned yet. Use Plan to draft them from your long-range plan.")
                    color: win.dim
                }

                Repeater {
                    model: view.focusObjectives
                    delegate: ColumnLayout {
                        id: objectiveRow
                        required property var modelData
                        readonly property var tally: view.counts(modelData.id)
                        Layout.fillWidth: true
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                textFormat: Text.PlainText
                                text: objectiveRow.modelData.title
                                color: win.ink
                                font.pixelSize: 16
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: objectiveRow.modelData.thisWeek ? qsTr("this week")
                                      : objectiveRow.modelData.taught ? qsTr("last taught %1").arg(objectiveRow.modelData.weekLabel)
                                      : qsTr("coming %1").arg(objectiveRow.modelData.weekLabel)
                                color: objectiveRow.modelData.thisWeek ? theme.accent : win.dim
                                font.pixelSize: 12
                            }
                        }
                        Label {
                            textFormat: Text.PlainText
                            text: objectiveRow.modelData.joint
                                  ? qsTr("%1 · connects to %2").arg(objectiveRow.modelData.idea).arg(objectiveRow.modelData.joint)
                                  : objectiveRow.modelData.idea
                            color: win.dim
                            font.pixelSize: 13
                        }

                        // Where the class is: one segment per level, widest
                        // where most students are.
                        Row {
                            Layout.fillWidth: true
                            height: 26
                            spacing: 2
                            readonly property int total: Math.max(1, backend.students.length)
                            Repeater {
                                model: backend.levelNames.length + 1
                                delegate: Rectangle {
                                    required property int index
                                    readonly property bool uncheckedSlot: index === backend.levelNames.length
                                    readonly property int n: uncheckedSlot ? objectiveRow.tally.unchecked : objectiveRow.tally.levels[index]
                                    readonly property string level: uncheckedSlot ? "" : backend.levelNames[index]
                                    visible: n > 0
                                    width: Math.max(28, (parent.width - 12) * n / parent.total)
                                    height: parent.height
                                    radius: Math.min(4, theme.radius)
                                    color: uncheckedSlot ? "transparent" : win.levelFill(level)
                                    border.color: uncheckedSlot || index === 0 ? win.line : "transparent"
                                    Label {
                                        anchors.centerIn: parent
                                        text: parent.n
                                        color: parent.uncheckedSlot ? win.dim : win.levelInk(parent.level)
                                        font.pixelSize: 12
                                    }
                                    ToolTip.visible: tip.hovered
                                    ToolTip.text: uncheckedSlot ? qsTr("%n not checked yet", "", n) : qsTr("%1: %2").arg(level).arg(n)
                                    HoverHandler { id: tip }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- Planning notes, shared with the agent ----
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: notesColumn.implicitHeight + 40
            radius: theme.radius
            color: win.surface
            border.color: win.line

            ColumnLayout {
                id: notesColumn
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: qsTr("Next steps"); font.pixelSize: 18; font.weight: Font.DemiBold; color: win.ink }
                    Button {
                        text: qsTr("Plan next steps with my agent")
                        flat: true
                        onClicked: backend.askAgent("next")
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    // notes.md is the teacher's own file; links open in the browser.
                    textFormat: backend.notes ? Text.MarkdownText : Text.PlainText
                    onLinkActivated: link => Qt.openUrlExternally(link)
                    // The file's own title repeats this card's, so it's dropped,
                    // and dated headings are kept small.
                    text: backend.notes.replace(/^# .*$/m, "").replace(/^##+ /gm, "##### ").trim()
                          || qsTr("Nothing yet. Your agent writes its suggestions here, in notes.md, and so can you.")
                    color: backend.notes ? win.ink : win.dim
                    font.pixelSize: 14
                    lineHeight: 1.15
                }
            }
        }

        // ---- The course's core ideas ----
        Rectangle {
            Layout.fillWidth: true
            visible: (backend.current.ideas || []).length > 0
            implicitHeight: ideasColumn.implicitHeight + 40
            radius: theme.radius
            color: win.surface
            border.color: win.line

            ColumnLayout {
                id: ideasColumn
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                Label { text: qsTr("Core ideas"); font.pixelSize: 18; font.weight: Font.DemiBold; color: win.ink }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    text: qsTr("From One Body, Many Members. Each idea is a joint this course shares with other subjects.")
                    color: win.dim
                    font.pixelSize: 13
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: backend.current.ideas || []
                        delegate: Rectangle {
                            required property var modelData
                            width: ideaText.implicitWidth + 20
                            height: 30
                            radius: 15
                            color: "transparent"
                            border.color: win.line
                            Label {
                                id: ideaText
                                anchors.centerIn: parent
                                textFormat: Text.PlainText
                                text: modelData.name + "  ·  " + modelData.joint
                                color: win.ink
                                font.pixelSize: 13
                            }
                            ToolTip.visible: ideaHover.hovered
                            ToolTip.text: modelData.description
                            HoverHandler { id: ideaHover }
                        }
                    }
                }
            }
        }
    }
}
