import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The one-time setup behind omamastery://setup. One job per screen: connect
// Canvas, maybe Blackbaud, pick classes, then hand the plans to the agent.
Rectangle {
    id: view
    color: "#0e0e10"
    signal closed()

    MouseArea { anchors.fill: parent } // keep clicks off the app underneath

    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: view.visible && !setup.busy
        onActivated: view.closed()
    }

    ToolButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        text: "✕"
        enabled: !setup.busy
        onClicked: view.closed()
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 520)
        height: Math.min(parent.height - 48, implicitHeight)
        spacing: 16

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            font.pixelSize: 26
            font.weight: Font.DemiBold
            color: win.ink
            text: setup.busy ? qsTr("One moment")
                : setup.step === "canvas" ? qsTr("Connect your classes")
                : setup.step === "blackbaud" ? qsTr("Add your schedule?")
                : setup.step === "classes" ? qsTr("Pick your classes")
                : qsTr("You're set up")
        }

        // ---- Working ----
        RowLayout {
            visible: setup.busy
            spacing: 12
            BusyIndicator { running: setup.busy; implicitWidth: 32; implicitHeight: 32 }
            Label { textFormat: Text.PlainText; text: setup.message; color: win.dim }
        }

        // ---- Canvas ----
        ColumnLayout {
            visible: !setup.busy && setup.step === "canvas"
            Layout.fillWidth: true
            spacing: 12
            onVisibleChanged: if (visible) (address.visible ? address : token).forceActiveFocus()
            Component.onCompleted: if (visible) (address.visible ? address : token).forceActiveFocus()

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: win.dim
                text: qsTr("OmaMastery reads your courses, rosters and modules from Canvas. Student names stay on this computer.")
            }
            TextField {
                id: address
                Layout.fillWidth: true
                visible: setup.canvasUrl === ""
                focus: visible
                placeholderText: qsTr("yourschool.instructure.com")
                onAccepted: setup.setCanvasUrl(text)
                onEditingFinished: setup.setCanvasUrl(text)
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("1. Get a token from Canvas")
                enabled: setup.canvasUrl !== ""
                onClicked: setup.openTokenPage()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("In Canvas: New access token, purpose \"OmaMastery\", then copy it")
            }
            TextField {
                id: token
                Layout.fillWidth: true
                focus: !address.visible
                echoMode: TextInput.Password
                placeholderText: qsTr("2. Paste the token here")
                onAccepted: connectButton.clicked()
            }
            Button {
                id: connectButton
                Layout.fillWidth: true
                text: qsTr("Connect")
                highlighted: true
                Material.foreground: theme.accentForeground
                enabled: token.text.trim() !== ""
                onClicked: {
                    if (address.visible) setup.setCanvasUrl(address.text)
                    setup.connectCanvas(token.text)
                    token.text = ""
                }
            }
        }

        // ---- Blackbaud ----
        ColumnLayout {
            visible: !setup.busy && setup.step === "blackbaud"
            Layout.fillWidth: true
            spacing: 12
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: win.dim
                text: qsTr("Blackbaud knows your A/B days, blocks, rooms and bell times, so OmaMastery and OmaClass open on the class happening now.")
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Sign in to Blackbaud")
                highlighted: true
                Material.foreground: theme.accentForeground
                onClicked: setup.connectBlackbaud()
            }
            Button {
                Layout.fillWidth: true
                flat: true
                text: qsTr("Skip")
                onClicked: setup.skipBlackbaud()
            }
        }

        // ---- Classes ----
        ListView {
            visible: !setup.busy && setup.step === "classes"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 360)
            Layout.fillHeight: true
            clip: true
            model: setup.classes
            delegate: ItemDelegate {
                id: choice
                required property var modelData
                required property int index
                width: ListView.view.width
                onClicked: setup.toggle(index)
                contentItem: RowLayout {
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText; text: choice.modelData.name; color: win.ink }
                        Label { Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText; text: choice.modelData.detail || qsTr("Day and block not known yet"); color: win.dim; font.pixelSize: 12 }
                    }
                    CheckBox {
                        checked: choice.modelData.selected
                        onClicked: setup.toggle(choice.index)
                    }
                }
            }
        }
        Button {
            visible: !setup.busy && setup.step === "classes"
            onVisibleChanged: if (visible) forceActiveFocus()
            Keys.onReturnPressed: clicked()
            Layout.fillWidth: true
            text: qsTr("Set up %n classes", "", setup.chosen)
            highlighted: true
            Material.foreground: theme.accentForeground
            enabled: setup.chosen > 0
            onClicked: setup.finish()
        }

        // ---- Done ----
        ColumnLayout {
            visible: !setup.busy && setup.step === "done"
            Layout.fillWidth: true
            spacing: 12
            Repeater {
                model: setup.results
                delegate: Label {
                    required property string modelData
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    text: modelData
                    color: win.dim
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: win.ink
                text: qsTr("Next, put each class's long-range plan in its folder. Your agent turns the plans into objectives; it never sees the roster.")
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Draft objectives with my agent")
                highlighted: true
                Material.foreground: theme.accentForeground
                onClicked: setup.draftObjectives()
            }
            Button {
                Layout.fillWidth: true
                visible: setup.canAddOmaClass
                text: qsTr("Add OmaClass to my bar")
                onClicked: setup.addOmaClassToBar()
            }
            Button {
                Layout.fillWidth: true
                flat: true
                text: qsTr("Done")
                onClicked: view.closed()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !setup.busy && setup.message !== ""
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            text: setup.message
            color: setup.step === "done" ? win.dim : theme.accent
        }
    }
}
