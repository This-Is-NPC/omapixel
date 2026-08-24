import QtQuick

// A searchable view over commands owned by Main.qml. This component knows how
// to find and choose a command, but never how to perform one; menus, buttons,
// shortcuts and this list all return to the same command dispatcher.
Sheet {
    id: palette
    objectName: "commandPalette"
    title: T.t("command.palette.title")

    property var commands: []
    property var matches: []
    property var hostWindow: null
    property var previousFocusItem: null
    property string pendingCommand: ""
    property var pendingArgs: ({})
    readonly property string query: search.input.text
    readonly property int resultCount: matches.length

    signal commandRequested(string commandId, var args)

    Shortcut {
        sequence: "Esc"
        enabled: palette.opened
        context: Qt.ApplicationShortcut
        onActivated: palette.close()
    }

    function rank(command, needle) {
        if (needle === "")
            return 3
        var label = String(command.label).toLowerCase()
        var group = String(command.group).toLowerCase()
        var terms = label + " " + group + " " + String(command.keywords || "").toLowerCase()
        if (label.indexOf(needle) === 0)
            return 0
        if ((" " + terms).indexOf(" " + needle) >= 0)
            return 1
        return terms.indexOf(needle) >= 0 ? 2 : -1
    }

    function refine(text) {
        var needle = text.trim().toLowerCase()
        var ranked = []
        for (var i = 0; i < commands.length; ++i) {
            var score = rank(commands[i], needle)
            if (score >= 0)
                ranked.push({ command: commands[i], score: score, order: i })
        }
        ranked.sort(function (a, b) {
            return a.score === b.score ? a.order - b.order : a.score - b.score
        })
        matches = ranked.map(function (entry) { return entry.command })
        results.currentIndex = matches.length > 0 ? 0 : -1
    }

    function show() {
        previousFocusItem = hostWindow ? hostWindow.activeFocusItem : null
        pendingCommand = ""
        pendingArgs = ({})
        search.input.text = ""
        refine("")
        open()
        search.focusEntry()
    }

    function step(delta) {
        if (matches.length === 0)
            return
        results.currentIndex = Math.max(
            0, Math.min(matches.length - 1, results.currentIndex + delta))
        results.positionViewAtIndex(results.currentIndex, ListView.Contain)
    }

    function choose(index) {
        if (index < 0 || index >= matches.length || matches[index].enabled === false)
            return
        pendingCommand = matches[index].id
        pendingArgs = matches[index].args || ({})
        close()
    }

    onClosed: {
        var command = pendingCommand
        var args = pendingArgs
        pendingCommand = ""
        pendingArgs = ({})
        if (previousFocusItem)
            previousFocusItem.forceActiveFocus()
        if (command !== "")
            Qt.callLater(function () { palette.commandRequested(command, args) })
    }

    body: [
        Field {
            id: search
            objectName: "commandPaletteSearch"
            label: T.t("command.palette.search")
            boxWidth: 520
            onEdited: function (text) { palette.refine(text) }
            onStepped: function (delta) { palette.step(delta) }
            onConfirmed: palette.choose(results.currentIndex)
            onEscaped: palette.close()
        },

        Rectangle {
            width: 520
            height: 330
            radius: theme.rounding
            color: theme.sunken
            border.width: 1
            border.color: theme.fill(theme.foreground, 0.18)
            clip: true

            ListView {
                id: results
                objectName: "commandPaletteResults"
                anchors.fill: parent
                anchors.margins: 4
                model: palette.matches
                spacing: 2
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                highlightRangeMode: ListView.ApplyRange
                preferredHighlightBegin: 30
                preferredHighlightEnd: height - 30

                delegate: Rectangle {
                    id: result
                    required property var modelData
                    required property int index
                    width: results.width
                    height: 42
                    radius: theme.rounding
                    color: index === results.currentIndex
                           ? theme.fill(theme.accent, 0.18)
                           : (resultHover.hovered
                              ? theme.fill(theme.foreground, 0.07) : "transparent")
                    opacity: modelData.enabled === false ? 0.42 : 1

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: result.modelData.checkable && result.modelData.checked ? "✓" : ""
                        color: theme.accent
                        font.family: theme.fontFamily
                        font.pixelSize: 11
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 28
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 158
                        spacing: 1
                        Text {
                            width: parent.width
                            text: result.modelData.label
                            color: theme.foreground
                            font.family: theme.fontFamily
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: result.modelData.group
                            color: theme.dim
                            font.family: theme.fontFamily
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 110
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        text: result.modelData.enabled === false
                              ? T.t("command.palette.unavailable")
                              : String(result.modelData.shortcut || "")
                        color: theme.dim
                        font.family: theme.fontFamily
                        font.pixelSize: 10
                    }

                    HoverHandler { id: resultHover }
                    // keyboard-equivalent: search owns Up/Down and Enter calls choose().
                    TapHandler {
                        onTapped: {
                            results.currentIndex = result.index
                            palette.choose(result.index)
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: palette.matches.length === 0
                    text: T.t("command.palette.empty")
                }
            }
        },

        Label {
            width: 520
            text: T.t("command.palette.help")
            color: theme.dim
        }
    ]
}
