import QtQuick
import QtQuick.Window
import omapixel

// The studio.
//
// Layout and input, and nothing else. Every rule -- what a resize does, whether
// a clip may lose its last frame, what a slot with no colour means -- lives in
// the core, and this file only calls it. Where the QML prototype held the
// document, this holds a reference to one.
//
//   left    the clips and the palette
//   middle  the open frame, large, with the reference and the onion skin
//   right   size, file, reference, and the sprite at true size
//   bottom  the timeline of the open clip
Window {
    id: win

    width: 1280
    height: 800
    visible: true
    title: (doc.dirty ? "• " : "") + (doc.path === "" ? "untitled" : doc.path)
           + " — omapixel"
    color: theme.background

    // `theme` is a context property from C++ that follows omarchy's active
    // theme, live. There is no palette declared in this file on purpose: a
    // window that hard-codes its colours is a window that stops matching the
    // desktop the first time somebody runs `omarchy theme set`.

    property string tool: "pencil"
    property string slot: "I"
    property real zoom: 12
    property bool onion: false
    property bool mesh: true
    property bool playing: false

    property string referencePath: ""
    property real referenceAlpha: 0.5
    property bool referenceOnTop: false

    // The size the controls point at, which only becomes the document's size
    // when somebody confirms. Without that middleman, typing "6" on the way to
    // "64" resizes the document to six columns.
    property int wantColumns: doc.columns
    property int wantRows: doc.rows
    readonly property bool sizeChanged: wantColumns !== doc.columns
                                        || wantRows !== doc.rows
    readonly property int wouldLose: sizeChanged
                                     ? doc.wouldLose(wantColumns, wantRows) : 0

    Connections {
        target: doc
        function onChanged() {
            win.wantColumns = doc.columns
            win.wantRows = doc.rows
        }
    }

    Timer {
        interval: Math.max(16, Math.round(1000 / doc.fps))
        running: win.playing && doc.frameCount > 1
        repeat: true
        onTriggered: doc.frame = (doc.frame + 1) % doc.frameCount
    }

    Item {
        anchors.fill: parent
        focus: true

        Keys.onPressed: function (event) {
            switch (event.key) {
            case Qt.Key_B: win.tool = "pencil"; break
            case Qt.Key_E: win.tool = "eraser"; break
            case Qt.Key_F: win.tool = "bucket"; break
            case Qt.Key_I: win.tool = "picker"; break
            case Qt.Key_H: win.tool = "hand"; break
            case Qt.Key_O: win.onion = !win.onion; break
            case Qt.Key_M: win.mesh = !win.mesh; break
            case Qt.Key_Space: win.playing = !win.playing; break
            case Qt.Key_Left: doc.frame = Math.max(0, doc.frame - 1); break
            case Qt.Key_Right:
                doc.frame = Math.min(doc.frameCount - 1, doc.frame + 1); break
            case Qt.Key_Plus:
            case Qt.Key_Equal: win.zoom = Math.min(40, win.zoom + 2); break
            case Qt.Key_Minus: win.zoom = Math.max(2, win.zoom - 2); break
            case Qt.Key_S:
                if (event.modifiers & Qt.ControlModifier) doc.save()
                break
            case Qt.Key_Z:
                if (event.modifiers & Qt.ControlModifier) {
                    // Ctrl+Shift+Z as well as Ctrl+Y: both are in people's
                    // fingers, and which one depends on where they came from.
                    if (event.modifiers & Qt.ShiftModifier) doc.redo()
                    else doc.undo()
                }
                break
            case Qt.Key_Y:
                if (event.modifiers & Qt.ControlModifier) doc.redo()
                break
            case Qt.Key_Q: Qt.quit(); break
            default: event.accepted = false; return
            }
            event.accepted = true
        }

        Column {
            anchors.fill: parent
            spacing: 0

            // ------------------------------------------------------- the head

            Rectangle {
                id: head
                width: parent.width
                // Grows instead of overflowing. The controls used to sit in a
                // Row anchored right: in a narrow window the ones at the far
                // end simply left the glass, and undo was among them.
                height: Math.max(52, tools.height + 18)
                color: theme.panel

                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 14
                    spacing: 10

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "omapixel"
                        color: theme.foreground
                        font.family: theme.fontFamily
                        font.bold: true
                        font.pixelSize: 13
                    }
                    Label { text: doc.note }
                }

                Flow {
                    id: tools
                    anchors.right: parent.right
                    anchors.left: parent.left
                    anchors.leftMargin: 250
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 14
                    spacing: 8
                    layoutDirection: Qt.RightToLeft

                    Chip { label: "save"; on: doc.dirty
                           usable: doc.path !== "" || pathField.value !== ""
                           onClicked: doc.save(pathField.value) }

                    Rectangle { width: 1; height: 22; color: theme.fill(theme.foreground, 0.18) }

                    Chip { label: "↷ redo"; usable: doc.canRedo; onClicked: doc.redo() }
                    Chip { label: "↶ undo"; usable: doc.canUndo; onClicked: doc.undo() }

                    Rectangle { width: 1; height: 22; color: theme.fill(theme.foreground, 0.18) }

                    Chip { label: "+"; usable: win.zoom < 40
                           onClicked: stage.zoomAt(stage.width / 2, stage.height / 2,
                                                   win.zoom + (win.zoom >= 16 ? 4 : 1)) }
                    Chip { label: win.zoom + "×  fit"
                           onClicked: { stage.touched = false; stage.fit() } }
                    Chip { label: "−"; usable: win.zoom > 1
                           onClicked: stage.zoomAt(stage.width / 2, stage.height / 2,
                                                   win.zoom - (win.zoom > 16 ? 4 : 1)) }

                    Rectangle { width: 1; height: 22; color: theme.fill(theme.foreground, 0.18) }

                    Chip { label: "m mesh";  on: win.mesh
                           onClicked: win.mesh = !win.mesh }
                    Chip { label: "o onion"; on: win.onion
                           onClicked: win.onion = !win.onion }

                    Rectangle { width: 1; height: 22; color: theme.fill(theme.foreground, 0.18) }

                    Chip { label: "h hand";    on: win.tool === "hand"
                           onClicked: win.tool = "hand" }
                    Chip { label: "i picker";  on: win.tool === "picker"
                           onClicked: win.tool = "picker" }
                    Chip { label: "f bucket";  on: win.tool === "bucket"
                           onClicked: win.tool = "bucket" }
                    Chip { label: "e eraser";  on: win.tool === "eraser"
                           onClicked: win.tool = "eraser" }
                    Chip { label: "b pencil";  on: win.tool === "pencil"
                           onClicked: win.tool = "pencil" }
                }
            }

            Rectangle { width: parent.width; height: 1; color: theme.fill(theme.foreground, 0.18) }

            // ------------------------------------------------------- the body

            Row {
                width: parent.width
                height: parent.height - head.height - timeline.height - 2
                spacing: 0

                Rail {
                    id: leftRail
                    width: 232
                    height: parent.height

                    Label { text: "clips"; quiet: false }

                    Repeater {
                        model: doc.clipNames

                        Rectangle {
                            id: clipRow
                            required property var modelData
                            width: 204
                            height: 26
                            radius: theme.rounding
                            color: modelData === doc.clip ? theme.fill(theme.accent, 0.18)
                                              : theme.fill(theme.foreground, 0.04)
                            border.width: 1
                            border.color: modelData === doc.clip
                                          ? theme.accent
                                          : theme.fill(theme.foreground, 0.14)

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: clipRow.modelData
                                color: theme.foreground
                                font.family: theme.fontFamily
                                font.pixelSize: 11
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: doc.clip = clipRow.modelData
                            }
                        }
                    }

                    Row {
                        spacing: 6
                        Chip {
                            label: "+ clip"
                            onClicked: {
                                var n = 1
                                while (doc.clipNames.indexOf("clip" + n) >= 0) n++
                                doc.addClip("clip" + n)
                            }
                        }
                        Chip {
                            label: "delete"
                            usable: doc.clipNames.length > 1
                            role: theme.urgent
                            onClicked: doc.removeClip(doc.clip)
                        }
                    }

                    Field {
                        label: "clip name"
                        value: doc.clip
                        onCommitted: function (text) { doc.renameClip(doc.clip, text) }
                    }

                    Rectangle { width: 204; height: 1; color: theme.fill(theme.foreground, 0.18) }

                    Label { text: "palette"; quiet: false }

                    Flow {
                        width: 204
                        spacing: 4

                        Repeater {
                            model: [{ slot: ".", colour: "" }].concat(doc.palette)

                            Rectangle {
                                id: pip
                                required property var modelData
                                width: 26
                                height: 26
                                radius: theme.rounding
                                color: modelData.slot === "." ? "transparent"
                                                              : modelData.colour
                                border.width: win.slot === modelData.slot ? 2 : 1
                                border.color: win.slot === modelData.slot
                                              ? theme.accent
                                              : theme.fill(theme.foreground, 0.18)

                                // The empty slot draws as an X: a transparent
                                // square is a square the colour of the
                                // background, and nobody guesses that is the
                                // eraser.
                                Text {
                                    anchors.centerIn: parent
                                    text: pip.modelData.slot === "." ? "×" : pip.modelData.slot
                                    color: pip.modelData.slot === "." ? theme.dim : theme.background
                                    font.family: theme.fontFamily
                                    font.pixelSize: pip.modelData.slot === "." ? 14 : 9
                                    opacity: pip.modelData.slot === "." ? 1 : 0.55
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: win.slot = pip.modelData.slot
                                }
                            }
                        }
                    }

                    Field {
                        visible: win.slot !== "."
                        label: "colour of slot " + win.slot
                        value: {
                            for (var i = 0; i < doc.palette.length; i++)
                                if (doc.palette[i].slot === win.slot)
                                    return doc.palette[i].colour
                            return ""
                        }
                        onEdited: function (text) {
                            if (/^#[0-9a-fA-F]{6}$/.test(text))
                                doc.setPaletteColour(win.slot, text)
                        }
                    }
                }

                Rectangle { width: 1; height: parent.height; color: theme.fill(theme.foreground, 0.18) }

                Surface {
                    id: stage
                    objectName: "stage"
                    width: parent.width - leftRail.width - rightRail.width - 2
                    height: parent.height
                }

                Rectangle { width: 1; height: parent.height; color: theme.fill(theme.foreground, 0.18) }

                Rail {
                    id: rightRail
                    width: 232
                    height: parent.height

                    Label { text: "size"; quiet: false }

                    Label {
                        text: doc.columns + " × " + doc.rows
                              + (win.sizeChanged ? "  →  " + win.wantColumns
                                                   + " × " + win.wantRows : "")
                        quiet: !win.sizeChanged
                    }

                    Flow {
                        width: 204
                        spacing: 5
                        Repeater {
                            model: doc.sizePresets()
                            Chip {
                                required property var modelData
                                label: modelData.w + "×" + modelData.h
                                on: win.wantColumns === modelData.w
                                    && win.wantRows === modelData.h
                                onClicked: {
                                    win.wantColumns = modelData.w
                                    win.wantRows = modelData.h
                                }
                            }
                        }
                    }

                    Row {
                        spacing: 6
                        Field {
                            label: "columns"
                            boxWidth: 92
                            value: String(win.wantColumns)
                            onEdited: function (text) {
                                var n = parseInt(text, 10)
                                if (isFinite(n) && n > 0) win.wantColumns = Math.min(512, n)
                            }
                        }
                        Field {
                            label: "rows"
                            boxWidth: 92
                            value: String(win.wantRows)
                            onEdited: function (text) {
                                var n = parseInt(text, 10)
                                if (isFinite(n) && n > 0) win.wantRows = Math.min(512, n)
                            }
                        }
                    }

                    // Shrinking crops, and cropping has no undo yet. So the
                    // button says what it will cost before it is pressed, rather
                    // than asking afterwards -- nobody reads a dialog, everybody
                    // reads a number sitting on the button.
                    Label {
                        width: 204
                        wrapMode: Text.Wrap
                        visible: win.wouldLose > 0
                        color: theme.urgent
                        text: "resizing crops " + win.wouldLose + " drawn pixel(s)"
                    }

                    Row {
                        spacing: 6
                        Chip {
                            label: "resize"
                            on: win.sizeChanged
                            usable: win.sizeChanged
                            role: win.wouldLose > 0 ? theme.urgent : theme.accent
                            onClicked: doc.resize(win.wantColumns, win.wantRows)
                        }
                        Chip {
                            label: "new"
                            onClicked: doc.reset(win.wantColumns, win.wantRows)
                        }
                    }

                    Rectangle { width: 204; height: 1; color: theme.fill(theme.foreground, 0.18) }

                    Label { text: "file"; quiet: false }

                    Field {
                        id: pathField
                        label: "path"
                        value: doc.path
                        onEdited: function (text) { doc.path = text }
                    }

                    Chip {
                        label: "open"
                        usable: pathField.value !== ""
                        onClicked: doc.open(pathField.value)
                    }

                    Rectangle { width: 204; height: 1; color: theme.fill(theme.foreground, 0.18) }

                    Label { text: "reference"; quiet: false }

                    Field {
                        label: "path to an image"
                        value: win.referencePath
                        onEdited: function (text) { win.referencePath = text }
                    }

                    Flow {
                        width: 204
                        spacing: 5
                        Repeater {
                            model: [0, 25, 50, 75, 100]
                            Chip {
                                required property int modelData
                                label: modelData + "%"
                                on: Math.round(win.referenceAlpha * 100) === modelData
                                onClicked: win.referenceAlpha = modelData / 100
                            }
                        }
                    }

                    Chip {
                        label: win.referenceOnTop ? "on top" : "behind"
                        on: win.referenceOnTop
                        onClicked: win.referenceOnTop = !win.referenceOnTop
                    }

                    Rectangle { width: 204; height: 1; color: theme.fill(theme.foreground, 0.18) }

                    Label { text: "true size"; quiet: false }

                    // The sprite at the size it will live at, not the size it is
                    // comfortable to draw at. A pixel judged at 12× is not the
                    // pixel anybody sees.
                    // A PixelGridItem sizes itself from the document, so at
                    // 160x90 the 3x preview is 480 wide. Centred in a fixed box
                    // with no clip, it painted straight over the rest of the
                    // rail. Each preview now takes the size it actually needs,
                    // and a scale that will not fit the rail is not offered --
                    // a cropped centre of a sprite is not "true size", it is a
                    // different picture.
                    Flow {
                        width: 204
                        spacing: 10
                        Repeater {
                            model: [1, 2, 3]
                            Column {
                                id: real
                                required property int modelData
                                visible: doc.columns * modelData <= 204
                                         && doc.rows * modelData <= 180
                                spacing: 3
                                Rectangle {
                                    id: tile
                                    width: Math.max(30, doc.columns * real.modelData + 8)
                                    height: Math.max(30, doc.rows * real.modelData + 8)
                                    radius: theme.rounding
                                    color: theme.sunken
                                    clip: true

                                    PixelGridItem {
                                        id: mini
                                        anchors.centerIn: parent
                                        model: doc
                                        clip: doc.clip
                                        frame: doc.frame
                                        cell: real.modelData
                                    }

                                    // The 1x tile doubles as the overview: it
                                    // already shows the whole drawing, so the
                                    // frame of what is on screen and a click to
                                    // go there cost one rectangle each. Zooming
                                    // without a way back to the rest of the
                                    // picture is a one-way trip.
                                    readonly property bool isMap: real.modelData === 1
                                                                  && (stage.scrollsX || stage.scrollsY)

                                    Rectangle {
                                        visible: tile.isMap
                                        color: theme.fill(theme.accent, 0.14)
                                        border.width: 1
                                        border.color: theme.accent
                                        x: mini.x + Math.max(0, stage.viewColumn)
                                        y: mini.y + Math.max(0, stage.viewRow)
                                        width: Math.min(mini.width - (x - mini.x), stage.viewColumns)
                                        height: Math.min(mini.height - (y - mini.y), stage.viewRows)
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: tile.isMap
                                        cursorShape: tile.isMap ? Qt.PointingHandCursor
                                                                : Qt.ArrowCursor
                                        onPressed: function (mouse) {
                                            stage.centreOn(mouse.x - mini.x, mouse.y - mini.y)
                                        }
                                        onPositionChanged: function (mouse) {
                                            if (pressed)
                                                stage.centreOn(mouse.x - mini.x, mouse.y - mini.y)
                                        }
                                    }
                                }
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: real.modelData + "×"
                                }
                            }
                        }
                    }

                    Label {
                        width: 204
                        wrapMode: Text.Wrap
                        visible: doc.columns > 204 || doc.rows > 180
                        text: "too wide to show at true size"
                    }

                    Rectangle { width: 204; height: 1; color: theme.fill(theme.foreground, 0.18) }

                    Label {
                        width: 204
                        wrapMode: Text.Wrap
                        text: stage.hoverColumn < 0
                              ? "hover the drawing"
                              : "column " + stage.hoverColumn + ", row " + stage.hoverRow
                                + "  ·  slot " + doc.slotAt(stage.hoverColumn,
                                                            stage.hoverRow)
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: theme.fill(theme.foreground, 0.18) }

            Timeline {
                id: timeline
                width: parent.width
            }
        }
    }
}
