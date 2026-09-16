import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

EffectSection {
    id: root
    required property ProcessingAdapter processing
    objectName: "denoiseSection"
    title: qsTr("Denoise")
    disclosureName: "denoiseDisclosure"
    optionsName: "denoiseOptions"
    enabledItemName: "denoiseEnabled"
    effectEnabled: root.processing.denoiseEnabled
    controlsAvailable: root.processing.available
    onCollapsing: root.processing.cancelUncommittedInput()
    onEnabledRequested: enabled => root.processing.setDenoiseEnabled(enabled)

    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Mode"); color: Theme.textSecondary; Layout.fillWidth: true }
        ComboBox {
            id: mode
            objectName: "denoiseMode"
            Layout.preferredWidth: 112
            model: [
                { name: qsTr("Gaussian"), value: "gaussian" },
                { name: qsTr("Median"), value: "median" }
            ]
            textRole: "name"
            valueRole: "value"
            currentValue: root.processing.denoiseMode
            enabled: root.processing.available
            Accessible.name: qsTr("Denoise mode")
            onActivated: root.processing.setDenoiseMode(currentValue)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Kernel"); color: Theme.textSecondary; Layout.fillWidth: true }
        ComboBox {
            id: kernel
            objectName: "denoiseKernel"
            Layout.preferredWidth: 112
            model: root.processing.denoiseKernelOptions
            textRole: "name"
            valueRole: "value"
            currentValue: root.processing.denoiseKernelSize
            enabled: root.processing.available
            Accessible.name: qsTr("Denoise kernel")
            onActivated: root.processing.commitDenoiseKernelSize(currentValue)
        }
    }
    ExactProcessingField {
        processing: root.processing
        label: qsTr("Sigma")
        fieldName: "denoiseSigmaField"; sliderName: "denoiseSigmaSlider"
        formattedValue: root.processing.denoiseSigmaText
        value: root.processing.denoiseSigma
        minimum: root.processing.denoiseSigmaMinimum; maximum: root.processing.denoiseSigmaMaximum
        helpText: qsTr("0 uses automatic smoothing.")
        sliderVisible: false
        enabled: root.processing.available && root.processing.denoiseMode === "gaussian"
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitDenoiseSigmaText(text)
    }
    Connections {
        target: root.processing
        function onDraftReplaced() {
            mode.popup.close()
            kernel.popup.close()
        }
    }
}
