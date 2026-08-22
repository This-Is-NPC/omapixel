import QtQuick

// The key hints along the bottom, the way a terminal program does it.
//
// Keys in inverse video, the name of what they do beside them. It is the oldest
// affordance there is and it survives because it works: the keys are on screen
// while you use them, so nobody has to remember a list or go looking in a menu
// for something they will do forty times an hour.
//
// The list changes with what is happening. A hint bar that shows the same
// twelve keys whatever you are doing is wallpaper; one that answers the
// question in front of you is a manual you never have to open.
Rectangle {
    id: hints

    property var model: []

    height: 24
    // The panel colour, like the rails and the timeline. `sunken` is the well
    // the drawing sits in, and a strip of it along the bottom read as a hole in
    // the window rather than as part of it.
    color: theme.panel

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        Repeater {
            model: hints.model

            Row {
                id: pair
                required property var modelData
                spacing: 5

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: cap.implicitWidth + 8
                    height: 16
                    radius: theme.rounding
                    color: theme.fill(theme.foreground, 0.16)

                    Text {
                        id: cap
                        anchors.centerIn: parent
                        text: pair.modelData.key
                        color: theme.foreground
                        font.family: theme.fontFamily
                        font.pixelSize: 10
                        font.bold: true
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: pair.modelData.label
                    color: theme.dim
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                }
            }
        }
    }
}
