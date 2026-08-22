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

    // Reachable by Tab, and worked by Space or Enter. Almost every control in
    // this window is a Chip, so the keyboard reaches almost all of them from
    // this one place -- and a control that can only be clicked is a control
    // somebody cannot use.
    activeFocusOnTab: usable
    Keys.onSpacePressed: function (event) {
        if (chip.usable) { chip.clicked(); event.accepted = true }
    }
    Keys.onReturnPressed: function (event) {
        if (chip.usable) { chip.clicked(); event.accepted = true }
    }
    Keys.onEnterPressed: function (event) {
        if (chip.usable) { chip.clicked(); event.accepted = true }
    }

    // The focus ring is drawn outside the control rather than as its border,
    // which is already carrying the on/off state. Two meanings on one line is
    // how you end up unable to tell them apart.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: theme.rounding + 2
        color: "transparent"
        border.width: 1
        border.color: theme.accent
        visible: chip.activeFocus
    }

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
