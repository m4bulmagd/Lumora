import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    required property ProcessingAdapter processing
    property bool cancellingInput: false
    spacing: Theme.spacingSm

    // Cancel text before transferring pointer focus, which finishes editing.
    function cancelBeforePointerAction() {
        cancellingInput = true
        processing.cancelUncommittedInput()
        cancellingInput = false
    }

    ComboBox {
        id: selector
        objectName: "presetSelector"
        Layout.fillWidth: true
        model: root.processing.presets
        textRole: "name"
        valueRole: "id"
        currentValue: root.processing.selectedPresetId
        enabled: root.processing.available
        focusPolicy: Qt.TabFocus
        hoverEnabled: true
        Accessible.name: qsTr("Processing preset")
        ToolTip.visible: hovered
        ToolTip.text: currentText
        onPressedChanged: if (pressed) {
            root.cancelBeforePointerAction()
            forceActiveFocus()
        }
        onActivated: root.processing.selectPreset(currentValue)
    }
    Button {
        objectName: "resetProcessingButton"
        text: qsTr("Reset")
        implicitWidth: 60
        enabled: root.processing.available
        focusPolicy: Qt.TabFocus
        Accessible.name: qsTr("Reset processing to Original")
        onPressedChanged: if (pressed) {
            root.cancelBeforePointerAction()
            forceActiveFocus()
        }
        onClicked: root.processing.resetProcessing()
    }
    Connections {
        target: root.processing
        function onDraftReplaced() {
            if (!root.cancellingInput) selector.popup.close()
        }
    }
}
