#include "Ops.h"

#include <QStack>

#include <cmath>

namespace omapixel {
namespace ops {

void paint(Grid &grid, int x, int y, QChar slot)
{
    grid.set(x, y, slot);
}

void line(Grid &grid, QPoint from, QPoint to, QChar slot)
{
    if (grid.isEmpty())
        return;

    if (!grid.contains(from.x(), from.y()) || !grid.contains(to.x(), to.y())) {
        const long double x0 = from.x();
        const long double y0 = from.y();
        const long double dx = static_cast<long double>(to.x()) - x0;
        const long double dy = static_cast<long double>(to.y()) - y0;
        const long double right = grid.columns() - 1;
        const long double bottom = grid.rows() - 1;
        long double entering = 0;
        long double leaving = 1;

        const auto clip = [&](long double p, long double q) {
            if (p == 0)
                return q >= 0;
            const long double t = q / p;
            if (p < 0) {
                if (t > leaving)
                    return false;
                entering = qMax(entering, t);
            } else {
                if (t < entering)
                    return false;
                leaving = qMin(leaving, t);
            }
            return true;
        };

        if (!clip(-dx, x0) || !clip(dx, right - x0) || !clip(-dy, y0)
            || !clip(dy, bottom - y0))
            return;

        const auto coordinate = [](long double value, int maximum) {
            value = std::round(value);
            if (value <= 0)
                return 0;
            if (value >= maximum)
                return maximum;
            return static_cast<int>(value);
        };
        const auto pointAt = [&](long double t) {
            return QPoint(coordinate(x0 + t * dx, grid.columns() - 1),
                          coordinate(y0 + t * dy, grid.rows() - 1));
        };
        from = pointAt(entering);
        to = pointAt(leaving);
    }

    qint64 x0 = from.x(), y0 = from.y();
    const qint64 x1 = to.x(), y1 = to.y();
    const qint64 dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    const qint64 dy = y1 >= y0 ? -(y1 - y0) : -(y0 - y1);
    const qint64 sx = x0 < x1 ? 1 : -1;
    const qint64 sy = y0 < y1 ? 1 : -1;
    qint64 error = dx + dy;

    forever {
        grid.set(static_cast<int>(x0), static_cast<int>(y0), slot);
        if (x0 == x1 && y0 == y1)
            break;
        const qint64 doubled = error + error;
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
    const int clippedLeft = qMax(left, 0);
    const int clippedRight = qMin(right, grid.columns() - 1);
    const int clippedTop = qMax(top, 0);
    const int clippedBottom = qMin(bottom, grid.rows() - 1);

    if (clippedLeft > clippedRight || clippedTop > clippedBottom)
        return;

    for (int y = clippedTop; y <= clippedBottom; ++y) {
        for (int x = clippedLeft; x <= clippedRight; ++x) {
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
