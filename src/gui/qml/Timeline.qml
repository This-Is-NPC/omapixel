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
    objectName: "timeline"

    signal commandRequested(string commandId)

    function focusFirst() { playButton.forceActiveFocus() }

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

        Label { anchors.verticalCenter: parent.verticalCenter; text: T.t("timeline.clip") }

        Repeater {
            model: doc.clipNames

            Chip {
                required property var modelData
                required property int index
                label: modelData
                on: modelData === doc.clip
                onClicked: line.commandRequested("timeline.clip." + index)
            }
        }

        Chip { label: T.t("timeline.addClip"); onClicked: line.commandRequested("timeline.addClip") }
        Chip {
            label: T.t("timeline.removeClip")
            usable: doc.clipNames.length > 1
            role: theme.urgent
            onClicked: line.commandRequested("timeline.removeClip")
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

        Label { anchors.verticalCenter: parent.verticalCenter; text: T.t("timeline.fps").arg(doc.fps) }
        Chip { label: T.t("timeline.decreaseFps"); onClicked: line.commandRequested("timeline.fpsDown") }
        Chip { label: T.t("timeline.increaseFps"); onClicked: line.commandRequested("timeline.fpsUp") }
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
            id: playButton
            objectName: "timelineFirstControl"
            label: win.playing ? T.t("timeline.pause") : T.t("timeline.play")
            on: win.playing
            usable: doc.frameCount > 1
            onClicked: line.commandRequested("timeline.play")
        }
        Chip { label: T.t("timeline.addFrame"); onClicked: line.commandRequested("timeline.addFrame") }
        Chip { label: T.t("timeline.duplicate"); onClicked: line.commandRequested("timeline.duplicateFrame") }
        Chip { label: T.t("timeline.previousFrame"); usable: doc.frame > 0; onClicked: line.commandRequested("timeline.moveBack") }
        Chip { label: T.t("timeline.nextFrame"); usable: doc.frame < doc.frameCount - 1
               onClicked: line.commandRequested("timeline.moveOn") }
        Chip { label: T.t("timeline.delete"); usable: doc.frameCount > 1; role: theme.urgent
               onClicked: line.commandRequested("timeline.deleteFrame") }
    }

    ListView {
        anchors.left: transport.right
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.top: clips.bottom
        anchors.topMargin: 8
        height: 72
        orientation: ListView.Horizontal
        spacing: 6
        model: doc.frameCount
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        delegate: Rectangle {
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
                onClicked: line.commandRequested("timeline.frame." + cellBox.index)
            }
        }
    }
}
