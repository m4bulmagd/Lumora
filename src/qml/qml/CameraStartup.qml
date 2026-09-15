import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    required property CameraAdapter camera
    required property InstallationAdapter installation
    clip: true
    contentWidth: availableWidth
    ColumnLayout {
        width: root.availableWidth
        spacing: Theme.spacingSm
        Label { text: qsTr("CAMERA"); color: Theme.textSecondary; font.bold: true; font.pixelSize: 11 }
        Label { text: root.camera.status; font.pixelSize: 18; Layout.fillWidth: true; wrapMode: Text.Wrap }
        ComboBox {
            id: selector
            objectName: "sourceSelector"
            Layout.fillWidth: true
            model: root.camera.devices
            textRole: "displayName"
            valueRole: "id"
            currentIndex: indexOfValue(root.camera.selectedCameraId)
            enabled: root.camera.selectionEnabled
            Accessible.name: qsTr("Camera source")
            onActivated: root.camera.selectCamera(currentValue)
        }
        GridLayout {
            columns: 2
            Layout.fillWidth: true
            columnSpacing: Theme.spacingXs
            rowSpacing: Theme.spacingXs
            Button { objectName: "refreshButton"; text: qsTr("Refresh"); Layout.fillWidth: true; visible: root.camera.refreshVisible; enabled: root.camera.refreshEnabled; onClicked: root.camera.refreshDevices() }
            Button { objectName: "connectButton"; text: qsTr("Connect"); Layout.fillWidth: true; visible: root.camera.connectVisible; enabled: root.camera.connectEnabled; onClicked: root.camera.connectCamera() }
            Button { objectName: "applyButton"; text: qsTr("Apply"); Layout.fillWidth: true; visible: root.camera.applyVisible; enabled: root.camera.applyEnabled; onClicked: root.camera.applyConfiguration() }
            Button { objectName: "confirmButton"; text: qsTr("Confirm"); Layout.fillWidth: true; visible: root.camera.confirmVisible; enabled: root.camera.confirmEnabled; onClicked: root.camera.confirmConfiguration() }
            Button { objectName: "startButton"; text: qsTr("Start Live"); Layout.fillWidth: true; highlighted: true; visible: root.camera.startVisible; enabled: root.camera.startEnabled; onClicked: root.camera.startLive() }
            Button { objectName: "stopButton"; text: qsTr("Stop"); Layout.fillWidth: true; visible: root.camera.stopVisible || (root.camera.pending && root.camera.stopEnabled); enabled: root.camera.stopEnabled; onClicked: root.camera.stopLive() }
            Button { objectName: "disconnectButton"; text: qsTr("Disconnect"); Layout.fillWidth: true; visible: root.camera.disconnectVisible; enabled: root.camera.disconnectEnabled; onClicked: root.camera.disconnectCamera() }
            Button { objectName: "retryButton"; text: qsTr("Retry camera"); Layout.fillWidth: true; visible: root.camera.retryVisible; enabled: root.camera.retryEnabled; onClicked: root.camera.retry() }
            Button { objectName: "resumeLiveButton"; text: qsTr("Resume saved Live"); Layout.columnSpan: 2; Layout.fillWidth: true; visible: root.camera.resumeLiveVisible; enabled: root.camera.resumeLiveEnabled; onClicked: root.camera.resumeLive() }
            Button { objectName: "cameraSettingsButton"; text: qsTr("Camera settings"); Layout.columnSpan: 2; Layout.fillWidth: true; visible: root.camera.settingsVisible; enabled: root.camera.settingsEnabled; onClicked: { root.installation.closeSettings(); root.camera.settings.openSettings() } }
            Button { objectName: "installationSettingsButton"; text: qsTr("Installation orientation"); Layout.columnSpan: 2; Layout.fillWidth: true; visible: root.camera.installationVisible; enabled: root.camera.installationEnabled; onClicked: { root.camera.settings.closeSettings(); root.installation.openSettings() } }
        }
        Label { text: root.camera.pending ? qsTr("Camera operation pending…") : qsTr("Apply → review → Confirm → Start"); color: Theme.textSecondary; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        Label { text: qsTr("REQUESTED"); font.bold: true; font.pixelSize: 11; color: Theme.textSecondary }
        Label { objectName: "requestedReadback"; text: root.camera.requestedSummary; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 }
        Label { text: qsTr("CURRENT READBACK"); font.bold: true; font.pixelSize: 11; color: Theme.textSecondary }
        Label { objectName: "currentReadback"; text: root.camera.currentSummary; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 }
        Label { text: root.camera.orientation; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary; font.pixelSize: 12 }
        Label { text: root.camera.warning; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.amber }
        Label { text: root.camera.error; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.amber }
    }
    CameraSettings { settings: root.camera.settings }
    InstallationSettings { settings: root.installation }
}
