import QtQuick.Controls.Basic as C

// One semantic Studio operation. Every surface points back to this Action, so
// its label, shortcut, state and implementation cannot drift apart.
C.Action {
    required property string commandId
    property string group: ""
    property string keywords: ""
    property string shownShortcut: shortcut ? String(shortcut) : ""
}
