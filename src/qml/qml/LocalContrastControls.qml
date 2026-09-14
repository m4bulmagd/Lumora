import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property ProcessingAdapter processing
    spacing: Theme.spacingSm
    CheckBox {
        objectName: "localContrastEnabled"
        text: qsTr("Local contrast")
        Accessible.name: qsTr("Local contrast (CLAHE)")
        Accessible.description: qsTr("Contrast Limited Adaptive Histogram Equalization (CLAHE)")
        checked: root.processing.localContrastEnabled
        enabled: root.processing.available
        onClicked: root.processing.setLocalContrastEnabled(checked)
    }
    ExactProcessingField {
        processing: root.processing
        label: qsTr("Clip limit")
        fieldName: "clipLimitField"; sliderName: "clipLimitSlider"
        formattedValue: root.processing.clipLimitText
        value: root.processing.clipLimit
        minimum: root.processing.clipLimitMinimum; maximum: root.processing.clipLimitMaximum
        sliderStep: (maximum - minimum) / 1000
        enabled: root.processing.available && root.processing.localContrastEnabled
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitClipLimitText(text)
        onValueCommitted: value => root.processing.commitClipLimit(value)
        onValueDragged: value => root.processing.dragClipLimit(value)
        onValueReleased: root.processing.releaseClipLimit()
    }
    ExactProcessingField {
        processing: root.processing
        label: qsTr("Tile grid")
        fieldName: "tileGridField"; sliderName: ""
        formattedValue: root.processing.tileGridSizeText
        value: root.processing.tileGridSize
        minimum: root.processing.tileGridSizeMinimum; maximum: root.processing.tileGridSizeMaximum
        wholeNumbers: true
        sliderVisible: false
        enabled: root.processing.available && root.processing.localContrastEnabled
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitTileGridSizeText(text)
    }
}
