import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property ProcessingAdapter processing
    spacing: Theme.spacingSm
    CheckBox {
        objectName: "windowLevelEnabled"
        text: qsTr("Window / level")
        Accessible.name: text
        checked: root.processing.stageEnabled
        enabled: root.processing.available
        onClicked: root.processing.setStageEnabled(checked)
    }
    ExactProcessingField {
        processing: root.processing
        label: qsTr("Window")
        fieldName: "windowField"; sliderName: "windowSlider"
        formattedValue: root.processing.windowText
        value: root.processing.window
        minimum: root.processing.windowMinimum; maximum: root.processing.windowMaximum
        enabled: root.processing.available
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitWindowText(text)
        onValueCommitted: value => root.processing.commitWindow(value)
        onValueDragged: value => root.processing.dragWindow(value)
        onValueReleased: root.processing.releaseWindow(root.processing.window)
    }
    ExactProcessingField {
        processing: root.processing
        label: qsTr("Level")
        fieldName: "levelField"; sliderName: "levelSlider"
        formattedValue: root.processing.levelText
        value: root.processing.level
        minimum: root.processing.levelMinimum; maximum: root.processing.levelMaximum
        enabled: root.processing.available
        Layout.fillWidth: true
        onTextCommitted: text => root.processing.commitLevelText(text)
        onValueCommitted: value => root.processing.commitLevel(value)
        onValueDragged: value => root.processing.dragLevel(value)
        onValueReleased: root.processing.releaseLevel(root.processing.level)
    }
}
