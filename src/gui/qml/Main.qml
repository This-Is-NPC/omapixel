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
    title: (doc.dirty ? T.t("window.modified") : "")
           + (doc.path === "" ? T.t("file.untitled") : doc.path)
           + T.t("window.separator") + T.t("brand.name")
    color: theme.background

    property string tool: "pencil"
    property string slot: "I"
    property real zoom: 12
    function zoomLabel() {
        return T.t("status.zoom").arg(Math.round(zoom * 100) / 100)
    }
    property bool onion: cfg.settings["canvas.onion"]
    property bool mesh: cfg.settings["canvas.grid"]
    property bool playing: false
    property string pendingAction: ""
    property bool allowClosing: false

    // The inspector is a real layout column, not a fixed decoration. Its
    // default follows the config architecture, while the value itself belongs
    // to this window so a drag remains stable for the rest of the session.
    readonly property int inspectorMinimumWidth: 260
    readonly property int inspectorMaximumWidth: 520
    property real inspectorWidth: clampInspectorWidth(cfg.settings["window.inspector_width"])
    function clampInspectorWidth(value) {
        return Math.max(inspectorMinimumWidth,
                        Math.min(inspectorMaximumWidth, value))
    }
    onInspectorWidthChanged: {
        var bounded = clampInspectorWidth(inspectorWidth)
        if (inspectorWidth !== bounded)
            inspectorWidth = bounded
    }

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
    readonly property bool commandPaletteOpen: commandPalette.opened
    readonly property bool navigationMode: navigationOverlay.visible

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
                { key: cfg.keys.go_to_pixel, label: T.t("hint.goToPixel") },
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

    LayerToolWindow {
        id: layerTool
        structuralIds: layerDock.selectedIds
        returnFocusItem: layerDock
        onCommandRequested: function (commandId) { commands.invoke(commandId) }
        onConfirmationRequested: function (kind, layerId, report) {
            layerSheet.show(kind, layerId, report)
        }
    }

    function pastePixels() {
        var x = doc.hasSelection ? doc.selectionX
                                 : (caretColumn >= 0 ? caretColumn : 0)
        var y = doc.hasSelection ? doc.selectionY
                                 : (caretRow >= 0 ? caretRow : 0)
        if (doc.pastePixels(x, y)) {
            caretColumn = x
            caretRow = y
            selectionAnchorColumn = x
            selectionAnchorRow = y
        }
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
        else if (shotSheet === "goto")
            goToSheet.show()
        else if (shotSheet === "commands")
            win.openCommandPalette()
        else if (shotSheet === "navigate")
            navigationOverlay.show()
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

    // ------------------------------------------------------ command registry

    NavigationCommands { id: navigationCommands; host: win; overlay: navigationOverlay; menuBar: menus; firstControl: toolsFirst }
    FileEditCommands { id: fileEditCommands; host: win; saveDialog: saveDialog; exportDialog: exportSheet }
    CanvasViewCommands { id: canvasViewCommands; host: win; stage: stage; goToSheet: goToSheet; colourSheet: colourSheet; referenceDialog: referenceDialog }
    LayerCommands { id: layerCommands; layerDock: layerDock; layerTool: layerTool }
    InspectorCommands {
        id: inspectorCommands; host: win; dock: dock; layerDock: layerDock
        paletteSection: paletteSection; previewSection: previewSection
        spriteSection: spriteSection; referenceSection: referenceSection
        historySection: historySection; referenceDialog: referenceDialog
    }
    TimelineCommands { id: timelineCommands; host: win; timeline: timeline }

    CommandRegistry {
        id: commands
        providers: [navigationCommands, fileEditCommands, canvasViewCommands,
                    layerCommands, inspectorCommands, timelineCommands]
    }

    property var commandEntries: []

    function openCommandPalette() {
        win.requestActivate()
        commandEntries = commands.snapshot()
        Qt.callLater(function () { commandPalette.show() })
    }

    function cancelCanvasState() {
        if (doc.hasSelection)
            doc.clearSelection()
        else if (win.linePoints.length > 0)
            win.linePoints = []
        else if (win.mode !== "") {
            win.releaseHeld()
            win.mode = ""
        } else {
            win.caretColumn = -1
            win.caretRow = -1
        }
    }

    function paintAtCaret(which) {
        if (!doc.hasSelection && win.caretColumn < 0)
            return
        if (win.linePoints.length > 0) {
            win.commitLine(which)
            return
        }
        doc.beginStroke()
        doc.paint(win.caretColumn, win.caretRow, which)
        doc.endStroke()
    }

    function focusInspectorSection(section) {
        dock.contentY = Math.max(0, section.y)
        section.focusHeader()
    }

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
            case "tool_pencil": commands.invoke("canvas.tool.pencil"); break
            case "tool_eraser": commands.invoke("canvas.tool.eraser"); break
            case "tool_bucket": commands.invoke("canvas.tool.bucket"); break
            case "tool_picker": commands.invoke("canvas.tool.picker"); break
            case "tool_hand": commands.invoke("canvas.tool.hand"); break
            case "toggle_onion": commands.invoke("view.onion"); break
            case "toggle_grid": commands.invoke("view.grid"); break
            case "toggle_hints": commands.invoke("view.hints"); break
            case "toggle_loop": commands.invoke("view.loop"); break
            // The arrows walk the drawing, Shift extends a rectangle, and Ctrl
            // jumps by canvas.big_step. Frames use comma and full stop, where
            // every other sprite editor puts them -- the arrows are worth more
            // here, and stepping through frames is not something you do while
            // your hand is on the canvas.
            case "caret_left":  commands.invoke("canvas.moveLeft"); break
            case "caret_right": commands.invoke("canvas.moveRight"); break
            case "caret_up":    commands.invoke("canvas.moveUp"); break
            case "caret_down":  commands.invoke("canvas.moveDown"); break
            case "select_left":  commands.invoke("canvas.selectLeft"); break
            case "select_right": commands.invoke("canvas.selectRight"); break
            case "select_up":    commands.invoke("canvas.selectUp"); break
            case "select_down":  commands.invoke("canvas.selectDown"); break
            case "select_left_far":  commands.invoke("canvas.extendLeft"); break
            case "select_right_far": commands.invoke("canvas.extendRight"); break
            case "select_up_far":    commands.invoke("canvas.extendUp"); break
            case "select_down_far":  commands.invoke("canvas.extendDown"); break
            case "caret_left_far":  commands.invoke("canvas.jumpLeft"); break
            case "caret_right_far": commands.invoke("canvas.jumpRight"); break
            case "caret_up_far":    commands.invoke("canvas.jumpUp"); break
            case "caret_down_far":  commands.invoke("canvas.jumpDown"); break
            case "go_to_pixel": commands.invoke("view.goTo"); break

            case "frame_previous": commands.invoke("timeline.previousFrame"); break
            case "frame_next": commands.invoke("timeline.nextFrame"); break

            // Draw where the cursor is. Return paints, backspace clears, and
            // both work on the pixel you can see the outline around.
            // Paint with the colour in hand: the pending line if there is one,
            // otherwise the pixel under the cursor.
            case "paint":
                commands.invoke("canvas.paint")
                break
            case "erase":
                commands.invoke("canvas.erase")
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
                commands.invoke("canvas.cancel")
                break

            // The leader. Semicolon rather than the comma, which is already the
            // previous frame and worth more there.
            case "slot_leader": commands.invoke("canvas.chooseSlot"); break

            // A colour you do not know the name of yet.
            case "choose_colour": commands.invoke("canvas.chooseColour"); break
            case "replace_colour": commands.invoke("canvas.replaceColour"); break

            // And one nobody knows.
            case "roulette": commands.invoke("canvas.roulette"); break



            // Draw mode: from here the arrows paint, but only while a colour
            // is held down. Moving and drawing are the same gesture with and
            // without your other hand on a number.
            case "draw_mode":
                commands.invoke("canvas.drawMode")
                break

            // Pick mode: the digits stop choosing a colour and start
            // collecting one. Point at a pixel, press a number, and that
            // colour is on that number.
            case "pick_mode":
                commands.invoke("canvas.pickMode")
                break

            // A line: once to drop the anchor, again to draw from it. Between
            // the two presses the canvas shows where it would go, because a
            // line you cannot see before you commit to it is a line you draw
            // twice.
            // Each press drops a corner. Nothing is drawn until you name a
            // colour, so a right angle is two corners and one press instead of
            // two lines whose ends you have to line up by hand.
            case "line_point":
                commands.invoke("canvas.linePoint")
                break

            // Tab is left alone, so it walks the window's controls the way it
            // does everywhere else. It used to be taken for "give me the
            // drawing back", which answered one complaint by making every
            // button in the window unreachable -- Escape does that job now,
            // from anywhere, and it is the key people already press to leave
            // something.
            case "zoom_in":
                commands.invoke("view.zoomIn"); break
            case "zoom_out":
                commands.invoke("view.zoomOut"); break
            case "zoom_fit":
                commands.invoke("view.fit"); break
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
                    text: T.t("brand.name")
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
                objectName: "menuBar"
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter

                Drop {
                    objectName: "fileMenu"
                    title: T.t("menu.file")
                    Cmd { action: commands.action("file.new") }
                    Cmd { action: commands.action("file.open") }
                    Rule {}
                    Cmd { action: commands.action("file.save") }
                    Cmd { action: commands.action("file.saveAs") }
                    Rule {}
                    Cmd { action: commands.action("file.export") }
                    Cmd { action: commands.action("file.exportSheet") }
                    Rule {}
                    Cmd { action: commands.action("file.quit") }
                }

                Drop {
                    objectName: "editMenu"
                    title: T.t("menu.edit")
                    Cmd { action: commands.action("edit.undo") }
                    Cmd { action: commands.action("edit.redo") }
                    Rule {}
                    Cmd { action: commands.action("edit.copy") }
                    Cmd { action: commands.action("edit.paste") }
                    Rule {}
                    Cmd { action: commands.action("edit.clear") }
                    Cmd { action: commands.action("edit.flipX") }
                    Cmd { action: commands.action("edit.flipY") }
                }

                Drop {
                    objectName: "spriteMenu"
                    title: T.t("menu.sprite")
                    Cmd { action: commands.action("inspector.canvasSize") }
                    Cmd { action: commands.action("edit.trim") }
                    Rule {}
                    Cmd { action: commands.action("timeline.addClip") }
                    Cmd { action: commands.action("timeline.removeClip") }
                    Rule {}
                    Cmd { action: commands.action("canvas.chooseColour") }
                    Cmd { action: commands.action("canvas.replaceColour") }
                    Cmd { action: commands.action("canvas.roulette") }
                    Rule {}
                    Cmd { action: commands.action("timeline.previousClip") }
                    Cmd { action: commands.action("timeline.nextClip") }
                    Rule {}
                    Cmd { action: commands.action("timeline.addFrame") }
                    Cmd { action: commands.action("timeline.duplicateFrame") }
                    Cmd { action: commands.action("timeline.deleteFrame") }
                    Cmd { action: commands.action("timeline.moveBack") }
                    Cmd { action: commands.action("timeline.moveOn") }
                    Rule {}
                    Cmd { action: commands.action("timeline.play") }
                }

                Drop {
                    objectName: "viewMenu"
                    title: T.t("menu.view")
                    Cmd { action: commands.action("view.zoomIn") }
                    Cmd { action: commands.action("view.zoomOut") }
                    Cmd { action: commands.action("view.fit") }
                    Cmd { action: commands.action("view.goTo"); shownShortcut: cfg.keys.go_to_pixel }
                    Rule {}
                     Cmd { action: commands.action("view.onion") }
                     Cmd { action: commands.action("view.grid") }
                     Rule {}
                     Cmd { action: commands.action("view.scopeFrame") }
                     Cmd { action: commands.action("view.scopeAll") }
                     Cmd { action: commands.action("view.pickerActive") }
                     Cmd { action: commands.action("view.pickerComposite") }
                     Cmd { action: commands.action("view.loop") }
                    Rule {}
                    Cmd { action: commands.action("view.reference") }
                    Rule {}
                    Cmd { action: commands.action("view.hints") }
                }
            }
            }

            Rectangle { width: parent.width; height: 1
                        color: theme.fill(theme.foreground, 0.18) }

            // ----------------------------------------------------------- body

            Row {
                id: body
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

                        ToolButton { id: toolsFirst; objectName: "toolsFirstControl"
                                     glyph: "B"; key: "B"; caption: T.t("tool.pencil")
                                     on: win.tool === "pencil"
                                     onClicked: commands.invoke("canvas.tool.pencil") }
                        ToolButton { objectName: "toolEraser"; glyph: "E"; key: "E"; caption: T.t("tool.eraser")
                                      on: win.tool === "eraser"
                                      onClicked: commands.invoke("canvas.tool.eraser") }
                        ToolButton { objectName: "toolBucket"; glyph: "F"; key: "F"; caption: T.t("tool.bucket")
                                      on: win.tool === "bucket"
                                      onClicked: commands.invoke("canvas.tool.bucket") }
                        ToolButton { objectName: "toolPicker"; glyph: "I"; key: "I"; caption: T.t("tool.picker")
                                      on: win.tool === "picker"
                                      onClicked: commands.invoke("canvas.tool.picker") }
                        ToolButton { objectName: "toolHand"; glyph: "H"; key: "H"; caption: T.t("tool.pan")
                                      on: win.tool === "hand"
                                      onClicked: commands.invoke("canvas.tool.hand") }

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
                                    onTapped: if (chip.letter !== "")
                                                  commands.invoke("palette.select",
                                                                  { slot: chip.letter })
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
                                text: T.t("tool.roulette")
                                color: theme.urgent
                                font.family: theme.fontFamily
                                font.pixelSize: 16
                                font.bold: true
                            }

                            HoverHandler { id: gambleHover }
                            TapHandler { onTapped: commands.invoke("canvas.roulette") }

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
                    width: parent.width - tools.width - dockPanel.width
                           - dockSplitter.width - 2
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
                    id: dockSplitter
                    objectName: "dockSplitter"
                    width: 7
                    height: parent.height
                    color: splitterHover.hovered
                           ? theme.fill(theme.accent, 0.22)
                           : theme.fill(theme.foreground, 0.06)
                    border.width: 1
                    border.color: splitterHover.hovered
                                  ? theme.accent
                                  : theme.fill(theme.foreground, 0.12)
                    activeFocusOnTab: true
                    Accessible.name: T.t("accessibility.layers.resizeInspector")
                    Accessible.description: T.t("accessibility.layers.resizeHelp")

                    Column {
                        anchors.centerIn: parent
                        spacing: 3
                        Rectangle {
                            width: 10
                            height: 1
                            color: splitterHover.hovered ? theme.accent : theme.dim
                        }
                        Rectangle {
                            width: 10
                            height: 1
                            color: splitterHover.hovered ? theme.accent : theme.dim
                        }
                    }

                    HoverHandler { id: splitterHover; cursorShape: Qt.SizeHorCursor }
                    Keys.onLeftPressed: win.inspectorWidth = win.clampInspectorWidth(
                                             win.inspectorWidth + 12)
                    Keys.onRightPressed: win.inspectorWidth = win.clampInspectorWidth(
                                              win.inspectorWidth - 12)
                    property real dragStartWidth: 0
                    DragHandler {
                        target: null
                        onActiveChanged: if (active) dockSplitter.dragStartWidth = win.inspectorWidth
                        onTranslationChanged: if (active)
                            win.inspectorWidth = win.clampInspectorWidth(
                                dockSplitter.dragStartWidth - translation.x)
                    }
                }

                Rectangle {
                    id: dockPanel
                    objectName: "dockPanel"
                    width: win.inspectorWidth
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

                            LayerDock {
                                id: layerDock
                                width: parent.width
                                onCommandRequested: function (commandId, args) {
                                    commands.invoke(commandId, args)
                                }
                                onLayerActivated: function (layerId) {
                                    layerTool.openFor(layerId)
                                }
                            }

                            Section {
                                id: paletteSection
                                objectName: "paletteSection"
                                commandRegistry: commands
                                sectionId: "palette"
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
                                        required property int index
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
                                                commands.invoke("palette.select",
                                                                { slot: pip.slot })
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
                                    onClicked: commands.invoke("inspector.paletteRows")
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
                                id: previewSection
                                objectName: "previewSection"
                                commandRegistry: commands
                                sectionId: "preview"
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

                                                // keyboard-equivalent: go-to pixel reaches any canvas location.
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
                                objectName: "spriteSection"
                                commandRegistry: commands
                                sectionId: "sprite"
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
                                            required property int index
                                            label: modelData.w + "×" + modelData.h
                                            on: win.wantColumns === modelData.w
                                                && win.wantRows === modelData.h
                                            onClicked: commands.invoke("inspector.setSize",
                                                                       { width: modelData.w,
                                                                         height: modelData.h })
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
                                    onClicked: commands.invoke("inspector.resize")
                                }
                            }

                            Section {
                                id: referenceSection
                                objectName: "referenceSection"
                                commandRegistry: commands
                                sectionId: "reference"
                                title: T.t("panel.reference")
                                hint: win.referencePath === ""
                                      ? T.t("panel.reference.none")
                                      : Math.round(win.referenceAlpha * 100) + "%"
                                open: false

                                Chip {
                                    label: T.t("panel.reference.choose")
                                    onClicked: commands.invoke("inspector.reference.choose")
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
                                            onClicked: commands.invoke(
                                                "inspector.reference.setOpacity",
                                                { percent: modelData })
                                        }
                                    }
                                }

                                Row {
                                    spacing: 5
                                    Chip {
                                        label: win.referenceOnTop ? T.t("panel.reference.onTop") : T.t("panel.reference.behind")
                                        on: win.referenceOnTop
                                        usable: win.referencePath !== ""
                                        onClicked: commands.invoke("inspector.reference.position")
                                    }
                                    Chip {
                                        label: T.t("panel.reference.clear")
                                        usable: win.referencePath !== ""
                                        onClicked: commands.invoke("inspector.reference.clear")
                                    }
                                }
                            }

                            Section {
                                id: historySection
                                objectName: "historySection"
                                commandRegistry: commands
                                sectionId: "history"
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
                onCommandRequested: function (commandId, args) {
                    commands.invoke(commandId, args)
                }
            }

            StatusBar {
                id: status
                width: parent.width
                column: stage.hoverColumn
                row: stage.hoverRow
            }
        }
    }

    // A short-lived spatial chooser. It does not rearrange the Studio; it lays
    // the three stable work regions over their real geometry, then hands focus
    // to the selected region.
    Item {
        id: navigationOverlay
        objectName: "navigationOverlay"
        anchors.fill: parent
        visible: false
        z: 100
        focus: visible
        property var previousFocusItem: null

        function show() {
            previousFocusItem = win.activeFocusItem
            visible = true
            forceActiveFocus()
        }

        function choose(region) {
            visible = false
            if (region === 1) {
                win.focusCanvas()
            } else if (region === 2) {
                dock.contentY = 0
                layerDock.focusList()
            } else if (region === 3) {
                timeline.focusFirst()
            }
        }

        function cancel() {
            visible = false
            if (previousFocusItem)
                previousFocusItem.forceActiveFocus()
            else
                win.focusCanvas()
        }

        Keys.onPressed: function (event) {
            if (event.key >= Qt.Key_1 && event.key <= Qt.Key_3) {
                var regions = ["navigate.canvas", "navigate.inspector",
                               "navigate.timeline"]
                commands.invoke(regions[event.key - Qt.Key_1])
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                navigationOverlay.cancel()
                event.accepted = true
            }
        }

        Rectangle {
            anchors.fill: parent
            color: theme.fill(theme.background, 0.72)
        }

        Rectangle {
            x: body.x
            y: body.y
            width: dockSplitter.x
            height: body.height
            color: theme.fill(theme.accent, 0.10)
            border.width: 2
            border.color: theme.accent
            radius: theme.rounding
            Text {
                anchors.centerIn: parent
                text: String(1) + "\n" + T.t("command.navigate.canvasRegion")
                horizontalAlignment: Text.AlignHCenter
                color: theme.foreground
                font.family: theme.fontFamily
                font.pixelSize: 22
                font.bold: true
            }
            TapHandler { onTapped: commands.invoke("navigate.canvas") }
        }

        Rectangle {
            x: dockPanel.x
            y: body.y
            width: dockPanel.width
            height: body.height
            color: theme.fill(theme.accent, 0.10)
            border.width: 2
            border.color: theme.accent
            radius: theme.rounding
            Text {
                anchors.centerIn: parent
                text: String(2) + "\n" + T.t("command.navigate.inspectorRegion")
                horizontalAlignment: Text.AlignHCenter
                color: theme.foreground
                font.family: theme.fontFamily
                font.pixelSize: 22
                font.bold: true
            }
            TapHandler { onTapped: commands.invoke("navigate.inspector") }
        }

        Rectangle {
            x: timeline.x
            y: timeline.y
            width: timeline.width
            height: timeline.height
            color: theme.fill(theme.accent, 0.10)
            border.width: 2
            border.color: theme.accent
            radius: theme.rounding
            Text {
                anchors.centerIn: parent
                text: String(3) + "\n" + T.t("command.navigate.timelineRegion")
                horizontalAlignment: Text.AlignHCenter
                color: theme.foreground
                font.family: theme.fontFamily
                font.pixelSize: 22
                font.bold: true
            }
            TapHandler { onTapped: commands.invoke("navigate.timeline") }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 36
            width: navigationHelp.implicitWidth + 20
            height: 26
            radius: theme.rounding
            color: theme.panel
            border.width: 1
            border.color: theme.accent
            Label {
                id: navigationHelp
                anchors.centerIn: parent
                text: T.t("command.navigate.help")
                color: theme.foreground
            }
        }
    }

    // ---------------------------------------------------------------- dialogs

    CommandPalette {
        id: commandPalette
        hostWindow: win
        commands: win.commandEntries
        onCommandRequested: function (commandId, args) {
            commands.invoke(commandId, args)
        }
    }

    Sheet {
        id: goToSheet
        objectName: "goToSheet"
        title: T.t("sheet.goToPixel")
        firstFocusItem: goToX.input
        onClosed: win.focusCanvas()

        property string columnText: "0"
        property string rowText: "0"
        property string problem: ""

        function show() {
            columnText = String(win.caretColumn >= 0 ? win.caretColumn : 0)
            rowText = String(win.caretRow >= 0 ? win.caretRow : 0)
            problem = ""
            open()
            Qt.callLater(goToX.focusEntry)
        }

        function navigate() {
            var digits = /^\d+$/
            var column = digits.test(columnText) ? Number(columnText) : -1
            var row = digits.test(rowText) ? Number(rowText) : -1
            if (column < 0 || column >= doc.columns || row < 0 || row >= doc.rows) {
                problem = T.t("sheet.goToPixel.outOfRange")
                           .arg(doc.columns - 1).arg(doc.rows - 1)
                return
            }
            doc.clearSelection()
            win.selectionAnchorColumn = -1
            win.selectionAnchorRow = -1
            win.caretColumn = column
            win.caretRow = row
            stage.reveal(column, row)
            close()
        }

        body: [
            Label {
                width: 300
                wrapMode: Text.Wrap
                text: T.t("sheet.goToPixel.range")
                      .arg(doc.columns - 1).arg(doc.rows - 1)
            },
            Row {
                spacing: 8
                Field {
                    id: goToX
                    objectName: "goToX"
                    label: T.t("field.x")
                    boxWidth: 140
                    value: goToSheet.columnText
                    onEdited: function (text) {
                        goToSheet.columnText = text
                        goToSheet.problem = ""
                    }
                    onConfirmed: goToY.focusEntry()
                    onEscaped: goToSheet.close()
                }
                Field {
                    id: goToY
                    objectName: "goToY"
                    label: T.t("field.y")
                    boxWidth: 140
                    value: goToSheet.rowText
                    onEdited: function (text) {
                        goToSheet.rowText = text
                        goToSheet.problem = ""
                    }
                    onConfirmed: goToSheet.navigate()
                    onEscaped: goToSheet.close()
                }
            },
            Label {
                width: 300
                wrapMode: Text.Wrap
                visible: goToSheet.problem !== ""
                color: theme.urgent
                text: goToSheet.problem
            },
            Row {
                spacing: 6
                Chip {
                    label: T.t("sheet.goToPixel.go")
                    on: true
                    onClicked: goToSheet.navigate()
                }
                Chip {
                    label: T.t("action.cancel")
                    onClicked: goToSheet.close()
                }
            }
        ]
    }

    Sheet {
        id: unsavedSheet
        objectName: "unsavedSheet"
        title: T.t("dialog.unsaved")
        firstFocusItem: unsavedSave
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
                    id: unsavedSave
                    objectName: "unsavedSave"
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
        objectName: "trimSheet"
        title: T.t("sheet.trim")
        firstFocusItem: trimConfirm
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
                    id: trimConfirm
                    objectName: "trimConfirm"
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

    Sheet {
        id: layerSheet
        objectName: "layerSheet"
        firstFocusItem: layerConfirm
        title: kind === "storage" ? T.t("panel.layers")
                                   : (kind === "merge-down" ? T.t("panel.layers.mergeDown")
                                                         : T.t("panel.layers.flatten"))
        onClosed: layerTool.focusAfterConfirmation()

        property string kind: ""
        property string layerId: ""
        property var report: ({})

        function show(action, id, consequence) {
            kind = action
            layerId = id
            report = consequence
            open()
        }
        function message() {
            if (kind === "storage") {
                var target = report.storage === "shared" ? T.t("panel.layers.shared")
                                                           : T.t("panel.layers.animated")
                return T.t("sheet.layers.storage").arg(report.id).arg(target)
                    .arg(report.lost || 0)
            }
            return T.t(kind === "merge-down" ? "sheet.layers.merge" : "sheet.layers.flatten")
                       .arg(report.affectedPixels || 0).arg(report.removedLayers || 0)
        }
        function apply() {
            if (kind === "storage")
                doc.setLayerStorage(report.id, report.storage, true)
            else if (kind === "merge-down")
                doc.mergeDown(layerId)
            else
                doc.flatten()
            close()
        }

        body: [
            Label {
                width: 330
                wrapMode: Text.Wrap
                text: layerSheet.message()
            },
            Label {
                width: 330
                wrapMode: Text.Wrap
                visible: (layerSheet.report.approximatedPixels || 0) > 0
                color: theme.urgent
                text: T.t("sheet.layers.approximated")
                      .arg(layerSheet.report.approximatedPixels || 0)
            },
            Label {
                width: 330
                wrapMode: Text.Wrap
                visible: layerSheet.report.ok === false
                color: theme.urgent
                text: layerSheet.report.error || ""
            },
            Row {
                spacing: 6
                Chip {
                    id: layerConfirm
                    objectName: "layerConfirm"
                    label: T.t("sheet.layers.confirm")
                    on: true
                    role: theme.urgent
                    onClicked: layerSheet.apply()
                }
                Chip {
                    label: T.t("action.cancel")
                    onClicked: layerSheet.close()
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
        objectName: "newSheet"
        firstFocusItem: newColumns.input
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
                    id: newColumns
                    objectName: "newColumns"
                    onEscaped: newSheet.close()
                    label: T.t("field.columns")
                    boxWidth: 130
                    value: String(newSheet.columns)
                    onEdited: function (text) {
                        var n = parseInt(text, 10)
                        if (isFinite(n) && n > 0) newSheet.columns = Math.min(512, n)
                    }
                }
                Field {
                    objectName: "newRows"
                    onEscaped: newSheet.close()
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
        objectName: "colourSheet"
        firstFocusItem: search.input
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
                        // keyboard-equivalent: search owns Up/Down and Enter selects the row.
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

                            // keyboard-equivalent: the focused digits item accepts 0-9.
                            TapHandler { onTapped: colourSheet.place(parent.index) }
                        }
                    }
                }
            }
        ]
    }

    Sheet {
        id: exportSheet
        objectName: "exportSheet"
        firstFocusItem: exportChecker
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
                id: exportChecker
                objectName: "exportChecker"
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
