import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property VideoSourcesAdapter sources: null
    objectName: "videoSourcesDialog"
    parent: Overlay.overlay
    title: qsTr("Network sources")
    modal: true
    focus: true
    width: Math.min(540, parent ? parent.width - 32 : 540)
    height: Math.min(680, parent ? parent.height - 48 : 680)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    standardButtons: Dialog.Close
    onClosed: { username.clear(); password.clear() }

    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: Theme.spacingSm
            Label {
                text: qsTr("Add an RTSP camera, then select it in Video source. Connect, review and confirm before starting Live.")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textSecondary
            }
            Label { text: qsTr("Saved sources"); font.bold: true }
            ComboBox {
                id: saved
                objectName: "savedNetworkSources"
                model: root.sources ? root.sources.sources : []
                textRole: "name"
                valueRole: "id"
                Layout.fillWidth: true
                Accessible.name: qsTr("Saved network sources")
                displayText: count > 0 ? currentText : qsTr("No network sources added")
                onActivated: { username.clear(); password.clear() }
            }
            Label {
                text: root.sources && saved.currentIndex >= 0 && saved.currentIndex < root.sources.sources.length
                    ? root.sources.sources[saved.currentIndex].url : ""
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: Theme.textSecondary
            }
            Button {
                objectName: "removeNetworkSource"
                text: qsTr("Remove selected source")
                enabled: root.sources && root.sources.editable && saved.currentIndex >= 0
                onClicked: root.sources.removeSource(saved.currentValue)
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
            Label { text: qsTr("Add a source"); font.bold: true }
            TextField {
                id: sourceName
                objectName: "networkSourceName"
                placeholderText: qsTr("Source name")
                placeholderTextColor: Theme.textSecondary
                Accessible.name: qsTr("Network source name")
                Layout.fillWidth: true
                maximumLength: 128
                enabled: root.sources && root.sources.editable
                selectByMouse: true
            }
            TextField {
                id: address
                objectName: "networkSourceAddress"
                placeholderText: qsTr("rtsp://camera-address/stream")
                placeholderTextColor: Theme.textSecondary
                Accessible.name: qsTr("RTSP stream address")
                Layout.fillWidth: true
                maximumLength: 4096
                enabled: root.sources && root.sources.editable
                selectByMouse: true
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
            }
            Label {
                text: qsTr("Use a non-secret address. Enter any username and password below; credentials last only until Lumora closes.")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textSecondary
            }
            TextField {
                id: username
                objectName: "networkSourceUsername"
                placeholderText: qsTr("Username (optional)")
                placeholderTextColor: Theme.textSecondary
                Accessible.name: qsTr("Camera username for this session")
                Layout.fillWidth: true
                maximumLength: 1024
                enabled: root.sources && root.sources.editable
                selectByMouse: true
                inputMethodHints: Qt.ImhNoPredictiveText
            }
            TextField {
                id: password
                objectName: "networkSourcePassword"
                placeholderText: qsTr("Password (optional)")
                placeholderTextColor: Theme.textSecondary
                Accessible.name: qsTr("Camera password for this session")
                Layout.fillWidth: true
                maximumLength: 1024
                enabled: root.sources && root.sources.editable
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    objectName: "addNetworkSource"
                    text: qsTr("Add source")
                    highlighted: true
                    enabled: root.sources && root.sources.editable && sourceName.text.trim().length > 0 && address.text.trim().length > 0
                    onClicked: if (root.sources.addSource(sourceName.text, address.text, username.text, password.text)) {
                        sourceName.clear(); address.clear(); username.clear(); password.clear()
                    }
                }
                Button {
                    objectName: "setNetworkCredentials"
                    text: qsTr("Use credentials for selected")
                    enabled: root.sources && root.sources.editable && saved.currentIndex >= 0
                    onClicked: if (root.sources.setCredentials(saved.currentValue, username.text, password.text)) {
                        username.clear(); password.clear()
                    }
                }
            }
            Label {
                text: root.sources && !root.sources.editable ? qsTr("Disconnect the current source to make changes.") : ""
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textSecondary
            }
            Label {
                objectName: "networkSourceError"
                text: root.sources ? root.sources.error : ""
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.amber
            }
        }
    }
}
