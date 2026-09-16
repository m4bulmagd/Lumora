import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property string title
    required property string disclosureName
    required property string optionsName
    required property string enabledItemName
    required property bool effectEnabled
    property bool controlsAvailable: true
    property bool expanded: false
    default property alias contentData: body.data
    signal enabledRequested(bool enabled)
    signal collapsing()

    Layout.fillWidth: true
    spacing: Theme.spacingSm

    data: [
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusSm
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.borderSubtle

        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacingXs

            ToolButton {
                id: disclosure
                objectName: root.disclosureName
                checkable: true
                checked: root.expanded
                text: checked ? qsTr("▾") : qsTr("▸")
                implicitWidth: Theme.controlHeight
                implicitHeight: Theme.controlHeight
                enabled: root.controlsAvailable
                focusPolicy: Qt.TabFocus
                Accessible.name: qsTr("%1 controls").arg(root.title)
                Accessible.description: checked ? qsTr("Collapse controls") : qsTr("Expand controls")
                onPressedChanged: if (pressed && checked) root.collapsing()
                onClicked: {
                    if (!checked) {
                        options.closeMenu()
                    }
                    root.expanded = checked
                }
            }
            Label {
                text: root.title
                Layout.fillWidth: true
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
                text: qsTr("Off")
                visible: !root.effectEnabled
                color: Theme.amber
                font.pixelSize: 11
                font.bold: true
            }
            EffectMenu {
                id: options
                objectName: root.optionsName
                effectTitle: root.title
                effectEnabled: root.effectEnabled
                enabledItemName: root.enabledItemName
                enabled: root.controlsAvailable
                onOpening: root.collapsing()
                onEnabledRequested: enabled => root.enabledRequested(enabled)
            }
        }
    },
    ColumnLayout {
        id: body
        visible: root.expanded
        enabled: root.controlsAvailable
        Layout.fillWidth: true
        Layout.leftMargin: Theme.spacingXs
        Layout.rightMargin: Theme.spacingXs
        spacing: Theme.spacingSm
    }
    ]
}
