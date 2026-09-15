import QtQuick
import QtQuick.Controls

// Synthetic installation reference only. Actual camera orientation and frame
// storage remain in the C++ processing pipeline.
Item {
    id: root
    property bool flipHorizontal: false
    property bool flipVertical: false
    property int rotationIndex: 0
    implicitWidth: 200
    implicitHeight: 140
    clip: true
    Item {
        id: rotatedReference
        anchors.centerIn: parent
        width: 192
        height: 128
        rotation: root.rotationIndex * 90
        scale: Math.min(root.width / (root.rotationIndex % 2 ? height : width),
                        root.height / (root.rotationIndex % 2 ? width : height))
        Rectangle {
            anchors.fill: parent
            color: "#18232e"
            border.color: "#d5e4ee"
            transform: Scale {
                origin.x: 96
                origin.y: 64
                xScale: root.flipHorizontal ? -1 : 1
                yScale: root.flipVertical ? -1 : 1
            }
            Rectangle { objectName: "referenceTopLeft"; x: 4; y: 4; width: 24; height: 24; color: "#ef6262" }
            Rectangle { objectName: "referenceTopRight"; x: 164; y: 4; width: 24; height: 24; color: "#64d18a" }
            Rectangle { objectName: "referenceBottomLeft"; x: 4; y: 100; width: 24; height: 24; color: "#68a7ee" }
            Rectangle { objectName: "referenceBottomRight"; x: 164; y: 100; width: 24; height: 24; color: "#f0cd60" }
            Label { anchors.horizontalCenter: parent.horizontalCenter; y: 8; text: qsTr("TOP ↑"); color: "white"; font.bold: true }
            Label { anchors.centerIn: parent; text: qsTr("L → RIGHT"); color: "white"; font.bold: true; font.pixelSize: 17 }
            Label { anchors.horizontalCenter: parent.horizontalCenter; y: 103; text: qsTr("BOTTOM"); color: "white" }
        }
    }
}
