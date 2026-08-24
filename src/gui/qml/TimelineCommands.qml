import QtQuick

CommandProvider {
    required property var host
    required property var timeline

    actions: [
        CommandAction { commandId: "timeline.addClip"; text: T.t("menu.addClip"); group: T.t("command.group.timeline"); onTriggered: doc.addClip("clip " + (doc.clipNames.length + 1)) },
        CommandAction { commandId: "timeline.removeClip"; text: T.t("menu.deleteClip"); group: T.t("command.group.timeline"); enabled: doc.clipNames.length > 1; onTriggered: doc.removeClip(doc.clip) },
        CommandAction { commandId: "timeline.renameClip"; text: T.t("timeline.clip"); group: T.t("command.group.timeline"); onTriggered: timeline.focusClipName() },
        CommandAction { commandId: "timeline.fpsDown"; text: T.t("timeline.decreaseFps"); group: T.t("command.group.timeline"); onTriggered: doc.setFps(doc.fps - 1) },
        CommandAction { commandId: "timeline.fpsUp"; text: T.t("timeline.increaseFps"); group: T.t("command.group.timeline"); onTriggered: doc.setFps(doc.fps + 1) },
        CommandAction { commandId: "timeline.play"; text: host.playing ? T.t("timeline.pause") : T.t("timeline.play"); group: T.t("command.group.timeline"); shortcut: cfg.shortcuts.play; enabled: doc.frameCount > 1; checkable: true; checked: host.playing; onTriggered: host.togglePlay() },
        CommandAction { commandId: "timeline.addFrame"; text: T.t("menu.addFrame"); group: T.t("command.group.timeline"); shortcut: cfg.shortcuts.frame_add; onTriggered: doc.addFrame(false) },
        CommandAction { commandId: "timeline.duplicateFrame"; text: T.t("menu.dupFrame"); group: T.t("command.group.timeline"); shortcut: cfg.shortcuts.frame_duplicate; onTriggered: doc.addFrame(true) },
        CommandAction { commandId: "timeline.moveBack"; text: T.t("menu.frameBack"); group: T.t("command.group.timeline"); shortcut: cfg.shortcuts.frame_move_back; enabled: doc.frame > 0; onTriggered: doc.moveFrame(-1) },
        CommandAction { commandId: "timeline.moveOn"; text: T.t("menu.frameOn"); group: T.t("command.group.timeline"); shortcut: cfg.shortcuts.frame_move_on; enabled: doc.frame < doc.frameCount - 1; onTriggered: doc.moveFrame(1) },
        CommandAction { commandId: "timeline.deleteFrame"; text: T.t("menu.delFrame"); group: T.t("command.group.timeline"); enabled: doc.frameCount > 1; onTriggered: doc.removeFrame() },
        CommandAction { commandId: "timeline.previousFrame"; text: T.t("command.timeline.previousFrame"); group: T.t("command.group.timeline"); shownShortcut: cfg.keys.frame_previous; enabled: doc.frame > 0; onTriggered: doc.frame = Math.max(0, doc.frame - 1) },
        CommandAction { commandId: "timeline.nextFrame"; text: T.t("command.timeline.nextFrame"); group: T.t("command.group.timeline"); shownShortcut: cfg.keys.frame_next; enabled: doc.frame < doc.frameCount - 1; onTriggered: doc.frame = Math.min(doc.frameCount - 1, doc.frame + 1) },
        CommandAction { commandId: "timeline.previousClip"; text: T.t("menu.prevClip"); group: T.t("command.group.timeline"); shortcut: cfg.shortcuts.clip_previous; enabled: doc.clipNames.length > 1; onTriggered: host.stepClip(-1) },
        CommandAction { commandId: "timeline.nextClip"; text: T.t("menu.nextClip"); group: T.t("command.group.timeline"); shortcut: cfg.shortcuts.clip_next; enabled: doc.clipNames.length > 1; onTriggered: host.stepClip(1) }
    ]

    function dynamicEntries() {
        var entries = []
        for (var i = 0; i < doc.clips.length; ++i) {
            var clip = doc.clips[i]
            entries.push({
                id: "timeline.selectClip", args: { clipId: clip.id },
                label: T.t("command.timeline.selectClip").arg(clip.name),
                group: T.t("command.group.timeline"), keywords: "", shortcut: "",
                enabled: true, checkable: true, checked: doc.activeClipId === clip.id
            })
        }
        for (var frame = 0; frame < doc.frameCount; ++frame) {
            entries.push({
                id: "timeline.selectFrame", args: { frame: frame },
                label: T.t("command.timeline.selectFrame").arg(frame + 1),
                group: T.t("command.group.timeline"), keywords: "", shortcut: "",
                enabled: true, checkable: true, checked: doc.frame === frame
            })
        }
        return entries
    }

    function invokeDynamic(commandId, args) {
        if (commandId === "timeline.selectClip") {
            doc.selectClip(String(args.clipId || ""))
            return true
        }
        if (commandId === "timeline.selectFrame") {
            var frame = Number(args.frame)
            if (Number.isInteger(frame) && frame >= 0 && frame < doc.frameCount)
                doc.frame = frame
            return true
        }
        return false
    }
}
