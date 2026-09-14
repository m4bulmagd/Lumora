import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    required property ProcessingAdapter processing
    clip: true
    contentWidth: availableWidth
    ColumnLayout {
        width: root.availableWidth
        spacing: Theme.spacingSm
        Label { text: qsTr("PROCESSING"); color: Theme.textSecondary; font.bold: true; font.pixelSize: 11 }
        Label { text: root.processing.draftPresetName || qsTr("Window / level"); font.pixelSize: 18; Layout.fillWidth: true; wrapMode: Text.Wrap }
        CheckBox { text: qsTr("Window / level"); checked: root.processing.stageEnabled; enabled: root.processing.available; onClicked: root.processing.setStageEnabled(checked) }
        Label { text: qsTr("Window"); color: Theme.textSecondary }
        TextField {
            id: windowField
            objectName: "windowField"
            Layout.fillWidth: true
            enabled: root.processing.available
            Accessible.name: qsTr("Window value")
            selectByMouse: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            onEditingFinished: root.processing.commitWindowText(text)
            Binding { target: windowField; property: "text"; value: root.processing.windowText; when: !windowField.activeFocus; restoreMode: Binding.RestoreBindingOrValue }
        }
        Slider {
            id: windowSlider
            objectName: "windowSlider"
            Layout.fillWidth: true
            enabled: root.processing.available
            Accessible.name: qsTr("Window")
            from: root.processing.windowMinimum; to: root.processing.windowMaximum
            value: root.processing.window
            onMoved: { if (pressed) root.processing.dragWindow(value); else root.processing.commitWindow(value) }
            onPressedChanged: { if (!pressed) root.processing.releaseWindow(value) }
        }
        Label { text: qsTr("Level"); color: Theme.textSecondary }
        TextField {
            id: levelField
            objectName: "levelField"
            Layout.fillWidth: true
            enabled: root.processing.available
            Accessible.name: qsTr("Level value")
            selectByMouse: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            onEditingFinished: root.processing.commitLevelText(text)
            Binding { target: levelField; property: "text"; value: root.processing.levelText; when: !levelField.activeFocus; restoreMode: Binding.RestoreBindingOrValue }
        }
        Slider {
            id: levelSlider
            objectName: "levelSlider"
            Layout.fillWidth: true
            enabled: root.processing.available
            Accessible.name: qsTr("Level")
            from: root.processing.levelMinimum; to: root.processing.levelMaximum
            value: root.processing.level
            onMoved: { if (pressed) root.processing.dragLevel(value); else root.processing.commitLevel(value) }
            onPressedChanged: { if (!pressed) root.processing.releaseLevel(value) }
        }
        Label { text: !root.processing.available ? (root.processing.loadingState === ProcessingAdapter.Loading ? qsTr("Loading processing settings…") : qsTr("Processing settings unavailable.")) : (root.processing.pending ? qsTr("Applying processing draft…") : qsTr("Edits save after successful activation.")); color: Theme.textSecondary; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        Label { text: qsTr("ACKNOWLEDGED ACTIVE"); color: Theme.textSecondary; font.bold: true; font.pixelSize: 11 }
        Label { objectName: "activeProcessing"; text: root.processing.activeSummary; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 }
        Label {
            Layout.fillWidth: true
            text: [root.processing.loadError, root.processing.validationError, root.processing.modelError, root.processing.persistenceWarning].filter(message => message.length > 0).join("\n")
            visible: text.length > 0; wrapMode: Text.Wrap; color: Theme.amber
        }
    }
}
