#pragma once

#include <QChar>
#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

namespace omapixel {

/// Slot letter -> colour, in the order the author put them in.
///
/// Ordered, and that is why it is a list and not a QMap. The order is the order
/// the swatch strip draws in, so it is content and not an implementation
/// detail: a palette that reshuffles itself when saved is a palette whose
/// colours move under the cursor.
class Palette
{
public:
    struct Slot {
        QChar letter;
        QColor colour;
    };

    /// The set the studio starts from: a value ladder plus the warm tones a
    /// human figure needs. Colours separate by value alone until you add warm
    /// ones, and a brown beard against a black shirt separates by HUE.
    static Palette standard();

    int size() const { return m_entries.size(); }
    bool isEmpty() const { return m_entries.isEmpty(); }
    /// Named `entries` and not `slots`: `slots` is a Qt keyword macro, and a
    /// member by that name does not compile.
    const QList<Slot> &entries() const { return m_entries; }

    bool has(QChar letter) const;
    QColor colour(QChar letter) const;

    /// Adds at the end, or recolours in place if the letter is already there.
    void set(QChar letter, const QColor &colour);
    bool remove(QChar letter);

    /// Moves a slot to a new index, so the strip can be reordered.
    bool moveTo(QChar letter, int index);

    QList<QChar> letters() const;

private:
    /// Rebuilds the letter index after the list's shape changes.
    void reindex();

    QList<Slot> m_entries;
    /// letter -> position in m_entries.
    ///
    /// The list is the truth, because its order is content. This is only a way
    /// to find a letter without walking it: `colour()` is called once per pixel
    /// by the renderer, so a linear scan made drawing cost the size of the
    /// palette -- a document with three hundred slots rendered a hundred times
    /// slower than one with three, for no reason a person could see.
    QHash<QChar, int> m_index;
};

} // namespace omapixel
