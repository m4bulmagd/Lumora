import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property ProcessingAdapter processing
    required property Item focusedItem
    property bool detailsExpanded: false
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

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm
        Label {
            text: qsTr("PROCESSING")
            color: Theme.textSecondary
            font.bold: true
            font.pixelSize: 11
            Layout.fillWidth: true
        }
        Label {
            objectName: "processingStatus"
            text: !root.processing.available
                ? (root.processing.loadingState === ProcessingAdapter.Loading
                    ? qsTr("Loading…") : qsTr("Unavailable"))
                : (root.processing.pending ? qsTr("Applying…") : "")
            visible: text.length > 0
            color: Theme.textSecondary
            font.pixelSize: 11
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
        }
    }
    PresetControls { processing: root.processing; Layout.fillWidth: true }
    ToolButton {
        objectName: "processingDetailsDisclosure"
        implicitHeight: 26
        text: checked ? qsTr("▾ Details") : qsTr("▸ Details")
        checkable: true
        checked: root.detailsExpanded
        visible: root.processing.hasAcknowledged
        focusPolicy: Qt.TabFocus
        Accessible.name: qsTr("Active processing details")
        onClicked: root.detailsExpanded = checked
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
                Button {
                    objectName: "invertEnabled"
                    text: qsTr("Invert")
                    checkable: true
                    checked: root.processing.invertEnabled
                    enabled: root.processing.available
                    Layout.alignment: Qt.AlignLeft
                    Accessible.description: checked ? qsTr("Invert is on") : qsTr("Invert is off")
                    onClicked: root.processing.setInvertEnabled(checked)
                }
                Label {
                    objectName: "activeProcessing"
                    text: root.processing.activeSummary
                    visible: root.detailsExpanded && root.processing.hasAcknowledged
                    Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
                }
            }
        }
    }
}
