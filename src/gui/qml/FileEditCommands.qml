import QtQuick

CommandProvider {
    required property var host
    required property var saveDialog
    required property var exportDialog

    actions: [
        CommandAction { commandId: "file.new"; text: T.t("menu.new"); group: T.t("command.group.file"); shortcut: cfg.shortcuts.new; onTriggered: host.requestAction("new") },
        CommandAction { commandId: "file.open"; text: T.t("menu.open"); group: T.t("command.group.file"); shortcut: cfg.shortcuts.open; onTriggered: host.requestAction("open") },
        CommandAction { commandId: "file.save"; text: T.t("menu.save"); group: T.t("command.group.file"); shortcut: cfg.shortcuts.save; onTriggered: doc.path === "" ? saveDialog.open() : doc.save() },
        CommandAction { commandId: "file.saveAs"; text: T.t("menu.saveAs"); group: T.t("command.group.file"); shortcut: cfg.shortcuts.save_as; onTriggered: saveDialog.open() },
        CommandAction { commandId: "file.export"; text: T.t("menu.exportPng"); group: T.t("command.group.file"); shortcut: cfg.shortcuts.export_png; onTriggered: { exportDialog.asSheet = false; exportDialog.open() } },
        CommandAction { commandId: "file.exportSheet"; text: T.t("menu.exportSheet"); group: T.t("command.group.file"); shortcut: cfg.shortcuts.export_sheet; onTriggered: { exportDialog.asSheet = true; exportDialog.open() } },
        CommandAction { commandId: "file.quit"; text: T.t("menu.quit"); group: T.t("command.group.file"); shortcut: cfg.shortcuts.quit; onTriggered: host.requestAction("quit") },

        CommandAction { commandId: "edit.undo"; text: T.t("menu.undo"); group: T.t("command.group.edit"); shortcut: cfg.shortcuts.undo; enabled: doc.canUndo; onTriggered: doc.undo() },
        CommandAction { commandId: "edit.redo"; text: T.t("menu.redo"); group: T.t("command.group.edit"); shortcut: cfg.shortcuts.redo; enabled: doc.canRedo; onTriggered: doc.redo() },
        CommandAction { commandId: "edit.copy"; text: T.t("menu.copyPixels"); group: T.t("command.group.edit"); shortcut: cfg.shortcuts.copy_pixels; enabled: doc.hasSelection; onTriggered: doc.copySelection() },
        CommandAction { commandId: "edit.paste"; text: T.t("menu.pastePixels"); group: T.t("command.group.edit"); shortcut: cfg.shortcuts.paste_pixels; onTriggered: host.pastePixels() },
        CommandAction { commandId: "edit.clear"; text: T.t("menu.clearFrame"); group: T.t("command.group.edit"); shortcut: cfg.shortcuts.clear_frame; onTriggered: doc.clearFrame() },
        CommandAction { commandId: "edit.flipX"; text: T.t("menu.flipX"); group: T.t("command.group.edit"); onTriggered: doc.flip("x") },
        CommandAction { commandId: "edit.flipY"; text: T.t("menu.flipY"); group: T.t("command.group.edit"); onTriggered: doc.flip("y") },
        CommandAction { commandId: "edit.trim"; text: T.t("menu.trim"); group: T.t("command.group.edit"); shortcut: cfg.shortcuts.trim; onTriggered: host.requestTrim() }
    ]
}
