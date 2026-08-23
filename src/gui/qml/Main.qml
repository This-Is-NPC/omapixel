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

    // From the config file, so a person who wants the studio to open the size
    // of their screen says so once. A drag on the window edge replaces the
    // binding, which is what a window resize should do.
    width: cfg.settings["window.width"]
    height: cfg.settings["window.height"]
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: (doc.dirty ? "• " : "") + (doc.path === "" ? T.t("file.untitled") : doc.path)
           + " — omapixel"
    color: theme.background

    property string tool: "pencil"
    property string slot: "I"
    property real zoom: 12
    function zoomLabel() {
        return (Math.round(zoom * 100) / 100) + "×"
    }
    property bool onion: cfg.settings["canvas.onion"]
    property bool mesh: cfg.settings["canvas.grid"]
    property bool playing: false
    property string pendingAction: ""
    property bool allowClosing: false

    /// Whether ▶ starts the clip over at the end. `playback.loop` in the
    /// config, and **View → Loop playback** while the window is open: a loop is
    /// right for judging movement and wrong for judging the last frame, and
    /// which of those you are doing changes several times an hour.
    property bool loop: cfg.settings["playback.loop"]

    /// Play, pause, and the one case that needs thinking about: pressing play
    /// while stopped on the last frame with looping off. Doing nothing there
    /// reads as a broken button, so it starts over.
    function togglePlay() {
        if (win.playing) {
            win.playing = false
            return
        }
        if (!win.loop && doc.frame >= doc.frameCount - 1)
            doc.frame = 0
        win.playing = true
    }

    /// How far shift and an arrow jumps. `canvas.big_step` in the config.
    readonly property int bigStep: Math.max(1, cfg.settings["canvas.big_step"])

    // The keyboard cursor. Drawing with a mouse is fine for shapes and hopeless
    // for placing one pixel exactly; -1 means it has not been used yet and
    // nothing is drawn for it.
    property bool showHints: cfg.settings["window.hints"]

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

    function reconcileRegisters() {
        var known = doc.palette.map(function (entry) { return entry.slot })
        var next = registers.map(function (entry) {
            return entry === "." || known.indexOf(entry) >= 0 ? entry : ""
        })
        registers = next
        if (slot !== "." && known.indexOf(slot) < 0) {
            var usable = next.filter(function (entry) { return entry !== "" })
            slot = usable.length > 0 ? usable[0] : "."
        }
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
            doc.say(T.t("note.paletteFull"))
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
        if (!doc.hasSelection && caretColumn < 0)
            moveCaret(0, 0)
        var letter = doc.freeSlot()
        if (letter === "") {
            doc.say(T.t("note.paletteFull"))
            return
        }
        // One stroke around both edits: adding the colour and painting with it
        // is one act, so it costs one document snapshot and one undo step
        // rather than two of each.
        var hex = doc.randomColour()
        doc.beginStroke()
        doc.setPaletteColour(letter, hex)
        slot = letter
        doc.paint(caretColumn, caretRow, letter)
        doc.endStroke()
        doc.say(T.t("note.roulette").arg(hex).arg(letter))
    }

    function putOnDigit(digit, which) {
        var next = registers.slice()
        while (next.length <= digit)
            next.push("")
        next[digit] = which
        registers = next
        doc.say(T.t("note.nowOn")
                    .arg(which === "." ? T.t("note.empty")
                                       : T.t("note.slot").arg(which))
                    .arg(digit === 9 ? 0 : digit + 1))
    }

    function useSlot(which, paintIt) {
        if (which === "")
            return
        slot = which
        if (paintIt && (doc.hasSelection || caretColumn >= 0)) {
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
            return [{ key: "A–Z  a–g", label: T.t("hint.slotToUse") },
                    { key: ".", label: T.t("hint.empty") },
                    { key: cfg.keys.cancel, label: T.t("hint.cancel") }]

        if (linePoints.length > 0)
            return [{ key: "arrows", label: T.t("hint.moveFreeEnd") },
                    { key: cfg.keys.line_point, label: T.t("hint.pinCorner") },
                    { key: "1–0", label: T.t("hint.drawInColour") },
                    { key: cfg.keys.paint, label: T.t("hint.drawIt") },
                    { key: cfg.keys.cancel, label: T.t("hint.throwAway") }]

        if (mode === "pick")
            return [{ key: "arrows", label: T.t("hint.moveOverColour") },
                    { key: "1–0", label: T.t("hint.putOnNumber") },
                    { key: cfg.keys.cancel, label: T.t("hint.leavePicking") }]

        if (mode === "draw")
            return [{ key: "hold 1–0", label: T.t("hint.paintAsYouMove") },
                    { key: cfg.keys.paint, label: T.t("hint.onePixel") },
                    { key: cfg.keys.line_point, label: T.t("hint.straightLine") },
                    { key: cfg.keys.cancel, label: T.t("hint.leaveDrawing") }]

        if (caretColumn >= 0)
            return [{ key: "arrows", label: T.t("hint.move") },
                    { key: "Shift", label: T.t("hint.select") },
                    { key: "Ctrl", label: T.t("hint.byEight").arg(win.bigStep) },
                    { key: cfg.keys.paint,
                      label: T.t(doc.hasSelection ? "hint.paintSelection"
                                                  : "hint.paintPixel") },
                    { key: cfg.keys.erase, label: T.t("hint.eraseIt") },
                    { key: "1–0", label: T.t("hint.colours") },
                    { key: cfg.keys.draw_mode, label: T.t("hint.drawAsYouMove") },
                    { key: cfg.keys.pick_mode, label: T.t("hint.pickColours") },
                    { key: cfg.keys.choose_colour, label: T.t("hint.findColour") },
                    { key: cfg.keys.roulette, label: T.t("hint.roulette") },
                    { key: cfg.keys.replace_colour, label: T.t("hint.replaceColour") },
                    { key: cfg.keys.line_point, label: T.t("hint.straightLine") },
                    { key: cfg.keys.cancel, label: T.t("hint.putAway") }]

        return [{ key: "arrows", label: T.t("hint.drawWithKeyboard") },
                { key: [cfg.keys.tool_pencil, cfg.keys.tool_eraser, cfg.keys.tool_bucket,
                        cfg.keys.tool_picker, cfg.keys.tool_hand].join(" "),
                  label: T.t("hint.tools") },
                { key: "1–0", label: T.t("hint.colours") },
                { key: cfg.keys.slot_leader, label: T.t("hint.changeColour") },
                { key: cfg.keys.play, label: T.t("hint.play") },
                { key: cfg.keys.frame_previous + "  " + cfg.keys.frame_next,
                  label: T.t("hint.frame") },
                { key: cfg.keys.clip_previous + "  " + cfg.keys.clip_next,
                  label: T.t("hint.clip") },
                { key: "Tab", label: T.t("hint.controls") },
                { key: cfg.keys.menus, label: T.t("hint.menus") },
                { key: cfg.keys.undo, label: T.t("hint.undo") },
                { key: cfg.keys.save, label: T.t("hint.save") },
                { key: cfg.keys.export_png, label: T.t("hint.export") }]
    }

    // Waiting for the letter of a palette slot. Every letter is already a tool
    // or a toggle, so choosing a colour by its letter needs a key to say that
    // the next one names a colour -- the way a leader key works.
    property bool awaitingSlot: false

    property int caretColumn: -1
    property int caretRow: -1
    property int selectionAnchorColumn: -1
    property int selectionAnchorRow: -1

    /// Gives the keyboard back to the drawing.
    ///
    /// The canvas keys live on one item that holds focus, and a window full of
    /// controls takes focus away constantly: opening a menu, typing a clip
    /// name, clicking a field. Nothing gave it back, so the arrows worked until
    /// the first click anywhere and then never again -- which reads as "the
    /// keyboard does not work" rather than "the keyboard is somewhere else".
    /// The clip before or after this one, wrapping. Switching clip was
    /// mouse-only, and it is the one thing you do as often as switching frame.
    function stepClip(by) {
        var names = doc.clipNames
        if (names.length < 2)
            return
        var at = names.indexOf(doc.clip)
        doc.clip = names[(at + by + names.length) % names.length]
    }

    function focusCanvas() {
        keys.forceActiveFocus()
    }

    function performPendingAction(action) {
        if (action === "new")
            newSheet.open()
        else if (action === "open")
            openDialog.open()
        else if (action === "quit") {
            allowClosing = true
            Qt.quit()
        }
        else if (action === "close") {
            allowClosing = true
            win.close()
        }
    }

    function requestAction(action) {
        if (!doc.dirty) {
            performPendingAction(action)
            return
        }
        pendingAction = action
        unsavedSheet.open()
    }

    function resumePendingAction() {
        var action = pendingAction
        pendingAction = ""
        unsavedSheet.close()
        performPendingAction(action)
    }

    function discardPendingChanges() {
        var action = pendingAction
        pendingAction = ""
        unsavedSheet.close()
        performPendingAction(action)
    }

    function savePendingChanges() {
        if (doc.path === "") {
            unsavedSheet.keepPendingAction = true
            unsavedSheet.close()
            saveDialog.open()
            return
        }
        if (doc.save())
            resumePendingAction()
    }

    onClosing: function (close) {
        if (allowClosing) {
            allowClosing = false
            return
        }
        if (!doc.dirty)
            return
        close.accepted = false
        requestAction("close")
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
        if (mode !== "draw" || heldSlot === ""
            || (!doc.hasSelection && caretColumn < 0))
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

    function moveCaret(dx, dy, selecting) {
        var appeared = caretColumn < 0
        if (caretColumn < 0) {
            // First press lands it in the middle rather than a corner: the
            // middle is on average the shortest walk to anywhere.
            caretColumn = Math.floor(doc.columns / 2)
            caretRow = Math.floor(doc.rows / 2)
        }
        if (selecting) {
            if (selectionAnchorColumn < 0) {
                selectionAnchorColumn = caretColumn
                selectionAnchorRow = caretRow
            }
            if (!appeared) {
                caretColumn = Math.max(0, Math.min(doc.columns - 1, caretColumn + dx))
                caretRow = Math.max(0, Math.min(doc.rows - 1, caretRow + dy))
            }
            doc.setSelection(selectionAnchorColumn, selectionAnchorRow,
                             caretColumn, caretRow)
        } else {
            doc.clearSelection()
            selectionAnchorColumn = -1
            selectionAnchorRow = -1
            if (!appeared) {
                caretColumn = Math.max(0, Math.min(doc.columns - 1, caretColumn + dx))
                caretRow = Math.max(0, Math.min(doc.rows - 1, caretRow + dy))
            }
            trail()
        }
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
    property var pendingTrim: ({})

    function applyTrim(anyway) {
        if (!doc.trim(anyway))
            return

        linePoints = []
        caretColumn = -1
        caretRow = -1
        pendingTrim = ({})
        focusCanvas()
    }

    function confirmTrim() {
        var latest = doc.trimPreview()
        if (latest.empty !== pendingTrim.empty
            || latest.changed !== pendingTrim.changed
            || latest.x !== pendingTrim.x || latest.y !== pendingTrim.y
            || latest.columns !== pendingTrim.columns
            || latest.rows !== pendingTrim.rows
            || latest.lost !== pendingTrim.lost) {
            trimSheet.close()
            pendingTrim = ({})
            Qt.callLater(win.requestTrim)
            return
        }
        trimSheet.close()
        applyTrim(true)
    }

    function requestTrim() {
        pendingTrim = doc.trimPreview()
        if (pendingTrim.empty || !pendingTrim.changed) {
            doc.trim(false)
            pendingTrim = ({})
        } else if (pendingTrim.lost > 0) {
            trimSheet.open()
        } else {
            applyTrim(false)
        }
    }

    Connections {
        target: doc
        function onChanged() {
            win.wantColumns = doc.columns
            win.wantRows = doc.rows
        }
        function onSelectionChanged() {
            if (doc.hasSelection && win.mode === "pick")
                win.mode = ""
            if (!doc.hasSelection) {
                win.selectionAnchorColumn = -1
                win.selectionAnchorRow = -1
            }
        }
        function onDocumentReplaced() { win.fillRegisters() }
        function onPaletteChanged() { win.reconcileRegisters() }
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
        onTriggered: {
            if (doc.frame + 1 < doc.frameCount) {
                doc.frame = doc.frame + 1
            } else if (win.loop) {
                doc.frame = 0
            } else {
                // Stopped ON the last frame, not one past it: the end of the
                // animation is a thing you look at.
                win.playing = false
            }
        }
    }

    // -------------------------------------------------------------- commands
    //
    // Declared once, as actions, and used by both the menus and the keyboard. A
    // command that exists twice -- once in a menu handler and once in a key
    // handler -- is a command that will one day do two different things.

    C.Action { id: actNew; text: T.t("menu.new"); shortcut: cfg.shortcuts.new
             onTriggered: requestAction("new") }
    C.Action { id: actOpen; text: T.t("menu.open"); shortcut: cfg.shortcuts.open
             onTriggered: requestAction("open") }
    C.Action { id: actSave; text: T.t("menu.save"); shortcut: cfg.shortcuts.save
             onTriggered: doc.path === "" ? saveDialog.open() : doc.save() }
    C.Action { id: actSaveAs; text: T.t("menu.saveAs"); shortcut: cfg.shortcuts.save_as
             onTriggered: saveDialog.open() }
    C.Action { id: actExport; text: T.t("menu.exportPng"); shortcut: cfg.shortcuts.export_png
             onTriggered: { exportSheet.asSheet = false; exportSheet.open() } }
    C.Action { id: actExportSheet; text: T.t("menu.exportSheet"); shortcut: cfg.shortcuts.export_sheet
             onTriggered: { exportSheet.asSheet = true; exportSheet.open() } }
    C.Action { id: actQuit; text: T.t("menu.quit"); shortcut: cfg.shortcuts.quit
             onTriggered: requestAction("quit") }

    C.Action { id: actUndo; text: T.t("menu.undo"); shortcut: cfg.shortcuts.undo
             enabled: doc.canUndo; onTriggered: doc.undo() }
    C.Action { id: actRedo; text: T.t("menu.redo"); shortcut: cfg.shortcuts.redo
             enabled: doc.canRedo; onTriggered: doc.redo() }
    C.Action { id: actClear; text: T.t("menu.clearFrame"); shortcut: cfg.shortcuts.clear_frame
             onTriggered: doc.clearFrame() }
    C.Action { id: actFlipX; text: T.t("menu.flipX"); onTriggered: doc.flip("x") }
    C.Action { id: actFlipY; text: T.t("menu.flipY"); onTriggered: doc.flip("y") }

    C.Action { id: actResize; text: T.t("menu.canvasSize")
             onTriggered: { spriteSection.open = true; dock.contentY = spriteSection.y } }
    C.Action { id: actTrim; text: T.t("menu.trim"); shortcut: cfg.shortcuts.trim
             onTriggered: win.requestTrim() }
    C.Action { id: actAddClip; text: T.t("menu.addClip")
             onTriggered: doc.addClip("clip " + (doc.clipNames.length + 1)) }
    C.Action { id: actRemoveClip; text: T.t("menu.deleteClip")
             enabled: doc.clipNames.length > 1
             onTriggered: doc.removeClip(doc.clip) }
    C.Action { id: actAddFrame; text: T.t("menu.addFrame"); shortcut: cfg.shortcuts.frame_add
             onTriggered: doc.addFrame(false) }
    C.Action { id: actDupFrame; text: T.t("menu.dupFrame"); shortcut: cfg.shortcuts.frame_duplicate
             onTriggered: doc.addFrame(true) }
    C.Action { id: actDelFrame; text: T.t("menu.delFrame")
             enabled: doc.frameCount > 1; onTriggered: doc.removeFrame() }
    C.Action { id: actPlay; text: T.t("menu.play"); shortcut: cfg.shortcuts.play
             enabled: doc.frameCount > 1; onTriggered: win.togglePlay() }
    C.Action { id: actLoop; text: T.t("menu.loop"); checkable: true; checked: win.loop
             shortcut: cfg.shortcuts.toggle_loop
             onTriggered: win.loop = !win.loop }

    C.Action { id: actZoomIn; text: T.t("menu.zoomIn"); shortcut: cfg.shortcuts.zoom_in
             onTriggered: stage.zoomStep(stage.width / 2, stage.height / 2, true) }
    C.Action { id: actZoomOut; text: T.t("menu.zoomOut"); shortcut: cfg.shortcuts.zoom_out
             onTriggered: stage.zoomStep(stage.width / 2, stage.height / 2, false) }
    C.Action { id: actFit; text: T.t("menu.fit"); shortcut: cfg.shortcuts.zoom_fit
             onTriggered: { stage.touched = false; stage.fit() } }
    C.Action { id: actOnion; text: T.t("menu.onion"); checkable: true; checked: win.onion
             onTriggered: win.onion = !win.onion }
    C.Action { id: actMesh; text: T.t("menu.grid"); checkable: true; checked: win.mesh
             onTriggered: win.mesh = !win.mesh }
    C.Action { id: actReference; text: T.t("menu.reference")
             onTriggered: referenceDialog.open() }
    C.Action { id: actColour; text: T.t("menu.chooseColour")
             onTriggered: colourSheet.show() }
    C.Action { id: actRoulette; text: T.t("menu.roulette")
             onTriggered: win.russianRoulette() }
    C.Action { id: actReplace; text: T.t("menu.replaceColour")
             enabled: win.focusedSlot !== ""
             onTriggered: colourSheet.show("replace") }
    // F10 puts the keyboard on the menu bar, where the arrows walk it. Alt and
    // the underlined letter opens one directly. Both are what every menu bar
    // has done for thirty years, and neither existed here.
    Shortcut { sequence: cfg.shortcuts.menus; onActivated: menus.forceActiveFocus() }

    C.Action { id: actNextClip; text: T.t("menu.nextClip"); shortcut: cfg.shortcuts.clip_next
             enabled: doc.clipNames.length > 1
             onTriggered: win.stepClip(1) }
    C.Action { id: actPrevClip; text: T.t("menu.prevClip"); shortcut: cfg.shortcuts.clip_previous
             enabled: doc.clipNames.length > 1
             onTriggered: win.stepClip(-1) }
    C.Action { id: actFrameBack; text: T.t("menu.frameBack"); shortcut: cfg.shortcuts.frame_move_back
             enabled: doc.frame > 0
             onTriggered: doc.moveFrame(-1) }
    C.Action { id: actFrameOn; text: T.t("menu.frameOn"); shortcut: cfg.shortcuts.frame_move_on
             enabled: doc.frame < doc.frameCount - 1
             onTriggered: doc.moveFrame(1) }

    C.Action { id: actHints; text: T.t("menu.keyHints"); checkable: true; checked: win.showHints
             shortcut: cfg.shortcuts.toggle_hints
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
            var act = cfg.action(event.key, event.modifiers)
            if (event.isAutoRepeat
                && (act === "draw_mode" || act === "pick_mode"
                    || act === "line_point" || act === "slot_leader")) {
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
                    doc.say(T.t("note.noSlot").arg(wanted))
                    return
                }
                win.slot = wanted
                if (doc.hasSelection || win.caretColumn >= 0) {
                    doc.beginStroke()
                    doc.paint(win.caretColumn, win.caretRow, wanted)
                    doc.endStroke()
                }
                return
            }

            // Dispatched on what the key MEANS, not on which key it is. The
            // config file is the map between the two, so a rebind changes one
            // line of TOML rather than a case label in here.
            switch (act) {
            case "tool_pencil": win.tool = "pencil"; break
            case "tool_eraser": win.tool = "eraser"; break
            case "tool_bucket": win.tool = "bucket"; break
            case "tool_picker": win.tool = "picker"; break
            case "tool_hand": win.tool = "hand"; break
            case "toggle_onion": win.onion = !win.onion; break
            case "toggle_grid": win.mesh = !win.mesh; break
            case "toggle_hints": win.showHints = !win.showHints; break
            case "toggle_loop": win.loop = !win.loop; break
            // The arrows walk the drawing, Shift extends a rectangle, and Ctrl
            // jumps by canvas.big_step. Frames use comma and full stop, where
            // every other sprite editor puts them -- the arrows are worth more
            // here, and stepping through frames is not something you do while
            // your hand is on the canvas.
            case "caret_left":  win.moveCaret(-1, 0); break
            case "caret_right": win.moveCaret(1, 0); break
            case "caret_up":    win.moveCaret(0, -1); break
            case "caret_down":  win.moveCaret(0, 1); break
            case "select_left":  win.moveCaret(-1, 0, true); break
            case "select_right": win.moveCaret(1, 0, true); break
            case "select_up":    win.moveCaret(0, -1, true); break
            case "select_down":  win.moveCaret(0, 1, true); break
            case "select_left_far":  win.moveCaret(-win.bigStep, 0, true); break
            case "select_right_far": win.moveCaret(win.bigStep, 0, true); break
            case "select_up_far":    win.moveCaret(0, -win.bigStep, true); break
            case "select_down_far":  win.moveCaret(0, win.bigStep, true); break
            case "caret_left_far":  win.moveCaret(-win.bigStep, 0); break
            case "caret_right_far": win.moveCaret(win.bigStep, 0); break
            case "caret_up_far":    win.moveCaret(0, -win.bigStep); break
            case "caret_down_far":  win.moveCaret(0, win.bigStep); break

            case "frame_previous":  doc.frame = Math.max(0, doc.frame - 1); break
            case "frame_next":
                doc.frame = Math.min(doc.frameCount - 1, doc.frame + 1); break

            // Draw where the cursor is. Return paints, backspace clears, and
            // both work on the pixel you can see the outline around.
            // Paint with the colour in hand: the pending line if there is one,
            // otherwise the pixel under the cursor.
            case "paint":
                if (!doc.hasSelection && win.caretColumn < 0)
                    break
                if (win.linePoints.length > 0) {
                    win.commitLine(win.slot)
                } else {
                    doc.beginStroke()
                    doc.paint(win.caretColumn, win.caretRow, win.slot)
                    doc.endStroke()
                }
                break
            case "erase":
                if (doc.hasSelection || win.caretColumn >= 0) {
                    doc.beginStroke()
                    doc.paint(win.caretColumn, win.caretRow, ".")
                    doc.endStroke()
                }
                break
            // Escape undoes one thing at a time, most recent first: the line
            // you were about to draw, then the pen, then the cursor. One key
            // that always means "not that" is worth more than three.
            case "cancel":
                // Reached from anywhere: a key event that nothing else wanted
                // arrives here, because this item is an ancestor of every
                // control in the window. So Escape always means "back to the
                // drawing", whichever button Tab had walked to.
                if (!keys.activeFocus) {
                    win.focusCanvas()
                    event.accepted = true
                    return
                }
                if (doc.hasSelection) {
                    doc.clearSelection()
                } else if (win.linePoints.length > 0) {
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
            case "slot_leader": win.awaitingSlot = true; break

            // A colour you do not know the name of yet.
            case "choose_colour": colourSheet.show("assign"); break
            case "replace_colour": colourSheet.show("replace"); break

            // And one nobody knows.
            case "roulette": win.russianRoulette(); break



            // Draw mode: from here the arrows paint, but only while a colour
            // is held down. Moving and drawing are the same gesture with and
            // without your other hand on a number.
            case "draw_mode":
                if (win.caretColumn < 0)
                    win.moveCaret(0, 0)
                win.mode = win.mode === "draw" ? "" : "draw"
                break

            // Pick mode: the digits stop choosing a colour and start
            // collecting one. Point at a pixel, press a number, and that
            // colour is on that number.
            case "pick_mode":
                if (win.mode === "pick") {
                    win.mode = ""
                } else {
                    doc.clearSelection()
                    if (win.caretColumn < 0)
                        win.moveCaret(0, 0)
                    win.mode = "pick"
                }
                break

            // A line: once to drop the anchor, again to draw from it. Between
            // the two presses the canvas shows where it would go, because a
            // line you cannot see before you commit to it is a line you draw
            // twice.
            // Each press drops a corner. Nothing is drawn until you name a
            // colour, so a right angle is two corners and one press instead of
            // two lines whose ends you have to line up by hand.
            case "line_point":
                if (win.caretColumn < 0)
                    win.moveCaret(0, 0)
                win.linePoints = win.linePoints.concat(
                    [{ c: win.caretColumn, r: win.caretRow }])
                break

            // Tab is left alone, so it walks the window's controls the way it
            // does everywhere else. It used to be taken for "give me the
            // drawing back", which answered one complaint by making every
            // button in the window unreachable -- Escape does that job now,
            // from anywhere, and it is the key people already press to leave
            // something.
            case "zoom_in":
                stage.zoomStep(stage.width / 2, stage.height / 2, true); break
            case "zoom_out":
                stage.zoomStep(stage.width / 2, stage.height / 2, false); break
            case "zoom_fit":
                stage.touched = false; stage.fit(); break
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

                    if (doc.hasSelection || win.caretColumn >= 0) {
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
                    text: (doc.path === "" ? T.t("file.untitled")
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
                    title: T.t("menu.file")
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
                    title: T.t("menu.edit")
                    Cmd { action: actUndo }
                    Cmd { action: actRedo }
                    Rule {}
                    Cmd { action: actClear }
                    Cmd { action: actFlipX }
                    Cmd { action: actFlipY }
                }

                Drop {
                    title: T.t("menu.sprite")
                    Cmd { action: actResize }
                    Cmd { action: actTrim }
                    Rule {}
                    Cmd { action: actAddClip }
                    Cmd { action: actRemoveClip }
                    Rule {}
                    Cmd { action: actColour }
                    Cmd { action: actReplace }
                    Cmd { action: actRoulette }
                    Rule {}
                    Cmd { action: actPrevClip }
                    Cmd { action: actNextClip }
                    Rule {}
                    Cmd { action: actAddFrame }
                    Cmd { action: actDupFrame }
                    Cmd { action: actDelFrame }
                    Cmd { action: actFrameBack }
                    Cmd { action: actFrameOn }
                    Rule {}
                    Cmd { action: actPlay }
                }

                Drop {
                    title: T.t("menu.view")
                    Cmd { action: actZoomIn }
                    Cmd { action: actZoomOut }
                    Cmd { action: actFit }
                    Rule {}
                    Cmd { action: actOnion }
                    Cmd { action: actMesh }
                    Cmd { action: actLoop }
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

                        ToolButton { glyph: "B"; key: "B"; caption: T.t("tool.pencil")
                                     on: win.tool === "pencil"
                                     onClicked: win.tool = "pencil" }
                        ToolButton { glyph: "E"; key: "E"; caption: T.t("tool.eraser")
                                     on: win.tool === "eraser"
                                     onClicked: win.tool = "eraser" }
                        ToolButton { glyph: "F"; key: "F"; caption: T.t("tool.bucket")
                                     on: win.tool === "bucket"
                                     onClicked: win.tool = "bucket" }
                        ToolButton { glyph: "I"; key: "I"; caption: T.t("tool.picker")
                                     on: win.tool === "picker"
                                     onClicked: win.tool = "picker" }
                        ToolButton { glyph: "H"; key: "H"; caption: T.t("tool.pan")
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
                                color: (doc.paletteRevision, letter === "" ? "transparent" : doc.colourOf(letter))
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
                                              ? T.t("strip.emptyColour")
                                              : T.t("strip.slot").arg(chip.letter) + "   "
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
                                    text: T.t("menu.roulette") + "   R"
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
                                title: T.t("panel.palette")
                                hint: T.t("panel.palette.slots").arg(doc.palette.length)

                                // Nine rows, then it scrolls. A palette can run
                                // to hundreds of slots, and letting it push the
                                // panels below it off the bottom of the window
                                // costs more than it gives -- the ones you are
                                // working with are at the top, and the rest are
                                // a scroll or a click away.
                                property bool showAll: false
                                readonly property int rowsShown: 9
                                readonly property int swatchPitch: 28

                                // A list model, not a JS array: assigning a
                                // new array rebuilds every delegate, and this
                                // list is rebuilt whenever a colour is added.
                                // With a few hundred slots that is a few
                                // hundred items destroyed and made again on a
                                // keypress, which is the stutter.
                                GridView {
                                    id: swatches
                                    width: parent.width
                                    height: Math.min(contentHeight,
                                                     paletteSection.showAll
                                                     ? contentHeight
                                                     : paletteSection.rowsShown
                                                       * paletteSection.swatchPitch)
                                    cellWidth: paletteSection.swatchPitch
                                    cellHeight: paletteSection.swatchPitch
                                    boundsBehavior: Flickable.StopAtBounds
                                    clip: true
                                    model: doc.paletteModel
                                    cacheBuffer: 0

                                    Behavior on height {
                                        NumberAnimation { duration: 110
                                                          easing.type: Easing.OutCubic }
                                    }

                                    delegate: Rectangle {
                                        id: pip
                                        required property string slot
                                        required property color colour
                                        width: 24
                                        height: 24
                                        radius: theme.rounding
                                        color: slot === "." ? "transparent" : colour
                                        border.width: win.slot === slot ? 2 : 1
                                        border.color: win.slot === slot
                                                      ? theme.accent
                                                      : theme.fill(theme.foreground, 0.18)

                                        // The digit that reaches this slot, in
                                        // the corner. A shortcut you cannot see
                                        // is a shortcut only its author
                                        // remembers.
                                        Text {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.margins: 1
                                            readonly property int at:
                                                win.registers.indexOf(pip.slot)
                                            visible: at >= 0
                                            text: at === 9 ? "0" : String(at + 1)
                                            color: theme.accent
                                            font.family: theme.fontFamily
                                            font.pixelSize: 8
                                            font.bold: true
                                        }

                                        // Against the swatch's own colour, not
                                        // the theme's: a dark slot with a dark
                                        // letter is an unlabelled square.
                                        Text {
                                            anchors.centerIn: parent
                                            text: pip.slot === "." ? "×" : pip.slot
                                            color: {
                                                if (pip.slot === ".")
                                                    return theme.dim
                                                var lum = 0.2126 * pip.colour.r
                                                        + 0.7152 * pip.colour.g
                                                        + 0.0722 * pip.colour.b
                                                return lum > 0.5 ? "#101010" : "#f0f0f0"
                                            }
                                            font.family: theme.fontFamily
                                            font.pixelSize: 10
                                        }

                                        TapHandler {
                                            onTapped: {
                                                win.slot = pip.slot
                                                if (win.tool === "eraser")
                                                    win.tool = "pencil"
                                            }
                                        }
                                    }
                                }

                                Chip {
                                    visible: swatches.contentHeight
                                             > paletteSection.rowsShown
                                               * paletteSection.swatchPitch
                                    label: paletteSection.showAll
                                           ? T.t("panel.palette.showNine")
                                           : T.t("panel.palette.showAll")
                                                 .arg(doc.palette.length)
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
                                        color: (doc.paletteRevision,
                                                win.slot === "." ? "transparent"
                                                                 : doc.colourOf(win.slot))
                                        border.width: 1
                                        border.color: theme.fill(theme.foreground, 0.25)
                                    }

                                Field {
                                    onEscaped: win.focusCanvas()
                                    label: T.t("panel.palette.colourOf").arg(win.slot)
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
                                    text: T.t("panel.palette.warning")
                                          .arg(win.slot === "." ? T.t("panel.palette.it")
                                                                : win.slot)
                                }
                            }

                            Section {
                                title: T.t("panel.preview")
                                hint: win.zoomLabel()

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
                                    text: T.t("panel.preview.tooWide")
                                }
                            }

                            Section {
                                id: spriteSection
                                title: T.t("panel.sprite")
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
                                        label: T.t("field.columns")
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
                                        label: T.t("field.rows")
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
                                    text: T.t("panel.sprite.crops").arg(win.wouldLose)
                                }

                                Chip {
                                    label: T.t("panel.sprite.resize")
                                    on: win.sizeChanged
                                    usable: win.sizeChanged
                                    role: win.wouldLose > 0 ? theme.urgent : theme.accent
                                    onClicked: doc.resize(win.wantColumns, win.wantRows)
                                }
                            }

                            Section {
                                title: T.t("panel.reference")
                                hint: win.referencePath === ""
                                      ? T.t("panel.reference.none")
                                      : Math.round(win.referenceAlpha * 100) + "%"
                                open: false

                                Chip {
                                    label: T.t("panel.reference.choose")
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
                                        label: win.referenceOnTop ? T.t("panel.reference.onTop") : T.t("panel.reference.behind")
                                        on: win.referenceOnTop
                                        usable: win.referencePath !== ""
                                        onClicked: win.referenceOnTop = !win.referenceOnTop
                                    }
                                    Chip {
                                        label: T.t("panel.reference.clear")
                                        usable: win.referencePath !== ""
                                        onClicked: win.referencePath = ""
                                    }
                                }
                            }

                            Section {
                                title: T.t("panel.history")
                                hint: doc.changes.count > 0
                                      ? String(doc.changes.count)
                                      : T.t("panel.history.none")
                                open: false

                                // A record, not a mechanism: the entries read,
                                // and undo stays the only way back. Newest at
                                // the bottom, like a log, and six rows tall
                                // before it scrolls for the same reason the
                                // palette stops at nine.
                                ListView {
                                    width: parent.width
                                    height: Math.min(contentHeight, 6 * 40)
                                    boundsBehavior: Flickable.StopAtBounds
                                    clip: true
                                    model: doc.changes

                                    delegate: Item {
                                        required property string origin
                                        required property string description
                                        required property double whenMs
                                        width: ListView.view.width
                                        height: historyRow.implicitHeight + 6

                                        Column {
                                            id: historyRow
                                            width: parent.width
                                            spacing: 1

                                            Text {
                                                // Descriptions carry clip names
                                                // read from document JSON:
                                                // plain text, always, so a
                                                // name can never dress itself
                                                // up as markup.
                                                textFormat: Text.PlainText
                                                text: origin + " · "
                                                      + Qt.formatTime(
                                                            new Date(whenMs),
                                                            "hh:mm")
                                                font.pixelSize: 10
                                                color: theme.fill(theme.foreground, 0.55)
                                            }
                                            Text {
                                                textFormat: Text.PlainText
                                                width: parent.width
                                                text: description
                                                wrapMode: Text.Wrap
                                                font.pixelSize: 11
                                                elide: Text.ElideRight
                                                maximumLineCount: 2
                                                color: theme.foreground
                                            }
                                        }
                                    }
                                }

                                Label {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    visible: doc.changes.count === 0
                                    text: T.t("panel.history.empty")
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

    Sheet {
        id: unsavedSheet
        title: T.t("dialog.unsaved")
        property bool keepPendingAction: false
        onClosed: {
            if (keepPendingAction)
                keepPendingAction = false
            else
                win.pendingAction = ""
            win.focusCanvas()
        }

        body: [
            Label {
                width: 320
                wrapMode: Text.Wrap
                text: T.t("dialog.unsaved.message")
            },
            Row {
                spacing: 6
                Chip {
                    label: T.t("menu.save")
                    on: true
                    onClicked: win.savePendingChanges()
                }
                Chip {
                    label: T.t("action.discard")
                    onClicked: win.discardPendingChanges()
                }
                Chip {
                    label: T.t("action.cancel")
                    onClicked: {
                        win.pendingAction = ""
                        unsavedSheet.close()
                    }
                }
            }
        ]
    }

    Sheet {
        id: trimSheet
        title: T.t("sheet.trim")
        onClosed: win.focusCanvas()

        body: [
            Label {
                width: 320
                wrapMode: Text.Wrap
                text: T.t("sheet.trim.message")
                      .arg(win.pendingTrim.columns)
                      .arg(win.pendingTrim.rows)
                      .arg(win.pendingTrim.lost)
            },
            Row {
                spacing: 6
                Chip {
                    label: T.t("sheet.trim.confirm")
                    on: true
                    role: theme.urgent
                    onClicked: win.confirmTrim()
                }
                Chip {
                    label: T.t("action.cancel")
                    onClicked: trimSheet.close()
                }
            }
        ]
    }

    FileDialog {
        id: openDialog
        title: T.t("dialog.open")
        nameFilters: ["omapixel documents (*.json)", "All files (*)"]
        onAccepted: {
            doc.open(selectedFile)
            Qt.callLater(win.focusCanvas)
        }
        onRejected: Qt.callLater(win.focusCanvas)
    }

    FileDialog {
        id: saveDialog
        title: T.t("dialog.save")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["omapixel documents (*.json)", "All files (*)"]
        onAccepted: {
            var saved = doc.save(selectedFile)
            if (saved && win.pendingAction !== "")
                win.resumePendingAction()
            else if (!saved && win.pendingAction !== "")
                unsavedSheet.open()
            else
                Qt.callLater(win.focusCanvas)
        }
        onRejected: {
            if (win.pendingAction !== "")
                unsavedSheet.open()
            else
                Qt.callLater(win.focusCanvas)
        }
    }

    FileDialog {
        id: referenceDialog
        title: T.t("dialog.reference")
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)", "All files (*)"]
        onAccepted: {
            win.referencePath = selectedFile.toString().replace("file://", "")
            if (win.referenceAlpha === 0)
                win.referenceAlpha = 0.5
            Qt.callLater(win.focusCanvas)
        }
        onRejected: Qt.callLater(win.focusCanvas)
    }

    FileDialog {
        id: exportDialog
        title: T.t("dialog.export")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: ["PNG images (*.png)", "All files (*)"]
        onAccepted: {
            doc.exportImage(selectedFile, exportSheet.factor,
                            exportSheet.asSheet, exportSheet.checker)
            Qt.callLater(win.focusCanvas)
        }
        onRejected: Qt.callLater(win.focusCanvas)
    }

    Sheet {
        id: newSheet
        onClosed: win.focusCanvas()
        title: T.t("sheet.new")

        property int columns: cfg.settings["document.width"]
        property int rows: cfg.settings["document.height"]

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
                    label: T.t("field.columns")
                    boxWidth: 130
                    value: String(newSheet.columns)
                    onEdited: function (text) {
                        var n = parseInt(text, 10)
                        if (isFinite(n) && n > 0) newSheet.columns = Math.min(512, n)
                    }
                }
                Field {
                    onEscaped: win.focusCanvas()
                    label: T.t("field.rows")
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
                text: T.t("sheet.new.unsaved")
            },
            Row {
                spacing: 6
                Chip {
                    label: T.t("sheet.new.create")
                    on: true
                    onClicked: {
                        doc.reset(newSheet.columns, newSheet.rows)
                        newSheet.close()
                    }
                }
                Chip { label: T.t("action.cancel"); onClicked: newSheet.close() }
            }
        ]
    }

    Sheet {
        id: colourSheet
        title: purpose === "replace" ? T.t("sheet.replace").arg(replacing)
                                      : T.t("sheet.colour")
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
                label: T.t("sheet.colour.search")
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
                            return T.t("sheet.replace.counts")
                                       .arg(colourSheet.chosen)
                                       .arg(doc.countSlot(colourSheet.replacing, false))
                                       .arg(doc.countSlot(colourSheet.replacing, true))
                        return colourSheet.armed
                               ? T.t("sheet.colour.armed").arg(colourSheet.chosen)
                               : T.t("sheet.colour.choose").arg(colourSheet.chosen)
                    }
                }
            },

            // The ten number keys, with what is on each. Clicking one does the
            // same as pressing it, for a hand that is already on the mouse.
            Row {
                spacing: 6
                visible: colourSheet.purpose === "replace"

                Chip {
                    label: T.t("sheet.replace.thisFrame")
                    on: true
                    onClicked: colourSheet.arm(false)
                }
                Chip {
                    label: T.t("sheet.replace.everyFrame")
                    role: theme.urgent
                    onClicked: colourSheet.arm(true)
                }
                Chip { label: T.t("action.cancel"); onClicked: colourSheet.close() }
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
                            color: (doc.paletteRevision, letter === "" ? "transparent" : doc.colourOf(letter))
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
        title: asSheet ? T.t("sheet.exportSheet") : T.t("sheet.exportPng")

        property bool asSheet: false
        property int factor: 8
        property bool checker: false

        body: [
            Label {
                width: 320
                wrapMode: Text.Wrap
                text: exportSheet.asSheet
                      ? T.t("sheet.export.sheetSize").arg(doc.clip)
                            .arg(doc.columns * doc.frameCount * exportSheet.factor)
                            .arg(doc.rows * exportSheet.factor)
                      : T.t("sheet.export.frameSize")
                            .arg(doc.columns * exportSheet.factor)
                            .arg(doc.rows * exportSheet.factor)
            },
            Row {
                spacing: 5
                Label { text: T.t("sheet.export.scale"); anchors.verticalCenter: parent.verticalCenter }
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
                label: T.t("sheet.export.checker")
                on: exportSheet.checker
                onClicked: exportSheet.checker = !exportSheet.checker
            },
            Row {
                spacing: 6
                Chip {
                    label: T.t("sheet.export.choose")
                    on: true
                    onClicked: {
                        exportDialog.currentFile =
                            "file://" + doc.suggestedExportPath(exportSheet.asSheet)
                        exportSheet.close()
                        exportDialog.open()
                    }
                }
                Chip { label: T.t("action.cancel"); onClicked: exportSheet.close() }
            }
        ]
    }
}
