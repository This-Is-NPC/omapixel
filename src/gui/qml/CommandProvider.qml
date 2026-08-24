import QtQuick

QtObject {
    property list<QtObject> actions

    function dynamicEntries() { return [] }
    function invokeDynamic(commandId, args) { return false }
}
