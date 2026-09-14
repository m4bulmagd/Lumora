import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property ProcessingAdapter processing
    spacing: Theme.spacingSm

    ComboBox {
        objectName: "presetSelector"
        Layout.fillWidth: true
        model: root.processing.presets
        textRole: "name"
        valueRole: "id"
        currentValue: root.processing.selectedPresetId
        enabled: root.processing.available
        Accessible.name: qsTr("Processing preset")
        onActivated: root.processing.selectPreset(currentValue)
    }
    Button {
        objectName: "resetProcessingButton"
        Layout.fillWidth: true
        text: qsTr("Reset processing")
        enabled: root.processing.available
        Accessible.name: qsTr("Reset processing to Original")
        onClicked: root.processing.resetProcessing()
    }
}
