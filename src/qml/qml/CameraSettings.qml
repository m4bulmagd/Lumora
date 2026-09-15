import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property CameraSettingsAdapter settings
    property Item focusedControl: null
    objectName: "cameraSettingsDialog"
    parent: Overlay.overlay
    title: qsTr("Camera settings")
    modal: false
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    visible: settings.open
    width: Math.min(520, parent.width - 242)
    height: Math.min(570, parent.height - 70)
    x: parent.width - width - Theme.spacingMd
    y: 56
    onClosed: settings.closeSettings()
    onOpened: tabs.currentIndex = 0

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
        Label {
            text: root.settings.sourceSummary
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textSecondary
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            onCurrentIndexChanged: scroll.contentY = 0
            TabButton { objectName: "cameraExposureTab"; text: qsTr("Exposure / Gain") }
            TabButton { objectName: "cameraAcquisitionTab"; text: qsTr("FPS / Image region") }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            Flickable {
                id: scroll
                objectName: "cameraSettingsFlickable"
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
                    ColumnLayout {
                        visible: tabs.currentIndex === 0
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: Theme.spacingSm
                Label { text: qsTr("Exposure mode"); color: Theme.textSecondary }
                Label { text: qsTr("Exposure (µs)"); color: Theme.textSecondary }
                ComboBox {
                    objectName: "cameraExposureMode"
                    Layout.preferredWidth: 145
                    model: root.settings.exposureModes
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: indexOfValue(root.settings.exposureMode)
                    displayText: currentIndex < 0 ? qsTr("Unavailable") : currentText
                    enabled: root.settings.exposureModeEnabled
                    Accessible.name: qsTr("Exposure mode")
                    onActivated: root.settings.setExposureMode(currentValue)
                    onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                }
                TextField {
                    objectName: "cameraExposureValue"
                    Layout.fillWidth: true
                    text: root.settings.exposureText
                    enabled: root.settings.exposureValueEnabled
                    selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    Accessible.name: qsTr("Exposure in microseconds")
                    Accessible.description: root.settings.exposureRange
                    onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                    onTextEdited: root.settings.editExposureText(text)
                }
            }
            Label {
                text: root.settings.exposureRange
                visible: text.length > 0
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "cameraExposureReason"
                text: root.settings.exposureReason
                visible: text.length > 0
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: Theme.spacingSm
                Label { text: qsTr("Gain mode"); color: Theme.textSecondary }
                Label { text: qsTr("Gain (dB)"); color: Theme.textSecondary }
                ComboBox {
                    objectName: "cameraGainMode"
                    Layout.preferredWidth: 145
                    model: root.settings.gainModes
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: indexOfValue(root.settings.gainMode)
                    displayText: currentIndex < 0 ? qsTr("Unavailable") : currentText
                    enabled: root.settings.gainModeEnabled
                    Accessible.name: qsTr("Gain mode")
                    onActivated: root.settings.setGainMode(currentValue)
                    onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                }
                TextField {
                    objectName: "cameraGainValue"
                    Layout.fillWidth: true
                    text: root.settings.gainText
                    enabled: root.settings.gainValueEnabled
                    selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    Accessible.name: qsTr("Gain in decibels")
                    Accessible.description: root.settings.gainRange
                    onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                    onTextEdited: root.settings.editGainText(text)
                }
            }
            Label {
                text: root.settings.gainRange
                visible: text.length > 0
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "cameraGainReason"
                text: root.settings.gainReason
                visible: text.length > 0
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
                    }
                    ColumnLayout {
                        visible: tabs.currentIndex === 1
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        Label { text: qsTr("Acquisition frame rate (FPS)"); color: Theme.textSecondary }
                        TextField {
                            objectName: "cameraFrameRateValue"
                            Layout.fillWidth: true
                            text: root.settings.frameRateText
                            enabled: root.settings.frameRateEnabled
                            selectByMouse: true
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            Accessible.name: qsTr("Acquisition frames per second")
                            Accessible.description: root.settings.frameRateRange
                            onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                            onTextEdited: root.settings.editFrameRateText(text)
                        }
                        Label {
                            text: root.settings.frameRateRange
                            visible: text.length > 0
                            color: Theme.textSecondary
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Label {
                            text: root.settings.frameRateReason
                            visible: text.length > 0
                            color: Theme.textSecondary
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Pixel format"); color: Theme.textSecondary }
                            ComboBox {
                                objectName: "cameraPixelFormat"
                                Layout.fillWidth: true
                                model: root.settings.pixelFormats
                                textRole: "text"
                                valueRole: "value"
                                currentIndex: indexOfValue(root.settings.pixelFormat)
                                displayText: currentIndex < 0 ? qsTr("Unavailable") : currentText
                                enabled: root.settings.pixelFormatEnabled
                                Accessible.name: qsTr("Pixel format")
                                onActivated: root.settings.setPixelFormat(currentValue)
                                onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                            }
                        }
                        Label {
                            text: root.settings.pixelFormatReason
                            visible: text.length > 0
                            color: Theme.textSecondary
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Label { text: qsTr("Image region (sensor pixels)"); color: Theme.textSecondary }
                        GridLayout {
                            columns: 2
                            Layout.fillWidth: true
                            columnSpacing: Theme.spacingSm
                            Label { text: qsTr("X offset"); color: Theme.textSecondary }
                            Label { text: qsTr("Y offset"); color: Theme.textSecondary }
                            TextField {
                                objectName: "cameraRoiX"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                text: root.settings.roiXText
                                enabled: root.settings.roiEnabled
                                selectByMouse: true
                                inputMethodHints: Qt.ImhDigitsOnly
                                Accessible.name: qsTr("Image region x offset")
                                Accessible.description: root.settings.roiRange
                                onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                                onTextEdited: root.settings.editRoiText("x", text)
                            }
                            TextField {
                                objectName: "cameraRoiY"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                text: root.settings.roiYText
                                enabled: root.settings.roiEnabled
                                selectByMouse: true
                                inputMethodHints: Qt.ImhDigitsOnly
                                Accessible.name: qsTr("Image region y offset")
                                Accessible.description: root.settings.roiRange
                                onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                                onTextEdited: root.settings.editRoiText("y", text)
                            }
                            Label { text: qsTr("Width"); color: Theme.textSecondary }
                            Label { text: qsTr("Height"); color: Theme.textSecondary }
                            TextField {
                                objectName: "cameraRoiWidth"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                text: root.settings.roiWidthText
                                enabled: root.settings.roiEnabled
                                selectByMouse: true
                                inputMethodHints: Qt.ImhDigitsOnly
                                Accessible.name: qsTr("Image region width")
                                Accessible.description: root.settings.roiRange
                                onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                                onTextEdited: root.settings.editRoiText("width", text)
                            }
                            TextField {
                                objectName: "cameraRoiHeight"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                text: root.settings.roiHeightText
                                enabled: root.settings.roiEnabled
                                selectByMouse: true
                                inputMethodHints: Qt.ImhDigitsOnly
                                Accessible.name: qsTr("Image region height")
                                Accessible.description: root.settings.roiRange
                                onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealControl, this)
                                onTextEdited: root.settings.editRoiText("height", text)
                            }
                        }
                        Label {
                            text: root.settings.roiRange
                            visible: text.length > 0
                            color: Theme.textSecondary
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Label {
                            text: root.settings.roiReason
                            visible: text.length > 0
                            color: Theme.amber
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
            Label { text: qsTr("CURRENT READBACK"); font.bold: true; font.pixelSize: 11; color: Theme.textSecondary }
            Label {
                objectName: "cameraSettingsActual"
                text: root.settings.currentSummary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "cameraSettingsStatus"
                text: root.settings.status
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: root.settings.editable ? Theme.textSecondary : Theme.amber
            }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "applyCameraSettingsButton"
            text: qsTr("Apply settings")
            enabled: root.settings.applyEnabled
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: root.settings.apply()
        }
        Button {
            objectName: "closeCameraSettingsButton"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.settings.closeSettings()
        }
    }
}
