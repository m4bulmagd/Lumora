import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

EffectSection {
    id: root
    required property ProcessingAdapter processing
    objectName: "localContrastSection"
    title: qsTr("Local contrast")
    disclosureName: "localContrastDisclosure"
    optionsName: "localContrastOptions"
    enabledItemName: "localContrastEnabled"
    effectEnabled: root.processing.localContrastEnabled
    controlsAvailable: root.processing.available
    onCollapsing: root.processing.cancelUncommittedInput()
    onEnabledRequested: enabled => root.processing.setLocalContrastEnabled(enabled)

    ExactProcessingField {
        processing: root.processing
        label: qsTr("Clip limit")
        fieldName: "clipLimitField"; sliderName: "clipLimitSlider"
        formattedValue: root.processing.clipLimitText
        value: root.processing.clipLimit
        minimum: root.processing.clipLimitMinimum; maximum: root.processing.clipLimitMaximum
        sliderStep: (maximum - minimum) / 1000
        enabled: root.processing.available
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitClipLimitText(text)
        onValueCommitted: value => root.processing.commitClipLimit(value)
        onValueDragged: value => root.processing.dragClipLimit(value)
        onValueReleased: root.processing.releaseClipLimit()
    }
    ToolButton {
        id: advanced
        objectName: "localContrastAdvancedDisclosure"
        text: checked ? qsTr("▾ Technical controls") : qsTr("▸ Technical controls")
        checkable: true
        checked: false
        enabled: root.processing.available
        focusPolicy: Qt.TabFocus
        Accessible.name: qsTr("Local contrast technical controls")
        onPressedChanged: if (pressed && checked) root.processing.cancelUncommittedInput()
    }
    ColumnLayout {
        visible: advanced.checked
        enabled: root.processing.available
        Layout.fillWidth: true
        ExactProcessingField {
            processing: root.processing
            label: qsTr("Tile grid")
            fieldName: "tileGridField"; sliderName: ""
            formattedValue: root.processing.tileGridSizeText
            value: root.processing.tileGridSize
            minimum: root.processing.tileGridSizeMinimum; maximum: root.processing.tileGridSizeMaximum
            wholeNumbers: true
            sliderVisible: false
            enabled: root.processing.available
            Layout.fillWidth: true
            onTextCommitted: text => root.processing.commitTileGridSizeText(text)
        }
    }
}
