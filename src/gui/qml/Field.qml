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
    /// Up and down, for a field that drives a list beside it. -1 is up.
    signal stepped(int delta)
    /// Escape. Where it goes is the caller's business: out of a panel is not
    /// the same place as back to the drawing, and a field cannot know which
    /// it is in.
    signal escaped()
    /// Enter, with whatever was held down with it. `committed` cannot carry
    /// that: it comes from TextInput's own onAccepted, which reports the text
    /// and nothing else, and shift-Enter has to mean something different from
    /// Enter in at least one place.
    signal confirmed(string text, int modifiers)

    /// Takes the keyboard, and selects what is there so typing replaces it.
    function focusEntry() {
        entry.forceActiveFocus()
        entry.selectAll()
    }

    property alias input: entry

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
            // Escape leaves the field. Without it, a text box is a trap: the
            // arrows belong to it for as long as it holds focus, and nothing
            // says so.
            Keys.onEscapePressed: function (event) {
                field.escaped()
                event.accepted = true
            }
            // Not accepted, so TextInput still sees it and `committed` fires
            // for the callers that only want the text.
            Keys.onReturnPressed: function (event) {
                field.confirmed(text, event.modifiers)
                event.accepted = false
            }
            Keys.onEnterPressed: function (event) {
                field.confirmed(text, event.modifiers)
                event.accepted = false
            }
            Keys.onUpPressed: function (event) {
                field.stepped(-1)
                event.accepted = true
            }
            Keys.onDownPressed: function (event) {
                field.stepped(1)
                event.accepted = true
            }
        }
    }
}
