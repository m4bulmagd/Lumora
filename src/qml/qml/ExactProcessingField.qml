import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property ProcessingAdapter processing
    required property string label
    required property string fieldName
    required property string sliderName
    required property string formattedValue
    required property real value
    required property real minimum
    required property real maximum
    property real sliderStep: 0
    spacing: Theme.spacingSm
    signal textCommitted(string text)
    signal valueCommitted(real value)
    signal valueDragged(real value)
    signal valueReleased()

    Label { text: root.label; color: Theme.textSecondary }
    TextField {
        id: field
        objectName: root.fieldName
        property bool modified: false
        Layout.fillWidth: true
        Accessible.name: qsTr("%1 value").arg(root.label)
        Accessible.description: qsTr("Range: %1 to %2").arg(root.minimum).arg(root.maximum)
        selectByMouse: true
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        onTextEdited: modified = true
        onEditingFinished: {
            if (modified) {
                const entered = text
                modified = false
                root.textCommitted(entered)
            }
        }
        Binding {
            target: field
            property: "text"
            value: root.formattedValue
            when: !field.activeFocus || !field.modified
            restoreMode: Binding.RestoreNone
        }
    }
    Slider {
        id: slider
        property bool gestureCancelled: false
        objectName: root.sliderName
        Layout.fillWidth: true
        Accessible.name: root.label
        Accessible.description: qsTr("Use numeric entry for exact values.")
        from: root.minimum
        to: root.maximum
        value: root.value
        stepSize: root.sliderStep
        snapMode: root.sliderStep > 0 ? Slider.SnapAlways : Slider.NoSnap
        live: true
        Keys.onPressed: event => {
            if (!pressed) gestureCancelled = false
            event.accepted = false
        }
        onMoved: {
            // A held pointer can emit moved again after preset/reset replaced
            // the draft. Keep the replacement visible until a fresh gesture.
            if (gestureCancelled) {
                value = Qt.binding(() => root.value)
                return
            }
            if (pressed) root.valueDragged(value)
            else root.valueCommitted(value)
        }
        onPressedChanged: {
            if (pressed) gestureCancelled = false
            else {
                root.valueReleased()
                if (gestureCancelled) value = Qt.binding(() => root.value)
            }
        }
    }
    Connections {
        target: root.processing
        // Cancel dirty input before replacement can disable a field and move
        // focus. The binding then receives the new authoritative model text.
        function onDraftReplaced() {
            field.modified = false
            if (slider.pressed) slider.gestureCancelled = true
        }
    }
}
