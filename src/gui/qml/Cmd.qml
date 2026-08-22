import QtQuick
// Imported under a prefix on purpose: QtQuick.Controls defines Label and
// ToolButton too, and an imported module wins over a component sitting in the
// same directory.
import QtQuick.Controls.Basic as C

// One command in a menu.
//
// This exists because a Menu's `delegate` is only used for items the menu
// creates itself, from a model. Items written out by hand -- which is how every
// menu in this window is written, so that each one can name its action -- are
// not delegated at all: they arrive in the Basic style's font, with the Basic
// style's tick, and with no shortcut column, however carefully the delegate
// beside them was written. The styling has to live in the item.
//
// Command on the left, its key on the right, greyed when it cannot be used, a
// tick in the gutter when it is on. Showing the key beside the command is how a
// menu teaches itself out of a job: you read it there twice and then stop
// opening the menu.
C.MenuItem {
    id: entry

    implicitWidth: 268
    implicitHeight: 26
    padding: 0

    // The Basic style draws its own tick and arrow; ours is in the gutter.
    indicator: Item {}
    arrow: Item {}

    contentItem: Item {
        // The gutter holds the tick, so a checkable command and a plain one
        // start their text at the same place. A ragged left edge is what makes
        // a list of commands hard to skim.
        Text {
            x: 9
            anchors.verticalCenter: parent.verticalCenter
            text: entry.checked ? "✓" : ""
            color: theme.accent
            font.family: theme.fontFamily
            font.pixelSize: 11
        }
        Text {
            x: 26
            anchors.verticalCenter: parent.verticalCenter
            text: entry.text
            color: entry.enabled ? theme.foreground : theme.fill(theme.foreground, 0.35)
            font.family: theme.fontFamily
            font.pixelSize: 12
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            // An action without a shortcut has `shortcut` undefined, not empty,
            // and assigning undefined to a string is a warning per repaint.
            text: entry.action && entry.action.shortcut ? entry.action.shortcut : ""
            color: entry.enabled ? theme.dim : theme.fill(theme.foreground, 0.25)
            font.family: theme.fontFamily
            font.pixelSize: 11
        }
    }

    background: Rectangle {
        radius: theme.rounding
        color: entry.highlighted && entry.enabled ? theme.fill(theme.accent, 0.18)
                                                  : "transparent"
    }
}
