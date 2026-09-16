import QtQuick
import QtQuick.Controls

ToolButton {
    id: root
    required property string effectTitle
    required property bool effectEnabled
    required property string enabledItemName
    signal enabledRequested(bool enabled)
    signal opening()

    text: qsTr("⋯")
    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.TabFocus
    Accessible.name: qsTr("%1 options").arg(root.effectTitle)
    Accessible.description: root.effectEnabled
        ? qsTr("Open options, including temporary bypass.")
        : qsTr("Open options to enable this effect.")
    onPressedChanged: if (pressed) root.opening()
    onClicked: optionsMenu.open()

    Menu {
        id: optionsMenu
        y: root.height
        MenuItem {
            objectName: root.enabledItemName
            text: root.effectEnabled ? qsTr("Bypass temporarily") : qsTr("Enable effect")
            Accessible.name: text
            onTriggered: root.enabledRequested(!root.effectEnabled)
        }
    }

    function closeMenu() { optionsMenu.close() }
}
