pragma Singleton

import QtQuick

QtObject {
    readonly property color canvas: "#111412"
    readonly property color surface: "#171b18"
    readonly property color surfaceRaised: "#1d221e"
    readonly property color imageWell: "#090b0a"
    readonly property color border: "#343a35"
    readonly property color borderSubtle: "#252a27"

    readonly property color textPrimary: "#edf0eb"
    readonly property color textSecondary: "#aab1aa"
    readonly property color textMuted: "#747c75"

    readonly property color amber: "#d5a449"
    readonly property color amberSoft: "#3b3020"
    readonly property color green: "#78a87b"
    readonly property color greenSoft: "#203027"

    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 14
    readonly property int spacingLg: 22
    readonly property int radiusSm: 3
    readonly property int radiusMd: 6
    readonly property int controlHeight: 34
    readonly property real disabledControlOpacity: 0.42
}
