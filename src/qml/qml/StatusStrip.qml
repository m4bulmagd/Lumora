import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property CameraAdapter camera
    required property ViewerAdapter viewer
    required property ProcessingAdapter processing
    spacing: Theme.spacingXs
    Label {
        Layout.fillWidth: true
        text: root.camera.status + "  ·  " + root.viewer.playbackState + "  ·  " + root.viewer.freshness
        color: root.viewer.freshness === "Stale" ? Theme.amber : Theme.textPrimary
        wrapMode: Text.Wrap
        font.bold: true
    }
    Label {
        Layout.fillWidth: true
        text: root.viewer.orientation + (root.viewer.hasFrame ? qsTr("  ·  %1  ·  Age %2 ms").arg(root.viewer.frameTimestamp).arg(root.viewer.frameAgeMs) : "")
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        font.pixelSize: 12
    }
    Label { text: root.camera.error; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.amber }
    Label { text: root.viewer.error; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.amber }
    RowLayout {
        visible: root.processing.fallback || root.processing.processingError.length > 0
        Layout.fillWidth: true
        Label { text: root.processing.processingError || qsTr("Enhanced processing unavailable. Original remains live."); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.amber }
        Button { objectName: "retryProcessingButton"; text: root.processing.retryPending ? qsTr("Retrying…") : qsTr("Retry processing"); enabled: root.processing.retryEnabled; onClicked: root.processing.retry() }
    }
}
