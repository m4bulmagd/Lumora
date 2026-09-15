import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flow {
    id: root
    required property ViewerAdapter viewer
    required property CameraAdapter camera
    property bool showPriorityActions: false
    spacing: Theme.spacingXs
    Button { objectName: "compactStopButton"; text: qsTr("Stop"); visible: root.showPriorityActions && (root.camera.stopVisible || (root.camera.pending && root.camera.stopEnabled)); enabled: root.camera.stopEnabled; onClicked: root.camera.stopLive() }
    Button { objectName: "compactDisconnectButton"; text: qsTr("Disconnect"); visible: root.showPriorityActions && root.camera.disconnectVisible; enabled: root.camera.disconnectEnabled; onClicked: root.camera.disconnectCamera() }
    Button { objectName: "originalButton"; text: qsTr("Original"); checkable: true; checked: root.viewer.displayMode === "original"; enabled: root.viewer.originalAvailable; onClicked: root.viewer.setDisplayMode("original") }
    Button { objectName: "enhancedButton"; text: qsTr("Enhanced"); checkable: true; checked: root.viewer.displayMode === "enhanced"; enabled: root.viewer.enhancedAvailable; onClicked: root.viewer.setDisplayMode("enhanced") }
    Button { objectName: "compareButton"; text: qsTr("Compare"); checkable: true; checked: root.viewer.displayMode === "compare"; enabled: root.viewer.compareAvailable; onClicked: root.viewer.setDisplayMode("compare") }
    Button { objectName: "pauseButton"; text: root.viewer.playbackState === "Pausing" ? qsTr("Pausing…") : qsTr("Pause"); visible: root.viewer.playbackState !== "Paused"; enabled: root.viewer.hasFrame && root.viewer.playbackState !== "Pausing"; onClicked: root.viewer.pause() }
    Button { objectName: "resumeButton"; text: qsTr("Resume"); visible: root.viewer.playbackState === "Paused"; onClicked: root.viewer.resume() }
    Button { objectName: "fitButton"; text: qsTr("Fit"); enabled: root.viewer.hasFrame; onClicked: root.viewer.fit() }
    Button { objectName: "actualPixelsButton"; text: qsTr("100%"); enabled: root.viewer.hasFrame; onClicked: root.viewer.actualPixels() }
}
