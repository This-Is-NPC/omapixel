import QtQuick

// A control, in omarchy's shape: a translucent wash of a role over the surface
// with a hairline border, rather than a second opaque colour. It is what makes
// a button belong to the theme instead of sitting on top of it -- the fills are
// the theme's own alphas, so a light theme washes light and a dark one dark.
Rectangle {
    id: chip

    property string label: ""
    property bool on: false
    property bool usable: true
    property color role: theme.accent
    signal clicked

    implicitWidth: caption.implicitWidth + 20
    implicitHeight: 24
    width: implicitWidth
    height: implicitHeight
    // Hyprland's corner rounding, used as-is, the way the omarchy shell uses it.
    radius: theme.rounding

    color: !chip.usable ? "transparent"
         : chip.on      ? theme.fill(chip.role, 0.18)
         : hover.hovered ? theme.fill(theme.foreground, 0.08)
                         : theme.fill(theme.foreground, 0.04)

    border.width: 1
    border.color: chip.on ? chip.role
                : hover.hovered ? theme.fill(theme.foreground, 0.25)
                                : theme.fill(theme.foreground, 0.18)
    opacity: chip.usable ? 1 : 0.35

    Behavior on color { ColorAnimation { duration: 90 } }

    Text {
        id: caption
        anchors.centerIn: parent
        text: chip.label
        // On an accent wash rather than the accent itself, so the foreground
        // stays readable at every theme's accent lightness.
        color: chip.on ? theme.foreground : theme.foreground
        opacity: chip.on ? 1 : 0.85
        font.family: theme.fontFamily
        font.pixelSize: 11
    }

    HoverHandler { id: hover; cursorShape: chip.usable ? Qt.PointingHandCursor
                                                       : Qt.ArrowCursor }

    TapHandler {
        enabled: chip.usable
        onTapped: chip.clicked()
    }
}
