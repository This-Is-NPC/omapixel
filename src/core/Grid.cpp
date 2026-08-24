#include "Grid.h"

#include <QSet>

namespace omapixel {

Grid::Grid(int columns, int rows, QChar fill)
    : m_columns(qMax(0, columns)), m_rows(qMax(0, rows))
{
    m_cells = QString(m_columns * m_rows, fill);
}

Grid Grid::fromRows(const QStringList &rows)
{
    int width = 0;
    for (const QString &row : rows)
        width = qMax(width, int(row.size()));

    Grid grid(width, rows.size());
    for (int y = 0; y < rows.size(); ++y) {
        const QString &row = rows.at(y);
        for (int x = 0; x < row.size(); ++x)
            grid.set(x, y, row.at(x));
    }
    return grid;
}

bool Grid::contains(int x, int y) const
{
    return x >= 0 && y >= 0 && x < m_columns && y < m_rows;
}

QChar Grid::at(int x, int y) const
{
    return contains(x, y) ? m_cells.at(y * m_columns + x) : Empty;
}

void Grid::set(int x, int y, QChar slot)
{
    if (contains(x, y))
        m_cells[y * m_columns + x] = slot;
}

QString Grid::row(int y) const
{
    return (y >= 0 && y < m_rows) ? m_cells.mid(y * m_columns, m_columns)
                                  : QString();
}

QStringList Grid::toRows() const
{
    QStringList rows;
    rows.reserve(m_rows);
    for (int y = 0; y < m_rows; ++y)
        rows.append(row(y));
    return rows;
}

QSet<QChar> Grid::slotsUsed() const
{
    QSet<QChar> used;
    for (QChar cell : m_cells) {
        if (cell != Empty)
            used.insert(cell);
    }
    return used;
}

qint64 Grid::drawnCount() const
{
    qint64 drawn = 0;
    for (QChar cell : m_cells) {
        if (cell != Empty)
            ++drawn;
    }
    return drawn;
}

bool Grid::operator==(const Grid &other) const
{
    return m_columns == other.m_columns && m_rows == other.m_rows
           && m_cells == other.m_cells;
}

} // namespace omapixel
