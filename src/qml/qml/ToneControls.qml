import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property ProcessingAdapter processing
    spacing: Theme.spacingSm
    CheckBox {
        objectName: "brightnessContrastEnabled"
        text: qsTr("Brightness / contrast")
        Accessible.name: text
        checked: root.processing.brightnessContrastEnabled
        enabled: root.processing.available
        onClicked: root.processing.setBrightnessContrastEnabled(checked)
    }
    ExactProcessingField {
        processing: root.processing
        label: qsTr("Brightness")
        fieldName: "brightnessField"; sliderName: "brightnessSlider"
        formattedValue: root.processing.brightnessText
        value: root.processing.brightness
        minimum: root.processing.brightnessMinimum; maximum: root.processing.brightnessMaximum
        sliderStep: (maximum - minimum) / 1000
        enabled: root.processing.available && root.processing.brightnessContrastEnabled
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitBrightnessText(text)
        onValueCommitted: value => root.processing.commitBrightness(value)
        onValueDragged: value => root.processing.dragBrightness(value)
        onValueReleased: root.processing.releaseBrightness()
    }
    ExactProcessingField {
        processing: root.processing
        label: qsTr("Contrast")
        fieldName: "contrastField"; sliderName: "contrastSlider"
        formattedValue: root.processing.contrastText
        value: root.processing.contrast
        minimum: root.processing.contrastMinimum; maximum: root.processing.contrastMaximum
        sliderStep: (maximum - minimum) / 1000
        enabled: root.processing.available && root.processing.brightnessContrastEnabled
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitContrastText(text)
        onValueCommitted: value => root.processing.commitContrast(value)
        onValueDragged: value => root.processing.dragContrast(value)
        onValueReleased: root.processing.releaseContrast()
    }
    CheckBox {
        objectName: "gammaEnabled"
        text: qsTr("Gamma")
        Accessible.name: text
        checked: root.processing.gammaEnabled
        enabled: root.processing.available
        onClicked: root.processing.setGammaEnabled(checked)
    }
    ExactProcessingField {
        processing: root.processing
        label: qsTr("Gamma")
        fieldName: "gammaField"; sliderName: "gammaSlider"
        formattedValue: root.processing.gammaText
        value: root.processing.gamma
        minimum: root.processing.gammaMinimum; maximum: root.processing.gammaMaximum
        sliderStep: (maximum - minimum) / 1000
        enabled: root.processing.available && root.processing.gammaEnabled
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitGammaText(text)
        onValueCommitted: value => root.processing.commitGamma(value)
        onValueDragged: value => root.processing.dragGamma(value)
        onValueReleased: root.processing.releaseGamma()
    }
}
