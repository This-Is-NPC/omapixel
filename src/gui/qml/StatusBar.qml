import QtQuick

// The line along the bottom: what is true right now.
//
// Size, cursor, slot, zoom, frame count, whether it is saved. All of it used to
// be scattered through the panels, or nowhere -- the cursor position sat in the
// right rail under "hover the drawing", which is the last place anybody looks
// while drawing. Facts belong on one line that never moves.
Rectangle {
    id: bar

    property int column: -1
    property int row: -1

    height: 26
    color: theme.panel

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14

        StatusText { text: T.t("status.canvasSize").arg(doc.columns).arg(doc.rows) }
        // The colour being drawn with, as a colour. The letter alone tells you
        // which slot and nothing about what it will look like, which is the
        // thing you actually want to know before you press a key.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 12
                height: 12
                radius: theme.rounding
                color: (doc.paletteRevision,
                       win.slot === "." ? "transparent" : doc.colourOf(win.slot))
                border.width: 1
                border.color: theme.fill(theme.foreground, 0.35)

                Text {
                    anchors.centerIn: parent
                    visible: win.slot === "."
                    text: T.t("status.emptyMarker")
                    color: theme.dim
                    font.family: theme.fontFamily
                    font.pixelSize: 9
                }
            }

            StatusText {
                anchors.verticalCenter: parent.verticalCenter
                text: win.awaitingSlot ? T.t("status.pressLetter")
                                       : (win.slot === "." ? T.t("status.emptySlot")
                                                           : win.slot)
                alarm: win.awaitingSlot
            }
        }

        StatusText {
            // The keyboard cursor wins when it is on: it is the one the keys
            // act on, and the mouse may be nowhere near the drawing.
            text: win.caretColumn >= 0
                  ? T.t("status.caret").arg(win.caretColumn).arg(win.caretRow)
                  : (bar.column < 0 ? T.t("status.noCursor")
                                    : T.t("status.coordinates").arg(bar.column).arg(bar.row))
            alarm: false
        }
        StatusText {
            visible: doc.hasSelection
            text: T.t("status.selection")
                   .arg(doc.selectionWidth).arg(doc.selectionHeight)
                   .arg(doc.selectionCount)
            alarm: true
        }
        StatusText { text: win.zoomLabel() }
        StatusText {
            text: T.t(doc.frameCount === 1 ? "status.oneFrame" : "status.frames")
                      .arg(doc.clip).arg(doc.frameCount).arg(doc.fps)
        }
        StatusText {
            text: T.t("status.layer").arg(doc.clip).arg(doc.frame + 1)
                      .arg(doc.activeLayerName)
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14

        StatusText { text: doc.note; wide: true }
        StatusText {
            text: doc.dirty ? T.t("status.unsaved")
                            : (doc.path === "" ? T.t("status.new") : T.t("status.saved"))
            alarm: doc.dirty
        }
    }
}
