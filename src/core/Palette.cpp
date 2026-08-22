#include "Palette.h"

namespace omapixel {

Palette Palette::standard()
{
    Palette palette;
    // The ladder, darkest first, then the warm tones. The order is the order
    // the strip shows, so it is picked for finding a colour by eye rather than
    // for anything the code cares about.
    palette.set(u'I', QColor("#1A1B26"));   // darkest: outline
    palette.set(u'C', QColor("#2B3048"));   // shirt, one step above the outline
    palette.set(u'A', QColor("#3A3E55"));   // dark
    palette.set(u'K', QColor("#454C6E"));   // shadow of D
    palette.set(u'D', QColor("#565F89"));   // mid
    palette.set(u'F', QColor("#A9B1D6"));   // light
    palette.set(u'L', QColor("#E4E8F5"));   // lightest
    palette.set(u'E', QColor("#4A3227"));   // shadow of H
    palette.set(u'H', QColor("#6B4A3A"));   // brown
    palette.set(u'N', QColor("#8A6349"));   // highlight of H
    palette.set(u'T', QColor("#D3A995"));   // shadow of S
    palette.set(u'S', QColor("#F0CDBF"));   // warm skin
    palette.set(u'R', QColor("#F7768E"));   // pink
    palette.set(u'Y', QColor("#E0AF68"));   // ochre
    palette.set(u'G', QColor("#9ECE6A"));   // green
    palette.set(u'B', QColor("#7AA2F7"));   // blue
    palette.set(u'P', QColor("#BB9AF7"));   // purple
    return palette;
}

bool Palette::has(QChar letter) const
{
    for (const Slot &slot : m_entries) {
        if (slot.letter == letter)
            return true;
    }
    return false;
}

QColor Palette::colour(QChar letter) const
{
    for (const Slot &slot : m_entries) {
        if (slot.letter == letter)
            return slot.colour;
    }
    return QColor();
}

void Palette::set(QChar letter, const QColor &colour)
{
    for (Slot &slot : m_entries) {
        if (slot.letter == letter) {
            slot.colour = colour;
            return;
        }
    }
    m_entries.append({letter, colour});
}

bool Palette::remove(QChar letter)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).letter == letter) {
            m_entries.removeAt(i);
            return true;
        }
    }
    return false;
}

bool Palette::moveTo(QChar letter, int index)
{
    if (index < 0 || index >= m_entries.size())
        return false;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).letter == letter) {
            m_entries.move(i, index);
            return true;
        }
    }
    return false;
}

QList<QChar> Palette::letters() const
{
    QList<QChar> out;
    out.reserve(m_entries.size());
    for (const Slot &slot : m_entries)
        out.append(slot.letter);
    return out;
}

} // namespace omapixel
