import QtQuick
import omapixel

// The bottom of the window: which clip, and which frame of it.
//
// Two rows, because they answer two different questions. The top row picks the
// clip, renames it and sets how fast it plays -- properties of the whole
// sequence. The bottom row is the sequence itself, frame by frame, with the
// transport beside it. When the two were mixed, "delete" sat next to "+ clip"
// and could plausibly have meant either.
//
// Each thumbnail is the real frame drawn small, not an icon: the question a
// timeline answers is "is the movement right?", and it only answers that if you
// can see the movement in it.
Item {
    id: line

    implicitHeight: 128
    height: implicitHeight

    Rectangle { anchors.fill: parent; color: theme.panel }

    // --------------------------------------------------------------- the clip

    Row {
        id: clips
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 8
        spacing: 6

        Label { anchors.verticalCenter: parent.verticalCenter; text: "clip" }

        Repeater {
            model: doc.clipNames

            Chip {
                required property var modelData
                label: modelData
                on: modelData === doc.clip
                onClicked: doc.clip = modelData
            }
        }

        Chip { label: "+"; onClicked: doc.addClip("clip " + (doc.clipNames.length + 1)) }
        Chip {
            label: "−"
            usable: doc.clipNames.length > 1
            role: theme.urgent
            onClicked: doc.removeClip(doc.clip)
        }

        Rectangle { anchors.verticalCenter: parent.verticalCenter
                    width: 1; height: 20; color: theme.fill(theme.foreground, 0.18) }

        Field {
            onEscaped: win.focusCanvas()
            label: ""
            boxWidth: 150
            value: doc.clip
            onCommitted: function (text) { doc.renameClip(doc.clip, text) }
        }

        Rectangle { anchors.verticalCenter: parent.verticalCenter
                    width: 1; height: 20; color: theme.fill(theme.foreground, 0.18) }

        Label { anchors.verticalCenter: parent.verticalCenter; text: doc.fps + " fps" }
        Chip { label: "−"; onClicked: doc.setFps(doc.fps - 1) }
        Chip { label: "+"; onClicked: doc.setFps(doc.fps + 1) }
    }

    // ------------------------------------------------------------- the frames

    Row {
        id: transport
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.top: clips.bottom
        anchors.topMargin: 12
        spacing: 6

        Chip {
            label: win.playing ? "❚❚" : "▶"
            on: win.playing
            usable: doc.frameCount > 1
            onClicked: win.playing = !win.playing
        }
        Chip { label: "+ frame"; onClicked: doc.addFrame(false) }
        Chip { label: "duplicate"; onClicked: doc.addFrame(true) }
        Chip { label: "◂"; usable: doc.frame > 0; onClicked: doc.moveFrame(-1) }
        Chip { label: "▸"; usable: doc.frame < doc.frameCount - 1
               onClicked: doc.moveFrame(1) }
        Chip { label: "delete"; usable: doc.frameCount > 1; role: theme.urgent
               onClicked: doc.removeFrame() }
    }

    Flickable {
        anchors.left: transport.right
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.top: clips.bottom
        anchors.topMargin: 8
        height: 72
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
                    height: 72
                    radius: theme.rounding
                    color: index === doc.frame ? theme.sunken : "transparent"
                    border.width: 1
                    border.color: index === doc.frame ? theme.accent
                                                      : theme.fill(theme.foreground, 0.18)

                    PixelGridItem {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -6
                        model: doc
                        clip: doc.clip
                        frame: cellBox.index
                        cell: Math.min(46 / doc.columns, 46 / doc.rows)
                    }

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 3
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
