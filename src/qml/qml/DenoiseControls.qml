import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property ProcessingAdapter processing
    spacing: Theme.spacingSm

    CheckBox {
        objectName: "denoiseEnabled"
        text: qsTr("Denoise")
        checked: root.processing.denoiseEnabled
        enabled: root.processing.available
        onClicked: root.processing.setDenoiseEnabled(checked)
    }
    Label { text: qsTr("Mode"); color: Theme.textSecondary }
    ComboBox {
        id: mode
        objectName: "denoiseMode"
        Layout.fillWidth: true
        model: [
            { name: qsTr("Gaussian"), value: "gaussian" },
            { name: qsTr("Median"), value: "median" }
        ]
        textRole: "name"
        valueRole: "value"
        currentValue: root.processing.denoiseMode
        enabled: root.processing.available && root.processing.denoiseEnabled
        Accessible.name: qsTr("Denoise mode")
        onActivated: root.processing.setDenoiseMode(currentValue)
    }
    Label { text: qsTr("Kernel"); color: Theme.textSecondary }
    ComboBox {
        id: kernel
        objectName: "denoiseKernel"
        Layout.fillWidth: true
        model: root.processing.denoiseKernelOptions
        textRole: "name"
        valueRole: "value"
        currentValue: root.processing.denoiseKernelSize
        enabled: root.processing.available && root.processing.denoiseEnabled
        Accessible.name: qsTr("Denoise kernel")
        onActivated: root.processing.commitDenoiseKernelSize(currentValue)
    }
    ExactProcessingField {
        processing: root.processing
        label: qsTr("Sigma")
        fieldName: "denoiseSigmaField"; sliderName: "denoiseSigmaSlider"
        formattedValue: root.processing.denoiseSigmaText
        value: root.processing.denoiseSigma
        minimum: root.processing.denoiseSigmaMinimum; maximum: root.processing.denoiseSigmaMaximum
        sliderVisible: false
        enabled: root.processing.available && root.processing.denoiseEnabled
            && root.processing.denoiseMode === "gaussian"
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitDenoiseSigmaText(text)
    }
    Label {
        text: qsTr("Gaussian: 0 uses automatic sigma.")
        color: Theme.textSecondary
        font.pixelSize: 12
        Layout.fillWidth: true
        wrapMode: Text.Wrap
    }
    Connections {
        target: root.processing
        function onDraftReplaced() {
            mode.popup.close()
            kernel.popup.close()
        }
    }
}
