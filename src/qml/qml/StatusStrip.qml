import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property CameraAdapter camera
    required property ViewerAdapter viewer
    required property ProcessingAdapter processing
    property bool diagnosticsVisible: false
    property bool showEditorMessages: false
    readonly property string cameraMessage: root.camera.error === root.camera.warning
        ? root.camera.error
        : [root.camera.error, root.camera.warning]
            .filter(message => message.length > 0).join("\n")
    readonly property bool frameDetailsRequired: root.viewer.hasFrame
        && (root.diagnosticsVisible || root.viewer.playbackState === "Paused"
            || root.viewer.freshness === "Stale")
    spacing: Theme.spacingXs
    Label {
        objectName: "acquisitionStatus"
        Layout.fillWidth: true
        text: qsTr("Camera: %1 · View: %2")
            .arg(root.camera.status).arg(root.viewer.playbackState)
            + (root.camera.pending ? qsTr(" · operation pending") : "")
            + (root.viewer.freshness === "Current"
                ? "" : qsTr(" · %1").arg(root.viewer.freshness))
        color: root.viewer.freshness === "Stale" || root.cameraMessage.length > 0
            ? Theme.amber : Theme.textPrimary
        wrapMode: Text.Wrap
        font.bold: true
    }
    Label {
        objectName: "frameStatus"
        Layout.fillWidth: true
        text: root.viewer.orientation + (root.frameDetailsRequired
            ? qsTr("  ·  %1  ·  Age %2 ms")
                .arg(root.viewer.frameTimestamp).arg(root.viewer.frameAgeMs)
            : "")
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        font.pixelSize: 12
    }
    Label {
        objectName: "cameraError"
        text: root.cameraMessage
        visible: text.length > 0
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.amber
    }
    Label { objectName: "viewerError"; text: root.viewer.error; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.amber }
    Label {
        objectName: "persistentProcessingMessages"
        Layout.fillWidth: true
        text: [root.processing.loadError, root.processing.validationError, root.processing.modelError, root.processing.persistenceWarning].filter(message => message.length > 0).join("\n")
        visible: root.showEditorMessages && text.length > 0; wrapMode: Text.Wrap; color: Theme.amber; font.pixelSize: 12
    }
    RowLayout {
        visible: root.processing.fallback || root.processing.processingError.length > 0
        Layout.fillWidth: true
        Label { objectName: "processingFailure"; text: root.processing.processingError || qsTr("Enhanced processing unavailable. Original remains live."); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.amber }
        Button { objectName: "retryProcessingButton"; text: root.processing.retryPending ? qsTr("Retrying…") : qsTr("Retry processing"); enabled: root.processing.retryEnabled; onClicked: root.processing.retry() }
    }
    Label {
        objectName: "frameDiagnostics"
        visible: root.diagnosticsVisible
        text: qsTr("Source frame: %1 · View: %2").arg(root.viewer.hasFrame ? root.viewer.sourceFrameId : qsTr("none")).arg(root.viewer.displayMode)
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.textSecondary
        font.pixelSize: 12
    }
}
