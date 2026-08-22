import QtQuick
import omapixel

// The frames of the open clip, in a row, and what you do with them.
//
// Each thumbnail is the real frame drawn small, not an icon: the question a
// timeline answers is "is the movement right?", and it only answers that if you
// can see the movement in it.
Item {
    id: line

    implicitHeight: 96
    height: implicitHeight

    Rectangle { anchors.fill: parent; color: theme.panel }

    Row {
        id: controls
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 12
        spacing: 8

        Chip { label: win.playing ? "❚❚" : "▶"; on: win.playing
               onClicked: win.playing = !win.playing }
        Chip { label: "+ frame";   onClicked: doc.addFrame(false) }
        Chip { label: "duplicate"; onClicked: doc.addFrame(true) }
        Chip { label: "◂"; usable: doc.frame > 0
               onClicked: doc.moveFrame(-1) }
        Chip { label: "▸"; usable: doc.frame < doc.frameCount - 1
               onClicked: doc.moveFrame(1) }
        Chip { label: "delete"; usable: doc.frameCount > 1; role: theme.urgent
               onClicked: doc.removeFrame() }

        Rectangle { anchors.verticalCenter: parent.verticalCenter
                    width: 1; height: 24; color: theme.fill(theme.foreground, 0.18) }

        Label { anchors.verticalCenter: parent.verticalCenter
                text: doc.fps + " fps" }
        Chip { label: "−"; onClicked: doc.setFps(doc.fps - 1) }
        Chip { label: "+"; onClicked: doc.setFps(doc.fps + 1) }
    }

    Flickable {
        anchors.left: controls.right
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        height: 76
        contentWidth: strip.width
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Row {
            id: strip
            spacing: 6

            Repeater {
                model: doc.frameCount

                Rectangle {
                    id: cellBox
                    required property int index

                    width: 60
                    height: 76
                    radius: theme.rounding
                    color: index === doc.frame ? theme.sunken : "transparent"
                    border.width: 1
                    border.color: index === doc.frame ? theme.accent : theme.fill(theme.foreground, 0.18)

                    PixelGridItem {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -6
                        model: doc
                        clip: doc.clip
                        frame: cellBox.index
                        cell: Math.min(48 / doc.columns, 48 / doc.rows)
                    }

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 4
                        text: String(cellBox.index + 1)
                        quiet: cellBox.index !== doc.frame
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: doc.frame = cellBox.index
                    }
                }
            }
        }
    }
}
