import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property InstallationAdapter settings
    property Item focusedControl: null
    objectName: "installationSettingsDialog"
    parent: Overlay.overlay
    title: qsTr("Installation orientation")
    modal: false
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    visible: settings.open
    width: Math.min(560, parent.width - 242)
    height: Math.min(680, parent.height - 70)
    x: parent.width - width - Theme.spacingMd
    y: 56
    onClosed: settings.closeSettings()

    function revealControl(control: Item) {
        if (!control || !control.activeFocus) return
        root.focusedControl = control
        const rect = control.mapToItem(fields, 0, 0, control.width, control.height)
        let position = scroll.contentY
        if (rect.y < position) position = rect.y
        else if (rect.y + rect.height > position + scroll.height)
            position = rect.y + rect.height - scroll.height
        scroll.contentY = Math.max(0, Math.min(position, scroll.contentHeight - scroll.height))
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingSm
        Label { text: root.settings.sourceSummary; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            Flickable {
                id: scroll
                objectName: "installationSettingsFlickable"
                contentWidth: width
                contentHeight: fields.implicitHeight
                onHeightChanged: Qt.callLater(root.revealControl, root.focusedControl)
                onContentHeightChanged: Qt.callLater(root.revealControl, root.focusedControl)
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                ColumnLayout {
                    id: fields
                    width: scroll.width
                    spacing: Theme.spacingSm
                    Label { text: qsTr("ACTIVE ORIENTATION"); font.bold: true; font.pixelSize: 11; color: Theme.textSecondary }
                    Label { objectName: "installationActive"; text: root.settings.activeSummary; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Label { text: qsTr("SAVED INSTALLATION PROFILE"); font.bold: true; font.pixelSize: 11; color: Theme.textSecondary }
                    Label { objectName: "installationSaved"; text: root.settings.savedSummary; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                    RowLayout {
                        Layout.fillWidth: true
                        CheckBox {
                            objectName: "installationFlipHorizontal"
                            text: qsTr("Flip horizontal")
                            checked: root.settings.flipHorizontal
                            enabled: root.settings.editable
                            onClicked: root.settings.setFlipHorizontal(checked)
                            onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                        }
                        CheckBox {
                            objectName: "installationFlipVertical"
                            text: qsTr("Flip vertical")
                            checked: root.settings.flipVertical
                            enabled: root.settings.editable
                            onClicked: root.settings.setFlipVertical(checked)
                            onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Clockwise rotation"); color: Theme.textSecondary }
                        ComboBox {
                            objectName: "installationRotation"
                            Layout.fillWidth: true
                            model: [qsTr("0°"), qsTr("90°"), qsTr("180°"), qsTr("270°")]
                            currentIndex: root.settings.rotationIndex
                            enabled: root.settings.editable
                            Accessible.name: qsTr("Clockwise installation rotation")
                            onActivated: root.settings.setRotation(currentIndex)
                            onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                        }
                    }
                    Label { text: qsTr("REFERENCE PREVIEW · flips, then rotation"); color: Theme.textSecondary; font.pixelSize: 11 }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Original"); color: Theme.textSecondary }
                            OrientationPreview {
                                objectName: "installationOriginalPreview"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 140
                                flipHorizontal: root.settings.flipHorizontal
                                flipVertical: root.settings.flipVertical
                                rotationIndex: root.settings.rotationIndex
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Enhanced"); color: Theme.textSecondary }
                            OrientationPreview {
                                objectName: "installationEnhancedPreview"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 140
                                flipHorizontal: root.settings.flipHorizontal
                                flipVertical: root.settings.flipVertical
                                rotationIndex: root.settings.rotationIndex
                            }
                        }
                    }
                    CheckBox {
                        objectName: "installationConfirmation"
                        Layout.fillWidth: true
                        text: qsTr("I confirm this installation orientation")
                        checked: root.settings.confirmationChecked
                        enabled: root.settings.editable
                        onClicked: root.settings.setConfirmation(checked)
                        onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                    }
                    CheckBox {
                        objectName: "installationRepair"
                        Layout.fillWidth: true
                        text: qsTr("Replace the invalid installation profile file")
                        visible: root.settings.repairVisible
                        checked: root.settings.repairChecked
                        enabled: root.settings.editable
                        onClicked: root.settings.setRepairConsent(checked)
                        onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                    }
                }
            }
        }
        Label {
            objectName: "installationStatus"
            text: root.settings.status
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.amber
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "saveInstallationButton"
            text: qsTr("Save installation")
            enabled: root.settings.saveEnabled
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: root.settings.save()
        }
        Button {
            objectName: "closeInstallationButton"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.settings.closeSettings()
        }
    }
}
