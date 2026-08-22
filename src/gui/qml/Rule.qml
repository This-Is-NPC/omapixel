import QtQuick
import QtQuick.Controls.Basic as C

// A divider inside a menu.
//
// It exists because MenuSeparator has no delegate to override: unlike the items
// around it, it arrives wearing the Basic style's own line and its own padding,
// and one unstyled hairline is enough to make a whole panel look borrowed.
C.MenuSeparator {
    padding: 0
    topPadding: 5
    bottomPadding: 5

    contentItem: Rectangle {
        implicitWidth: 120
        implicitHeight: 1
        color: theme.fill(theme.foreground, 0.18)
    }
}
