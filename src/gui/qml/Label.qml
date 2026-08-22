import QtQuick

Text {
    property bool quiet: true
    color: quiet ? theme.dim : theme.foreground
    font.family: theme.fontFamily
    font.pixelSize: quiet ? 10 : 11
}
