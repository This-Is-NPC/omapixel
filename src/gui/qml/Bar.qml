import QtQuick
// Imported under a prefix on purpose: QtQuick.Controls defines Label and
// ToolButton too, and an imported module wins over a component sitting in the
// same directory. Without the prefix, this file's own Label silently becomes
// somebody else's.
import QtQuick.Controls.Basic as C

// The menu bar.
//
// Every command the studio has, named, grouped, and in one place. The studio
// used to put them all on one row of chips across the top: fifteen controls
// competing for the same strip, the least used beside the most used, and no
// room to add a sixteenth. A menu costs one click for the things you do rarely
// and nothing at all for the things you do often, because those have keys.
//
// An open menu is marked by a rule under its title rather than a filled block.
// A filled block is a button; a rule is a tab, and a menu bar is a row of tabs
// onto one panel. It also leaves the title readable, which a wash at the alpha
// the rest of the window uses does not reliably do at 12px.
//
// The Basic style is used deliberately: it is the one Controls style that is
// fully restyleable, so the menus wear the omarchy theme like everything else
// rather than arriving in somebody else's colours.
C.MenuBar {
    id: bar

    function openCurrentMenu() {
        var menu = menuAt(currentIndex >= 0 ? currentIndex : 0)
        if (menu) {
            menu.open()
            Qt.callLater(function () {
                menu.currentIndex = 0
                var item = menu.itemAt(0)
                if (item)
                    item.forceActiveFocus()
            })
        }
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Space
                || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            bar.openCurrentMenu()
            event.accepted = true
        }
    }

    // Sized to its own contents, so whatever sits beside it on the row -- the
    // document name, the save state -- can have the rest.
    width: implicitWidth

    background: Item {}

    delegate: C.MenuBarItem {
        id: barItem

        readonly property bool showing: barItem.menu && barItem.menu.opened

        implicitHeight: 30
        padding: 11

        contentItem: Text {
            // The ampersand marks the Alt key and is not meant to be read.
            // The style's own label strips it; a plain Text does not, so the
            // menu bar said "&File".
            text: barItem.text.replace("&", "")
            // Dimmed at rest, but not so far that reading the bar takes
            // effort: these are the names of everything the program can do.
            color: barItem.showing || barItem.hovered
                   ? theme.foreground : theme.fill(theme.foreground, 0.72)
            font.family: theme.fontFamily
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            Behavior on color { ColorAnimation { duration: 90 } }
        }

        background: Item {
            // `highlighted` is true for whichever item the bar considers
            // current, which is the first one before anybody has touched a
            // menu -- so File sat lit from the moment the window opened, as if
            // it were already open. What matters is whether the menu IS open.
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: barItem.showing ? parent.width - 12 : 0
                height: 2
                radius: 1
                color: theme.accent

                Behavior on width {
                    NumberAnimation { duration: 110; easing.type: Easing.OutCubic }
                }
            }
        }
    }
}
