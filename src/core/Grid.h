#pragma once

#include <QString>
#include <QStringList>
#include <QSet>

namespace omapixel {

/// One frame: a rectangle of palette slots, one character each.
///
/// A pixel here is a LETTER, not a colour. That is the only choice the format
/// makes, and it is the reason everything else is simple -- recolouring a whole
/// character is editing one line of the colour table, not repainting a thousand
/// frames. `Grid` therefore knows nothing about colour; it is the geometry and
/// the letters, and `Palette` is the other half.
///
/// A value type on purpose. Frames are copied constantly -- undo, onion skin,
/// duplication -- and a 128x128 grid is 16 KB, which is nothing next to the
/// aliasing bugs that shared mutable frames produce.
class Grid
{
public:
    /// The slot that means "nothing here". Never appears in a palette.
    static constexpr QChar Empty = u'.';

    Grid() = default;
    Grid(int columns, int rows, QChar fill = Empty);

    /// Builds from rows of text. Rows shorter than the widest are padded with
    /// `Empty` rather than rejected: a hand-written file with a short line has
    /// to open so it can be fixed.
    static Grid fromRows(const QStringList &rows);

    int columns() const { return m_columns; }
    int rows() const { return m_rows; }
    bool isEmpty() const { return m_columns == 0 || m_rows == 0; }

    /// Out-of-bounds reads return `Empty` instead of asserting. Every drawing
    /// tool works near an edge, and making each caller bounds-check is how one
    /// of them eventually forgets.
    QChar at(int x, int y) const;
    void set(int x, int y, QChar slot);

    /// Contiguous row-major storage for bulk readers such as the renderer.
    /// Mutations still go through set(), so callers cannot bypass bounds.
    const QChar *constData() const { return m_cells.constData(); }

    bool contains(int x, int y) const;

    QString row(int y) const;
    QStringList toRows() const;

    /// Every distinct slot in use, `Empty` excluded.
    QSet<QChar> slotsUsed() const;

    /// How many pixels are not `Empty`.
    qint64 drawnCount() const;

    bool operator==(const Grid &other) const;
    bool operator!=(const Grid &other) const { return !(*this == other); }

private:
    int m_columns = 0;
    int m_rows = 0;
    /// Row-major, m_columns * m_rows entries.
    QString m_cells;
};

} // namespace omapixel
