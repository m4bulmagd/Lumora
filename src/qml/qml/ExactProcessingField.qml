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
    property bool sliderVisible: true
    property bool wholeNumbers: false
    property string helpText: ""
    spacing: Theme.spacingXs
    signal textCommitted(string text)
    signal valueCommitted(real value)
    signal valueDragged(real value)
    signal valueReleased()

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        Label {
            text: root.label
            color: Theme.textSecondary
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        TextField {
            id: field
            objectName: root.fieldName
            property bool modified: false
            Layout.preferredWidth: activeFocus ? 122 : 82
            Layout.minimumWidth: 68
            implicitHeight: 28
            horizontalAlignment: TextInput.AlignRight
            leftPadding: Theme.spacingSm
            rightPadding: Theme.spacingSm
            topPadding: 3
            bottomPadding: 3
            hoverEnabled: true
            selectByMouse: true
            Accessible.name: qsTr("%1 value").arg(root.label)
            Accessible.description: (root.wholeNumbers ? qsTr("Whole numbers from %1 to %2.")
                : qsTr("Range: %1 to %2")).arg(root.minimum).arg(root.maximum)
                + (root.helpText.length > 0 ? " " + root.helpText : "")
            inputMethodHints: root.wholeNumbers ? Qt.ImhDigitsOnly : Qt.ImhFormattedNumbersOnly
            onTextEdited: modified = true
            onEditingFinished: {
                if (modified) {
                    const entered = text
                    modified = false
                    root.textCommitted(entered)
                }
            }
            background: Rectangle {
                radius: Theme.radiusSm
                color: field.activeFocus || field.hovered ? Theme.surfaceRaised : "transparent"
                border.width: field.activeFocus ? 1 : (field.hovered ? 1 : 0)
                border.color: field.activeFocus ? Theme.amber : Theme.border
            }
            color: activeFocus ? Theme.textPrimary : "transparent"
            Label {
                anchors.fill: parent
                leftPadding: field.leftPadding
                rightPadding: field.rightPadding
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                visible: !field.activeFocus
                color: field.enabled ? Theme.textPrimary : Theme.textMuted
                text: {
                    const exact = Number(root.value)
                    const shortened = root.wholeNumbers
                        ? Math.round(exact) : Number(exact.toPrecision(6))
                    return (shortened !== exact ? qsTr("≈") : "") + shortened.toString()
                }
                elide: Text.ElideRight
            }
            ToolTip.visible: hovered && !activeFocus
            ToolTip.text: root.formattedValue
                + (root.helpText.length > 0 ? "\n" + root.helpText : "")
            Binding {
                target: field
                property: "text"
                value: root.formattedValue
                when: !field.activeFocus || !field.modified
                restoreMode: Binding.RestoreNone
            }
        }
    }
    Slider {
        id: slider
        property bool gestureCancelled: false
        objectName: root.sliderName
        visible: root.sliderVisible
        Layout.fillWidth: true
        implicitHeight: 28
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
        function onDraftReplaced() {
            field.modified = false
            field.text = root.formattedValue
            if (slider.pressed) slider.gestureCancelled = true
        }
    }
}
