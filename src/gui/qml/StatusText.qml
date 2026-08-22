import QtQuick

// One fact on the status line.
Text {
    property bool alarm: false
    property bool wide: false

    color: alarm ? theme.urgent : theme.dim
    font.family: theme.fontFamily
    font.pixelSize: 11
    elide: Text.ElideRight
    width: wide ? Math.min(implicitWidth, 320) : implicitWidth
}
