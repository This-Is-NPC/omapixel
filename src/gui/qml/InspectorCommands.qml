import QtQuick

CommandProvider {
    id: provider

    required property var host
    required property var dock
    required property var layerDock
    required property var paletteSection
    required property var previewSection
    required property var spriteSection
    required property var referenceSection
    required property var historySection
    required property var referenceDialog

    actions: [
        CommandAction { commandId: "inspector.layers"; text: T.t("panel.layers"); group: T.t("command.group.inspector"); onTriggered: { dock.contentY = 0; layerDock.focusList() } },
        CommandAction { commandId: "inspector.palette"; text: T.t("panel.palette"); group: T.t("command.group.inspector"); onTriggered: host.focusInspectorSection(paletteSection) },
        CommandAction { commandId: "inspector.preview"; text: T.t("panel.preview"); group: T.t("command.group.inspector"); onTriggered: host.focusInspectorSection(previewSection) },
        CommandAction { commandId: "inspector.sprite"; text: T.t("panel.sprite"); group: T.t("command.group.inspector"); onTriggered: host.focusInspectorSection(spriteSection) },
        CommandAction { commandId: "inspector.reference"; text: T.t("panel.reference"); group: T.t("command.group.inspector"); onTriggered: host.focusInspectorSection(referenceSection) },
        CommandAction { commandId: "inspector.history"; text: T.t("panel.history"); group: T.t("command.group.inspector"); onTriggered: host.focusInspectorSection(historySection) },
        CommandAction { commandId: "inspector.paletteRows"; text: paletteSection.showAll ? T.t("panel.palette.showNine") : T.t("panel.palette.showAll").arg(doc.palette.length); group: T.t("command.group.inspector"); checkable: true; checked: paletteSection.showAll; onTriggered: paletteSection.showAll = !paletteSection.showAll },
        CommandAction { commandId: "inspector.canvasSize"; text: T.t("menu.canvasSize"); group: T.t("command.group.inspector"); onTriggered: { spriteSection.open = true; dock.contentY = spriteSection.y } },
        CommandAction { commandId: "inspector.resize"; text: T.t("panel.sprite.resize"); group: T.t("command.group.inspector"); enabled: host.sizeChanged; onTriggered: doc.resize(host.wantColumns, host.wantRows) },
        CommandAction { commandId: "inspector.reference.choose"; text: T.t("panel.reference.choose"); group: T.t("command.group.inspector"); onTriggered: referenceDialog.open() },
        CommandAction { commandId: "inspector.reference.position"; text: host.referenceOnTop ? T.t("panel.reference.behind") : T.t("panel.reference.onTop"); group: T.t("command.group.inspector"); enabled: host.referencePath !== ""; checkable: true; checked: host.referenceOnTop; onTriggered: host.referenceOnTop = !host.referenceOnTop },
        CommandAction { commandId: "inspector.reference.clear"; text: T.t("panel.reference.clear"); group: T.t("command.group.inspector"); enabled: host.referencePath !== ""; onTriggered: host.referencePath = "" }
    ]

    function dynamicEntries() {
        var entries = []
        var sectionIds = ["palette", "preview", "sprite", "reference", "history"]
        for (var sectionIndex = 0; sectionIndex < sectionIds.length; ++sectionIndex) {
            var sectionId = sectionIds[sectionIndex]
            var section = provider.section(sectionId)
            entries.push({
                id: "inspector.toggleSection", args: { sectionId: sectionId },
                label: T.t("command.inspector.toggleSection").arg(section.title),
                group: T.t("command.group.inspector"), keywords: "", shortcut: "",
                enabled: true, checkable: true, checked: section.open
            })
        }
        var presets = doc.sizePresets()
        for (var i = 0; i < presets.length; ++i) {
            entries.push({
                id: "inspector.setSize", args: { width: presets[i].w, height: presets[i].h },
                label: T.t("command.inspector.size").arg(presets[i].w).arg(presets[i].h),
                group: T.t("command.group.inspector"), keywords: "", shortcut: "",
                enabled: true, checkable: true,
                checked: host.wantColumns === presets[i].w && host.wantRows === presets[i].h
            })
        }
        for (var alpha = 0; alpha <= 100; alpha += 25) {
            entries.push({
                id: "inspector.reference.setOpacity", args: { percent: alpha },
                label: T.t("command.inspector.referenceOpacity").arg(alpha),
                group: T.t("command.group.inspector"), keywords: "", shortcut: "",
                enabled: host.referencePath !== "", checkable: true,
                checked: Math.round(host.referenceAlpha * 100) === alpha
            })
        }
        return entries
    }

    function invokeDynamic(commandId, args) {
        if (commandId === "inspector.toggleSection") {
            var target = provider.section(String(args.sectionId || ""))
            if (target)
                target.open = !target.open
            return true
        }
        if (commandId === "inspector.setSize") {
            var width = Number(args.width)
            var height = Number(args.height)
            var valid = doc.sizePresets().some(function (preset) {
                return preset.w === width && preset.h === height
            })
            if (valid) { host.wantColumns = width; host.wantRows = height }
            return true
        }
        if (commandId === "inspector.reference.setOpacity") {
            var percent = Number(args.percent)
            if (host.referencePath !== "" && percent >= 0 && percent <= 100
                    && percent % 25 === 0)
                host.referenceAlpha = percent / 100
            return true
        }
        return false
    }

    function section(sectionId) {
        if (sectionId === "palette") return paletteSection
        if (sectionId === "preview") return previewSection
        if (sectionId === "sprite") return spriteSection
        if (sectionId === "reference") return referenceSection
        if (sectionId === "history") return historySection
        return null
    }
}
