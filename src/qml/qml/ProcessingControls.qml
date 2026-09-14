import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property ProcessingAdapter processing
    required property Item focusedItem
    spacing: Theme.spacingSm

    function revealFocusedControl() {
        const control = root.focusedItem
        if (!control) return
        let ancestor = control.parent
        while (ancestor && ancestor !== adjustmentBody) ancestor = ancestor.parent
        if (ancestor !== adjustmentBody) return
        const rect = control.mapToItem(adjustmentBody, 0, 0, control.width, control.height)
        let position = adjustments.contentY
        if (rect.y < position) position = rect.y
        else if (rect.y + rect.height > position + adjustments.height)
            position = rect.y + rect.height - adjustments.height
        adjustments.contentY = Math.max(0, Math.min(position, adjustments.contentHeight - adjustments.height))
    }
    onFocusedItemChanged: Qt.callLater(root.revealFocusedControl)

    Label { text: qsTr("PROCESSING"); color: Theme.textSecondary; font.bold: true; font.pixelSize: 11 }
    PresetControls { processing: root.processing; Layout.fillWidth: true }
    Label {
        objectName: "processingStatus"
        text: !root.processing.available
            ? (root.processing.loadingState === ProcessingAdapter.Loading ? qsTr("Loading processing settings…") : qsTr("Processing settings unavailable."))
            : (root.processing.pending ? qsTr("Applying processing draft…") : qsTr("Edits save after successful activation."))
        color: Theme.textSecondary; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
    }
    Label {
        objectName: "activePresetSummary"
        text: root.processing.hasAcknowledged
            ? qsTr("Active: %1").arg(root.processing.activeSummary.split("\n")[0]) : qsTr("Active: awaiting processing")
        Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
    }
    Label {
        objectName: "processingMessages"
        Layout.fillWidth: true
        text: [root.processing.loadError, root.processing.validationError, root.processing.modelError, root.processing.persistenceWarning].filter(message => message.length > 0).join("\n")
        visible: text.length > 0; wrapMode: Text.Wrap; color: Theme.amber; font.pixelSize: 12
    }
    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        Flickable {
            id: adjustments
            objectName: "processingFlickable"
            contentWidth: width
            contentHeight: adjustmentBody.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            onHeightChanged: Qt.callLater(root.revealFocusedControl)
            onContentHeightChanged: Qt.callLater(root.revealFocusedControl)
            ColumnLayout {
                id: adjustmentBody
                width: adjustments.width
                spacing: Theme.spacingSm
                WindowLevelControls { processing: root.processing; Layout.fillWidth: true }
                ToneControls { processing: root.processing; Layout.fillWidth: true }
                LocalContrastControls { processing: root.processing; Layout.fillWidth: true }
                DenoiseControls { processing: root.processing; Layout.fillWidth: true }
                SharpenControls { processing: root.processing; Layout.fillWidth: true }
                CheckBox {
                    objectName: "invertEnabled"
                    text: qsTr("Invert")
                    checked: root.processing.invertEnabled
                    enabled: root.processing.available
                    onClicked: root.processing.setInvertEnabled(checked)
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                Label { text: qsTr("ACKNOWLEDGED ACTIVE"); color: Theme.textSecondary; font.bold: true; font.pixelSize: 11 }
                Label {
                    objectName: "activeProcessing"
                    text: root.processing.activeSummary
                    Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
                }
            }
        }
    }
}
