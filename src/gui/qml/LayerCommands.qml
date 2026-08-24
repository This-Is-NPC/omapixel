import QtQuick

CommandProvider {
    id: provider
    required property var layerDock
    required property var layerTool

    actions: [
        CommandAction { commandId: "layers.addAnimated"; text: T.t("panel.layers.addAnimated"); group: T.t("command.group.layers"); onTriggered: layerDock.add("animated") },
        CommandAction { commandId: "layers.addShared"; text: T.t("panel.layers.addShared"); group: T.t("command.group.layers"); onTriggered: layerDock.add("shared") },
        CommandAction { commandId: "layers.openTool"; text: T.t("panel.layers.toolOpen"); group: T.t("command.group.layers"); shortcut: cfg.shortcuts.layer_tool; enabled: doc.activeLayerId !== ""; onTriggered: layerDock.openCurrent() },
        CommandAction { commandId: "layers.visibility"; text: T.t("accessibility.layers.visibilityAll"); group: T.t("command.group.layers"); enabled: doc.activeLayerId !== ""; onTriggered: layerTool.toggleVisibility() },
        CommandAction { commandId: "layers.lock"; text: T.t("accessibility.layers.lockAll"); group: T.t("command.group.layers"); enabled: doc.activeLayerId !== ""; onTriggered: layerTool.toggleLock() },
        CommandAction { commandId: "layers.closeTool"; text: T.t("accessibility.layers.closeTool"); group: T.t("command.group.layers"); enabled: layerTool.visible; onTriggered: layerTool.closeTool() },
        CommandAction { commandId: "layers.duplicate"; text: T.t("panel.layers.duplicate"); group: T.t("command.group.layers"); enabled: doc.activeLayerId !== ""; onTriggered: layerTool.duplicate() },
        CommandAction { commandId: "layers.rename"; text: T.t("accessibility.layers.rename").arg(doc.activeLayerName); group: T.t("command.group.layers"); enabled: layerTool.visible && doc.activeLayerId !== ""; onTriggered: layerTool.focusRename() },
        CommandAction { commandId: "layers.opacity"; text: T.t("accessibility.layers.opacity").arg(doc.activeLayerName); group: T.t("command.group.layers"); enabled: layerTool.visible && doc.activeLayerId !== ""; onTriggered: layerTool.focusOpacity() },
        CommandAction { commandId: "layers.moveUp"; text: T.t("accessibility.layers.moveUp"); group: T.t("command.group.layers"); enabled: layerTool.activeIndex() > 0; onTriggered: layerTool.move(-1) },
        CommandAction { commandId: "layers.moveDown"; text: T.t("accessibility.layers.moveDown"); group: T.t("command.group.layers"); enabled: layerTool.activeIndex() >= 0 && layerTool.activeIndex() < doc.layers.length - layerTool.actionIds().length; onTriggered: layerTool.move(1) },
        CommandAction { commandId: "layers.delete"; text: T.t("accessibility.layers.delete"); group: T.t("command.group.layers"); enabled: doc.layers.length > 1; onTriggered: doc.removeLayers(layerTool.actionIds()) },
        CommandAction { commandId: "layers.clearFrame"; text: T.t("panel.layers.clearFrame"); group: T.t("command.group.layers"); enabled: doc.activeLayerId !== ""; onTriggered: layerTool.clearLayer(false) },
        CommandAction { commandId: "layers.clearAll"; text: T.t("panel.layers.clearAll"); group: T.t("command.group.layers"); enabled: doc.activeLayerId !== ""; onTriggered: layerTool.clearLayer(true) },
        CommandAction { commandId: "layers.mode.normal"; text: T.t("panel.layers.normal"); group: T.t("command.group.layers"); enabled: doc.activeLayerId !== ""; checkable: true; checked: provider.activeMode() === "normal"; onTriggered: doc.setLayerMode(doc.activeLayerId, "normal") },
        CommandAction { commandId: "layers.mode.multiply"; text: T.t("panel.layers.multiply"); group: T.t("command.group.layers"); enabled: doc.activeLayerId !== ""; checkable: true; checked: provider.activeMode() === "multiply"; onTriggered: doc.setLayerMode(doc.activeLayerId, "multiply") },
        CommandAction { commandId: "layers.mode.screen"; text: T.t("panel.layers.screen"); group: T.t("command.group.layers"); enabled: doc.activeLayerId !== ""; checkable: true; checked: provider.activeMode() === "screen"; onTriggered: doc.setLayerMode(doc.activeLayerId, "screen") },
        CommandAction { commandId: "layers.scope.frame"; text: T.t("accessibility.layers.frameScope"); group: T.t("command.group.layers"); enabled: doc.activeLayerStorage !== "shared"; checkable: true; checked: doc.editScope === "frame"; onTriggered: doc.editScope = "frame" },
        CommandAction { commandId: "layers.scope.all"; text: T.t("accessibility.layers.allFramesScope"); group: T.t("command.group.layers"); enabled: doc.activeLayerStorage !== "shared"; checkable: true; checked: doc.editScope === "all-frames"; onTriggered: doc.editScope = "all-frames" },
        CommandAction { commandId: "layers.convertAnimated"; text: T.t("accessibility.layers.convertAnimated"); group: T.t("command.group.layers"); enabled: doc.activeLayerStorage === "shared"; onTriggered: layerTool.convert("animated") },
        CommandAction { commandId: "layers.convertShared"; text: T.t("accessibility.layers.convertShared"); group: T.t("command.group.layers"); enabled: doc.activeLayerStorage === "animated"; onTriggered: layerTool.convert("shared") },
        CommandAction { commandId: "layers.mergeDown"; text: T.t("panel.layers.mergeDown"); group: T.t("command.group.layers"); enabled: layerTool.activeIndex() > 0; onTriggered: layerTool.merge() },
        CommandAction { commandId: "layers.flatten"; text: T.t("panel.layers.flatten"); group: T.t("command.group.layers"); enabled: doc.layers.length > 1; onTriggered: layerTool.flatten() }
    ]

    function activeMode() {
        for (var i = 0; i < doc.layers.length; ++i)
            if (doc.layers[i].id === doc.activeLayerId)
                return doc.layers[i].mode
        return ""
    }

    function dynamicEntries() {
        var entries = []
        for (var i = 0; i < doc.layers.length; ++i) {
            var layer = doc.layers[i]
            entries.push({
                id: "layers.toggleSelection", args: { layerId: layer.id },
                label: T.t("command.layers.toggleSelection").arg(layer.name),
                group: T.t("command.group.layers"), keywords: "", shortcut: "",
                enabled: true, checkable: true, checked: layerDock.has(layer.id)
            })
            entries.push({
                id: "layers.select", args: { layerId: layer.id },
                label: T.t("command.layers.select").arg(layer.name),
                group: T.t("command.group.layers"), keywords: "", shortcut: "",
                enabled: true, checkable: true, checked: doc.activeLayerId === layer.id
            })
        }
        return entries
    }

    function invokeDynamic(commandId, args) {
        if (commandId !== "layers.select" && commandId !== "layers.toggleSelection")
            return false
        var layerId = String(args.layerId || "")
        var exists = doc.layers.some(function (layer) { return layer.id === layerId })
        if (!exists)
            return true
        if (commandId === "layers.select")
            layerDock.activate(layerId)
        else
            layerDock.toggleStructural(layerId)
        return true
    }
}
