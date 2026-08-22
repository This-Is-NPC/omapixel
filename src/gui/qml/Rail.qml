import QtQuick

// A column of controls that scrolls on its own. In a short tiled window the
// column does not fit, and cutting a control off is worse than scrolling.
Rectangle {
    id: rail

    default property alias content: stack.data

    color: theme.panel

    Flickable {
        anchors.fill: parent
        contentHeight: stack.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: stack
            width: rail.width
            padding: 14
            spacing: 12
        }
    }
}
