import QtQuick

// A panel section that folds away.
//
// The right dock holds four groups of controls and a window is rarely tall
// enough for all of them at once. Scrolling past a group you are not using is
// worse than closing it: the thing you want should be reachable without hunting,
// and what you fold stays folded while you work.
//
// The header is the whole hit target, not just the triangle -- a 9px caret is a
// target people miss.
Item {
    id: section

    property string title: ""
    property bool open: true
    property string hint: ""          // a word on the right, e.g. the size
    default property alias content: body.data

    width: parent ? parent.width : 0
    implicitHeight: header.height + (open ? body.implicitHeight + 12 : 0)
    height: implicitHeight
    clip: true

    Behavior on height {
        NumberAnimation { duration: 110; easing.type: Easing.OutCubic }
    }

    Rectangle {
        id: header
        width: parent.width
        height: 30
        radius: theme.rounding
        color: hover.hovered ? theme.fill(theme.foreground, 0.06) : "transparent"
        Behavior on color { ColorAnimation { duration: 90 } }

        Text {
            id: caret
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            text: section.open ? T.t("section.open") : T.t("section.closed")
            color: theme.dim
            font.family: theme.fontFamily
            font.pixelSize: 9
        }

        Text {
            anchors.left: caret.right
            anchors.leftMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            text: section.title
            color: theme.foreground
            font.family: theme.fontFamily
            font.pixelSize: 12
            font.bold: true
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: section.hint
            color: theme.dim
            font.family: theme.fontFamily
            font.pixelSize: 11
            visible: text !== ""
        }

        HoverHandler { id: hover }
        TapHandler { onTapped: section.open = !section.open }

        activeFocusOnTab: true
        Keys.onSpacePressed: function (event) {
            section.open = !section.open
            event.accepted = true
        }
        Keys.onReturnPressed: function (event) {
            section.open = !section.open
            event.accepted = true
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: theme.rounding
            color: "transparent"
            border.width: 1
            border.color: theme.accent
            visible: header.activeFocus
        }
    }

    Column {
        id: body
        y: header.height + 6
        width: parent.width
        spacing: 8
        opacity: section.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 90 } }
    }
}
