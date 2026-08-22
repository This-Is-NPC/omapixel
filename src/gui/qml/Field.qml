import QtQuick

// A labelled text box. It only tells the outside world while the field has
// focus: the binding that fills the text also fires `onTextChanged`, and
// without that guard every default value becomes an edit nobody made.
Column {
    id: field

    property string label: ""
    property string value: ""
    property int boxWidth: 200
    signal edited(string text)
    signal committed(string text)

    spacing: 5

    Label { text: field.label; visible: field.label !== "" }

    Rectangle {
        width: field.boxWidth
        height: 26
        radius: theme.rounding
        color: theme.sunken
        border.width: 1
        border.color: entry.activeFocus ? theme.accent
                                        : theme.fill(theme.foreground, 0.18)

        Behavior on border.color { ColorAnimation { duration: 90 } }

        TextInput {
            id: entry
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: TextInput.AlignVCenter
            clip: true
            text: field.value
            color: theme.foreground
            selectionColor: theme.fill(theme.accent, 0.35)
            selectedTextColor: theme.foreground
            font.family: theme.fontFamily
            font.pixelSize: 12
            selectByMouse: true
            onTextChanged: if (activeFocus) field.edited(text)
            onAccepted: field.committed(text)
        }
    }
}
