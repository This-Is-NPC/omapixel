#pragma once

#include "Grid.h"

#include <QPoint>

namespace omapixel {

/// The drawing operations, as free functions over a Grid.
///
/// Free functions and not methods on Grid, because they are a different kind of
/// thing: Grid is the storage and these are the tools. Keeping them apart means
/// a new tool never touches the type every other piece of the program depends
/// on.
///
/// Every one of them is total: out-of-bounds coordinates clip rather than
/// assert, and an operation that would change nothing changes nothing. A CLI
/// hands user input straight to these, and user input is wrong all the time.
namespace ops {

void paint(Grid &grid, int x, int y, QChar slot);

/// Bresenham. A line tool that interpolates in floating point leaves gaps on
/// steep slopes, which on a 24-row sprite is a hole you can see.
void line(Grid &grid, QPoint from, QPoint to, QChar slot);

void rect(Grid &grid, QPoint from, QPoint to, QChar slot, bool filled);

/// Flood fill over the contiguous run of one slot, with an explicit stack. A
/// 128x128 grid of a single colour is sixteen thousand pixels, and recursion
/// that deep is a stack overflow rather than a bug report.
void fill(Grid &grid, int x, int y, QChar slot);

void clear(Grid &grid, QChar slot = Grid::Empty);

/// Moves the drawing. Whatever leaves the frame is gone -- there is no wrap,
/// because a sprite that wraps is a tiling pattern and that is a different
/// tool.
void shift(Grid &grid, int dx, int dy);

void flipHorizontal(Grid &grid);
void flipVertical(Grid &grid);

/// Replaces one slot with another everywhere. It is how a recolour that the
/// palette cannot express -- moving pixels between slots -- gets done.
int swapSlot(Grid &grid, QChar from, QChar to);

/// Every pixel that differs, as (point, mine, theirs).
struct Difference {
    QPoint at;
    QChar before;
    QChar after;
};
QList<Difference> diff(const Grid &before, const Grid &after);

} // namespace ops
} // namespace omapixel
