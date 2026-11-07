import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 760
    minimumWidth: 640
    minimumHeight: 520
    visible: true
    title: backend.current.name ? backend.current.name + " · OmaMastery" : "OmaMastery"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool setupVisible: startInSetup
    function openSetup() { setup.start(""); win.setupVisible = true }
    function whenText(c) {
        if (!c.day && !c.block) return qsTr("Day and block not set")
        return qsTr("%1 day · Block %2").arg(c.day || "?").arg(c.block || "?")
    }
    property int tab: 0
    readonly property color ink: "#e8e8ea"
    readonly property color dim: "#9a9aa2"
    readonly property color line: "#2a2a30"
    readonly property color surface: "#16161a"

    // Levels shade the accent from faint to full; none of them is red,
    // because "not yet" isn't a failure.
    function levelFill(level) {
        const i = backend.levelNames.indexOf(level)
        if (i <= 0) return "transparent"
        const c = Qt.color(theme.accent)
        return Qt.rgba(c.r, c.g, c.b, [0, 0.16, 0.36, 0.62, 0.92][i])
    }
    function levelInk(level) {
        return backend.levelNames.indexOf(level) >= 3 ? theme.accentForeground : win.ink
    }
    function levelOf(student, objective) {
        return backend.levels[student + "|" + objective] || ""
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.setupVisible; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.setupVisible; onActivated: Qt.quit() }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: !win.setupVisible; onActivated: win.tab = 0 }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: !win.setupVisible; onActivated: win.tab = 1 }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: !win.setupVisible; onActivated: win.tab = 2 }
    Shortcut { sequences: ["J", "Down"]; context: Qt.ApplicationShortcut; enabled: !win.setupVisible; onActivated: backend.selectClass(backend.currentIndex + 1) }
    Shortcut { sequences: ["K", "Up"]; context: Qt.ApplicationShortcut; enabled: !win.setupVisible; onActivated: backend.selectClass(backend.currentIndex - 1) }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: !win.setupVisible; onActivated: backend.goToNow() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.setupVisible; onActivated: backend.askAgent("next") }
    Shortcut { sequence: "O"; context: Qt.ApplicationShortcut; enabled: !win.setupVisible; onActivated: backend.openFolder() }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        // ---- First run: nothing in ~/Classes yet ----
        Column {
            anchors.centerIn: parent
            spacing: 16
            visible: backend.classes.length === 0
            width: 460

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("No classes yet")
                font.pixelSize: 26
                font.weight: Font.DemiBold
                color: win.ink
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                text: qsTr("OmaMastery keeps each class as plain files in %1, so you and your agent can both work on them. Bring in your classes from Canvas, or try made-up sample classes first.").arg(backend.root)
                color: win.dim
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Connect to Canvas")
                highlighted: true
                Material.foreground: theme.accentForeground
                onClicked: win.openSetup()
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Try sample classes")
                flat: true
                onClicked: backend.setUpSample()
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0
            visible: backend.classes.length > 0

            ClassRail {
                Layout.fillHeight: true
                Layout.preferredWidth: win.width < 1000 ? 190 : 250
            }

            Rectangle { Layout.fillHeight: true; width: 1; color: win.line }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: win.width < 1000 ? 16 : 24
                spacing: 16

                // ---- Header: the class, when it meets, the agent ----
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            textFormat: Text.PlainText
                            text: backend.current.name || ""
                            font.pixelSize: win.width < 1000 ? 22 : 28
                            font.weight: Font.DemiBold
                            color: win.ink
                        }
                        Label {
                            textFormat: Text.PlainText
                            color: win.dim
                            text: {
                                const c = backend.current
                                let parts = [win.whenText(c),
                                             qsTr("%n students", "", backend.students.length)]
                                if (backend.currentIndex === backend.nowIndex) parts.push(backend.nowText.split(" · ").slice(2).join(" · "))
                                if (c.sample) parts.push(qsTr("sample class, made-up students"))
                                return parts.filter(p => p).join("  ·  ")
                            }
                        }
                    }

                }

                Label {
                    Layout.fillWidth: true
                    visible: backend.error !== ""
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    text: qsTr("Couldn't read a file: %1").arg(backend.error)
                    color: theme.accent
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TabBar {
                        id: tabs
                        currentIndex: win.tab
                        onCurrentIndexChanged: win.tab = currentIndex
                        background: Rectangle { color: "transparent" }
                        TabButton { text: qsTr("Today"); width: implicitWidth + 16 }
                        TabButton { text: qsTr("Mastery"); width: implicitWidth + 16 }
                        TabButton { text: qsTr("Plan"); width: implicitWidth + 16 }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: qsTr("Open folder")
                        flat: true
                        visible: win.width >= 900
                        onClicked: backend.openFolder()
                    }
                    Button {
                        text: qsTr("Plan next steps")
                        highlighted: true
                        Material.foreground: theme.accentForeground
                        onClicked: backend.askAgent("next")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Opens your AI agent in this class's folder to suggest next steps")
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: win.tab
                    TodayView {}
                    MasteryView {}
                    PlanView {}
                }

                Label {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: backend.status
                    color: win.dim
                    elide: Text.ElideRight
                    font.pixelSize: 13
                }
            }
        }

        // ---- One-time setup (omamastery://setup) ----
        SetupView {
            anchors.fill: parent
            visible: win.setupVisible
            onClosed: win.setupVisible = false
        }

        // ---- Keyboard help ----
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: Qt.rgba(0, 0, 0, 0.7)
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Rectangle {
                anchors.centerIn: parent
                width: 420
                height: helpColumn.implicitHeight + 48
                radius: theme.radius
                color: win.surface
                border.color: win.line

                Column {
                    id: helpColumn
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 8
                    Label { text: qsTr("Keyboard"); font.pixelSize: 20; font.weight: Font.DemiBold; color: win.ink }
                    Repeater {
                        model: [
                            ["1  2  3", qsTr("Today, Mastery, Plan")],
                            ["J / K", qsTr("Next or previous class")],
                            ["N", qsTr("The class happening now")],
                            ["Space", qsTr("Plan next steps with your agent")],
                            ["O", qsTr("Open the class folder")],
                            [qsTr("Click / right-click"), qsTr("Raise or lower a mastery level")],
                            ["?", qsTr("This help")],
                            ["Q", qsTr("Quit")]
                        ]
                        delegate: Row {
                            required property var modelData
                            spacing: 16
                            Label { width: 140; text: modelData[0]; color: theme.accent; font.family: "monospace" }
                            Label { text: modelData[1]; color: win.ink }
                        }
                    }
                }
            }
        }
    }
}
