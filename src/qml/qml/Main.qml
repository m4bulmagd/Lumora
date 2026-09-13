import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    objectName: "workstationWindow"

    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("Lumora — Interface preview")
    color: Theme.canvas

    palette.window: Theme.canvas
    palette.windowText: Theme.textPrimary
    palette.base: Theme.imageWell
    palette.alternateBase: Theme.surfaceRaised
    palette.text: Theme.textPrimary
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.green
    palette.highlightedText: Theme.canvas
    palette.mid: Theme.border
    palette.placeholderText: Theme.textMuted

    Dialog {
        id: previewDetailsDialog
        objectName: "previewDetailsDialog"
        anchors.centerIn: parent
        width: Math.min(480, root.width - 48)
        modal: true
        focus: true
        title: qsTr("About this preview")
        standardButtons: Dialog.Close

        contentItem: ColumnLayout {
            spacing: Theme.spacingMd
            Accessible.name: qsTr("Interface preview details")

            Label {
                Layout.fillWidth: true
                text: qsTr("This checkpoint previews Lumora's QML theme and workstation layout.")
                color: Theme.textPrimary
                font.pixelSize: 16
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("No camera, image pipeline, processing, or saved preferences are connected. Disabled controls show the intended interface hierarchy only.")
                color: Theme.textSecondary
                font.pixelSize: 13
                lineHeight: 1.35
                wrapMode: Text.WordWrap
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 29
            color: Theme.amberSoft
            border.color: "#5b492b"
            border.width: 1

            Label {
                id: evaluationBanner
                objectName: "evaluationBanner"
                anchors.centerIn: parent
                text: qsTr("EVALUATION — NOT FOR CLINICAL USE")
                color: "#f0cf8c"
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 1.4
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 63
            color: Theme.surface
            border.color: Theme.borderSubtle
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLg
                anchors.rightMargin: Theme.spacingMd
                spacing: Theme.spacingMd

                ColumnLayout {
                    spacing: 0

                    Label {
                        text: qsTr("LUMORA")
                        color: Theme.textPrimary
                        font.pixelSize: 19
                        font.weight: Font.DemiBold
                        font.letterSpacing: 2.1
                    }

                    Label {
                        text: qsTr("IMAGING WORKSTATION")
                        color: Theme.textMuted
                        font.pixelSize: 9
                        font.letterSpacing: 1.25
                    }
                }

                Rectangle {
                    Layout.leftMargin: Theme.spacingSm
                    implicitWidth: previewBadgeRow.implicitWidth + 20
                    implicitHeight: 26
                    radius: Theme.radiusSm
                    color: Theme.greenSoft
                    border.color: "#3c5843"

                    RowLayout {
                        id: previewBadgeRow
                        anchors.centerIn: parent
                        spacing: 7

                        Rectangle {
                            implicitWidth: 7
                            implicitHeight: 7
                            radius: 4
                            color: Theme.green
                        }

                        Label {
                            id: previewLabel
                            objectName: "previewLabel"
                            text: qsTr("Interface preview")
                            color: "#bad8bd"
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Label {
                    visible: root.width >= 1080
                    text: qsTr("Foundation checkpoint")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }

                Button {
                    id: previewDetailsButton
                    objectName: "previewDetailsButton"
                    text: qsTr("Preview details")
                    enabled: true
                    activeFocusOnTab: true
                    palette.button: Theme.greenSoft
                    palette.buttonText: "#dbe9dc"
                    palette.highlight: Theme.green
                    Accessible.name: qsTr("Open interface preview details")
                    onClicked: previewDetailsDialog.open()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingMd
            spacing: Theme.spacingMd

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: root.width < 1080 ? 184 : 212
                color: Theme.surface
                border.color: Theme.borderSubtle
                radius: Theme.radiusMd

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingSm

                    Label {
                        text: qsTr("SOURCE")
                        color: Theme.textMuted
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.35
                    }

                    ComboBox {
                        objectName: "sourceSelector"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.controlHeight
                        model: [qsTr("No source")]
                        enabled: false
                        opacity: Theme.disabledControlOpacity
                        Accessible.name: qsTr("Image source; unavailable in interface preview")
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Camera discovery begins in a later checkpoint.")
                        color: Theme.textMuted
                        font.pixelSize: 11
                        lineHeight: 1.25
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingMd
                        Layout.preferredHeight: 1
                        color: Theme.borderSubtle
                    }

                    Label {
                        Layout.topMargin: Theme.spacingSm
                        text: qsTr("SESSION")
                        color: Theme.textMuted
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.35
                    }

                    Label {
                        text: qsTr("Not connected")
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Button {
                            objectName: "connectButton"
                            Layout.fillWidth: true
                            text: qsTr("Connect")
                            enabled: false
                            opacity: Theme.disabledControlOpacity
                            Accessible.name: qsTr("Connect; unavailable in interface preview")
                        }

                        Button {
                            objectName: "startButton"
                            Layout.fillWidth: true
                            text: qsTr("Start")
                            enabled: false
                            opacity: Theme.disabledControlOpacity
                            Accessible.name: qsTr("Start; unavailable in interface preview")
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: sourceNote.implicitHeight + 20
                        color: Theme.imageWell
                        border.color: Theme.borderSubtle
                        radius: Theme.radiusSm

                        Label {
                            id: sourceNote
                            anchors.fill: parent
                            anchors.margins: 10
                            text: qsTr("Preview only\nNo camera commands are sent")
                            color: Theme.textMuted
                            font.pixelSize: 11
                            lineHeight: 1.35
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacingSm

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 43
                    color: Theme.surface
                    border.color: Theme.borderSubtle
                    radius: Theme.radiusSm

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSm
                        anchors.rightMargin: Theme.spacingSm
                        spacing: 5

                        Button {
                            text: qsTr("Original")
                            enabled: false
                            opacity: Theme.disabledControlOpacity
                            Accessible.name: qsTr("Original view; unavailable in interface preview")
                        }
                        Button {
                            text: qsTr("Enhanced")
                            enabled: false
                            opacity: Theme.disabledControlOpacity
                            Accessible.name: qsTr("Enhanced view; unavailable in interface preview")
                        }
                        Button {
                            text: qsTr("Compare")
                            enabled: false
                            opacity: Theme.disabledControlOpacity
                            Accessible.name: qsTr("Compare view; unavailable in interface preview")
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: qsTr("Fit")
                            enabled: false
                            opacity: Theme.disabledControlOpacity
                            Accessible.name: qsTr("Fit image; unavailable while waiting")
                        }
                        Button {
                            visible: root.width >= 1040
                            text: qsTr("100%")
                            enabled: false
                            opacity: Theme.disabledControlOpacity
                            Accessible.name: qsTr("Actual image size; unavailable while waiting")
                        }
                    }
                }

                Item {
                    id: imageArea
                    objectName: "imageArea"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Accessible.role: Accessible.Pane
                    Accessible.name: qsTr("Image area, waiting for image")

                    Rectangle {
                        anchors.fill: parent
                        color: Theme.imageWell
                        border.color: Theme.border
                        radius: Theme.radiusSm
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 10
                        color: "transparent"
                        border.color: "#171c18"
                        border.width: 1
                    }

                    Item {
                        anchors.centerIn: parent
                        width: Math.min(320, parent.width - 48)
                        height: waitingColumn.implicitHeight

                        ColumnLayout {
                            id: waitingColumn
                            anchors.fill: parent
                            spacing: Theme.spacingSm

                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                implicitWidth: 36
                                implicitHeight: 36
                                radius: 18
                                color: Theme.amberSoft
                                border.color: "#665130"

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: Theme.amber
                                }
                            }

                            Label {
                                id: waitingLabel
                                objectName: "waitingLabel"
                                Layout.fillWidth: true
                                text: qsTr("Waiting for image")
                                color: Theme.textPrimary
                                font.pixelSize: 20
                                font.weight: Font.Medium
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("The image pipeline is intentionally disconnected in this interface preview.")
                                color: Theme.textMuted
                                font.pixelSize: 12
                                lineHeight: 1.3
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: Theme.spacingMd
                        width: 34
                        height: 1
                        color: Theme.border
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: Theme.spacingMd
                        width: 1
                        height: 34
                        color: Theme.border
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: Theme.spacingMd
                        width: 34
                        height: 1
                        color: Theme.border
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: Theme.spacingMd
                        width: 1
                        height: 34
                        color: Theme.border
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 31
                    color: Theme.surface
                    border.color: Theme.borderSubtle
                    radius: Theme.radiusSm

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMd
                        anchors.rightMargin: Theme.spacingMd

                        Label {
                            text: qsTr("PREVIEW")
                            color: Theme.green
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.1
                        }

                        Label {
                            text: qsTr("No active session")
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            text: qsTr("Image unavailable")
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: root.width < 1080 ? 214 : 250
                color: Theme.surface
                border.color: Theme.borderSubtle
                radius: Theme.radiusMd

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingSm

                    Label {
                        text: qsTr("ADJUSTMENTS")
                        color: Theme.textMuted
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.35
                    }

                    Label {
                        text: qsTr("Window")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    Slider {
                        objectName: "windowSlider"
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: 50
                        enabled: false
                        opacity: Theme.disabledControlOpacity
                        Accessible.name: qsTr("Window; unavailable in interface preview")
                    }

                    Label {
                        text: qsTr("Level")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    Slider {
                        objectName: "levelSlider"
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: 50
                        enabled: false
                        opacity: Theme.disabledControlOpacity
                        Accessible.name: qsTr("Level; unavailable in interface preview")
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingMd
                        Layout.preferredHeight: 1
                        color: Theme.borderSubtle
                    }

                    Label {
                        Layout.topMargin: Theme.spacingSm
                        text: qsTr("PROCESSING")
                        color: Theme.textMuted
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.35
                    }

                    CheckBox {
                        objectName: "detailProcessingCheckBox"
                        text: qsTr("Detail processing")
                        enabled: false
                        opacity: Theme.disabledControlOpacity
                        Accessible.name: qsTr("Detail processing; unavailable in interface preview")
                    }

                    CheckBox {
                        objectName: "noiseReductionCheckBox"
                        text: qsTr("Noise reduction")
                        enabled: false
                        opacity: Theme.disabledControlOpacity
                        Accessible.name: qsTr("Noise reduction; unavailable in interface preview")
                    }

                    Button {
                        objectName: "advancedProcessingButton"
                        Layout.fillWidth: true
                        text: qsTr("Advanced parameters")
                        enabled: false
                        opacity: Theme.disabledControlOpacity
                        Accessible.name: qsTr("Advanced parameters; unavailable in interface preview")
                    }

                    Item { Layout.fillHeight: true }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Processing controls become available when the simulator pipeline is integrated.")
                        color: Theme.textMuted
                        font.pixelSize: 11
                        lineHeight: 1.3
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
