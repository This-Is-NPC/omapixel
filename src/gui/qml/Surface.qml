import QtQuick
import omapixel

// The drawing surface: the open frame, large, with what you draw on it and what
// you measure it against.
//
// Three layers in the same rectangle, and the order is the whole idea:
//
//   reference   the target, behind or on top, at adjustable opacity
//   onion       the previous frame, faded, to see the MOVEMENT
//   frame       what you are drawing
//
// Judging art against your memory of the target is how you get it wrong; having
// the target in the same rectangle is how you fix it.
Item {
    id: surface

    // `clip`, because the drawing is often bigger than the space for it. A
    // 160x90 document at 12x is 1920 wide; without this it painted straight
    // over both rails and the studio looked like it had lost its controls.
    clip: true

    /// Emitted when somebody puts the pointer down on the drawing. The window
    /// uses it to take the keyboard back.
    signal pressedOnCanvas()

    property int hoverColumn: -1
    property int hoverRow: -1

    // Where the drawing sits inside the viewport. Kept as an offset from the
    // centre rather than an absolute position, so a drawing smaller than the
    // viewport stays centred without any special case.
    property real panX: 0
    property real panY: 0
    // The requested position stays smooth while the displayed position snaps
    // to cell boundaries. Without the separate value, trackpad deltas smaller
    // than one cell would be rounded away one event at a time and never add up.
    property real requestedPanX: 0
    property real requestedPanY: 0

    /// Puts the whole drawing on screen. What you want on opening a file, and
    /// what you want after losing yourself at 40x.
    ///
    /// The size is worked out here rather than read from a bound property. A
    /// binding on width and height has not been re-evaluated yet when
    /// `onHeightChanged` runs, so fitting from one used the PREVIOUS size: the
    /// window would lay itself out correctly and the drawing would still open
    /// at 1x, having been fitted to a pane that no longer existed.
    function fit() {
        var byWidth = (width - 24) / Math.max(1, doc.columns)
        var byHeight = (height - 24) / Math.max(1, doc.rows)
        win.zoom = Math.max(0.01, Math.min(40, Math.min(byWidth, byHeight)))
        requestedPanX = 0
        requestedPanY = 0
        panX = 0
        panY = 0
    }

    /// Zooms about a point in viewport coordinates, so the pixel under the
    /// cursor stays under the cursor. Zooming about the centre instead means
    /// every zoom is followed by a hunt for what you were looking at.
    function zoomAt(px, py, next) {
        next = Math.max(0.01, Math.min(40, next))
        if (next === win.zoom)
            return
        touched = true
        var ratio = next / win.zoom
        // The offset from the viewport centre to the anchor, before and after.
        var ax = px - width / 2 - panX
        var ay = py - height / 2 - panY
        requestedPanX = panX - ax * (ratio - 1)
        requestedPanY = panY - ay * (ratio - 1)
        win.zoom = next
        clampPan()
    }

    readonly property real contentWidth: doc.columns * win.zoom
    readonly property real contentHeight: doc.rows * win.zoom
    property var caretMarginX: cfg.settings["canvas.caret_margin_x"]
    property var caretMarginY: cfg.settings["canvas.caret_margin_y"]

    function cellsAcross(space, setting) {
        var cells = Math.max(1, Math.floor(space / win.zoom))
        // There is no central cell in an even-sized run. Giving up at most one
        // edge cell makes `center` mean the literal centre rather than one of
        // two almost-central choices.
        if (String(setting) === "center" && cells > 1 && cells % 2 === 0)
            cells -= 1
        return cells
    }

    readonly property real viewportWidth: contentWidth > width
                                          ? cellsAcross(width, caretMarginX) * win.zoom
                                          : width
    readonly property real viewportHeight: contentHeight > height
                                           ? cellsAcross(height, caretMarginY) * win.zoom
                                           : height
    readonly property bool scrollsX: contentWidth > viewportWidth
    readonly property bool scrollsY: contentHeight > viewportHeight
    onViewportWidthChanged: clampPan()
    onViewportHeightChanged: clampPan()

    // What part of the drawing is on screen, in columns and rows. Published so
    // the overview in the rail can draw the viewport and jump to a point
    // without knowing anything about pans and zooms.
    readonly property real viewColumn:
        ((contentWidth - viewportWidth) / 2 - panX) / win.zoom
    readonly property real viewRow:
        ((contentHeight - viewportHeight) / 2 - panY) / win.zoom
    readonly property real viewColumns: viewportWidth / win.zoom
    readonly property real viewRows: viewportHeight / win.zoom

    /// Keeps the drawing reachable. Once it is larger than the viewport it may
    /// not be dragged past its own edges -- scrolling into empty space and
    /// losing the drawing entirely is the thing that makes a zoomed view feel
    /// broken. While it still fits, a little slack is harmless.
    function clampPan() {
        var slackX = Math.abs(contentWidth - viewportWidth) / 2
        var slackY = Math.abs(contentHeight - viewportHeight) / 2
        requestedPanX = Math.max(-slackX, Math.min(slackX, requestedPanX))
        requestedPanY = Math.max(-slackY, Math.min(slackY, requestedPanY))

        function aligned(requested, viewportSize, contentSize, scrolls) {
            if (!scrolls)
                return requested
            // Align the stage origin, not pan in isolation. When the document
            // and viewport have different parity their centred origins differ
            // by half a cell; rounding pan alone preserves that half-cell cut.
            var base = (viewportSize - contentSize) / 2
            return Math.round((base + requested) / win.zoom) * win.zoom - base
        }

        panX = Math.max(-slackX, Math.min(slackX,
                    aligned(requestedPanX, viewportWidth, contentWidth, scrollsX)))
        panY = Math.max(-slackY, Math.min(slackY,
                    aligned(requestedPanY, viewportHeight, contentHeight, scrollsY)))
    }

    /// Moves the view by a wheel's worth of delta.
    function scrollBy(dx, dy) {
        if (dx === 0 && dy === 0)
            return
        touched = true
        requestedPanX += dx * 0.7
        requestedPanY += dy * 0.7
        clampPan()
    }

    /// Keeps the keyboard cursor inside its configured safe area. Each axis is
    /// independent: a horizontal edge must not disturb a vertical view the
    /// artist positioned deliberately, and vice versa.
    function reveal(column, row) {
        if (!scrollsX && !scrollsY)
            return

        function shouldMove(value, start, count, scrolls, setting) {
            if (!scrolls)
                return false
            if (String(setting) === "center")
                return true
            // Leave at least one stable pixel when a requested margin is wider
            // than half of the currently visible drawing.
            var margin = Math.min(Math.max(0, Number(setting)),
                                  Math.max(0, (count - 1) / 2))
            return value < start + margin || value >= start + count - margin
        }

        var moveX = shouldMove(column, viewColumn, viewColumns,
                               scrollsX, caretMarginX)
        var moveY = shouldMove(row, viewRow, viewRows,
                               scrollsY, caretMarginY)
        if (!moveX && !moveY)
            return
        if (moveX)
            requestedPanX = (doc.columns / 2 - column - 0.5) * win.zoom
        if (moveY)
            requestedPanY = (doc.rows / 2 - row - 0.5) * win.zoom
        touched = true
        clampPan()
    }

    /// Puts a point of the DRAWING, in columns and rows, at the middle of the
    /// viewport. This is what "go to that bit over there" means, and what the
    /// scrollbars and the overview both end up calling.
    function centreOn(column, row) {
        requestedPanX = (doc.columns / 2 - column - 0.5) * win.zoom
        requestedPanY = (doc.rows / 2 - row - 0.5) * win.zoom
        touched = true
        clampPan()
    }

    // A document opens before the window has laid itself out, and the pane
    // passes through several sizes on the way to its real one. Fitting once, at
    // the first size that looked plausible, fit a 160x90 picture to a pane
    // 100px wide and left it at 1x. So: keep fitting to the pane until somebody
    // zooms or pans, and never again after that. Following the window is what
    // you want right up to the moment you have chosen a view of your own.
    property bool touched: false

    /// The zoom a drawing opens at: `canvas.zoom` in the config file, which is
    /// either "fit" or a number of screen pixels per drawing pixel. Somebody
    /// who always works on 32x32 tiles at 16x should not have to reach for the
    /// same two keys every time they open one.
    function opening() {
        var asked = cfg.settings["canvas.zoom"]
        var fixed = Number(asked)
        if (String(asked) !== "fit" && isFinite(fixed) && fixed > 0) {
            win.zoom = Math.max(1, Math.min(40, Math.round(fixed)))
            requestedPanX = 0
            requestedPanY = 0
            panX = 0
            panY = 0
            clampPan()
            return
        }
        fit()
    }

    function settle() {
        if (!touched && width > 80 && height > 80)
            opening()
        else
            clampPan()
    }

    onWidthChanged: settle()
    onHeightChanged: settle()
    Component.onCompleted: {
        settle()
        log.say("surface ready — the QML side of the log is live")
    }

    Connections {
        target: doc
        // A different document is a different size; whatever pan suited the old
        // one is meaningless, and starting scrolled off the edge looks broken.
        function onDocumentReplaced() { surface.touched = false; surface.settle() }
        function onChanged() { surface.settle() }
    }

    // Panning. The middle button always does it; the left one does it too while
    // the hand tool is chosen, because a middle button is not something every
    // mouse or trackpad has, and dragging the picture around is the first thing
    // anybody tries. Buttons this area does not accept fall through to drawing.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: win.tool === "hand" ? (Qt.LeftButton | Qt.MiddleButton)
                                             : Qt.MiddleButton
        z: 5
        property real fromX: 0
        property real fromY: 0
        property real fromPanX: 0
        property real fromPanY: 0
        cursorShape: pressed ? Qt.ClosedHandCursor
                             : (win.tool === "hand" ? Qt.OpenHandCursor : Qt.ArrowCursor)
        // MouseArea has a wheel signal of its own. If one of them is eating the
        // event before the handler sees it, this is where it shows.
        // The wheel is handled HERE, in a MouseArea, and not in a WheelHandler.
        //
        // WheelHandler is the modern spelling and it is the one that does not
        // work: on this Qt and Wayland it never receives a wheel event at all.
        // Measured, not guessed -- with logging on both sides of the QML
        // boundary, 456 wheel events reached the window, both MouseAreas saw
        // every one of them, and the handler ran zero times. A minimal Item
        // whose only child is a WheelHandler behaves the same way. MouseArea
        // gets them, so MouseArea is what this uses.
        onWheel: function (w) {
            log.say("wheel angle " + w.angleDelta.x + "," + w.angleDelta.y
                    + "  pixel " + w.pixelDelta.x + "," + w.pixelDelta.y
                    + "  modifiers " + w.modifiers
                    + " | zoom " + win.zoom + "  panY " + Math.round(surface.panY)
                    + "  scrollsY " + surface.scrollsY)
            w.accepted = true

            if (w.modifiers & (Qt.ControlModifier | Qt.AltModifier)) {
                // Whichever axis carries the turn. Alt and the wheel arrives as
                // a HORIZONTAL delta -- libinput remaps it -- so reading only
                // `y` meant alt-wheel zoomed out and never in.
                var turn = w.angleDelta.y || w.angleDelta.x
                        || w.pixelDelta.y || w.pixelDelta.x
                if (turn !== 0)
                    surface.zoomStep(w.x, w.y, turn > 0)
                return
            }

            var dx = w.angleDelta.x
            var dy = w.angleDelta.y
            // Pixels first when they are there: that is already the distance
            // asked for. A trackpad otherwise reports fractions of a degree --
            // deltas of two and four where a wheel notch is 120 -- so its
            // events need a much larger factor to cover the same ground.
            if (w.pixelDelta.x !== 0 || w.pixelDelta.y !== 0)
                surface.scrollBy(w.pixelDelta.x * 3, w.pixelDelta.y * 3)
            else if (w.phase !== 0)
                surface.scrollBy(dx * 6, dy * 6)      // trackpad, high resolution
            else
                surface.scrollBy(dx * 0.6, dy * 0.6)  // one notch of a wheel
        }
        onPressed: function (mouse) {
            // Shift-left is selection even while the hand tool is active. Let
            // the drawing's MouseArea below take that gesture instead of pan.
            if (mouse.button === Qt.LeftButton
                && (mouse.modifiers & Qt.ShiftModifier)) {
                mouse.accepted = false
                return
            }
            fromX = mouse.x
            fromY = mouse.y
            fromPanX = surface.requestedPanX
            fromPanY = surface.requestedPanY
        }
        onPositionChanged: function (mouse) {
            if (!pressed)
                return
            surface.touched = true
            surface.requestedPanX = fromPanX + mouse.x - fromX
            surface.requestedPanY = fromPanY + mouse.y - fromY
            surface.clampPan()
        }
    }

    // Three handlers, each filtering on an exact modifier set, rather than one
    // handler branching on `event.modifiers`. Two reasons, both learned the
    // hard way:
    //
    //   `acceptedModifiers` matches the WHOLE set, not any member of it. An
    //   earlier `Qt.ShiftModifier | Qt.ControlModifier` meant "both at once"
    //   and so never fired for either.
    //
    //   And branching inside on `event.modifiers` depends on that property
    //   being exposed on the event; where it is not, it reads as undefined,
    //   `undefined & Qt.ControlModifier` is 0, and every wheel event silently
    //   takes the unmodified branch. Filtering by declaration cannot fail
    //   quietly like that: a handler that does not match simply does not run.

    /// One wheel notch of zoom, about the cursor.
    function zoomStep(px, py, up) {
        if (win.zoom < 1) {
            zoomAt(px, py, up ? Math.min(1, win.zoom * 1.25)
                              : win.zoom / 1.25)
            return
        }
        // Bigger steps once the pixels are large, so 12x to 40x is a flick
        // rather than a grind.
        var by = win.zoom >= 16 ? 4 : (win.zoom >= 8 ? 2 : 1)
        zoomAt(px, py, win.zoom + (up ? by : -by))
    }

    PinchHandler {
        // Two fingers on a trackpad, with no modifier at all -- the one path
        // here that no keyboard state can spoil.
        target: null
        property real fromZoom: 1
        onActiveChanged: if (active) fromZoom = win.zoom
        onActiveScaleChanged: {
            if (!active)
                return
            surface.zoomAt(centroid.position.x, centroid.position.y,
                           fromZoom * activeScale)
        }
    }

    // Scrollbars. These went missing twice while the wheel was being chased --
    // both times because a block above them was replaced by position rather
    // than by name, and they sat between it and the stage.
    component Bar : Rectangle {
        property bool vertical: false
        property real fraction: 0      // where along the content the view sits
        property real portion: 1       // how much of it is on screen
        signal moved(real fraction)

        color: "transparent"
        visible: portion < 1

        Rectangle {
            anchors.fill: parent
            radius: theme.rounding
            color: theme.fill(theme.foreground, 0.06)
        }

        Rectangle {
            id: handle
            radius: theme.rounding
            color: drag.pressed || hover.hovered ? theme.fill(theme.accent, 0.55)
                                                 : theme.fill(theme.foreground, 0.28)
            Behavior on color { ColorAnimation { duration: 90 } }

            readonly property real span: parent.vertical ? parent.height : parent.width
            readonly property real len: Math.max(24, span * parent.portion)
            readonly property real pos: (span - len) * parent.fraction

            x: parent.vertical ? 0 : pos
            y: parent.vertical ? pos : 0
            width: parent.vertical ? parent.width : len
            height: parent.vertical ? len : parent.height
        }

        HoverHandler { id: hover }

        MouseArea {
            id: drag
            anchors.fill: parent
            property real grabbed: 0

            function fractionAt(along, centred) {
                var span = parent.vertical ? height : width
                var offset = centred ? handle.len / 2 : grabbed
                var free = span - handle.len
                return free <= 0 ? 0 : Math.max(0, Math.min(1, (along - offset) / free))
            }

            onPressed: function (mouse) {
                var along = parent.vertical ? mouse.y : mouse.x
                if (along >= handle.pos && along <= handle.pos + handle.len) {
                    grabbed = along - handle.pos           // dragging the handle
                } else {
                    parent.moved(fractionAt(along, true))  // clicking the track jumps
                    grabbed = handle.len / 2
                }
            }
            onPositionChanged: function (mouse) {
                if (pressed)
                    parent.moved(fractionAt(parent.vertical ? mouse.y : mouse.x, false))
            }
        }
    }

    Bar {
        id: barX
        z: 6
        height: 10
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  leftMargin: 6; rightMargin: 16; bottomMargin: 6 }
        portion: surface.scrollsX ? surface.viewportWidth / surface.contentWidth : 1
        fraction: surface.scrollsX
                  ? 0.5 - surface.panX
                          / (surface.contentWidth - surface.viewportWidth) : 0
        onMoved: function (f) {
            surface.touched = true
            surface.requestedPanX =
                (0.5 - f) * (surface.contentWidth - surface.viewportWidth)
            surface.clampPan()
        }
    }

    Bar {
        id: barY
        z: 6
        vertical: true
        width: 10
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right
                  topMargin: 6; bottomMargin: 16; rightMargin: 6 }
        portion: surface.scrollsY ? surface.viewportHeight / surface.contentHeight : 1
        fraction: surface.scrollsY
                  ? 0.5 - surface.panY
                          / (surface.contentHeight - surface.viewportHeight) : 0
        onMoved: function (f) {
            surface.touched = true
            surface.requestedPanY =
                (0.5 - f) * (surface.contentHeight - surface.viewportHeight)
            surface.clampPan()
        }
    }

    Item {
        id: viewport
        objectName: "pixelViewport"
        x: (surface.width - width) / 2
        y: (surface.height - height) / 2
        width: surface.viewportWidth
        height: surface.viewportHeight
        clip: true

        Item {
            id: stage
            objectName: "pixelStage"
            x: (viewport.width - width) / 2 + surface.panX
            y: (viewport.height - height) / 2 + surface.panY
            width: doc.columns * win.zoom
            height: doc.rows * win.zoom

            Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            color: "transparent"
            border.width: 1
            border.color: theme.fill(theme.foreground, 0.18)
            }

        // The checkerboard is its own layer rather than a background painted
        // inside the frame. Painted together it is opaque and covers the
        // reference -- which is precisely the layer that needs to be underneath.
            PixelGridItem {
            anchors.fill: parent
            model: doc
            clip: doc.clip
            frame: doc.frame
            cell: win.zoom
            checker: true
            checkerDark: theme.checkerDark
            checkerLight: theme.checkerLight
            z: -2
            opacity: 0
            Component.onCompleted: opacity = 1
            }

        Image {
            anchors.fill: parent
            source: win.referencePath === "" ? "" : "file://" + win.referencePath
            fillMode: Image.PreserveAspectFit
            smooth: false
            opacity: win.referenceAlpha
            visible: opacity > 0 && win.referencePath !== ""
            z: win.referenceOnTop ? 2 : -1
        }

        // The previous frame, underneath. Frame beside frame shows two poses;
        // one on top of the other shows the path between them.
        PixelGridItem {
            anchors.fill: parent
            model: doc
            clip: doc.clip
            frame: (doc.frame + doc.frameCount - 1) % Math.max(1, doc.frameCount)
            cell: win.zoom
            opacity: 0.3
            visible: win.onion && doc.frameCount > 1
            z: 0
        }

        PixelGridItem {
            id: sheet
            anchors.fill: parent
            model: doc
            clip: doc.clip
            frame: doc.frame
            cell: win.zoom
            // Below 4x the mesh has more lines than the drawing has pixels: it
            // stops measuring the grid and starts hiding it.
            mesh: win.mesh && win.zoom >= 4
            meshColour: theme.fill(theme.foreground, 0.10)
            z: 1
        }

        MouseArea {
            id: pointer
            objectName: "canvasPointer"
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            z: 3

            property int lastColumn: -1
            property int lastRow: -1
            property bool selecting: false

            function columnAt(mx) {
                return Math.max(0, Math.min(doc.columns - 1,
                                            Math.floor(mx / win.zoom)))
            }

            function rowAt(my) {
                return Math.max(0, Math.min(doc.rows - 1,
                                            Math.floor(my / win.zoom)))
            }

            function apply(mx, my, erase) {
                var col = Math.floor(mx / win.zoom)
                var row = Math.floor(my / win.zoom)
                if (col < 0 || row < 0 || col >= doc.columns || row >= doc.rows)
                    return
                if (win.tool === "picker") {
                    var found = doc.slotAt(col, row)
                    if (found !== ".") { win.slot = found; win.tool = "pencil" }
                    return
                }
                var slot = (erase || win.tool === "eraser") ? "." : win.slot
                if (win.tool === "bucket" && !erase) {
                    doc.fill(col, row, slot)
                    return
                }
                // Dragging paints the PATH and not the samples. At a fast drag
                // the mouse skips cells, and painting only where the events land
                // leaves a dotted line.
                if (lastColumn >= 0)
                    doc.line(lastColumn, lastRow, col, row, slot)
                else
                    doc.paint(col, row, slot)
                lastColumn = col
                lastRow = row
            }

            // The whole drag is one undo step. The model takes its snapshot at
            // the first pixel that actually changes, so a click that missed
            // does not leave an entry behind.
            onPressed: function (mouse) {
                surface.pressedOnCanvas()
                lastColumn = -1
                lastRow = -1
                if (mouse.button === Qt.LeftButton
                    && (mouse.modifiers & Qt.ShiftModifier)) {
                    selecting = true
                    win.linePoints = []
                    win.selectionAnchorColumn = columnAt(mouse.x)
                    win.selectionAnchorRow = rowAt(mouse.y)
                    win.caretColumn = win.selectionAnchorColumn
                    win.caretRow = win.selectionAnchorRow
                    doc.setSelection(win.selectionAnchorColumn,
                                     win.selectionAnchorRow,
                                     win.caretColumn, win.caretRow)
                    return
                }
                selecting = false
                doc.clearSelection()
                doc.beginStroke()
                apply(mouse.x, mouse.y, mouse.button === Qt.RightButton)
            }
            onReleased: {
                if (!selecting)
                    doc.endStroke()
                selecting = false
                lastColumn = -1
                lastRow = -1
            }
            onCanceled: {
                if (!selecting)
                    doc.endStroke()
                selecting = false
                lastColumn = -1
                lastRow = -1
            }
            onPositionChanged: function (mouse) {
                if (selecting) {
                    win.caretColumn = columnAt(mouse.x)
                    win.caretRow = rowAt(mouse.y)
                    doc.setSelection(win.selectionAnchorColumn,
                                     win.selectionAnchorRow,
                                     win.caretColumn, win.caretRow)
                    return
                }
                surface.hoverColumn = Math.floor(mouse.x / win.zoom)
                surface.hoverRow = Math.floor(mouse.y / win.zoom)
                if (pressed && win.tool !== "bucket" && win.tool !== "picker")
                    apply(mouse.x, mouse.y, pressedButtons & Qt.RightButton)
            }
            onExited: { surface.hoverColumn = -1; surface.hoverRow = -1 }
        }

        Rectangle {
            visible: doc.hasSelection
            x: doc.selectionX * win.zoom
            y: doc.selectionY * win.zoom
            width: doc.selectionWidth * win.zoom
            height: doc.selectionHeight * win.zoom
            color: theme.fill(theme.accent, 0.20)
            border.width: 2
            border.color: theme.accent
            z: 2
        }

        // Where the line would go: every corner placed so far, and out to the
        // cursor. A line you cannot see before you commit to it is a line you
        // draw twice, once to find out and once to get it right.
        Item {
            id: preview
            visible: win.linePoints.length > 0 && win.caretColumn >= 0
            z: 6

            readonly property var path: win.linePoints.concat(
                [{ c: win.caretColumn, r: win.caretRow }])

            Repeater {
                model: Math.max(0, preview.path.length - 1)

                Item {
                    id: leg
                    required property int index
                    readonly property var a: preview.path[index]
                    readonly property var b: preview.path[index + 1]
                    readonly property real ax: (a.c + 0.5) * win.zoom
                    readonly property real ay: (a.r + 0.5) * win.zoom
                    readonly property real bx: (b.c + 0.5) * win.zoom
                    readonly property real by: (b.r + 0.5) * win.zoom
                    readonly property real len: Math.hypot(bx - ax, by - ay)
                    readonly property real turn:
                        Math.atan2(by - ay, bx - ax) * 180 / Math.PI

                    // Two strokes, a dark one under a bright one: a single
                    // line in the accent is legible over the background and
                    // lost over a drawing of the same brightness, which is
                    // most of a drawing.
                    Rectangle {
                        x: leg.ax; y: leg.ay - height / 2
                        width: leg.len; height: Math.max(3, win.zoom / 2)
                        transformOrigin: Item.Left; rotation: leg.turn
                        color: "#000000"; opacity: 0.5
                    }
                    Rectangle {
                        x: leg.ax; y: leg.ay - height / 2
                        width: leg.len; height: Math.max(1, win.zoom / 4)
                        transformOrigin: Item.Left; rotation: leg.turn
                        color: theme.accent
                    }
                }
            }

            // The corners themselves, so it is clear which points are pinned
            // and which end is still following the cursor.
            Repeater {
                model: win.linePoints

                Rectangle {
                    required property var modelData
                    x: modelData.c * win.zoom
                    y: modelData.r * win.zoom
                    width: win.zoom
                    height: win.zoom
                    color: theme.fill(theme.accent, 0.30)
                    border.width: Math.max(1, Math.round(win.zoom / 6))
                    border.color: theme.accent
                }
            }
        }

        // Guides through the keyboard cursor, the width and height of the
        // drawing. At 5x the cursor is a five-pixel square on a picture made of
        // five-pixel squares: findable only if you already know where it is.
        Rectangle {
            visible: win.caretColumn >= 0
            x: (win.caretColumn + 0.5) * win.zoom - width / 2
            y: 0
            width: Math.max(2, win.zoom)
            height: parent.height
            color: theme.fill(theme.accent, 0.10)
            z: 4
        }
        Rectangle {
            visible: win.caretColumn >= 0
            x: 0
            y: (win.caretRow + 0.5) * win.zoom - height / 2
            width: parent.width
            height: Math.max(2, win.zoom)
            color: theme.fill(theme.accent, 0.10)
            z: 4
        }

        // The keyboard cursor. Drawn in the accent and heavier than the
        // mouse's outline, because it is a position the program is holding on
        // your behalf rather than one your hand is already on.
        Rectangle {
            objectName: "keyboardCursor"
            visible: win.caretColumn >= 0
            x: (win.caretColumn + 0.5) * win.zoom - width / 2
            y: (win.caretRow + 0.5) * win.zoom - height / 2
            width: Math.max(7, win.zoom)
            height: Math.max(7, win.zoom)
            // Against the pixel it sits on, not against the theme. An accent
            // that reads beautifully over the background is invisible over a
            // drawing that happens to use the accent's own family of colours --
            // and a cursor you cannot find is worse than no cursor.
            readonly property color against: {
                var c = doc.contrastAt(win.caretColumn, win.caretRow)
                return c.a > 0 ? c : theme.foreground
            }

            color: "transparent"
            border.width: Math.max(1, Math.round(win.zoom / 6))
            border.color: against
            z: 5

            // A second outline, one pixel in and in the opposite direction, so
            // the cursor survives sitting exactly on the boundary between two
            // colours -- where one edge would match whatever it crosses.
            Rectangle {
                anchors.fill: parent
                anchors.margins: parent.border.width
                color: "transparent"
                border.width: 1
                border.color: parent.against.hslLightness > 0.5 ? "#000000" : "#ffffff"
                opacity: 0.55
            }
        }

        // The outline of the pixel under the cursor. On a 32-column grid at 12×
        // that is the difference between hitting the column and guessing.
        Rectangle {
            visible: surface.hoverColumn >= 0 && surface.hoverRow >= 0
            x: surface.hoverColumn * win.zoom
            y: surface.hoverRow * win.zoom
            width: win.zoom
            height: win.zoom
            color: "transparent"
            border.width: 1
            border.color: theme.foreground
            opacity: 0.7
            z: 4
        }
    }
}
}
