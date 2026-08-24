import QtQuick
import QtQuick.Controls.Basic as C

// Small icon action used only where the row cannot afford another label. The
// tooltip is part of the control contract, not decoration: the glyph alone is
// deliberately not expected to be self-explanatory.
Rectangle {
    id: action

    property string glyph: ""
    property string tooltip: ""
    property bool checked: false
    property bool usable: true
    readonly property bool tooltipVisible: hover.hovered
    signal clicked

    width: 28
    height: 26
    radius: theme.rounding
    opacity: usable ? 1 : 0.35
    color: !usable ? "transparent"
         : checked ? theme.fill(theme.accent, 0.18)
                   : hover.hovered ? theme.fill(theme.foreground, 0.10)
                                   : theme.fill(theme.foreground, 0.04)
    border.width: 1
    border.color: checked ? theme.accent
                : hover.hovered ? theme.fill(theme.foreground, 0.32)
                                : theme.fill(theme.foreground, 0.18)

    activeFocusOnTab: true
    function updateTabFocus() {
        if (!usable && activeFocus) {
            var next = nextItemInFocusChain(true)
            if (next && next !== action)
                next.forceActiveFocus()
        }
        activeFocusOnTab = usable
    }
    onUsableChanged: updateTabFocus()
    Component.onCompleted: updateTabFocus()
    Accessible.name: tooltip
    Accessible.description: tooltip

    Keys.onSpacePressed: function (event) {
        if (usable)
            clicked()
        event.accepted = true
    }
    Keys.onReturnPressed: function (event) {
        if (usable)
            clicked()
        event.accepted = true
    }
    Keys.onEnterPressed: function (event) {
        if (usable)
            clicked()
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: theme.rounding + 2
        color: "transparent"
        border.width: 1
        border.color: theme.accent
        visible: action.activeFocus
    }

    Text {
        anchors.centerIn: parent
        text: action.glyph
        color: action.checked ? theme.foreground : theme.dim
        font.family: theme.fontFamily
        font.pixelSize: 12
        font.bold: action.checked
    }

    HoverHandler { id: hover; cursorShape: action.usable ? Qt.PointingHandCursor
                                                             : Qt.ArrowCursor }
    TapHandler {
        enabled: action.usable
        onTapped: action.clicked()
    }

    C.ToolTip.visible: hover.hovered && action.tooltip !== ""
    C.ToolTip.delay: 350
    C.ToolTip.text: action.tooltip
}
