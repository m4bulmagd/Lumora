import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property CameraSettingsAdapter settings
    objectName: "cameraSettingsDialog"
    parent: Overlay.overlay
    title: qsTr("Camera settings")
    modal: false
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    visible: settings.open
    // Keep the startup column reachable for Stop and Disconnect.
    width: Math.min(520, parent.width - 242)
    height: Math.min(520, parent.height - 70)
    x: parent.width - width - Theme.spacingMd
    y: 56
    onClosed: settings.closeSettings()

    contentItem: ScrollView {
        id: scroller
        clip: true
        contentWidth: availableWidth
        implicitHeight: fields.implicitHeight
        ColumnLayout {
            id: fields
            width: scroller.availableWidth
            spacing: Theme.spacingSm
            Label {
                text: root.settings.sourceSummary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textSecondary
            }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: Theme.spacingSm
                Label { text: qsTr("Exposure mode"); color: Theme.textSecondary }
                Label { text: qsTr("Exposure (µs)"); color: Theme.textSecondary }
                ComboBox {
                    objectName: "cameraExposureMode"
                    Layout.preferredWidth: 145
                    model: root.settings.exposureModes
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: indexOfValue(root.settings.exposureMode)
                    displayText: currentIndex < 0 ? qsTr("Unavailable") : currentText
                    enabled: root.settings.exposureModeEnabled
                    Accessible.name: qsTr("Exposure mode")
                    onActivated: root.settings.setExposureMode(currentValue)
                }
                TextField {
                    objectName: "cameraExposureValue"
                    Layout.fillWidth: true
                    text: root.settings.exposureText
                    enabled: root.settings.exposureValueEnabled
                    selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    Accessible.name: qsTr("Exposure in microseconds")
                    Accessible.description: root.settings.exposureRange
                    onTextEdited: root.settings.editExposureText(text)
                }
            }
            Label {
                text: root.settings.exposureRange
                visible: text.length > 0
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "cameraExposureReason"
                text: root.settings.exposureReason
                visible: text.length > 0
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: Theme.spacingSm
                Label { text: qsTr("Gain mode"); color: Theme.textSecondary }
                Label { text: qsTr("Gain (dB)"); color: Theme.textSecondary }
                ComboBox {
                    objectName: "cameraGainMode"
                    Layout.preferredWidth: 145
                    model: root.settings.gainModes
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: indexOfValue(root.settings.gainMode)
                    displayText: currentIndex < 0 ? qsTr("Unavailable") : currentText
                    enabled: root.settings.gainModeEnabled
                    Accessible.name: qsTr("Gain mode")
                    onActivated: root.settings.setGainMode(currentValue)
                }
                TextField {
                    objectName: "cameraGainValue"
                    Layout.fillWidth: true
                    text: root.settings.gainText
                    enabled: root.settings.gainValueEnabled
                    selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    Accessible.name: qsTr("Gain in decibels")
                    Accessible.description: root.settings.gainRange
                    onTextEdited: root.settings.editGainText(text)
                }
            }
            Label {
                text: root.settings.gainRange
                visible: text.length > 0
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "cameraGainReason"
                text: root.settings.gainReason
                visible: text.length > 0
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
            Label { text: qsTr("CURRENT READBACK"); font.bold: true; font.pixelSize: 11; color: Theme.textSecondary }
            Label {
                objectName: "cameraSettingsActual"
                text: root.settings.currentSummary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "cameraSettingsStatus"
                text: root.settings.status
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: root.settings.editable ? Theme.textSecondary : Theme.amber
            }
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "applyCameraSettingsButton"
            text: qsTr("Apply settings")
            enabled: root.settings.applyEnabled
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: root.settings.apply()
        }
        Button {
            objectName: "closeCameraSettingsButton"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.settings.closeSettings()
        }
    }
}
