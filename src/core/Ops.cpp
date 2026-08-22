#include "Ops.h"

#include <QStack>

namespace omapixel {
namespace ops {

void paint(Grid &grid, int x, int y, QChar slot)
{
    grid.set(x, y, slot);
}

void line(Grid &grid, QPoint from, QPoint to, QChar slot)
{
    int x0 = from.x(), y0 = from.y();
    const int x1 = to.x(), y1 = to.y();
    const int dx = qAbs(x1 - x0);
    const int dy = -qAbs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    forever {
        grid.set(x0, y0, slot);
        if (x0 == x1 && y0 == y1)
            break;
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void rect(Grid &grid, QPoint from, QPoint to, QChar slot, bool filled)
{
    const int left = qMin(from.x(), to.x());
    const int right = qMax(from.x(), to.x());
    const int top = qMin(from.y(), to.y());
    const int bottom = qMax(from.y(), to.y());

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const bool edge = x == left || x == right || y == top || y == bottom;
            if (filled || edge)
                grid.set(x, y, slot);
        }
    }
}

void fill(Grid &grid, int x, int y, QChar slot)
{
    if (!grid.contains(x, y))
        return;
    const QChar from = grid.at(x, y);
    if (from == slot)
        return;

    QStack<QPoint> pending;
    pending.push(QPoint(x, y));
    while (!pending.isEmpty()) {
        const QPoint at = pending.pop();
        if (!grid.contains(at.x(), at.y()) || grid.at(at.x(), at.y()) != from)
            continue;
        grid.set(at.x(), at.y(), slot);
        pending.push(QPoint(at.x() + 1, at.y()));
        pending.push(QPoint(at.x() - 1, at.y()));
        pending.push(QPoint(at.x(), at.y() + 1));
        pending.push(QPoint(at.x(), at.y() - 1));
    }
}

void clear(Grid &grid, QChar slot)
{
    for (int y = 0; y < grid.rows(); ++y) {
        for (int x = 0; x < grid.columns(); ++x)
            grid.set(x, y, slot);
    }
}

void shift(Grid &grid, int dx, int dy)
{
    Grid moved(grid.columns(), grid.rows());
    for (int y = 0; y < grid.rows(); ++y) {
        for (int x = 0; x < grid.columns(); ++x)
            moved.set(x, y, grid.at(x - dx, y - dy));
    }
    grid = moved;
}

void flipHorizontal(Grid &grid)
{
    Grid flipped(grid.columns(), grid.rows());
    for (int y = 0; y < grid.rows(); ++y) {
        for (int x = 0; x < grid.columns(); ++x)
            flipped.set(x, y, grid.at(grid.columns() - 1 - x, y));
    }
    grid = flipped;
}

void flipVertical(Grid &grid)
{
    Grid flipped(grid.columns(), grid.rows());
    for (int y = 0; y < grid.rows(); ++y) {
        for (int x = 0; x < grid.columns(); ++x)
            flipped.set(x, y, grid.at(x, grid.rows() - 1 - y));
    }
    grid = flipped;
}

int swapSlot(Grid &grid, QChar from, QChar to)
{
    int changed = 0;
    for (int y = 0; y < grid.rows(); ++y) {
        for (int x = 0; x < grid.columns(); ++x) {
            if (grid.at(x, y) == from) {
                grid.set(x, y, to);
                ++changed;
            }
        }
    }
    return changed;
}

QList<Difference> diff(const Grid &before, const Grid &after)
{
    QList<Difference> out;
    const int columns = qMax(before.columns(), after.columns());
    const int rows = qMax(before.rows(), after.rows());
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const QChar mine = before.at(x, y);
            const QChar theirs = after.at(x, y);
            if (mine != theirs)
                out.append({QPoint(x, y), mine, theirs});
        }
    }
    return out;
}

} // namespace ops
} // namespace omapixel
