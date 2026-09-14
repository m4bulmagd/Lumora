import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property ProcessingAdapter processing
    spacing: Theme.spacingSm

    CheckBox {
        objectName: "sharpenEnabled"
        text: qsTr("Sharpen")
        checked: root.processing.sharpenEnabled
        enabled: root.processing.available
        onClicked: root.processing.setSharpenEnabled(checked)
    }
    ExactProcessingField {
        processing: root.processing
        label: qsTr("Amount")
        fieldName: "sharpenAmountField"; sliderName: "sharpenAmountSlider"
        formattedValue: root.processing.sharpenAmountText
        value: root.processing.sharpenAmount
        minimum: root.processing.sharpenAmountMinimum; maximum: root.processing.sharpenAmountMaximum
        sliderStep: (maximum - minimum) / 1000
        enabled: root.processing.available && root.processing.sharpenEnabled
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitSharpenAmountText(text)
        onValueCommitted: value => root.processing.commitSharpenAmount(value)
        onValueDragged: value => root.processing.dragSharpenAmount(value)
        onValueReleased: root.processing.releaseSharpenAmount()
    }
    CheckBox {
        id: advanced
        objectName: "sharpenAdvanced"
        text: qsTr("Advanced")
        Accessible.name: qsTr("Advanced sharpen controls")
        checked: false
        enabled: root.processing.available && root.processing.sharpenEnabled
    }
    ColumnLayout {
        visible: advanced.checked
        enabled: root.processing.available && root.processing.sharpenEnabled && advanced.checked
        Layout.fillWidth: true
        spacing: Theme.spacingSm
        ExactProcessingField {
            processing: root.processing
            label: qsTr("Radius")
            fieldName: "sharpenRadiusField"; sliderName: "sharpenRadiusSlider"
            formattedValue: root.processing.sharpenRadiusText
            value: root.processing.sharpenRadius
            minimum: root.processing.sharpenRadiusMinimum; maximum: root.processing.sharpenRadiusMaximum
            sliderVisible: false
            Layout.fillWidth: true
            onTextCommitted: text => root.processing.commitSharpenRadiusText(text)
        }
        ExactProcessingField {
            processing: root.processing
            label: qsTr("Threshold")
            fieldName: "sharpenThresholdField"; sliderName: "sharpenThresholdSlider"
            formattedValue: root.processing.sharpenThresholdText
            value: root.processing.sharpenThreshold
            minimum: root.processing.sharpenThresholdMinimum; maximum: root.processing.sharpenThresholdMaximum
            sliderVisible: false
            Layout.fillWidth: true
            onTextCommitted: text => root.processing.commitSharpenThresholdText(text)
        }
    }
}
