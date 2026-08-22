import QtQuick
// Imported under a prefix on purpose: QtQuick.Controls defines Label and
// ToolButton too, and an imported module wins over a component sitting in the
// same directory. Without the prefix, this file's own Label silently becomes
// somebody else's.
import QtQuick.Controls.Basic as C

// One menu in the bar, wearing the theme.
//
// The panel and its geometry only: the items inside are `Cmd`, and the
// separators are `Rule`. A Menu's `delegate` would be the obvious place for the
// item styling and it does not work here -- it is used only for items the menu
// builds from a model, never for ones written out by hand.
C.Menu {
    id: menu

    // Drawn INSIDE the window, not as a surface of its own.
    //
    // By default a menu opens as a separate window, and a compositor decorates
    // its own windows: under Hyprland the panel arrived wrapped in the
    // compositor's border, rounding and shadow, none of which match the ones
    // this window draws. The panel was themed correctly and still looked like
    // it came from somewhere else, because half of what you saw around it was
    // not ours to style.
    popupType: C.Popup.Item

    implicitWidth: 268
    padding: 6

    background: Rectangle {
        color: theme.panel
        radius: theme.rounding
        border.width: 1
        border.color: theme.fill(theme.foreground, 0.25)
    }

}
