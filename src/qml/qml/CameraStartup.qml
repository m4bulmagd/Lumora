import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    required property CameraAdapter camera
    required property InstallationAdapter installation
    property bool setupExpanded: false
    property bool setupWorkflowLatched: false
    readonly property bool hasAppliedConfiguration:
        Object.keys(root.camera.appliedConfiguration).length > 0
    readonly property bool reviewRequired: root.camera.confirmVisible
    readonly property bool configurationRequired: !root.camera.pending
        && root.camera.applyVisible && !root.camera.startEnabled
        && !root.camera.resumeLiveEnabled
    readonly property bool setupRequired: root.camera.connectVisible
        || root.reviewRequired
        || root.configurationRequired
        || root.setupWorkflowLatched
        || (!root.hasAppliedConfiguration && !root.camera.stopVisible)
    readonly property bool setupVisible: root.setupRequired || root.setupExpanded
    readonly property bool readbacksVisible: root.reviewRequired || root.setupExpanded
    readonly property bool compact: !root.setupVisible

    function toggleSetup() {
        if (!root.setupRequired)
            root.setupExpanded = !root.setupExpanded
    }

    onConfigurationRequiredChanged: {
        if (root.configurationRequired)
            root.setupWorkflowLatched = true
    }

    Connections {
        target: root.camera

        function onStateChanged() {
            if (root.configurationRequired
                    || (root.camera.pending && root.setupVisible
                        && root.camera.applyVisible && !root.camera.stopVisible)) {
                root.setupWorkflowLatched = true
            } else if (root.camera.startEnabled || root.camera.resumeLiveEnabled
                       || root.camera.stopVisible) {
                root.setupWorkflowLatched = false
            }
        }
    }

    clip: true
    contentWidth: availableWidth
    onVisibleChanged: if (!visible) selector.popup.close()

    ColumnLayout {
        width: root.availableWidth
        spacing: Theme.spacingSm

        Label {
            text: qsTr("VIDEO SOURCE")
            color: Theme.textSecondary
            font.bold: true
            font.pixelSize: 11
        }
        GridLayout {
            columns: 2
            Layout.fillWidth: true
            columnSpacing: Theme.spacingXs
            rowSpacing: Theme.spacingXs
            visible: root.camera.stopVisible || root.camera.disconnectVisible
                || (root.camera.pending && root.camera.stopEnabled)

            Button {
                objectName: "stopButton"
                text: qsTr("Stop")
                Layout.fillWidth: true
                visible: root.camera.stopVisible
                    || (root.camera.pending && root.camera.stopEnabled)
                enabled: root.camera.stopEnabled
                onClicked: root.camera.stopLive()
            }
            Button {
                objectName: "disconnectButton"
                text: qsTr("Disconnect")
                Layout.fillWidth: true
                visible: root.camera.disconnectVisible
                enabled: root.camera.disconnectEnabled
                onClicked: root.camera.disconnectCamera()
            }
        }
        ColumnLayout {
            objectName: "cameraSetupContent"
            visible: root.setupVisible
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ComboBox {
                id: selector
                objectName: "sourceSelector"
                Layout.fillWidth: true
                model: root.camera.devices
                textRole: "displayName"
                valueRole: "id"
                currentIndex: indexOfValue(root.camera.selectedCameraId)
                enabled: root.camera.selectionEnabled
                Accessible.name: qsTr("Video source")
                onActivated: root.camera.selectCamera(currentValue)
            }
            Button {
                objectName: "networkSourcesButton"
                text: qsTr("Network sources…")
                Layout.fillWidth: true
                visible: root.camera.videoSources !== null
                onClicked: networkSources.open()
            }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: Theme.spacingXs
                rowSpacing: Theme.spacingXs

                Button {
                    objectName: "refreshButton"
                    text: qsTr("Refresh")
                    Layout.fillWidth: true
                    visible: root.camera.refreshVisible
                    enabled: root.camera.refreshEnabled
                    onClicked: root.camera.refreshDevices()
                }
                Button {
                    objectName: "connectButton"
                    text: qsTr("Connect")
                    Layout.fillWidth: true
                    visible: root.camera.connectVisible
                    enabled: root.camera.connectEnabled
                    onClicked: root.camera.connectCamera()
                }
                Button {
                    objectName: "applyButton"
                    text: qsTr("Apply")
                    Layout.fillWidth: true
                    visible: root.camera.applyVisible
                    enabled: root.camera.applyEnabled
                    onClicked: root.camera.applyConfiguration()
                }
                Button {
                    objectName: "confirmButton"
                    text: qsTr("Confirm")
                    Layout.fillWidth: true
                    visible: root.camera.confirmVisible
                    enabled: root.camera.confirmEnabled
                    onClicked: root.camera.confirmConfiguration()
                }
                Button {
                    objectName: "cameraSettingsButton"
                    text: qsTr("Camera settings")
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    visible: root.camera.settingsVisible
                    enabled: root.camera.settingsEnabled
                    onClicked: {
                        root.installation.closeSettings()
                        root.camera.settings.openSettings()
                    }
                }
                Button {
                    objectName: "installationSettingsButton"
                    text: qsTr("Installation orientation")
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    visible: root.camera.installationVisible
                    enabled: root.camera.installationEnabled
                    onClicked: {
                        root.camera.settings.closeSettings()
                        root.installation.openSettings()
                    }
                }
            }

            ColumnLayout {
                objectName: "cameraReviewDetails"
                visible: root.readbacksVisible
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: Theme.border
                }
                Label {
                    text: qsTr("REQUESTED")
                    font.bold: true
                    font.pixelSize: 11
                    color: Theme.textSecondary
                }
                Label {
                    objectName: "requestedReadback"
                    text: root.camera.requestedSummary
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }
                Label {
                    text: qsTr("CURRENT READBACK")
                    font.bold: true
                    font.pixelSize: 11
                    color: Theme.textSecondary
                }
                Label {
                    objectName: "currentReadback"
                    text: root.camera.currentSummary
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }
                Label {
                    text: root.camera.orientation
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
            }
        }
    }

    CameraSettings { settings: root.camera.settings }
    InstallationSettings { settings: root.installation }
    VideoSources { id: networkSources; sources: root.camera.videoSources }
}
