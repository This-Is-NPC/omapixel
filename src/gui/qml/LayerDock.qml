import QtQuick
import omapixel

// The sidebar is deliberately only a layer browser. Editing and structural
// operations live in LayerToolWindow so the list stays scannable and the same
// tool can be moved to another monitor without taking the Studio with it.
Item {
    id: dock
    objectName: "layerDock"

    property var selectedIds: doc.activeLayerId === "" ? [] : [doc.activeLayerId]
    property int rowHeight: 64
    signal layerActivated(string layerId)

    implicitHeight: content.implicitHeight
    height: implicitHeight

    function has(id) { return selectedIds.indexOf(id) >= 0 }

    function activeIndex() {
        for (var i = 0; i < doc.layers.length; ++i)
            if (doc.layers[i].id === doc.activeLayerId)
                return i
        return -1
    }

    function selectPaint(id) {
        doc.activeLayerId = id
        selectedIds = [id]
    }

    function activate(id) {
        selectPaint(id)
        layerActivated(id)
    }

    function openCurrent() {
        var index = layerList.currentIndex >= 0 ? layerList.currentIndex : activeIndex()
        if (index < 0 || index >= doc.layers.length)
            return
        layerList.currentIndex = index
        activate(doc.layers[index].id)
    }

    function focusList() {
        var index = activeIndex()
        if (index >= 0)
            layerList.currentIndex = index
        layerList.forceActiveFocus()
    }

    function toggleStructural(id) {
        var next = selectedIds.slice()
        var at = next.indexOf(id)
        if (at >= 0 && next.length > 1)
            next.splice(at, 1)
        else if (at < 0)
            next.push(id)
        selectedIds = next
    }

    function add(storage) {
        var id = doc.nextLayerId()
        if (doc.addLayer(id, doc.nextLayerName(), storage))
            activate(id)
    }

    function reconcileSelection() {
        var known = doc.layers.map(function (layer) { return layer.id })
        var next = selectedIds.filter(function (id) { return known.indexOf(id) >= 0 })
        if (next.length === 0 && doc.activeLayerId !== "")
            next = [doc.activeLayerId]
        selectedIds = next
        if (layerList.currentIndex >= doc.layers.length)
            layerList.currentIndex = Math.max(0, doc.layers.length - 1)
    }

    Connections {
        target: doc
        function onChanged() { dock.reconcileSelection() }
        function onViewChanged() { dock.reconcileSelection() }
    }

    Column {
        id: content
        width: parent.width
        spacing: 8

        Rectangle {
            objectName: "layerDockHeader"
            width: parent.width
            height: 58
            radius: theme.rounding
            color: theme.fill(theme.foreground, 0.045)
            border.width: 1
            border.color: theme.fill(theme.foreground, 0.16)

            Column {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: T.t("panel.layers")
                    color: theme.foreground
                    font.family: theme.fontFamily
                    font.pixelSize: 14
                    font.bold: true
                }
                Label { text: T.t("panel.layers.count").arg(doc.layers.length) }
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: doc.activeLayerName === ""
                      ? T.t("panel.layers.noTarget")
                      : T.t("panel.layers.target").arg(doc.activeLayerName)
                color: theme.accent
                font.family: theme.fontFamily
                font.pixelSize: 10
                font.bold: true
                width: Math.max(80, parent.width - 150)
                height: 36
                wrapMode: Text.Wrap
                maximumLineCount: 2
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        Flow {
            width: parent.width
            spacing: 5
            Chip {
                label: T.t("panel.layers.addAnimated")
                onClicked: dock.add("animated")
                Accessible.name: T.t("accessibility.layers.addAnimated")
            }
            Chip {
                label: T.t("panel.layers.addShared")
                onClicked: dock.add("shared")
                Accessible.name: T.t("accessibility.layers.addShared")
            }
            Label {
                text: T.t("panel.layers.toolOpen")
                color: theme.dim
                visible: parent.width >= 300
            }
        }

        Rectangle {
            objectName: "structuralSelectionBanner"
            width: parent.width
            height: selectionText.implicitHeight + 14
            radius: theme.rounding
            color: theme.fill(theme.accent, 0.08)
            border.width: 1
            border.color: theme.fill(theme.accent, 0.35)
            Text {
                id: selectionText
                anchors.fill: parent
                anchors.margins: 7
                text: T.t("panel.layers.selectionSummary").arg(selectedIds.length)
                color: theme.foreground
                font.family: theme.fontFamily
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }
        }

        ListView {
            id: layerList
            objectName: "layerList"
            width: parent.width
            height: Math.min(contentHeight, 5 * dock.rowHeight)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: doc.layers
            spacing: 5
            currentIndex: dock.activeIndex()
            activeFocusOnTab: true
            Accessible.name: T.t("panel.layers")
            Accessible.description: T.t("panel.layers.toolWindowHint")

            function activateCurrent() { dock.openCurrent() }
            function step(delta) {
                var next = Math.max(0, Math.min(count - 1, currentIndex + delta))
                currentIndex = next
                positionViewAtIndex(next, ListView.Contain)
            }

            Keys.onUpPressed: function (event) {
                layerList.step(-1)
                event.accepted = true
            }
            Keys.onDownPressed: function (event) {
                layerList.step(1)
                event.accepted = true
            }
            Keys.onReturnPressed: function (event) {
                layerList.activateCurrent()
                event.accepted = true
            }
            Keys.onEnterPressed: function (event) {
                layerList.activateCurrent()
                event.accepted = true
            }
            Keys.onSpacePressed: function (event) {
                if (event.modifiers & Qt.ControlModifier)
                    dock.toggleStructural(doc.layers[layerList.currentIndex].id)
                else
                    layerList.activateCurrent()
                event.accepted = true
            }

            delegate: Rectangle {
                id: layerRow
                required property var modelData
                required property int index
                objectName: "layerRow_" + modelData.id
                property bool activePaintTarget: modelData.id === doc.activeLayerId
                property bool structurallySelected: dock.has(modelData.id)
                width: layerList.width
                height: dock.rowHeight
                radius: theme.rounding
                color: activePaintTarget ? theme.fill(theme.accent, 0.16)
                                         : (index === layerList.currentIndex
                                            ? theme.fill(theme.foreground, 0.08)
                                            : theme.fill(theme.foreground, 0.035))
                border.width: activePaintTarget ? 1 : 0
                border.color: theme.accent
                Accessible.name: activePaintTarget
                              ? T.t("accessibility.layers.activeRow").arg(modelData.name)
                              : modelData.name

                Rectangle {
                    width: activePaintTarget ? 4 : 0
                    height: parent.height
                    color: theme.accent
                    radius: theme.rounding
                }

                LayerAction {
                    id: structural
                    objectName: "structuralAction_" + modelData.id
                    x: 7
                    y: 8
                    width: 30
                    tooltip: T.t("accessibility.layers.select").arg(modelData.name)
                    glyph: activePaintTarget ? "[*]" : (dock.has(modelData.id) ? "[x]" : "[ ]")
                    checked: dock.has(modelData.id)
                    onClicked: dock.toggleStructural(modelData.id)
                }

                PixelGridItem {
                    x: 43
                    y: 6
                    width: 52
                    height: 52
                    model: doc
                    clip: doc.clip
                    frame: doc.frame
                    isolatedLayer: modelData.id
                    cell: Math.min(width / Math.max(1, doc.columns),
                                   height / Math.max(1, doc.rows))
                    checker: true
                    checkerDark: theme.checkerDark
                    checkerLight: theme.checkerLight
                }

                Column {
                    x: 104
                    y: 8
                    width: Math.max(60, layerRow.width - 170)
                    spacing: 3
                    Text {
                        text: activePaintTarget ? T.t("panel.layers.paintTarget") : modelData.name
                        color: activePaintTarget ? theme.accent : theme.foreground
                        font.family: theme.fontFamily
                        font.pixelSize: 11
                        font.bold: activePaintTarget
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Text {
                        text: modelData.name
                        color: theme.foreground
                        font.family: theme.fontFamily
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        width: parent.width
                        visible: activePaintTarget
                    }
                    Flow {
                        width: parent.width
                        spacing: 5
                        Label {
                            text: modelData.storage === "shared"
                                  ? T.t("panel.layers.shared") : T.t("panel.layers.animated")
                        }
                        Label { text: Math.round(modelData.opacity * 100 / 255) + "%" }
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 5
                    Text {
                        text: modelData.visible ? T.t("panel.layers.visible")
                                                 : T.t("panel.layers.hidden")
                        color: modelData.visible ? theme.foreground : theme.dim
                        font.pixelSize: 12
                    }
                    Text {
                        text: modelData.locked ? T.t("panel.layers.locked")
                                               : T.t("panel.layers.unlocked")
                        color: modelData.locked ? theme.accent : theme.dim
                        font.pixelSize: 12
                    }
                }

                HoverHandler { id: hover }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: dock.activate(modelData.id)
                }
            }
        }
    }
}
