import QtQuick

CommandProvider {
    required property var host
    required property var overlay
    required property var menuBar
    required property var firstControl

    actions: [
        CommandAction {
            commandId: "command.palette"; text: T.t("command.palette.title")
            group: T.t("command.group.navigation"); shortcut: cfg.shortcuts.command_palette
            onTriggered: host.openCommandPalette()
        },
        CommandAction {
            commandId: "navigate"; text: T.t("command.navigate")
            group: T.t("command.group.navigation"); keywords: T.t("command.navigate.keywords")
            onTriggered: overlay.show()
        },
        CommandAction {
            commandId: "navigate.canvas"; text: T.t("command.navigate.canvas")
            group: T.t("command.group.navigation"); shownShortcut: "1"
            onTriggered: overlay.choose(1)
        },
        CommandAction {
            commandId: "navigate.inspector"; text: T.t("command.navigate.inspector")
            group: T.t("command.group.navigation"); shownShortcut: "2"
            onTriggered: overlay.choose(2)
        },
        CommandAction {
            commandId: "navigate.timeline"; text: T.t("command.navigate.timeline")
            group: T.t("command.group.navigation"); shownShortcut: "3"
            onTriggered: overlay.choose(3)
        },
        CommandAction {
            commandId: "navigate.controls"; text: T.t("hint.controls")
            group: T.t("command.group.navigation"); shownShortcut: "Tab"
            onTriggered: firstControl.forceActiveFocus()
        },
        CommandAction {
            commandId: "navigate.menus"; text: T.t("hint.menus")
            group: T.t("command.group.navigation"); shortcut: cfg.shortcuts.menus
            onTriggered: menuBar.forceActiveFocus()
        }
    ]
}
