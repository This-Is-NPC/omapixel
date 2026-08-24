#include "Palette.h"

#include "TextSafety.h"

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
    return m_index.contains(letter);
}

bool Palette::validSlot(QChar letter, QString *error)
{
    const ushort code = letter.unicode();
    const bool valid = !letter.isNull() && letter != QLatin1Char('.')
        && letter != QLatin1Char('"') && letter != QLatin1Char('\\')
        && !text::isUnsafe(code);
    if (!valid && error)
        *error = QStringLiteral("invalid palette slot");
    return valid;
}

QColor Palette::colour(QChar letter) const
{
    const auto at = m_index.constFind(letter);
    return at == m_index.constEnd() ? QColor() : m_entries.at(at.value()).colour;
}

void Palette::reindex()
{
    m_index.clear();
    m_index.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i)
        m_index.insert(m_entries.at(i).letter, i);
}

bool Palette::set(QChar letter, const QColor &colour)
{
    if (!validSlot(letter) || !colour.isValid())
        return false;
    const auto at = m_index.constFind(letter);
    if (at != m_index.constEnd()) {
        m_entries[at.value()].colour = colour;   // the order did not move
        return true;
    }
    if (m_entries.size() >= maxSlots)
        return false;
    m_entries.append({letter, colour});
    m_index.insert(letter, m_entries.size() - 1);
    return true;
}

bool Palette::remove(QChar letter)
{
    const auto at = m_index.constFind(letter);
    if (at == m_index.constEnd())
        return false;
    m_entries.removeAt(at.value());
    reindex();   // everything after it moved down
    return true;
}

bool Palette::moveTo(QChar letter, int index)
{
    if (index < 0 || index >= m_entries.size())
        return false;
    const auto at = m_index.constFind(letter);
    if (at == m_index.constEnd())
        return false;
    m_entries.move(at.value(), index);
    reindex();   // everything between the two positions shifted
    return true;
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
