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
    readonly property LayoutAdapter windowLayout: workstation.layout
    readonly property bool panelsVisible: !windowLayout.panelsCollapsed && !windowLayout.fullscreen
    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("Lumora · %1").arg(root.camera.sourceName)
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

    Component.onCompleted: root.windowLayout.attachWindow(root)
    Connections {
        target: root.windowLayout
        function onLayoutChanging() {
            root.processing.cancelUncommittedInput()
            root.camera.settings.closeSettings()
            root.workstation.installation.closeSettings()
            surface.forceActiveFocus()
        }
    }
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
            Label {
                objectName: "cameraSourceSummary"
                text: qsTr("%1 · %2").arg(root.camera.sourceName).arg(root.camera.status)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.pixelSize: 11
                Layout.fillWidth: true
            }
            ToolButton {
                objectName: "cameraSetupDisclosure"
                text: cameraPanel.setupRequired ? qsTr("Source setup required")
                    : cameraPanel.compact ? qsTr("Source setup") : qsTr("Hide source setup")
                visible: root.panelsVisible
                enabled: !cameraPanel.setupRequired
                Accessible.name: qsTr("Source setup")
                Accessible.description: cameraPanel.setupRequired
                    ? qsTr("Source setup is required before acquisition can continue.")
                    : qsTr("Show or hide source setup.")
                onClicked: cameraPanel.toggleSetup()
            }
            Button {
                objectName: "panelsButton"
                text: root.windowLayout.panelsCollapsed ? qsTr("Show panels") : qsTr("Hide panels")
                focusPolicy: Qt.TabFocus
                enabled: root.windowLayout.ready && !root.windowLayout.fullscreen
                onClicked: root.windowLayout.setPanelsCollapsed(!root.windowLayout.panelsCollapsed)
            }
            Button {
                objectName: "fullscreenButton"
                text: root.windowLayout.fullscreen ? qsTr("Exit fullscreen") : qsTr("Fullscreen")
                focusPolicy: Qt.TabFocus
                enabled: root.windowLayout.ready
                Accessible.description: qsTr("F11 toggles fullscreen. Escape exits fullscreen.")
                onClicked: root.windowLayout.toggleFullscreen()
            }
            Button {
                objectName: "diagnosticsButton"
                text: qsTr("Details")
                checkable: true
                checked: root.windowLayout.diagnosticsVisible
                focusPolicy: Qt.TabFocus
                enabled: root.windowLayout.ready
                Accessible.name: qsTr("Show frame details")
                onClicked: root.windowLayout.setDiagnosticsVisible(checked)
            }
            Label { objectName: "evaluationBanner"; text: qsTr("EVALUATION — NOT FOR CLINICAL USE"); color: Theme.amber; font.bold: true; font.pixelSize: 11 }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd
            CameraStartup {
                id: cameraPanel
                objectName: "cameraPanel"
                visible: root.panelsVisible && !cameraPanel.compact
                camera: root.camera
                installation: root.workstation.installation
                Layout.minimumWidth: 200
                Layout.preferredWidth: 200
                Layout.maximumWidth: 200
                Layout.fillHeight: true
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacingSm
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs
                    visible: root.camera.startVisible || root.camera.resumeLiveVisible
                        || root.camera.retryVisible

                    Button {
                        id: startButton
                        objectName: "startButton"
                        text: qsTr("Start Live")
                        highlighted: true
                        visible: root.camera.startVisible
                        enabled: root.camera.startEnabled
                        onClicked: root.camera.startLive()
                    }
                    Button {
                        id: resumeLiveButton
                        objectName: "resumeLiveButton"
                        text: qsTr("Resume saved Live")
                        visible: root.camera.resumeLiveVisible
                        enabled: root.camera.resumeLiveEnabled
                        onClicked: root.camera.resumeLive()
                    }
                    Button {
                        id: retryButton
                        objectName: "retryButton"
                        text: qsTr("Retry camera")
                        visible: root.camera.retryVisible
                        enabled: root.camera.retryEnabled
                        onClicked: root.camera.retry()
                    }
                }
                ViewingToolbar {
                    objectName: "viewingToolbar"
                    viewer: root.viewer
                    camera: root.camera
                    showPriorityActions: !cameraPanel.visible
                    Layout.fillWidth: true
                }
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
                        objectName: "viewerSurface"
                        anchors.fill: parent
                        viewer: root.viewer
                        focus: true
                        activeFocusOnTab: true
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
                    Rectangle {
                        objectName: "imageStateOverlay"
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.topMargin: Theme.spacingSm
                        width: stateText.implicitWidth + 24
                        height: stateText.implicitHeight + 12
                        color: Theme.canvas
                        border.color: Theme.amber
                        visible: root.viewer.hasFrame && (root.viewer.playbackState === "Paused" || root.viewer.freshness === "Stale")
                        Label {
                            id: stateText
                            objectName: "imageStateLabel"
                            anchors.centerIn: parent
                            text: root.viewer.playbackState === "Paused" ? qsTr("PAUSED") : qsTr("STALE IMAGE — NOT LIVE")
                            font.bold: true
                            color: Theme.amber
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
                StatusStrip { objectName: "statusStrip"; camera: root.camera; viewer: root.viewer; processing: root.processing; diagnosticsVisible: root.windowLayout.diagnosticsVisible; showEditorMessages: !root.panelsVisible; Layout.fillWidth: true }
                Label { objectName: "layoutWarning"; text: root.windowLayout.warning; visible: text.length > 0; color: Theme.amber; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Label { text: root.workstation.error; visible: text.length > 0; color: Theme.amber; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
            ProcessingControls { objectName: "processingPanel"; visible: root.panelsVisible; processing: root.processing; focusedItem: root.activeFocusItem; Layout.minimumWidth: 210; Layout.preferredWidth: 210; Layout.maximumWidth: 210; Layout.fillHeight: true }
        }
    }
}
