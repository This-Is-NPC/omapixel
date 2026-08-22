import QtQuick
import QtQuick.Window
// Imported under a prefix on purpose: QtQuick.Controls defines Label and
// ToolButton too, and an imported module wins over a component sitting in
// the same directory. Without the prefix, this file's own Label silently
// becomes somebody else's.
import QtQuick.Controls.Basic as C
import QtQuick.Dialogs
import omapixel

// The studio.
//
// Layout and input, and nothing else. Every rule -- what a resize does, whether
// a clip may lose its last frame, what a slot with no colour means -- lives in
// the core, and this file only calls it.
//
// The shape is the one every drawing program has settled on, for the reason
// they all settled on it:
//
//   menu bar    every command, named and grouped. Nothing is reachable only by
//               knowing a key, and nothing common needs the menu twice.
//   tool strip  the one piece of state that changes what the mouse does, in a
//               column that never moves and never reflows.
//   canvas      the middle, and as much of it as the window can spare.
//   dock        panels, folded away when you are not using them.
//   timeline    the clip and its frames, along the bottom, where time is.
//   status      what is true right now, on one line.
//
// The previous version put fifteen chips in a single row across the top, mixed
// tools with view toggles with file actions, had no way to export and no way to
// start a new document, and hid the cursor position in a side panel. Everything
// below is that pile, sorted.
Window {
    id: win

    width: 1280
    height: 820
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: (doc.dirty ? "• " : "") + (doc.path === "" ? "untitled" : doc.path)
           + " — omapixel"
    color: theme.background

    property string tool: "pencil"
    property string slot: "I"
    property real zoom: 12
    property bool onion: false
    property bool mesh: true
    property bool playing: false

    // The keyboard cursor. Drawing with a mouse is fine for shapes and hopeless
    // for placing one pixel exactly; -1 means it has not been used yet and
    // nothing is drawn for it.
    property bool showHints: true

    /// The colour the picker is currently on. Published so a test can read it
    /// without reaching into a popup's insides.
    readonly property string chosenColour: colourSheet.chosen

    // Ten colours on the number keys. A palette of thirty-three slots is a
    // wall to hunt through; the handful you are actually using at this moment
    // is small, and it changes as the drawing does. The digits are where you
    // put them.
    property var registers: []

    function fillRegisters() {
        var next = []
        for (var i = 0; i < 10 && i < doc.palette.length; ++i)
            next.push(doc.palette[i].slot)
        registers = next
    }

    /// The palette slot on a digit, or "" if that digit is empty.
    function registerAt(digit) {
        return digit < registers.length ? registers[digit] : ""
    }

    /// Puts a colour on a number key, by ADDING it to the palette.
    ///
    /// Never by recolouring the slot that number happened to be pointing at.
    /// A slot is not a swatch: every pixel drawn with it refers to it, and
    /// changing its colour repaints all of them at once. That is the best
    /// thing about this format and the worst thing to do by accident -- you
    /// reach for a nicer blue and the sky you painted last week changes with
    /// it. The number keys are a set of colours to draw WITH; the palette is
    /// what the drawing is MADE OF.
    function colourOnDigit(digit, hex) {
        var already = doc.palette.filter(function (e) {
            return e.colour.toUpperCase() === hex.toUpperCase()
        })
        var letter = already.length > 0 ? already[0].slot : doc.freeSlot()
        if (letter === "") {
            doc.say("the palette is full — remove a slot first")
            return
        }
        if (already.length === 0)
            doc.setPaletteColour(letter, hex)   // a letter nothing is using yet
        putOnDigit(digit, letter)
        slot = letter
    }

    /// Paints the pixel under the cursor with a colour nobody chose.
    ///
    /// It adds a slot rather than recolouring one, like every other way a
    /// colour arrives here: the gamble is which colour you get, not which of
    /// the ones already in the drawing gets ruined.
    /// The colour a replace would act on: what the cursor is standing on, and
    /// failing that what you are drawing with. Standing on a pixel is the
    /// clearer of the two ways of saying "that colour".
    readonly property string focusedSlot: {
        if (caretColumn >= 0) {
            var there = doc.slotAt(caretColumn, caretRow)
            if (there !== ".")
                return there
        }
        return slot === "." ? "" : slot
    }

    function russianRoulette() {
        if (caretColumn < 0)
            moveCaret(0, 0)
        var letter = doc.freeSlot()
        if (letter === "") {
            doc.say("the palette is full — remove a slot first")
            return
        }
        var hex = doc.randomColour()
        doc.setPaletteColour(letter, hex)
        slot = letter
        doc.beginStroke()
        doc.paint(caretColumn, caretRow, letter)
        doc.endStroke()
        doc.say("russian roulette — " + hex + " on slot " + letter)
    }

    function putOnDigit(digit, which) {
        var next = registers.slice()
        while (next.length <= digit)
            next.push("")
        next[digit] = which
        registers = next
        doc.say((which === "." ? "empty" : "slot " + which)
                + " is now on " + (digit === 9 ? 0 : digit + 1))
    }

    function useSlot(which, paintIt) {
        if (which === "")
            return
        slot = which
        if (paintIt && caretColumn >= 0) {
            doc.beginStroke()
            doc.paint(caretColumn, caretRow, which)
            doc.endStroke()
        }
    }

    // What the keyboard is doing. "" is plain moving; "draw" paints while you
    // travel; "pick" takes colours off the drawing. A mode is entered with one
    // key and left with Escape, always -- a toggle you have to remember the
    // state of is a toggle you get wrong.
    property string mode: ""

    // The digit being held down in draw mode. Holding the colour is what makes
    // the cursor paint, so the same arrows both move and draw depending on
    // whether your other hand is on a number.
    property string heldSlot: ""

    // The corners of the line being built. Each press of `l` adds one; nothing
    // is drawn until you say which colour, so a right angle is two points and
    // one press rather than two separate lines.
    property var linePoints: []

    /// The keys worth showing right now.
    ///
    /// Ordered by how often they are wanted, not by how the program is built,
    /// and answering the question in front of you: while the leader is pending
    /// there is exactly one thing to say, and saying eight other things would
    /// bury it.
    readonly property var hints: {
        if (awaitingSlot)
            return [{ key: "A–Z  a–g", label: "the palette slot to use" },
                    { key: ".", label: "empty" },
                    { key: "Esc", label: "cancel" }]

        if (linePoints.length > 0)
            return [{ key: "arrows", label: "move the free end" },
                    { key: "l", label: "pin a corner here" },
                    { key: "1–0", label: "draw it in that colour" },
                    { key: "Enter", label: "draw it" },
                    { key: "Esc", label: "throw it away" }]

        if (mode === "pick")
            return [{ key: "arrows", label: "move over the colour you want" },
                    { key: "1–0", label: "put it on that number" },
                    { key: "Esc", label: "leave picking" }]

        if (mode === "draw")
            return [{ key: "hold 1–0", label: "paint as you move, in that colour" },
                    { key: "Enter", label: "one pixel" },
                    { key: "l", label: "straight line" },
                    { key: "Esc", label: "leave drawing" }]

        if (caretColumn >= 0)
            return [{ key: "arrows", label: "move" },
                    { key: "Shift", label: "by eight" },
                    { key: "Enter", label: "paint this pixel" },
                    { key: "Bksp", label: "erase it" },
                    { key: "1–0", label: "colours" },
                    { key: "d", label: "draw as you move" },
                    { key: "p", label: "pick up colours" },
                    { key: "c", label: "find a colour" },
                    { key: "r", label: "russian roulette" },
                    { key: "⇧c", label: "replace this colour" },
                    { key: "l", label: "straight line" },
                    { key: "Esc", label: "put away" }]

        return [{ key: "Tab", label: "draw with the keyboard" },
                { key: "b e f i h", label: "tools" },
                { key: "1–0", label: "colours" },
                { key: ";", label: "change colour" },
                { key: "Space", label: "play" },
                { key: ",  .", label: "frame" },
                { key: "^Z", label: "undo" },
                { key: "^S", label: "save" },
                { key: "^E", label: "export" }]
    }

    // Waiting for the letter of a palette slot. Every letter is already a tool
    // or a toggle, so choosing a colour by its letter needs a key to say that
    // the next one names a colour -- the way a leader key works.
    property bool awaitingSlot: false

    property int caretColumn: -1
    property int caretRow: -1

    /// Gives the keyboard back to the drawing.
    ///
    /// The canvas keys live on one item that holds focus, and a window full of
    /// controls takes focus away constantly: opening a menu, typing a clip
    /// name, clicking a field. Nothing gave it back, so the arrows worked until
    /// the first click anywhere and then never again -- which reads as "the
    /// keyboard does not work" rather than "the keyboard is somewhere else".
    function focusCanvas() {
        keys.forceActiveFocus()
    }

    /// The character a key press names, for choosing a palette slot.
    ///
    /// `event.text` is the obvious source and it is not always filled in --
    /// with a modifier held it can arrive empty, and it depends on the layout.
    /// The key code is always there, and for the letters and digits it IS the
    /// character. Case matters: slots tell A from a, and a studio that quietly
    /// accepted either would pick the wrong colour in a palette that uses both.
    function slotFromKey(key, modifiers, text) {
        // The key and the shift state, in preference to `event.text`. The text
        // is not reliable: it can arrive lowercase with shift held, and empty
        // with other modifiers, and it varies with the layout. For a letter,
        // the key code IS the character and shift decides the case.
        if (key >= Qt.Key_A && key <= Qt.Key_Z) {
            var upper = String.fromCharCode(key)
            return (modifiers & Qt.ShiftModifier) ? upper : upper.toLowerCase()
        }
        if (key >= Qt.Key_0 && key <= Qt.Key_9)
            return String.fromCharCode(key)
        if (key === Qt.Key_Period)
            return "."
        return text
    }

    /// Paints where the cursor stands, if a colour is being held down.
    function trail() {
        if (mode !== "draw" || heldSlot === "" || caretColumn < 0)
            return
        doc.paint(caretColumn, caretRow, heldSlot)
    }

    function releaseHeld() {
        if (heldSlot === "")
            return
        heldSlot = ""
        doc.endStroke()
    }

    /// Draws the line through every point placed so far and out to the cursor.
    function commitLine(which) {
        if (linePoints.length === 0)
            return
        doc.beginStroke()
        var path = linePoints.concat([{ c: caretColumn, r: caretRow }])
        for (var i = 0; i + 1 < path.length; ++i)
            doc.line(path[i].c, path[i].r, path[i + 1].c, path[i + 1].r, which)
        doc.endStroke()
        linePoints = []
    }

    function moveCaret(dx, dy) {
        if (caretColumn < 0) {
            // First press lands it in the middle rather than a corner: the
            // middle is on average the shortest walk to anywhere.
            caretColumn = Math.floor(doc.columns / 2)
            caretRow = Math.floor(doc.rows / 2)
        } else {
            caretColumn = Math.max(0, Math.min(doc.columns - 1, caretColumn + dx))
            caretRow = Math.max(0, Math.min(doc.rows - 1, caretRow + dy))
        }
        trail()
        stage.reveal(caretColumn, caretRow)
    }

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
        // A new document has a new palette, and digits pointing at slots that
        // are no longer there are worse than digits pointing at nothing.
        function onFileChanged() { win.fillRegisters() }
    }

    Component.onCompleted: {
        fillRegisters()
        if (shotSheet === "colour")
            colourSheet.show()
        else if (shotSheet === "replace") {
            caretColumn = 60; caretRow = 40
            colourSheet.show("replace")
        }
        else if (shotSheet === "new")
            newSheet.open()
        else if (shotSheet === "export")
            exportSheet.open()
    }

    Timer {
        interval: Math.max(16, Math.round(1000 / doc.fps))
        running: win.playing && doc.frameCount > 1
        repeat: true
        onTriggered: doc.frame = (doc.frame + 1) % doc.frameCount
    }

    // -------------------------------------------------------------- commands
    //
    // Declared once, as actions, and used by both the menus and the keyboard. A
    // command that exists twice -- once in a menu handler and once in a key
    // handler -- is a command that will one day do two different things.

    C.Action { id: actNew; text: "New…"; shortcut: "Ctrl+N"
             onTriggered: newSheet.open() }
    C.Action { id: actOpen; text: "Open…"; shortcut: "Ctrl+O"
             onTriggered: openDialog.open() }
    C.Action { id: actSave; text: "Save"; shortcut: "Ctrl+S"
             onTriggered: doc.path === "" ? saveDialog.open() : doc.save() }
    C.Action { id: actSaveAs; text: "Save as…"; shortcut: "Ctrl+Shift+S"
             onTriggered: saveDialog.open() }
    C.Action { id: actExport; text: "Export PNG…"; shortcut: "Ctrl+E"
             onTriggered: { exportSheet.asSheet = false; exportSheet.open() } }
    C.Action { id: actExportSheet; text: "Export sprite sheet…"; shortcut: "Ctrl+Shift+E"
             onTriggered: { exportSheet.asSheet = true; exportSheet.open() } }
    C.Action { id: actQuit; text: "Quit"; shortcut: "Ctrl+Q"
             onTriggered: Qt.quit() }

    C.Action { id: actUndo; text: "Undo"; shortcut: "Ctrl+Z"
             enabled: doc.canUndo; onTriggered: doc.undo() }
    C.Action { id: actRedo; text: "Redo"; shortcut: "Ctrl+Shift+Z"
             enabled: doc.canRedo; onTriggered: doc.redo() }
    C.Action { id: actClear; text: "Clear frame"; shortcut: "Delete"
             onTriggered: doc.clearFrame() }
    C.Action { id: actFlipX; text: "Flip horizontally"; onTriggered: doc.flip("x") }
    C.Action { id: actFlipY; text: "Flip vertically"; onTriggered: doc.flip("y") }

    C.Action { id: actResize; text: "Canvas size…"
             onTriggered: { spriteSection.open = true; dock.contentY = spriteSection.y } }
    C.Action { id: actAddClip; text: "Add clip"
             onTriggered: doc.addClip("clip " + (doc.clipNames.length + 1)) }
    C.Action { id: actRemoveClip; text: "Delete clip"
             enabled: doc.clipNames.length > 1
             onTriggered: doc.removeClip(doc.clip) }
    C.Action { id: actAddFrame; text: "Add frame"; shortcut: "Ctrl+Shift+N"
             onTriggered: doc.addFrame(false) }
    C.Action { id: actDupFrame; text: "Duplicate frame"; shortcut: "Ctrl+D"
             onTriggered: doc.addFrame(true) }
    C.Action { id: actDelFrame; text: "Delete frame"
             enabled: doc.frameCount > 1; onTriggered: doc.removeFrame() }
    C.Action { id: actPlay; text: "Play"; shortcut: "Space"
             enabled: doc.frameCount > 1; onTriggered: win.playing = !win.playing }

    C.Action { id: actZoomIn; text: "Zoom in"; shortcut: "Ctrl++"
             onTriggered: stage.zoomStep(stage.width / 2, stage.height / 2, true) }
    C.Action { id: actZoomOut; text: "Zoom out"; shortcut: "Ctrl+-"
             onTriggered: stage.zoomStep(stage.width / 2, stage.height / 2, false) }
    C.Action { id: actFit; text: "Fit to window"; shortcut: "Ctrl+0"
             onTriggered: { stage.touched = false; stage.fit() } }
    C.Action { id: actOnion; text: "Onion skin"; checkable: true; checked: win.onion
             onTriggered: win.onion = !win.onion }
    C.Action { id: actMesh; text: "Pixel grid"; checkable: true; checked: win.mesh
             onTriggered: win.mesh = !win.mesh }
    C.Action { id: actReference; text: "Reference image…"
             onTriggered: referenceDialog.open() }
    C.Action { id: actColour; text: "Choose a colour…"
             onTriggered: colourSheet.show() }
    C.Action { id: actRoulette; text: "Russian roulette"
             onTriggered: win.russianRoulette() }
    C.Action { id: actReplace; text: "Replace this colour…"
             enabled: win.focusedSlot !== ""
             onTriggered: colourSheet.show("replace") }
    C.Action { id: actHints; text: "Key hints"; checkable: true; checked: win.showHints
             onTriggered: win.showHints = !win.showHints }

    // ------------------------------------------------------------- the window

    Item {
        id: keys
        objectName: "canvas keys"
        anchors.fill: parent
        focus: true
        // Claimed on the way up as well as declared. `focus: true` is a request
        // that another focus scope in the window -- a menu bar is one -- can
        // win before anybody has touched anything.
        Component.onCompleted: forceActiveFocus()

        // Single letters, handled here rather than as Shortcuts: a Shortcut is
        // application-wide and would fire while somebody is typing a clip name.
        // Keys reach a focused text field first.
        // Letting go of the colour ends the run -- and the undo step with it,
        // so one press takes back everything that one press drew.
        Keys.onReleased: function (event) {
            if (event.isAutoRepeat)
                return
            if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
                win.releaseHeld()
                event.accepted = true
            }
        }

        Keys.onPressed: function (event) {
            // Held keys repeat, and a repeat is a fresh press. For a toggle
            // that means it flips on and off between one pixel of a run and
            // the next: the stroke is closed and reopened dozens of times, and
            // undo then takes back the last fragment instead of the run. The
            // arrows are meant to repeat; the modes are not.
            if (event.isAutoRepeat
                && (event.key === Qt.Key_D || event.key === Qt.Key_L
                    || event.key === Qt.Key_Semicolon)) {
                event.accepted = true
                return
            }

            // A pending slot swallows the next key, whatever it would otherwise
            // have meant. That is the whole point of a leader: for one press,
            // the letters are colours again.
            if (win.awaitingSlot) {
                // A modifier held down is a key press of its own, and it
                // arrives BEFORE the letter it modifies. Consuming it would
                // eat the leader every time somebody reached for a capital --
                // which is half the palette.
                if (event.key === Qt.Key_Shift || event.key === Qt.Key_Control
                    || event.key === Qt.Key_Alt || event.key === Qt.Key_Meta
                    || event.key === Qt.Key_AltGr) {
                    event.accepted = true
                    return
                }
                event.accepted = true
                win.awaitingSlot = false
                if (event.key === Qt.Key_Escape)
                    return
                var wanted = win.slotFromKey(event.key, event.modifiers, event.text)
                if (wanted === "")
                    return
                var known = wanted === "." || doc.palette.some(function (e) {
                    return e.slot === wanted
                })
                if (!known) {
                    doc.say("no slot " + wanted)
                    return
                }
                win.slot = wanted
                if (win.caretColumn >= 0) {
                    doc.beginStroke()
                    doc.paint(win.caretColumn, win.caretRow, wanted)
                    doc.endStroke()
                }
                return
            }

            switch (event.key) {
            case Qt.Key_B: win.tool = "pencil"; break
            case Qt.Key_E: win.tool = "eraser"; break
            case Qt.Key_F: win.tool = "bucket"; break
            case Qt.Key_I: win.tool = "picker"; break
            case Qt.Key_H: win.tool = "hand"; break
            case Qt.Key_O: win.onion = !win.onion; break
            case Qt.Key_M: win.mesh = !win.mesh; break
            // The arrows walk the drawing, a pixel at a time, eight with
            // shift. Frames moved to the comma and full stop, which is where
            // every other sprite editor puts them -- the arrows are worth more
            // here, and stepping through frames is not something you do while
            // your hand is on the canvas.
            case Qt.Key_Left:  win.moveCaret(event.modifiers & Qt.ShiftModifier ? -8 : -1, 0); break
            case Qt.Key_Right: win.moveCaret(event.modifiers & Qt.ShiftModifier ?  8 :  1, 0); break
            case Qt.Key_Up:    win.moveCaret(0, event.modifiers & Qt.ShiftModifier ? -8 : -1); break
            case Qt.Key_Down:  win.moveCaret(0, event.modifiers & Qt.ShiftModifier ?  8 :  1); break

            case Qt.Key_Comma:  doc.frame = Math.max(0, doc.frame - 1); break
            case Qt.Key_Period:
                doc.frame = Math.min(doc.frameCount - 1, doc.frame + 1); break

            // Draw where the cursor is. Return paints, backspace clears, and
            // both work on the pixel you can see the outline around.
            // Paint with the colour in hand: the pending line if there is one,
            // otherwise the pixel under the cursor.
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_X:
                if (win.caretColumn < 0)
                    break
                if (win.linePoints.length > 0) {
                    win.commitLine(win.slot)
                } else {
                    doc.beginStroke()
                    doc.paint(win.caretColumn, win.caretRow, win.slot)
                    doc.endStroke()
                }
                break
            case Qt.Key_Backspace:
                if (win.caretColumn >= 0) {
                    doc.beginStroke()
                    doc.paint(win.caretColumn, win.caretRow, ".")
                    doc.endStroke()
                }
                break
            // Escape undoes one thing at a time, most recent first: the line
            // you were about to draw, then the pen, then the cursor. One key
            // that always means "not that" is worth more than three.
            case Qt.Key_Escape:
                if (win.linePoints.length > 0) {
                    win.linePoints = []
                } else if (win.mode !== "") {
                    win.releaseHeld()
                    win.mode = ""
                } else {
                    win.caretColumn = -1
                    win.caretRow = -1
                }
                break

            // The leader. Semicolon rather than the comma, which is already the
            // previous frame and worth more there.
            case Qt.Key_Semicolon: win.awaitingSlot = true; break

            // A colour you do not know the name of yet.
            case Qt.Key_C:
                colourSheet.show(event.modifiers & Qt.ShiftModifier ? "replace"
                                                                    : "assign")
                break

            // And one nobody knows.
            case Qt.Key_R: win.russianRoulette(); break



            // Draw mode: from here the arrows paint, but only while a colour
            // is held down. Moving and drawing are the same gesture with and
            // without your other hand on a number.
            case Qt.Key_D:
                if (win.caretColumn < 0)
                    win.moveCaret(0, 0)
                win.mode = win.mode === "draw" ? "" : "draw"
                break

            // Pick mode: the digits stop choosing a colour and start
            // collecting one. Point at a pixel, press a number, and that
            // colour is on that number.
            case Qt.Key_P:
                if (win.caretColumn < 0)
                    win.moveCaret(0, 0)
                win.mode = win.mode === "pick" ? "" : "pick"
                break

            // A line: once to drop the anchor, again to draw from it. Between
            // the two presses the canvas shows where it would go, because a
            // line you cannot see before you commit to it is a line you draw
            // twice.
            // Each press drops a corner. Nothing is drawn until you name a
            // colour, so a right angle is two corners and one press instead of
            // two lines whose ends you have to line up by hand.
            case Qt.Key_L:
                if (win.caretColumn < 0)
                    win.moveCaret(0, 0)
                win.linePoints = win.linePoints.concat(
                    [{ c: win.caretColumn, r: win.caretRow }])
                break

            // Tab means "give me the drawing". Left to Qt it walks the focus
            // chain and lands somewhere with nothing to show for it, which is
            // how you end up not knowing where the focus is at all.
            case Qt.Key_Tab:
            case Qt.Key_Backtab:
                win.focusCanvas()
                if (win.caretColumn < 0)
                    win.moveCaret(0, 0)
                break
            case Qt.Key_Plus:
            case Qt.Key_Equal:
                stage.zoomStep(stage.width / 2, stage.height / 2, true); break
            case Qt.Key_Minus:
                stage.zoomStep(stage.width / 2, stage.height / 2, false); break
            default:
                // The digits: a colour each. Shift puts the current colour on
                // one, plain uses it -- and painting on use, so choosing a
                // colour and applying it is one press rather than two.
                if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
                    var digit = (event.key === Qt.Key_0) ? 9 : event.key - Qt.Key_1

                    // In pick mode a digit COLLECTS instead of choosing: the
                    // colour under the cursor goes onto that number.
                    if (win.mode === "pick") {
                        if (win.caretColumn >= 0) {
                            var there = doc.slotAt(win.caretColumn, win.caretRow)
                            win.putOnDigit(digit, there)
                            win.slot = there
                        }
                        break
                    }

                    if (event.modifiers & Qt.ShiftModifier) {
                        win.putOnDigit(digit, win.slot)
                        break
                    }

                    var which = win.registerAt(digit)
                    if (which === "")
                        break
                    win.slot = which

                    // A pending line is drawn in the colour you name.
                    if (win.linePoints.length > 0) {
                        win.commitLine(which)
                        break
                    }

                    // Held down in draw mode, it paints as you travel. The
                    // press itself lays the first pixel down, so tapping it is
                    // the same as pressing Return.
                    if (win.mode === "draw" && !event.isAutoRepeat) {
                        win.heldSlot = which
                        doc.beginStroke()
                        win.trail()
                        break
                    }

                    if (win.caretColumn >= 0) {
                        doc.beginStroke()
                        doc.paint(win.caretColumn, win.caretRow, which)
                        doc.endStroke()
                    }
                    break
                }
                event.accepted = false
                return
            }
            event.accepted = true
        }

        Column {
            anchors.fill: parent
            spacing: 0

            // ------------------------------------------------------- menu bar

            // The menu bar and the document, on one line. The document name
            // belongs beside the commands that act on it, not only in the
            // window title -- which a tiling compositor may not draw at all.
            Rectangle {
                id: header
                width: parent.width
                height: 30
                color: theme.panel

                Text {
                    anchors.centerIn: parent
                    text: "omapixel"
                    color: theme.foreground
                    font.family: theme.fontFamily
                    font.pixelSize: 12
                    font.bold: true
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: (doc.path === "" ? "untitled"
                                           : doc.path.split("/").pop())
                          + (doc.dirty ? "  •" : "")
                    color: doc.dirty ? theme.foreground : theme.dim
                    font.family: theme.fontFamily
                    font.pixelSize: 12
                }

            Bar {
                id: menus
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter

                Drop {
                    title: "File"
                    Cmd { action: actNew }
                    Cmd { action: actOpen }
                    Rule {}
                    Cmd { action: actSave }
                    Cmd { action: actSaveAs }
                    Rule {}
                    Cmd { action: actExport }
                    Cmd { action: actExportSheet }
                    Rule {}
                    Cmd { action: actQuit }
                }

                Drop {
                    title: "Edit"
                    Cmd { action: actUndo }
                    Cmd { action: actRedo }
                    Rule {}
                    Cmd { action: actClear }
                    Cmd { action: actFlipX }
                    Cmd { action: actFlipY }
                }

                Drop {
                    title: "Sprite"
                    Cmd { action: actResize }
                    Rule {}
                    Cmd { action: actAddClip }
                    Cmd { action: actRemoveClip }
                    Rule {}
                    Cmd { action: actColour }
                    Cmd { action: actReplace }
                    Cmd { action: actRoulette }
                    Rule {}
                    Cmd { action: actAddFrame }
                    Cmd { action: actDupFrame }
                    Cmd { action: actDelFrame }
                    Rule {}
                    Cmd { action: actPlay }
                }

                Drop {
                    title: "View"
                    Cmd { action: actZoomIn }
                    Cmd { action: actZoomOut }
                    Cmd { action: actFit }
                    Rule {}
                    Cmd { action: actOnion }
                    Cmd { action: actMesh }
                    Rule {}
                    Cmd { action: actReference }
                    Rule {}
                    Cmd { action: actHints }
                }
            }
            }

            Rectangle { width: parent.width; height: 1
                        color: theme.fill(theme.foreground, 0.18) }

            // ----------------------------------------------------------- body

            Row {
                width: parent.width
                height: parent.height - header.height - timeline.height - status.height - 2
                spacing: 0

                Rectangle {
                    id: tools
                    width: 46
                    height: parent.height
                    color: theme.panel
                    // The hover captions hang outside this strip, over the
                    // canvas. A high z INSIDE the strip does not lift them
                    // above the strip's own siblings, so the strip is what has
                    // to be raised.
                    z: 5

                    // Tools, then the ten colours, in one column. They belong
                    // together: both answer "what will the next press do", and
                    // both are chosen with a single key. Scrollable because a
                    // short window should lose the bottom of the list rather
                    // than silently drop half of it.
                    Flickable {
                        anchors.fill: parent
                        anchors.topMargin: 8
                        contentHeight: strip.height + 16
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true

                    Column {
                        id: strip
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 4

                        ToolButton { glyph: "B"; key: "B"; caption: "Pencil"
                                     on: win.tool === "pencil"
                                     onClicked: win.tool = "pencil" }
                        ToolButton { glyph: "E"; key: "E"; caption: "Eraser"
                                     on: win.tool === "eraser"
                                     onClicked: win.tool = "eraser" }
                        ToolButton { glyph: "F"; key: "F"; caption: "Bucket"
                                     on: win.tool === "bucket"
                                     onClicked: win.tool = "bucket" }
                        ToolButton { glyph: "I"; key: "I"; caption: "Picker"
                                     on: win.tool === "picker"
                                     onClicked: win.tool = "picker" }
                        ToolButton { glyph: "H"; key: "H"; caption: "Pan"
                                     on: win.tool === "hand"
                                     onClicked: win.tool = "hand" }

                        Rectangle { width: 24; height: 1; x: 5
                                    color: theme.fill(theme.foreground, 0.18) }

                        // The ten colours on the number keys, with the one in
                        // use marked the way the active tool is. The colour you
                        // draw with is the same kind of fact as the tool you
                        // draw with, and was the only one of the two you could
                        // not see without opening a panel.
                        Repeater {
                            model: 10

                            Rectangle {
                                id: chip
                                required property int index
                                readonly property string letter: win.registerAt(index)
                                readonly property bool inUse: letter !== ""
                                                              && win.slot === letter

                                width: 34
                                height: 34
                                radius: theme.rounding
                                color: letter === "" ? "transparent" : doc.colourOf(letter)
                                border.width: inUse ? 2 : 1
                                border.color: inUse ? theme.accent
                                            : chipHover.hovered
                                              ? theme.fill(theme.foreground, 0.35)
                                              : theme.fill(theme.foreground, 0.18)

                                Text {
                                    anchors.centerIn: parent
                                    text: chip.index === 9 ? "0" : String(chip.index + 1)
                                    color: {
                                        if (chip.letter === "")
                                            return theme.dim
                                        var c = Qt.color(doc.colourOf(chip.letter))
                                        var lum = 0.2126 * c.r + 0.7152 * c.g
                                                + 0.0722 * c.b
                                        return lum > 0.5 ? "#101010" : "#f0f0f0"
                                    }
                                    font.family: theme.fontFamily
                                    font.pixelSize: 11
                                    font.bold: chip.inUse
                                }

                                HoverHandler { id: chipHover }
                                // Choosing, not painting: a click here is the
                                // hand that is already on the mouse, and it
                                // will draw by clicking the canvas next.
                                TapHandler {
                                    onTapped: if (chip.letter !== "") win.slot = chip.letter
                                }

                                Rectangle {
                                    visible: chipHover.hovered
                                    x: parent.width + 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: note.implicitWidth + 16
                                    height: 24
                                    radius: theme.rounding
                                    color: theme.panel
                                    border.width: 1
                                    border.color: theme.fill(theme.foreground, 0.25)
                                    z: 50

                                    Text {
                                        id: note
                                        anchors.centerIn: parent
                                        text: chip.letter === ""
                                              ? "empty — press c to find a colour"
                                              : "slot " + chip.letter + "   "
                                                + String(doc.colourOf(chip.letter)).toUpperCase()
                                        color: theme.foreground
                                        font.family: theme.fontFamily
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }

                        Rectangle { width: 24; height: 1; x: 5
                                    color: theme.fill(theme.foreground, 0.18) }

                        // The gamble. Its own colour and its own place at the
                        // bottom, away from the ten you chose on purpose.
                        Rectangle {
                            id: gamble
                            width: 34
                            height: 34
                            radius: theme.rounding
                            color: gambleHover.hovered ? theme.fill(theme.urgent, 0.22)
                                                       : theme.fill(theme.urgent, 0.10)
                            border.width: 1
                            border.color: gambleHover.hovered
                                          ? theme.urgent
                                          : theme.fill(theme.urgent, 0.45)

                            Text {
                                anchors.centerIn: parent
                                text: "?"
                                color: theme.urgent
                                font.family: theme.fontFamily
                                font.pixelSize: 16
                                font.bold: true
                            }

                            HoverHandler { id: gambleHover }
                            TapHandler { onTapped: win.russianRoulette() }

                            Rectangle {
                                visible: gambleHover.hovered
                                x: parent.width + 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: shot.implicitWidth + 16
                                height: 24
                                radius: theme.rounding
                                color: theme.panel
                                border.width: 1
                                border.color: theme.fill(theme.foreground, 0.25)
                                z: 50

                                Text {
                                    id: shot
                                    anchors.centerIn: parent
                                    text: "russian roulette   R"
                                    color: theme.foreground
                                    font.family: theme.fontFamily
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                    }
                }

                Rectangle { width: 1; height: parent.height
                            color: theme.fill(theme.foreground, 0.18) }

                // The canvas and its own footer. The hints belong to the thing
                // they are about: keys that draw, under the place you draw.
                // Across the whole window they read as a property of the
                // window, and sit as far from the drawing as the layout allows.
                Item {
                    id: middle
                    width: parent.width - tools.width - dockPanel.width - 2
                    height: parent.height

                    Surface {
                        id: stage
                        objectName: "stage"
                        onPressedOnCanvas: win.focusCanvas()
                        width: parent.width
                        height: parent.height - hintbar.height
                    }

                    Rectangle {
                        anchors.bottom: hintbar.top
                        width: parent.width
                        height: hintbar.visible ? 1 : 0
                        color: theme.fill(theme.foreground, 0.18)
                    }

                    Hints {
                        id: hintbar
                        anchors.bottom: parent.bottom
                        width: parent.width
                        visible: win.showHints
                        height: visible ? 24 : 0
                        model: win.hints
                    }
                }

                Rectangle { width: 1; height: parent.height
                            color: theme.fill(theme.foreground, 0.18) }

                // --------------------------------------------------- the dock

                Rectangle {
                    id: dockPanel
                    width: 268
                    height: parent.height
                    color: theme.panel

                    Flickable {
                        id: dock
                        anchors.fill: parent
                        anchors.margins: 10
                        contentHeight: panels.height
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true

                        Column {
                            id: panels
                            width: dock.width
                            spacing: 6

                            Section {
                                id: paletteSection
                                title: "Palette"
                                hint: doc.palette.length + " slots"

                                // Nine rows, then it scrolls. A palette can run
                                // to hundreds of slots, and letting it push the
                                // panels below it off the bottom of the window
                                // costs more than it gives -- the ones you are
                                // working with are at the top, and the rest are
                                // a scroll or a click away.
                                property bool showAll: false
                                readonly property int rowsShown: 9
                                readonly property int swatchPitch: 28

                                Flickable {
                                    width: parent.width
                                    height: Math.min(swatches.implicitHeight,
                                                     paletteSection.showAll
                                                     ? swatches.implicitHeight
                                                     : paletteSection.rowsShown
                                                       * paletteSection.swatchPitch)
                                    contentHeight: swatches.implicitHeight
                                    boundsBehavior: Flickable.StopAtBounds
                                    clip: true

                                    Behavior on height {
                                        NumberAnimation { duration: 110
                                                          easing.type: Easing.OutCubic }
                                    }

                                Flow {
                                    id: swatches
                                    width: parent.width
                                    spacing: 4

                                    Repeater {
                                        model: [{ slot: ".", colour: "" }].concat(doc.palette)

                                        Rectangle {
                                            id: pip
                                            required property var modelData
                                            width: 24
                                            height: 24
                                            radius: theme.rounding
                                            color: modelData.slot === "." ? "transparent"
                                                                          : modelData.colour
                                            border.width: win.slot === modelData.slot ? 2 : 1
                                            border.color: win.slot === modelData.slot
                                                          ? theme.accent
                                                          : theme.fill(theme.foreground, 0.18)

                                            // The empty slot draws as an X: a
                                            // transparent square is a square the
                                            // colour of the background, and
                                            // nobody guesses that is the eraser.
                                            // The digit that reaches this
                                            // slot, in the corner. A shortcut
                                            // you cannot see is a shortcut
                                            // only its author remembers.
                                            Text {
                                                anchors.right: parent.right
                                                anchors.top: parent.top
                                                anchors.margins: 1
                                                readonly property int at:
                                                    win.registers.indexOf(pip.modelData.slot)
                                                visible: at >= 0
                                                text: at === 9 ? "0" : String(at + 1)
                                                color: theme.accent
                                                font.family: theme.fontFamily
                                                font.pixelSize: 8
                                                font.bold: true
                                            }

                                            Text {
                                                anchors.centerIn: parent
                                                text: pip.modelData.slot === "." ? "×"
                                                                                 : pip.modelData.slot
                                                // Against the swatch's own
                                                // colour, not the theme's: a
                                                // dark slot with a dark letter
                                                // is an unlabelled square.
                                                color: {
                                                    if (pip.modelData.slot === ".")
                                                        return theme.dim
                                                    var c = Qt.color(pip.modelData.colour)
                                                    var lum = 0.2126 * c.r + 0.7152 * c.g
                                                            + 0.0722 * c.b
                                                    return lum > 0.5 ? "#101010" : "#f0f0f0"
                                                }
                                                font.family: theme.fontFamily
                                                font.pixelSize: 10
                                            }

                                            TapHandler {
                                                onTapped: {
                                                    win.slot = pip.modelData.slot
                                                    if (win.tool === "eraser")
                                                        win.tool = "pencil"
                                                }
                                            }
                                        }
                                    }
                                }
                                }

                                Chip {
                                    visible: swatches.implicitHeight
                                             > paletteSection.rowsShown
                                               * paletteSection.swatchPitch
                                    label: paletteSection.showAll
                                           ? "show nine rows"
                                           : "show all " + doc.palette.length
                                    on: paletteSection.showAll
                                    onClicked: paletteSection.showAll = !paletteSection.showAll
                                }

                                Row {
                                    spacing: 6

                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 0
                                        width: 26
                                        height: 26
                                        radius: theme.rounding
                                        color: win.slot === "." ? "transparent"
                                                                : doc.colourOf(win.slot)
                                        border.width: 1
                                        border.color: theme.fill(theme.foreground, 0.25)
                                    }

                                Field {
                                    onEscaped: win.focusCanvas()
                                    label: "colour of slot " + win.slot
                                    boxWidth: 168
                                    value: {
                                        var found = doc.palette.filter(function (e) {
                                            return e.slot === win.slot
                                        })
                                        return found.length > 0 ? found[0].colour : ""
                                    }
                                    // On Enter, not on every keystroke: "#FF0"
                                    // is a valid colour on the way to "#FF0055",
                                    // and each one would land its own undo step.
                                    onCommitted: function (text) {
                                        doc.setPaletteColour(win.slot, text)
                                    }
                                }
                                }

                                // Said out loud, because it is the one thing
                                // here that reaches back into the drawing. It
                                // is the best feature of this format and the
                                // worst surprise: the picker on `c` adds a
                                // colour, this one changes what is already
                                // painted.
                                Label {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    text: "changes every pixel drawn with "
                                          + (win.slot === "." ? "it" : win.slot)
                                }
                            }

                            Section {
                                title: "Preview"
                                hint: Math.round(win.zoom) + "×"

                                // The 1x tile doubles as the overview: it shows
                                // the whole drawing, so the frame of what is on
                                // screen and a click to go there cost one
                                // rectangle each.
                                Flow {
                                    width: parent.width
                                    spacing: 10

                                    Repeater {
                                        model: [1, 2, 3]

                                        Column {
                                            id: real
                                            required property int modelData
                                            visible: doc.columns * modelData <= 240
                                                     && doc.rows * modelData <= 200
                                            spacing: 3

                                            Rectangle {
                                                id: tile
                                                width: Math.max(30, doc.columns * real.modelData + 8)
                                                height: Math.max(30, doc.rows * real.modelData + 8)
                                                radius: theme.rounding
                                                color: theme.sunken
                                                clip: true

                                                readonly property bool isMap:
                                                    real.modelData === 1
                                                    && (stage.scrollsX || stage.scrollsY)

                                                PixelGridItem {
                                                    id: mini
                                                    anchors.centerIn: parent
                                                    model: doc
                                                    clip: doc.clip
                                                    frame: doc.frame
                                                    cell: real.modelData
                                                }

                                                Rectangle {
                                                    visible: tile.isMap
                                                    color: theme.fill(theme.accent, 0.14)
                                                    border.width: 1
                                                    border.color: theme.accent
                                                    x: mini.x + Math.max(0, stage.viewColumn)
                                                    y: mini.y + Math.max(0, stage.viewRow)
                                                    width: Math.min(mini.width - (x - mini.x),
                                                                    stage.viewColumns)
                                                    height: Math.min(mini.height - (y - mini.y),
                                                                     stage.viewRows)
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    enabled: tile.isMap
                                                    cursorShape: tile.isMap
                                                                 ? Qt.PointingHandCursor
                                                                 : Qt.ArrowCursor
                                                    onPressed: function (mouse) {
                                                        stage.centreOn(mouse.x - mini.x,
                                                                       mouse.y - mini.y)
                                                    }
                                                    onPositionChanged: function (mouse) {
                                                        if (pressed)
                                                            stage.centreOn(mouse.x - mini.x,
                                                                           mouse.y - mini.y)
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
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    visible: doc.columns > 240
                                    text: "too wide to show at true size"
                                }
                            }

                            Section {
                                id: spriteSection
                                title: "Sprite"
                                hint: doc.columns + "×" + doc.rows
                                open: false

                                Flow {
                                    width: parent.width
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
                                        onEscaped: win.focusCanvas()
                                        label: "columns"
                                        boxWidth: 97
                                        value: String(win.wantColumns)
                                        onEdited: function (text) {
                                            var n = parseInt(text, 10)
                                            if (isFinite(n) && n > 0)
                                                win.wantColumns = Math.min(512, n)
                                        }
                                    }
                                    Field {
                                        onEscaped: win.focusCanvas()
                                        label: "rows"
                                        boxWidth: 97
                                        value: String(win.wantRows)
                                        onEdited: function (text) {
                                            var n = parseInt(text, 10)
                                            if (isFinite(n) && n > 0)
                                                win.wantRows = Math.min(512, n)
                                        }
                                    }
                                }

                                // Shrinking crops, so the button says what it
                                // will cost before it is pressed. Nobody reads a
                                // dialog; everybody reads a number on a button.
                                Label {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    visible: win.wouldLose > 0
                                    color: theme.urgent
                                    text: "resizing crops " + win.wouldLose + " drawn pixel(s)"
                                }

                                Chip {
                                    label: "resize"
                                    on: win.sizeChanged
                                    usable: win.sizeChanged
                                    role: win.wouldLose > 0 ? theme.urgent : theme.accent
                                    onClicked: doc.resize(win.wantColumns, win.wantRows)
                                }
                            }

                            Section {
                                title: "Reference"
                                hint: win.referencePath === ""
                                      ? "none" : Math.round(win.referenceAlpha * 100) + "%"
                                open: false

                                Chip {
                                    label: "choose an image…"
                                    onClicked: referenceDialog.open()
                                }

                                Flow {
                                    width: parent.width
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

                                Row {
                                    spacing: 5
                                    Chip {
                                        label: win.referenceOnTop ? "on top" : "behind"
                                        on: win.referenceOnTop
                                        usable: win.referencePath !== ""
                                        onClicked: win.referenceOnTop = !win.referenceOnTop
                                    }
                                    Chip {
                                        label: "clear"
                                        usable: win.referencePath !== ""
                                        onClicked: win.referencePath = ""
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1
                        color: theme.fill(theme.foreground, 0.18) }

            Timeline {
                id: timeline
                width: parent.width
            }

            StatusBar {
                id: status
                width: parent.width
                column: stage.hoverColumn
                row: stage.hoverRow
            }
        }
    }

    // ---------------------------------------------------------------- dialogs

    FileDialog {
        id: openDialog
        title: "Open a document"
        nameFilters: ["omapixel documents (*.json)", "All files (*)"]
        onAccepted: doc.open(selectedFile)
    }

    FileDialog {
        id: saveDialog
        title: "Save the document"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["omapixel documents (*.json)", "All files (*)"]
        onAccepted: doc.save(selectedFile)
    }

    FileDialog {
        id: referenceDialog
        title: "Choose a reference image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)", "All files (*)"]
        onAccepted: {
            win.referencePath = selectedFile.toString().replace("file://", "")
            if (win.referenceAlpha === 0)
                win.referenceAlpha = 0.5
        }
    }

    FileDialog {
        id: exportDialog
        title: "Export a PNG"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: ["PNG images (*.png)", "All files (*)"]
        onAccepted: doc.exportImage(selectedFile, exportSheet.factor,
                                    exportSheet.asSheet, exportSheet.checker)
    }

    Sheet {
        id: newSheet
        onClosed: win.focusCanvas()
        title: "New document"

        property int columns: 32
        property int rows: 24

        body: [
            Flow {
                width: 320
                spacing: 5

                Repeater {
                    model: doc.sizePresets()

                    Chip {
                        required property var modelData
                        label: modelData.w + "×" + modelData.h
                               + (modelData.why === "" ? "" : "  " + modelData.why)
                        on: newSheet.columns === modelData.w && newSheet.rows === modelData.h
                        onClicked: {
                            newSheet.columns = modelData.w
                            newSheet.rows = modelData.h
                        }
                    }
                }
            },
            Row {
                spacing: 6
                Field {
                    onEscaped: win.focusCanvas()
                    label: "columns"
                    boxWidth: 130
                    value: String(newSheet.columns)
                    onEdited: function (text) {
                        var n = parseInt(text, 10)
                        if (isFinite(n) && n > 0) newSheet.columns = Math.min(512, n)
                    }
                }
                Field {
                    onEscaped: win.focusCanvas()
                    label: "rows"
                    boxWidth: 130
                    value: String(newSheet.rows)
                    onEdited: function (text) {
                        var n = parseInt(text, 10)
                        if (isFinite(n) && n > 0) newSheet.rows = Math.min(512, n)
                    }
                }
            },
            Label {
                width: 320
                wrapMode: Text.Wrap
                visible: doc.dirty
                color: theme.urgent
                text: "the open document has unsaved changes — this replaces it"
            },
            Row {
                spacing: 6
                Chip {
                    label: "create"
                    on: true
                    onClicked: {
                        doc.reset(newSheet.columns, newSheet.rows)
                        newSheet.close()
                    }
                }
                Chip { label: "cancel"; onClicked: newSheet.close() }
            }
        ]
    }

    Sheet {
        id: colourSheet
        title: purpose === "replace" ? "Replace slot " + replacing
                                      : "Choose a colour"
        onClosed: { armed = false; win.focusCanvas() }

        property string chosen: "#7AA2F7"
        // What the panel will do with the colour: put it on a number key, or
        // repaint every pixel of the one in focus. The search is the same
        // either way; only the last step differs, so it is one panel.
        property string purpose: "assign"
        property string replacing: ""
        // True once a colour has been settled on and the panel is waiting to
        // be told which number key it goes on. Two steps because the digits
        // are needed for typing a hex until the colour is chosen, and the same
        // key cannot mean two things at once.
        property bool armed: false

        function show(why) {
            purpose = why === "replace" ? "replace" : "assign"
            replacing = win.focusedSlot
            chosen = win.slot === "." ? "#7AA2F7"
                                      : String(doc.colourOf(win.slot)).toUpperCase()
            armed = false
            search.value = ""
            refine("")
            open()
            // Straight into the field. A panel that opens on a key and then
            // makes you reach for the mouse to type in it has given the key
            // back.
            search.focusEntry()
        }

        function refine(query) {
            found.model = doc.findColours(query)
            found.currentIndex = found.model.length > 0 ? 0 : -1
            take(found.currentIndex)
        }

        function take(row) {
            if (row >= 0 && row < found.model.length)
                chosen = found.model[row].colour
        }

        function arm(everywhere) {
            if (found.model.length === 0)
                return
            // Replacing has nowhere else to go: the colour it acts on was
            // already chosen by where the cursor was standing.
            if (purpose === "replace") {
                doc.replaceColour(replacing, chosen, everywhere === true)
                close()
                return
            }
            armed = true
            digits.forceActiveFocus()
        }

        function place(digit) {
            win.colourOnDigit(digit, chosen)
            close()
        }

        body: [
            Field {
                id: search
                label: "search by name, or type a hex"
                boxWidth: 380
                enabled: !colourSheet.armed
                onEdited: function (text) { colourSheet.refine(text) }
                // Up and down walk the matches without leaving the box, so
                // searching and choosing are one gesture rather than two.
                onStepped: function (delta) {
                    if (found.model.length === 0)
                        return
                    found.currentIndex = Math.max(
                        0, Math.min(found.model.length - 1, found.currentIndex + delta))
                    colourSheet.take(found.currentIndex)
                }
                // Shift decides the scope, so the two are one keystroke apart
                // instead of one being buried behind a button.
                onConfirmed: function (text, modifiers) {
                    colourSheet.arm((modifiers & Qt.ShiftModifier) !== 0)
                }
                onEscaped: colourSheet.close()
            },

            // The matches. A list rather than a grid: the name is half of what
            // you are looking for, and a grid of swatches makes you hover every
            // one of them to find out which is which.
            Rectangle {
                width: 380
                height: 200
                radius: theme.rounding
                color: theme.sunken
                border.width: 1
                border.color: theme.fill(theme.foreground, 0.18)
                clip: true

                ListView {
                    id: found
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    // Keeps the highlighted row on screen while the arrows
                    // walk past the bottom of the box.
                    highlightRangeMode: ListView.ApplyRange
                    preferredHighlightBegin: 26
                    preferredHighlightEnd: height - 26

                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: found.width
                        height: 26
                        radius: theme.rounding
                        color: index === found.currentIndex
                               ? theme.fill(theme.accent, 0.18)
                               : (swatchHover.hovered ? theme.fill(theme.foreground, 0.08)
                                                      : "transparent")

                        Rectangle {
                            x: 5
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18; height: 18
                            radius: theme.rounding
                            color: parent.modelData.colour
                            border.width: 1
                            border.color: theme.fill(theme.foreground, 0.25)
                        }
                        Text {
                            x: 32
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.modelData.name
                            color: theme.foreground
                            font.family: theme.fontFamily
                            font.pixelSize: 12
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.modelData.colour
                            color: theme.dim
                            font.family: theme.fontFamily
                            font.pixelSize: 11
                        }

                        HoverHandler { id: swatchHover }
                        TapHandler {
                            onTapped: {
                                found.currentIndex = parent.index
                                colourSheet.take(parent.index)
                                // Clicking a colour chooses it; in replace mode
                                // the scope is a separate, explicit press
                                // below, because a click that repaints twelve
                                // frames is not a click anybody expects.
                                if (colourSheet.purpose !== "replace")
                                    colourSheet.arm()
                            }
                        }
                    }
                }
            },

            // What happens next, said plainly, and changing when it changes.
            Row {
                spacing: 8

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26; height: 26
                    radius: theme.rounding
                    color: colourSheet.chosen
                    border.width: 1
                    border.color: theme.fill(theme.foreground, 0.25)
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 340
                    wrapMode: Text.Wrap
                    color: colourSheet.armed ? theme.accent : theme.dim
                    text: {
                        if (colourSheet.purpose === "replace")
                            return colourSheet.chosen + " — Enter repaints "
                                   + doc.countSlot(colourSheet.replacing, false)
                                   + " pixel(s) in this frame,  Shift+Enter "
                                   + doc.countSlot(colourSheet.replacing, true)
                                   + " in every frame"
                        return colourSheet.armed
                               ? colourSheet.chosen + " — now press a number key to keep it there"
                               : colourSheet.chosen + " — Enter to choose it"
                    }
                }
            },

            // The ten number keys, with what is on each. Clicking one does the
            // same as pressing it, for a hand that is already on the mouse.
            Row {
                spacing: 6
                visible: colourSheet.purpose === "replace"

                Chip {
                    label: "this frame"
                    on: true
                    onClicked: colourSheet.arm(false)
                }
                Chip {
                    label: "every frame"
                    role: theme.urgent
                    onClicked: colourSheet.arm(true)
                }
                Chip { label: "cancel"; onClicked: colourSheet.close() }
            },

            Item {
                id: digits
                width: 380
                height: colourSheet.purpose === "replace" ? 0 : 34
                visible: colourSheet.purpose !== "replace"
                focus: colourSheet.armed

                Keys.onPressed: function (event) {
                    if (event.key === Qt.Key_Escape) {
                        colourSheet.armed = false
                        search.focusEntry()
                        event.accepted = true
                        return
                    }
                    if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
                        colourSheet.place(event.key === Qt.Key_0 ? 9
                                                                 : event.key - Qt.Key_1)
                        event.accepted = true
                        return
                    }
                    event.accepted = false
                }

                Row {
                    spacing: 5

                    Repeater {
                        model: 10

                        Rectangle {
                            required property int index
                            width: 34
                            height: 34
                            radius: theme.rounding
                            readonly property string letter: win.registerAt(index)
                            color: letter === "" ? "transparent" : doc.colourOf(letter)
                            border.width: 1
                            border.color: colourSheet.armed
                                          ? theme.accent
                                          : theme.fill(theme.foreground, 0.25)

                            Text {
                                anchors.centerIn: parent
                                text: parent.index === 9 ? "0" : String(parent.index + 1)
                                color: {
                                    if (parent.letter === "")
                                        return theme.dim
                                    var c = Qt.color(doc.colourOf(parent.letter))
                                    var lum = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b
                                    return lum > 0.5 ? "#101010" : "#f0f0f0"
                                }
                                font.family: theme.fontFamily
                                font.pixelSize: 12
                                font.bold: true
                            }

                            TapHandler { onTapped: colourSheet.place(parent.index) }
                        }
                    }
                }
            }
        ]
    }

    Sheet {
        id: exportSheet
        onClosed: win.focusCanvas()
        title: asSheet ? "Export a sprite sheet" : "Export a PNG"

        property bool asSheet: false
        property int factor: 8
        property bool checker: false

        body: [
            Label {
                width: 320
                wrapMode: Text.Wrap
                text: exportSheet.asSheet
                      ? "Every frame of " + doc.clip + ", side by side: "
                        + (doc.columns * doc.frameCount * exportSheet.factor) + "×"
                        + (doc.rows * exportSheet.factor) + " pixels."
                      : "The open frame at " + (doc.columns * exportSheet.factor) + "×"
                        + (doc.rows * exportSheet.factor) + " pixels."
            },
            Row {
                spacing: 5
                Label { text: "scale"; anchors.verticalCenter: parent.verticalCenter }
                Repeater {
                    model: [1, 2, 4, 8, 16]
                    Chip {
                        required property int modelData
                        label: modelData + "×"
                        on: exportSheet.factor === modelData
                        onClicked: exportSheet.factor = modelData
                    }
                }
            },
            Chip {
                label: "chequerboard behind transparency"
                on: exportSheet.checker
                onClicked: exportSheet.checker = !exportSheet.checker
            },
            Row {
                spacing: 6
                Chip {
                    label: "choose a file…"
                    on: true
                    onClicked: {
                        exportDialog.currentFile =
                            "file://" + doc.suggestedExportPath(exportSheet.asSheet)
                        exportSheet.close()
                        exportDialog.open()
                    }
                }
                Chip { label: "cancel"; onClicked: exportSheet.close() }
            }
        ]
    }
}
