import QtQuick
// Imported under a prefix on purpose: QtQuick.Controls defines Label and
// ToolButton too, and an imported module wins over a component sitting in
// the same directory. Without the prefix, this file's own Label silently
// becomes somebody else's.
import QtQuick.Controls.Basic as C

// A small modal panel: new document, export options.
//
// Modal on purpose. Both of these end in a decision that changes the whole
// document, and a panel that can be left half-filled while you draw is a panel
// whose state nobody can reason about later.
C.Popup {
    id: sheet

    property string title: ""
    property var firstFocusItem: null

    // Centred on its default parent, the window's content item, rather than on
    // Controls' Overlay: this window is a plain Window, and depending on the
    // attached Overlay here is depending on something that may not be there.
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 3)
    modal: true
    // Inside the window, for the same reason the menus are: a dialog framed by
    // the compositor's decoration instead of the window's own does not look
    // like part of the program.
    popupType: C.Popup.Item
    focus: true
    padding: 16
    closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside
    onOpened: if (firstFocusItem) Qt.callLater(firstFocusItem.forceActiveFocus)

    background: Rectangle {
        color: theme.panel
        radius: theme.rounding
        border.width: 1
        border.color: theme.fill(theme.foreground, 0.25)
    }

    // The title sits above whatever the caller put inside.
    property alias body: column.data
    contentItem: Column {
        id: column
        spacing: 12

        Text {
            text: sheet.title
            color: theme.foreground
            font.family: theme.fontFamily
            font.pixelSize: 13
            font.bold: true
        }
    }
}
