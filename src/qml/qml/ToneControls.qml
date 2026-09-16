import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property ProcessingAdapter processing
    spacing: Theme.spacingSm

    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Brightness / contrast"); font.bold: true; Layout.fillWidth: true }
        Label {
            text: qsTr("Off")
            visible: !root.processing.brightnessContrastEnabled
            color: Theme.amber; font.pixelSize: 11; font.bold: true
        }
        EffectMenu {
            objectName: "brightnessContrastOptions"
            effectTitle: qsTr("Brightness / contrast")
            effectEnabled: root.processing.brightnessContrastEnabled
            enabledItemName: "brightnessContrastEnabled"
            enabled: root.processing.available
            onOpening: root.processing.cancelUncommittedInput()
            onEnabledRequested: enabled => root.processing.setBrightnessContrastEnabled(enabled)
        }
    }
    ExactProcessingField {
        processing: root.processing
        label: qsTr("Brightness")
        fieldName: "brightnessField"; sliderName: "brightnessSlider"
        formattedValue: root.processing.brightnessText
        value: root.processing.brightness
        minimum: root.processing.brightnessMinimum; maximum: root.processing.brightnessMaximum
        sliderStep: (maximum - minimum) / 1000
        enabled: root.processing.available
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
        enabled: root.processing.available
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitContrastText(text)
        onValueCommitted: value => root.processing.commitContrast(value)
        onValueDragged: value => root.processing.dragContrast(value)
        onValueReleased: root.processing.releaseContrast()
    }
    EffectSection {
        objectName: "gammaSection"
        title: qsTr("Gamma")
        disclosureName: "gammaDisclosure"
        optionsName: "gammaOptions"
        enabledItemName: "gammaEnabled"
        effectEnabled: root.processing.gammaEnabled
        controlsAvailable: root.processing.available
        onCollapsing: root.processing.cancelUncommittedInput()
        onEnabledRequested: enabled => root.processing.setGammaEnabled(enabled)

        ExactProcessingField {
            processing: root.processing
            label: qsTr("Value")
            fieldName: "gammaField"; sliderName: "gammaSlider"
            formattedValue: root.processing.gammaText
            value: root.processing.gamma
            minimum: root.processing.gammaMinimum; maximum: root.processing.gammaMaximum
            sliderStep: (maximum - minimum) / 1000
            enabled: root.processing.available
            Layout.fillWidth: true
            onTextCommitted: text => root.processing.commitGammaText(text)
            onValueCommitted: value => root.processing.commitGamma(value)
            onValueDragged: value => root.processing.dragGamma(value)
            onValueReleased: root.processing.releaseGamma()
        }
    }
}
