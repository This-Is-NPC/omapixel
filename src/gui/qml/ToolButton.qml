import QtQuick

// One tool, in the strip down the left edge.
//
// Tools live in a fixed column of their own rather than among the view and file
// controls. Which tool is active is the single piece of state that changes what
// the mouse does, so it gets a place that never moves and never reflows -- the
// thing every drawing program has had in the same corner for thirty years.
Rectangle {
    id: button

    property string glyph: ""
    property string key: ""
    property string caption: ""
    property bool on: false
    signal clicked

    activeFocusOnTab: true
    Keys.onSpacePressed: function (event) { button.clicked(); event.accepted = true }
    Keys.onReturnPressed: function (event) { button.clicked(); event.accepted = true }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: theme.rounding + 2
        color: "transparent"
        border.width: 1
        border.color: theme.accent
        visible: button.activeFocus
    }

    width: 34
    height: 34
    radius: theme.rounding
    color: button.on ? theme.fill(theme.accent, 0.18)
         : hover.hovered ? theme.fill(theme.foreground, 0.08)
                         : "transparent"
    border.width: 1
    border.color: button.on ? theme.accent
                : hover.hovered ? theme.fill(theme.foreground, 0.25)
                                : "transparent"
    Behavior on color { ColorAnimation { duration: 90 } }

    Text {
        anchors.centerIn: parent
        text: button.glyph
        color: button.on ? theme.foreground : theme.dim
        font.family: theme.fontFamily
        font.pixelSize: 15
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: button.clicked() }

    // The name and its key, on hover. A strip of glyphs is fast once you know
    // it and unusable until you do.
    Rectangle {
        visible: hover.hovered && button.caption !== ""
        x: parent.width + 8
        anchors.verticalCenter: parent.verticalCenter
        width: tip.implicitWidth + 16
        height: 24
        radius: theme.rounding
        color: theme.panel
        border.width: 1
        border.color: theme.fill(theme.foreground, 0.25)
        z: 50

        Text {
            id: tip
            anchors.centerIn: parent
            text: button.caption + (button.key === "" ? "" : "   " + button.key)
            color: theme.foreground
            font.family: theme.fontFamily
            font.pixelSize: 11
        }
    }
}
