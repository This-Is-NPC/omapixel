import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic as C
import omapixel

// One native, non-modal tool window for the active layer. It shares the one
// DocumentModel owned by Main.qml; creating this Window never creates a second
// Studio or a second published agent session.
Window {
    id: tool
    objectName: "layerToolWindow"
    flags: Qt.Window
    modality: Qt.NonModal
    transientParent: win
    visible: false
    width: 430
    height: 740
    minimumWidth: 360
    minimumHeight: 520
    color: theme.panel
    title: T.t("panel.layers.toolWindow") + " · " + doc.activeLayerName

    property var structuralIds: []
    property var returnFocusItem: null
    property bool positioned: false
    property bool wasVisible: false

    function currentLayer() {
        var found = doc.layers.filter(function (layer) {
            return layer.id === doc.activeLayerId
        })
        return found.length > 0 ? found[0] : ({})
    }

    function actionIds() {
        var ids = structuralIds || []
        return ids.length > 0 ? ids : (doc.activeLayerId === "" ? [] : [doc.activeLayerId])
    }

    function activeIndex() {
        for (var i = 0; i < doc.layers.length; ++i)
            if (doc.layers[i].id === doc.activeLayerId)
                return i
        return -1
    }

    function openFor(id) {
        if (id !== "" && doc.activeLayerId !== id)
            doc.activeLayerId = id
        if (!positioned) {
            x = win.x + win.width + 24
            y = win.y + 40
            positioned = true
        }
        show()
        requestActivate()
        Qt.callLater(tool.focusWindow)
    }

    function focusWindow() {
        requestActivate()
        toolContent.forceActiveFocus()
    }

    function returnFocus() {
        if (returnFocusItem)
            returnFocusItem.focusList()
        else
            win.focusCanvas()
    }

    function closeTool() {
        close()
        Qt.callLater(tool.returnFocus)
    }

    function toggleVisibility() {
        var layer = currentLayer()
        if (layer.id !== "")
            doc.setLayersVisible(actionIds(), !layer.visible)
    }

    function toggleLock() {
        var layer = currentLayer()
        if (layer.id !== "")
            doc.setLayersLocked(actionIds(), !layer.locked)
    }

    function duplicate() {
        if (doc.activeLayerId === "")
            return
        var id = doc.nextLayerId()
        if (doc.duplicateLayer(doc.activeLayerId, id, doc.nextLayerName()))
            doc.activeLayerId = id
    }

    function move(delta) {
        var index = activeIndex()
        if (index < 0)
            return
        doc.moveLayers(actionIds(), Math.max(0, Math.min(doc.layers.length - actionIds().length,
                                                         index + delta)))
    }

    function convert(storage) {
        var layer = currentLayer()
        if (layer.id !== "" && layer.storage !== storage)
            confirmationRequested("storage", layer.id,
                                  doc.layerStoragePreview(layer.id, storage))
    }

    function merge() {
        if (doc.activeLayerId !== "")
            confirmationRequested("merge-down", doc.activeLayerId,
                                  doc.mergeDownPreview(doc.activeLayerId))
    }

    function flatten() {
        confirmationRequested("flatten", "", doc.flattenPreview())
    }

    function clearLayer(allFrames) {
        if (doc.activeLayerId !== "")
            doc.clearLayer(doc.activeLayerId, allFrames)
    }

    function focusAfterConfirmation() {
        if (visible)
            focusWindow()
        else
            returnFocus()
    }

    signal confirmationRequested(string kind, string layerId, var report)
    signal commandRequested(string commandId)

    onVisibleChanged: {
        if (!visible && wasVisible)
            Qt.callLater(tool.returnFocus)
        wasVisible = visible
    }
    onClosing: Qt.callLater(tool.returnFocus)

    Item {
        id: toolContent
        objectName: "layerToolContent"
        anchors.fill: parent
        anchors.margins: 14
        activeFocusOnTab: true
        Accessible.name: T.t("panel.layers.toolWindow")
        Accessible.description: T.t("panel.layers.toolWindowHint")

        Keys.onEscapePressed: function (event) {
            tool.closeTool()
            event.accepted = true
        }

        Flickable {
            id: toolScroll
            anchors.fill: parent
            contentWidth: width
            contentHeight: toolBody.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: toolBody
                width: toolScroll.width
                spacing: 8

                Rectangle {
                    id: targetCard
                    objectName: "layerToolTargetCard"
                    width: parent.width
                    height: targetColumn.implicitHeight + 20
                    radius: theme.rounding
                    color: theme.fill(theme.accent, 0.14)
                    border.width: 1
                    border.color: theme.accent
                    Column {
                        id: targetColumn
                        x: 10
                        y: 10
                        width: parent.width - 20
                        spacing: 3
                        Text {
                            id: targetText
                            objectName: "layerToolTargetText"
                            width: parent.width
                            text: T.t("panel.layers.toolTarget").arg(doc.activeLayerName)
                            color: theme.foreground
                            font.family: theme.fontFamily
                            font.pixelSize: 14
                            font.bold: true
                            wrapMode: Text.Wrap
                        }
                        Label {
                            objectName: "layerToolStructuralTargetText"
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: T.t("panel.layers.structuralTarget").arg((structuralIds || []).length)
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: 6
                    Chip {
                        objectName: "toolVisibilityAction"
                        label: T.t("panel.layers.visibility")
                        on: currentLayer().visible
                        usable: doc.activeLayerId !== ""
                        onClicked: tool.commandRequested("layers.visibility")
                        Accessible.name: T.t("accessibility.layers.visibilityAll")
                    }
                    Chip {
                        objectName: "toolLockAction"
                        label: T.t("panel.layers.lock")
                        on: currentLayer().locked
                        usable: doc.activeLayerId !== ""
                        onClicked: tool.commandRequested("layers.lock")
                        Accessible.name: T.t("accessibility.layers.lockAll")
                    }
                    Chip {
                        objectName: "toolCloseAction"
                        label: T.t("action.close")
                        onClicked: tool.commandRequested("layers.closeTool")
                        Accessible.name: T.t("accessibility.layers.closeTool")
                    }
                }

                Section {
                    title: T.t("panel.layers.inspector")
                    hint: currentLayer().name || T.t("panel.layers.none")

                    Field {
                        objectName: "toolRenameField"
                        width: parent.width
                        boxWidth: parent.width
                        label: T.t("accessibility.layers.rename").arg(currentLayer().name || "")
                        value: currentLayer().name || ""
                        Accessible.name: T.t("accessibility.layers.rename").arg(currentLayer().name || "")
                        onEscaped: tool.focusWindow()
                        onCommitted: function (name) {
                            if (doc.activeLayerId !== "")
                                doc.renameLayer(doc.activeLayerId, name.trim())
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: 8
                        Label {
                            text: T.t("panel.layers.opacity")
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        C.Slider {
                            id: opacitySlider
                            objectName: "toolOpacitySlider"
                            width: Math.max(80, parent.width - 120)
                            from: 0
                            to: 100
                            value: currentLayer().opacity * 100 / 255
                            onMoved: if (doc.activeLayerId !== "")
                                         doc.setLayerOpacity(doc.activeLayerId,
                                                             Math.round(value * 255 / 100))
                            Accessible.name: T.t("accessibility.layers.opacitySlider")
                        }
                        Label {
                            objectName: "toolOpacityValue"
                            width: 38
                            text: Math.round(currentLayer().opacity * 100 / 255) + "%"
                            horizontalAlignment: Text.AlignRight
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: T.t("accessibility.layers.opacity").arg(currentLayer().name || "")
                        }
                    }

                    Flow {
                        width: parent.width
                        spacing: 5
                        Label { text: T.t("panel.layers.mode") }
                        Chip {
                            objectName: "toolNormalModeButton"
                            label: T.t("panel.layers.normal")
                            on: currentLayer().mode === "normal"
                            usable: doc.activeLayerId !== ""
                            onClicked: tool.commandRequested("layers.mode.normal")
                            Accessible.name: T.t("accessibility.layers.mode") + ": " + T.t("panel.layers.normal")
                        }
                        Chip {
                            objectName: "toolMultiplyModeButton"
                            label: T.t("panel.layers.multiply")
                            on: currentLayer().mode === "multiply"
                            usable: doc.activeLayerId !== ""
                            onClicked: tool.commandRequested("layers.mode.multiply")
                            Accessible.name: T.t("accessibility.layers.mode") + ": " + T.t("panel.layers.multiply")
                        }
                        Chip {
                            objectName: "toolScreenModeButton"
                            label: T.t("panel.layers.screen")
                            on: currentLayer().mode === "screen"
                            usable: doc.activeLayerId !== ""
                            onClicked: tool.commandRequested("layers.mode.screen")
                            Accessible.name: T.t("accessibility.layers.mode") + ": " + T.t("panel.layers.screen")
                        }
                    }
                }

                Section {
                    title: T.t("panel.layers.scope")
                    hint: doc.activeLayerStorage === "shared" ? T.t("panel.layers.allFrames") : doc.editScope
                    Row {
                        width: parent.width
                        spacing: 5
                        Chip {
                            width: Math.max(120, (parent.width - 5) / 2)
                            label: T.t("panel.layers.frame")
                            on: doc.editScope === "frame"
                            usable: doc.activeLayerStorage !== "shared"
                            onClicked: tool.commandRequested("layers.scope.frame")
                            Accessible.name: T.t("accessibility.layers.frameScope")
                        }
                        Chip {
                            width: Math.max(120, (parent.width - 5) / 2)
                            label: T.t("panel.layers.allFrames")
                            on: doc.activeLayerStorage === "shared" || doc.editScope === "all-frames"
                            usable: doc.activeLayerStorage !== "shared"
                            onClicked: tool.commandRequested("layers.scope.all")
                            Accessible.name: T.t("accessibility.layers.allFramesScope")
                        }
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        visible: doc.activeLayerStorage === "shared"
                        text: T.t("panel.layers.sharedScope")
                    }
                }

                Section {
                    title: T.t("panel.layers.arrange")
                    hint: T.t("panel.layers.selectionHint")
                    Flow {
                        width: parent.width
                        spacing: 5
                        Chip {
                            objectName: "toolDuplicateAction"
                            label: T.t("panel.layers.duplicate")
                            usable: doc.activeLayerId !== ""
                            onClicked: tool.commandRequested("layers.duplicate")
                            Accessible.name: T.t("accessibility.layers.duplicate")
                        }
                        Chip {
                            objectName: "toolMoveUpAction"
                            label: T.t("panel.layers.up")
                            usable: doc.activeLayerId !== ""
                            onClicked: tool.commandRequested("layers.moveUp")
                            Accessible.name: T.t("accessibility.layers.moveUp")
                        }
                        Chip {
                            objectName: "toolMoveDownAction"
                            label: T.t("panel.layers.down")
                            usable: doc.activeLayerId !== ""
                            onClicked: tool.commandRequested("layers.moveDown")
                            Accessible.name: T.t("accessibility.layers.moveDown")
                        }
                        Chip {
                            objectName: "toolDeleteAction"
                            label: T.t("panel.layers.delete")
                            role: theme.urgent
                            usable: doc.layers.length > 1
                            onClicked: tool.commandRequested("layers.delete")
                            Accessible.name: T.t("accessibility.layers.delete")
                        }
                    }
                }

                Section {
                    title: T.t("panel.layers.content")
                    Flow {
                        width: parent.width
                        spacing: 5
                        Chip {
                            objectName: "toolClearFrameAction"
                            label: T.t("panel.layers.clearFrame")
                            usable: doc.activeLayerId !== ""
                            onClicked: tool.commandRequested("layers.clearFrame")
                            Accessible.name: T.t("accessibility.layers.clearFrame")
                        }
                        Chip {
                            objectName: "toolClearAllAction"
                            label: T.t("panel.layers.clearAll")
                            usable: doc.activeLayerId !== ""
                            onClicked: tool.commandRequested("layers.clearAll")
                            Accessible.name: T.t("accessibility.layers.clearAll")
                        }
                    }
                }

                Section {
                    title: T.t("panel.layers.conversion")
                    hint: T.t("panel.layers.confirmationHint")
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: T.t("panel.layers.conversionHelp")
                    }
                    Flow {
                        width: parent.width
                        spacing: 5
                        Chip {
                            objectName: "toolConvertAnimatedAction"
                            label: T.t("panel.layers.convertAnimated")
                            usable: doc.activeLayerStorage === "shared"
                            onClicked: tool.commandRequested("layers.convertAnimated")
                            Accessible.name: T.t("accessibility.layers.convertAnimated")
                        }
                        Chip {
                            objectName: "toolConvertSharedAction"
                            label: T.t("panel.layers.convertShared")
                            usable: doc.activeLayerStorage === "animated"
                            onClicked: tool.commandRequested("layers.convertShared")
                            Accessible.name: T.t("accessibility.layers.convertShared")
                        }
                    }
                }

                Section {
                    title: T.t("panel.layers.destructive")
                    hint: T.t("panel.layers.confirmationHint")
                    Flow {
                        width: parent.width
                        spacing: 5
                        Chip {
                            objectName: "toolMergeAction"
                            label: T.t("panel.layers.mergeDown")
                            usable: tool.activeIndex() > 0
                            onClicked: tool.commandRequested("layers.mergeDown")
                            Accessible.name: T.t("accessibility.layers.mergeDown")
                        }
                        Chip {
                            objectName: "toolFlattenAction"
                            label: T.t("panel.layers.flatten")
                            role: theme.urgent
                            usable: doc.layers.length > 1
                            onClicked: tool.commandRequested("layers.flatten")
                            Accessible.name: T.t("accessibility.layers.flatten")
                        }
                    }
                }
            }
        }
    }
}
