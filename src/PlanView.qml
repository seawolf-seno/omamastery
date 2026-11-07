import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The semester, week by week: each objective with the core idea it builds.
// The agent drafts and spaces objectives from the long-range plan; the
// teacher reads and adjusts here or in plan.toml.
ColumnLayout {
    spacing: 12

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            text: backend.current.hasLongRangePlan
                  ? qsTr("%n objectives, from the long-range plan in this class's folder.", "", backend.objectives.length)
                  : qsTr("Add your long-range plan to this class's folder (long-range-plan.md or .pdf), then let your agent draft and space the objectives.")
            color: win.dim
        }
        Button {
            text: qsTr("Draft objectives")
            flat: true
            onClicked: backend.askAgent("objectives")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Your agent drafts objectives from the long-range plan for you to review")
        }
        Button {
            text: qsTr("Space across the semester")
            highlighted: true
            Material.foreground: theme.accentForeground
            onClicked: backend.askAgent("space")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Your agent spreads the objectives over the school calendar, skipping days off")
        }
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 2
        model: backend.objectives
        ScrollBar.vertical: ScrollBar {}

        section.property: "weekLabel"
        section.delegate: Label {
            required property string section
            width: ListView.view.width
            topPadding: 14
            bottomPadding: 4
            textFormat: Text.PlainText
            text: section === "Not scheduled" ? qsTr("Not scheduled yet") : qsTr("Week of %1").arg(section)
            color: win.dim
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
        }

        delegate: Rectangle {
            id: item
            required property var modelData
            width: ListView.view.width
            height: 48
            radius: Math.min(6, theme.radius)
            color: modelData.thisWeek ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
            border.color: modelData.thisWeek ? theme.accent : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 12

                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: item.modelData.taught ? theme.accent : "transparent"
                    border.color: theme.accent
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: item.modelData.title
                    color: item.modelData.taught && !item.modelData.thisWeek ? win.dim : win.ink
                }
                Label {
                    textFormat: Text.PlainText
                    text: item.modelData.joint ? item.modelData.idea + " · " + item.modelData.joint : item.modelData.idea
                    color: win.dim
                    font.pixelSize: 12
                }
            }
        }
    }
}
