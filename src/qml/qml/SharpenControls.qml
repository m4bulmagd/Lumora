import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

EffectSection {
    id: root
    required property ProcessingAdapter processing
    objectName: "sharpenSection"
    title: qsTr("Sharpen")
    disclosureName: "sharpenDisclosure"
    optionsName: "sharpenOptions"
    enabledItemName: "sharpenEnabled"
    effectEnabled: root.processing.sharpenEnabled
    controlsAvailable: root.processing.available
    onCollapsing: root.processing.cancelUncommittedInput()
    onEnabledRequested: enabled => root.processing.setSharpenEnabled(enabled)

    ExactProcessingField {
        processing: root.processing
        label: qsTr("Amount")
        fieldName: "sharpenAmountField"; sliderName: "sharpenAmountSlider"
        formattedValue: root.processing.sharpenAmountText
        value: root.processing.sharpenAmount
        minimum: root.processing.sharpenAmountMinimum; maximum: root.processing.sharpenAmountMaximum
        sliderStep: (maximum - minimum) / 1000
        enabled: root.processing.available
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitSharpenAmountText(text)
        onValueCommitted: value => root.processing.commitSharpenAmount(value)
        onValueDragged: value => root.processing.dragSharpenAmount(value)
        onValueReleased: root.processing.releaseSharpenAmount()
    }
    ToolButton {
        id: advanced
        objectName: "sharpenAdvancedDisclosure"
        text: checked ? qsTr("▾ Technical controls") : qsTr("▸ Technical controls")
        checkable: true
        checked: false
        enabled: root.processing.available
        focusPolicy: Qt.TabFocus
        Accessible.name: qsTr("Sharpen technical controls")
        onPressedChanged: if (pressed && checked) root.processing.cancelUncommittedInput()
    }
    ColumnLayout {
        visible: advanced.checked
        enabled: root.processing.available
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
            enabled: root.processing.available
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
            enabled: root.processing.available
            Layout.fillWidth: true
            onTextCommitted: text => root.processing.commitSharpenThresholdText(text)
        }
    }
}
