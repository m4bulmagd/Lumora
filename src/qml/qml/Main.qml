import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    objectName: "workstationWindow"
    required property QmlWorkstation workstation
    required property CameraAdapter camera
    required property ProcessingAdapter processing
    required property ViewerAdapter viewer
    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("Lumora · SIM-LIVE pilot")
    color: Theme.canvas
    font.family: "Sans Serif"
    font.pixelSize: 13
    palette.window: Theme.canvas
    palette.windowText: Theme.textPrimary
    palette.base: Theme.imageWell
    palette.text: Theme.textPrimary
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.greenSoft
    palette.highlightedText: Theme.textPrimary
    palette.mid: Theme.border
    palette.light: Theme.border
    palette.dark: Theme.imageWell

    onClosing: close => {
        if (!root.workstation.closed) {
            close.accepted = false
            root.workstation.requestShutdown()
        }
    }
    Connections {
        target: root.workstation
        function onShutdownComplete() { root.close() }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm
        RowLayout {
            Layout.fillWidth: true
            Label { text: "LUMORA"; font.letterSpacing: 3; font.bold: true; font.pixelSize: 18 }
            Label { text: qsTr("SIM-LIVE PILOT"); color: Theme.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
            Label { objectName: "evaluationBanner"; text: qsTr("EVALUATION — NOT FOR CLINICAL USE"); color: Theme.amber; font.bold: true; font.pixelSize: 11 }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd
            CameraStartup { camera: root.camera; Layout.preferredWidth: 200; Layout.fillHeight: true }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacingSm
                ViewingToolbar { objectName: "viewingToolbar"; viewer: root.viewer; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: root.viewer.displayMode === "compare" ? qsTr("ORIGINAL") : (root.viewer.displayMode === "enhanced" ? qsTr("ENHANCED") : qsTr("ORIGINAL")); color: Theme.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                    Label { text: qsTr("ENHANCED"); visible: root.viewer.displayMode === "compare"; color: Theme.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                }
                Rectangle {
                    id: imageArea
                    objectName: "imageArea"
                    color: Theme.imageWell
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ViewerSurface {
                        id: surface
                        anchors.fill: parent
                        viewer: root.viewer
                        focus: true
                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_Space) {
                                if (root.viewer.playbackState === "Paused") root.viewer.resume()
                                else if (root.viewer.playbackState !== "Pausing") root.viewer.pause()
                            } else if (event.key === Qt.Key_F) root.viewer.fit()
                            else if (event.key === Qt.Key_1) root.viewer.actualPixels()
                            else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) root.viewer.zoomAt(width / 2, height / 2, 1.2)
                            else if (event.key === Qt.Key_Minus) root.viewer.zoomAt(width / 2, height / 2, 1 / 1.2)
                            else { event.accepted = false; return }
                            event.accepted = true
                        }
                    }
                    Label { objectName: "waitingLabel"; anchors.centerIn: parent; width: parent.width - 32; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; visible: !root.viewer.hasFrame; text: root.workstation.closing ? qsTr("Closing camera and renderer…") : qsTr("Waiting for an image\nConnect, Apply and review, then Confirm and Start."); color: Theme.textSecondary }
                    MouseArea {
                        anchors.fill: parent
                        property point previous
                        onPressed: mouse => { surface.forceActiveFocus(); previous = Qt.point(mouse.x, mouse.y) }
                        onPositionChanged: mouse => { if (pressed) { root.viewer.panBy(mouse.x - previous.x, mouse.y - previous.y); previous = Qt.point(mouse.x, mouse.y) } }
                        onWheel: wheel => root.viewer.zoomAt(wheel.x, wheel.y, Math.pow(1.0015, wheel.angleDelta.y))
                    }
                }
                StatusStrip { objectName: "statusStrip"; camera: root.camera; viewer: root.viewer; processing: root.processing; Layout.fillWidth: true }
                Label { text: root.workstation.error; visible: text.length > 0; color: Theme.amber; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
            WindowLevelControls { processing: root.processing; Layout.preferredWidth: 210; Layout.fillHeight: true }
        }
    }
}
