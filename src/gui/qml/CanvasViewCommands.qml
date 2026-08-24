import QtQuick

CommandProvider {
    id: provider

    required property var host
    required property var stage
    required property var goToSheet
    required property var colourSheet
    required property var referenceDialog

    actions: [
        CommandAction { commandId: "canvas.tool.pencil"; text: T.t("tool.pencil"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.tool_pencil; onTriggered: host.tool = "pencil" },
        CommandAction { commandId: "canvas.tool.eraser"; text: T.t("tool.eraser"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.tool_eraser; onTriggered: host.tool = "eraser" },
        CommandAction { commandId: "canvas.tool.bucket"; text: T.t("tool.bucket"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.tool_bucket; onTriggered: host.tool = "bucket" },
        CommandAction { commandId: "canvas.tool.picker"; text: T.t("tool.picker"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.tool_picker; onTriggered: host.tool = "picker" },
        CommandAction { commandId: "canvas.tool.hand"; text: T.t("tool.pan"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.tool_hand; onTriggered: host.tool = "hand" },

        CommandAction { commandId: "canvas.moveLeft"; text: T.t("command.canvas.moveLeft"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.caret_left; onTriggered: host.moveCaret(-1, 0) },
        CommandAction { commandId: "canvas.moveRight"; text: T.t("command.canvas.moveRight"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.caret_right; onTriggered: host.moveCaret(1, 0) },
        CommandAction { commandId: "canvas.moveUp"; text: T.t("command.canvas.moveUp"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.caret_up; onTriggered: host.moveCaret(0, -1) },
        CommandAction { commandId: "canvas.moveDown"; text: T.t("command.canvas.moveDown"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.caret_down; onTriggered: host.moveCaret(0, 1) },
        CommandAction { commandId: "canvas.selectLeft"; text: T.t("command.canvas.selectLeft"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.select_left; onTriggered: host.moveCaret(-1, 0, true) },
        CommandAction { commandId: "canvas.selectRight"; text: T.t("command.canvas.selectRight"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.select_right; onTriggered: host.moveCaret(1, 0, true) },
        CommandAction { commandId: "canvas.selectUp"; text: T.t("command.canvas.selectUp"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.select_up; onTriggered: host.moveCaret(0, -1, true) },
        CommandAction { commandId: "canvas.selectDown"; text: T.t("command.canvas.selectDown"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.select_down; onTriggered: host.moveCaret(0, 1, true) },
        CommandAction { commandId: "canvas.jumpLeft"; text: T.t("command.canvas.jumpLeft").arg(host.bigStep); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.caret_left_far; onTriggered: host.moveCaret(-host.bigStep, 0) },
        CommandAction { commandId: "canvas.jumpRight"; text: T.t("command.canvas.jumpRight").arg(host.bigStep); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.caret_right_far; onTriggered: host.moveCaret(host.bigStep, 0) },
        CommandAction { commandId: "canvas.jumpUp"; text: T.t("command.canvas.jumpUp").arg(host.bigStep); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.caret_up_far; onTriggered: host.moveCaret(0, -host.bigStep) },
        CommandAction { commandId: "canvas.jumpDown"; text: T.t("command.canvas.jumpDown").arg(host.bigStep); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.caret_down_far; onTriggered: host.moveCaret(0, host.bigStep) },
        CommandAction { commandId: "canvas.extendLeft"; text: T.t("command.canvas.extendLeft").arg(host.bigStep); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.select_left_far; onTriggered: host.moveCaret(-host.bigStep, 0, true) },
        CommandAction { commandId: "canvas.extendRight"; text: T.t("command.canvas.extendRight").arg(host.bigStep); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.select_right_far; onTriggered: host.moveCaret(host.bigStep, 0, true) },
        CommandAction { commandId: "canvas.extendUp"; text: T.t("command.canvas.extendUp").arg(host.bigStep); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.select_up_far; onTriggered: host.moveCaret(0, -host.bigStep, true) },
        CommandAction { commandId: "canvas.extendDown"; text: T.t("command.canvas.extendDown").arg(host.bigStep); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.select_down_far; onTriggered: host.moveCaret(0, host.bigStep, true) },

        CommandAction { commandId: "canvas.paint"; text: T.t("command.canvas.paint"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.paint; enabled: doc.hasSelection || host.caretColumn >= 0; onTriggered: host.paintAtCaret(host.slot) },
        CommandAction { commandId: "canvas.erase"; text: T.t("command.canvas.erase"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.erase; enabled: doc.hasSelection || host.caretColumn >= 0; onTriggered: host.paintAtCaret(".") },
        CommandAction {
            commandId: "canvas.drawMode"; text: T.t("hint.drawAsYouMove"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.draw_mode
            onTriggered: { if (host.caretColumn < 0) host.moveCaret(0, 0); host.mode = host.mode === "draw" ? "" : "draw" }
        },
        CommandAction {
            commandId: "canvas.pickMode"; text: T.t("hint.pickColours"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.pick_mode
            onTriggered: {
                if (host.mode === "pick") host.mode = ""
                else { doc.clearSelection(); if (host.caretColumn < 0) host.moveCaret(0, 0); host.mode = "pick" }
            }
        },
        CommandAction {
            commandId: "canvas.linePoint"; text: T.t("hint.straightLine"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.line_point
            onTriggered: { if (host.caretColumn < 0) host.moveCaret(0, 0); host.linePoints = host.linePoints.concat([{ c: host.caretColumn, r: host.caretRow }]) }
        },
        CommandAction { commandId: "canvas.chooseSlot"; text: T.t("hint.changeColour"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.slot_leader; onTriggered: { host.awaitingSlot = true; host.focusCanvas() } },
        CommandAction { commandId: "canvas.chooseColour"; text: T.t("menu.chooseColour"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.choose_colour; onTriggered: colourSheet.show() },
        CommandAction { commandId: "canvas.replaceColour"; text: T.t("menu.replaceColour"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.replace_colour; enabled: host.focusedSlot !== ""; onTriggered: colourSheet.show("replace") },
        CommandAction { commandId: "canvas.roulette"; text: T.t("menu.roulette"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.roulette; onTriggered: host.russianRoulette() },
        CommandAction { commandId: "canvas.cancel"; text: T.t("action.cancel"); group: T.t("command.group.canvas"); shownShortcut: cfg.keys.cancel; onTriggered: { host.cancelCanvasState(); host.focusCanvas() } },

        CommandAction { commandId: "view.zoomIn"; text: T.t("menu.zoomIn"); group: T.t("command.group.view"); shortcut: cfg.shortcuts.zoom_in; onTriggered: stage.zoomStep(stage.width / 2, stage.height / 2, true) },
        CommandAction { commandId: "view.zoomOut"; text: T.t("menu.zoomOut"); group: T.t("command.group.view"); shortcut: cfg.shortcuts.zoom_out; onTriggered: stage.zoomStep(stage.width / 2, stage.height / 2, false) },
        CommandAction { commandId: "view.fit"; text: T.t("menu.fit"); group: T.t("command.group.view"); shortcut: cfg.shortcuts.zoom_fit; onTriggered: { stage.touched = false; stage.fit() } },
        CommandAction { commandId: "view.goTo"; text: T.t("menu.goToPixel"); group: T.t("command.group.view"); shownShortcut: cfg.keys.go_to_pixel; onTriggered: goToSheet.show() },
        CommandAction { commandId: "view.onion"; text: T.t("menu.onion"); group: T.t("command.group.view"); shownShortcut: cfg.keys.toggle_onion; checkable: true; checked: host.onion; onTriggered: host.onion = !host.onion },
        CommandAction { commandId: "view.grid"; text: T.t("menu.grid"); group: T.t("command.group.view"); shownShortcut: cfg.keys.toggle_grid; checkable: true; checked: host.mesh; onTriggered: host.mesh = !host.mesh },
        CommandAction { commandId: "view.hints"; text: T.t("menu.keyHints"); group: T.t("command.group.view"); shortcut: cfg.shortcuts.toggle_hints; checkable: true; checked: host.showHints; onTriggered: host.showHints = !host.showHints },
        CommandAction { commandId: "view.loop"; text: T.t("menu.loop"); group: T.t("command.group.view"); shortcut: cfg.shortcuts.toggle_loop; checkable: true; checked: host.loop; onTriggered: host.loop = !host.loop },
        CommandAction { commandId: "view.pickerActive"; text: T.t("menu.pickerActive"); group: T.t("command.group.view"); checkable: true; checked: doc.pickerScope === "active"; onTriggered: doc.pickerScope = "active" },
        CommandAction { commandId: "view.pickerComposite"; text: T.t("menu.pickerComposite"); group: T.t("command.group.view"); checkable: true; checked: doc.pickerScope === "composite"; onTriggered: doc.pickerScope = "composite" },
        CommandAction { commandId: "view.scopeFrame"; text: T.t("menu.scopeFrame"); group: T.t("command.group.view"); checkable: true; checked: doc.editScope === "frame"; onTriggered: doc.editScope = "frame" },
        CommandAction { commandId: "view.scopeAll"; text: T.t("menu.scopeAllFrames"); group: T.t("command.group.view"); checkable: true; checked: doc.editScope === "all-frames"; onTriggered: doc.editScope = "all-frames" },
        CommandAction { commandId: "view.reference"; text: T.t("menu.reference"); group: T.t("command.group.view"); onTriggered: referenceDialog.open() }
    ]

    function dynamicEntries() {
        var entries = []
        for (var i = 0; i < doc.palette.length; ++i) {
            var paletteEntry = doc.palette[i]
            entries.push({
                id: "palette.select", args: { slot: paletteEntry.slot },
                label: T.t("command.palette.selectSlot").arg(paletteEntry.slot),
                group: T.t("command.group.inspector"), keywords: String(paletteEntry.colour),
                shortcut: "", enabled: true, checkable: true,
                checked: host.slot === paletteEntry.slot
            })
        }
        return entries
    }

    function invokeDynamic(commandId, args) {
        if (commandId !== "palette.select")
            return false
        var slot = String(args.slot || "")
        var exists = slot === "." || doc.palette.some(function (entry) {
            return entry.slot === slot
        })
        if (!exists)
            return true
        host.slot = slot
        if (host.tool === "eraser")
            host.tool = "pencil"
        return true
    }
}
