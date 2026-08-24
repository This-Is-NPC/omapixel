import QtQuick

QtObject {
    id: registry
    objectName: "commandRegistry"

    property list<QtObject> providers
    property var actionIndex: ({})
    property int revision: 0

    function rebuildIndex() {
        var next = {}
        for (var p = 0; p < providers.length; ++p) {
            var actions = providers[p].actions
            for (var a = 0; a < actions.length; ++a) {
                var command = actions[a]
                if (next[command.commandId] !== undefined) {
                    console.error("duplicate command id: " + command.commandId)
                    continue
                }
                next[command.commandId] = command
            }
        }
        actionIndex = next
        revision += 1
    }

    function action(commandId) {
        var currentRevision = revision
        return actionIndex[commandId] || null
    }

    function invoke(commandId, args) {
        var command = action(commandId)
        if (command) {
            if (!command.enabled)
                return false
            command.trigger()
            return true
        }
        for (var p = 0; p < providers.length; ++p)
            if (providers[p].invokeDynamic(commandId, args || {}))
                return true
        return false
    }

    function descriptor(command, args) {
        return {
            id: command.commandId,
            args: args || {},
            label: command.text,
            group: command.group,
            keywords: command.keywords,
            shortcut: command.shownShortcut,
            enabled: command.enabled,
            checkable: command.checkable,
            checked: command.checked
        }
    }

    function snapshot() {
        var entries = []
        for (var p = 0; p < providers.length; ++p) {
            var provider = providers[p]
            for (var a = 0; a < provider.actions.length; ++a)
                entries.push(descriptor(provider.actions[a], {}))
            var dynamic = provider.dynamicEntries()
            for (var d = 0; d < dynamic.length; ++d)
                entries.push(dynamic[d])
        }
        return entries
    }

    Component.onCompleted: rebuildIndex()
}
