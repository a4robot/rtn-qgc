import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

content = content.replace(
    "import QtQuick.Layouts",
    "import QtQuick.Layouts\nimport QtMultimedia"
)

old_panel_start = """    // Top Right Info UI - Matches HTML gauge-panel design
    Rectangle {
        id: rightInfoPanel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 0.75
        anchors.topMargin: ScreenTools.defaultFontPixelHeight * 0.75
        width: infoPanelColumn.width + _panelPadding * 2
        height: infoPanelColumn.height + _panelPadding * 2
        color: "#333333"
        radius: ScreenTools.defaultFontPixelWidth / 2

        property real _panelPadding: ScreenTools.defaultFontPixelWidth * 0.4"""

new_panel_start = """    // Top Right Info UI - Matches HTML gauge-panel design
    Rectangle {
        id: rightInfoPanel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 0.75
        anchors.topMargin: ScreenTools.defaultFontPixelHeight * 0.75
        width: infoPanelColumn.width + _panelPadding * 2
        height: infoPanelColumn.height + _panelPadding * 2
        color: Qt.rgba(qgcPal.windowTransparent.r, qgcPal.windowTransparent.g, qgcPal.windowTransparent.b, qgcPal.windowTransparent.a)
        radius: ScreenTools.defaultFontPixelWidth / 2

        property real _panelPadding: ScreenTools.defaultFontPixelWidth * 0.4

        property bool isFuelValid: _fuelFact && fuelValue > -999.0
        property bool fuelBelow20: isFuelValid && fuelValue < 20.0 && fuelValue >= 10.0
        property bool fuelBelow10: isFuelValid && fuelValue < 10.0

        MediaPlayer {
            id: alertPlayer
            source: "qrc:/res/audio/alert.wav"
            audioOutput: AudioOutput {}
        }

        SequentialAnimation {
            id: warn20Anim
            ColorAnimation { target: rightInfoPanel; property: "color"; to: "yellow"; duration: 250 }
            ColorAnimation { target: rightInfoPanel; property: "color"; to: Qt.rgba(qgcPal.windowTransparent.r, qgcPal.windowTransparent.g, qgcPal.windowTransparent.b, qgcPal.windowTransparent.a); duration: 250 }
        }

        SequentialAnimation {
            id: warn10Anim
            ColorAnimation { target: rightInfoPanel; property: "color"; to: "red"; duration: 250 }
            ColorAnimation { target: rightInfoPanel; property: "color"; to: Qt.rgba(qgcPal.windowTransparent.r, qgcPal.windowTransparent.g, qgcPal.windowTransparent.b, qgcPal.windowTransparent.a); duration: 250 }
            PauseAnimation { duration: 400 }
            ColorAnimation { target: rightInfoPanel; property: "color"; to: "red"; duration: 250 }
            ColorAnimation { target: rightInfoPanel; property: "color"; to: Qt.rgba(qgcPal.windowTransparent.r, qgcPal.windowTransparent.g, qgcPal.windowTransparent.b, qgcPal.windowTransparent.a); duration: 250 }
        }

        Timer {
            id: warn20Timer
            interval: 10000
            repeat: true
            running: rightInfoPanel.fuelBelow20
            onTriggered: {
                alertPlayer.play();
                warn20Anim.start();
            }
            onRunningChanged: {
                if (running) triggered();
            }
        }

        Timer {
            id: warn10Timer
            interval: 5000
            repeat: true
            running: rightInfoPanel.fuelBelow10
            onTriggered: {
                alertPlayer.play();
                warn10Anim.start();
                warn10SecondBeepTimer.start();
            }
            onRunningChanged: {
                if (running) triggered();
            }
        }

        Timer {
            id: warn10SecondBeepTimer
            interval: 700
            repeat: false
            onTriggered: {
                alertPlayer.play();
            }
        }"""

content = content.replace(old_panel_start, new_panel_start)

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
