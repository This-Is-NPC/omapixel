#include "Document.h"

#include "Ops.h"

namespace omapixel {

Document Document::blank(int columns, int rows)
{
    Document doc;
    doc.m_columns = qBound(1, columns, maxDimension);
    doc.m_rows = qBound(1, rows, maxDimension);
    doc.m_palette = Palette::standard();
    doc.addClip(QStringLiteral("idle"));
    return doc;
}

Document Document::empty(int columns, int rows)
{
    Document doc;
    doc.m_columns = qBound(1, columns, maxDimension);
    doc.m_rows = qBound(1, rows, maxDimension);
    doc.m_palette = Palette::standard();
    return doc;
}

QStringList Document::clipNames() const
{
    QStringList names;
    names.reserve(m_clips.size());
    for (const Clip &clip : m_clips)
        names.append(clip.name);
    return names;
}

int Document::indexOfClip(const QString &name) const
{
    for (int i = 0; i < m_clips.size(); ++i) {
        if (m_clips.at(i).name == name)
            return i;
    }
    return -1;
}

const Clip *Document::clip(const QString &name) const
{
    const int at = indexOfClip(name);
    return at < 0 ? nullptr : &m_clips.at(at);
}

Clip *Document::clip(const QString &name)
{
    const int at = indexOfClip(name);
    return at < 0 ? nullptr : &m_clips[at];
}

Grid Document::frame(const QString &name, int index) const
{
    const Clip *found = clip(name);
    if (!found || index < 0 || index >= found->frames.size())
        return Grid();
    return found->frames.at(index);
}

bool Document::setFrame(const QString &name, int index, const Grid &grid)
{
    Clip *found = clip(name);
    if (!found || index < 0 || index >= found->frames.size())
        return false;
    if (grid.columns() != m_columns || grid.rows() != m_rows)
        return false;
    found->frames[index] = grid;
    return true;
}

// ----------------------------------------------------------------- the clips

bool Document::addClip(const QString &name, int fps)
{
    if (name.isEmpty() || indexOfClip(name) >= 0)
        return false;
    Clip clip;
    clip.name = name;
    clip.fps = qBound(1, fps, 60);
    clip.frames.append(Grid(m_columns, m_rows));
    m_clips.append(clip);
    return true;
}

bool Document::removeClip(const QString &name)
{
    // The last clip stays, for the same reason a clip keeps its last frame: a
    // document with no clips has nothing to draw and nothing to select. The
    // studio opens it on an empty frame with no way back, and every command
    // that takes a frame answers "the document has no clips". Guarding it in
    // the front end only, as this once did, leaves the command line able to
    // write a file that neither front end can then edit.
    const int at = indexOfClip(name);
    if (at < 0 || m_clips.size() <= 1)
        return false;
    m_clips.removeAt(at);
    return true;
}

bool Document::renameClip(const QString &from, const QString &to)
{
    const int at = indexOfClip(from);
    if (at < 0 || to.isEmpty() || indexOfClip(to) >= 0)
        return false;
    m_clips[at].name = to;
    return true;
}

bool Document::setFps(const QString &name, int fps)
{
    Clip *found = clip(name);
    if (!found)
        return false;
    found->fps = qBound(1, fps, 60);
    return true;
}

// ---------------------------------------------------------------- the frames

bool Document::addFrame(const QString &name, int after, bool duplicate)
{
    Clip *found = clip(name);
    if (!found)
        return false;
    const int at = qBound(-1, after, found->frames.size() - 1);
    const Grid grid = (duplicate && at >= 0) ? found->frames.at(at)
                                             : Grid(m_columns, m_rows);
    found->frames.insert(at + 1, grid);
    return true;
}

bool Document::removeFrame(const QString &name, int index)
{
    Clip *found = clip(name);
    // A clip with no frames is a clip that cannot be drawn, so the last one
    // stays. Deleting the clip is a different command, and it is spelled out.
    if (!found || found->frames.size() <= 1 || index < 0
        || index >= found->frames.size())
        return false;
    found->frames.removeAt(index);
    return true;
}

bool Document::moveFrame(const QString &name, int index, int to)
{
    Clip *found = clip(name);
    if (!found || index < 0 || index >= found->frames.size() || to < 0
        || to >= found->frames.size())
        return false;
    found->frames.move(index, to);
    return true;
}

// ------------------------------------------------------------------ the size

int Document::wouldLose(int columns, int rows) const
{
    const int dx = (columns - m_columns) / 2;
    const int dy = (rows - m_rows) / 2;
    int lost = 0;
    for (const Clip &clip : m_clips) {
        for (const Grid &grid : clip.frames) {
            for (int y = 0; y < grid.rows(); ++y) {
                for (int x = 0; x < grid.columns(); ++x) {
                    if (grid.at(x, y) == Grid::Empty)
                        continue;
                    const int ny = y + dy;
                    const int nx = x + dx;
                    if (ny < 0 || ny >= rows || nx < 0 || nx >= columns)
                        ++lost;
                }
            }
        }
    }
    return lost;
}

void Document::resize(int columns, int rows)
{
    columns = qBound(1, columns, maxDimension);
    rows = qBound(1, rows, maxDimension);

    // No early return when the size already matches. A hand-written file can
    // hold a frame whose rows are not the size the document declares, and this
    // is the operation somebody reaches for to repair it; returning early left
    // `check` reporting a problem that nothing could fix.
    for (Clip &clip : m_clips) {
        for (Grid &grid : clip.frames) {
            // Measured against the FRAME rather than the document, so an
            // off-size frame is centred by what it actually is. For a
            // well-formed document the two are the same number.
            //
            // Integer division, and the same on both axes. It is the whole
            // definition of "centred" here, and what makes growing then
            // shrinking back hand the original drawing over.
            const int dx = (columns - grid.columns()) / 2;
            const int dy = (rows - grid.rows()) / 2;
            Grid next(columns, rows);
            for (int y = 0; y < rows; ++y) {
                for (int x = 0; x < columns; ++x)
                    next.set(x, y, grid.at(x - dx, y - dy));
            }
            grid = next;
        }
    }

    m_columns = columns;
    m_rows = rows;
}

QRect Document::drawnBounds(const QString &clipName, int frameIndex) const
{
    const Clip *found = clip(clipName);
    if (!found || frameIndex < 0 || frameIndex >= found->frames.size())
        return QRect();

    const Grid &grid = found->frames.at(frameIndex);
    int left = grid.columns();
    int top = grid.rows();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < grid.rows(); ++y) {
        for (int x = 0; x < grid.columns(); ++x) {
            if (grid.at(x, y) == Grid::Empty)
                continue;
            left = qMin(left, x);
            top = qMin(top, y);
            right = qMax(right, x);
            bottom = qMax(bottom, y);
        }
    }
    return right < left ? QRect()
                        : QRect(left, top, right - left + 1, bottom - top + 1);
}

int Document::wouldLoseOutside(const QRect &kept) const
{
    int lost = 0;
    for (const Clip &clip : m_clips) {
        for (const Grid &grid : clip.frames) {
            for (int y = 0; y < grid.rows(); ++y) {
                for (int x = 0; x < grid.columns(); ++x) {
                    if (grid.at(x, y) != Grid::Empty && !kept.contains(x, y))
                        ++lost;
                }
            }
        }
    }
    return lost;
}

bool Document::crop(const QRect &kept)
{
    const QRect whole(0, 0, m_columns, m_rows);
    if (!kept.isValid() || !whole.contains(kept) || kept == whole)
        return false;

    for (Clip &clip : m_clips) {
        for (Grid &grid : clip.frames) {
            Grid next(kept.width(), kept.height());
            for (int y = 0; y < kept.height(); ++y) {
                for (int x = 0; x < kept.width(); ++x)
                    next.set(x, y, grid.at(x + kept.x(), y + kept.y()));
            }
            grid = next;
        }
    }
    m_columns = kept.width();
    m_rows = kept.height();
    return true;
}

int Document::replaceSlot(QChar from, QChar to)
{
    if (from == to)
        return 0;
    int changed = 0;
    for (Clip &clip : m_clips) {
        for (Grid &grid : clip.frames)
            changed += ops::swapSlot(grid, from, to);
    }
    return changed;
}

int Document::replaceSlotInFrame(const QString &clip, int frame, QChar from,
                                 QChar to)
{
    if (from == to)
        return 0;
    Grid grid = this->frame(clip, frame);
    if (grid.isEmpty())
        return 0;
    const int changed = ops::swapSlot(grid, from, to);
    if (changed > 0)
        setFrame(clip, frame, grid);
    return changed;
}

bool Document::usesSlot(QChar slot) const
{
    for (const Clip &clip : m_clips) {
        for (const Grid &grid : clip.frames) {
            for (int y = 0; y < grid.rows(); ++y) {
                for (int x = 0; x < grid.columns(); ++x) {
                    if (grid.at(x, y) == slot)
                        return true;
                }
            }
        }
    }
    return false;
}

// ------------------------------------------------------------------ problems

QStringList Document::problems() const
{
    QStringList out;
    if (m_columns <= 0 || m_rows <= 0) {
        out.append(QStringLiteral("size has to be positive"));
        return out;
    }

    for (const Palette::Slot &slot : m_palette.entries()) {
        if (slot.letter == Grid::Empty) {
            out.append(QStringLiteral(
                "`.` is always transparent and cannot be in the palette"));
        }
        if (!slot.colour.isValid()) {
            out.append(QStringLiteral("slot %1: not a colour")
                           .arg(slot.letter));
        }
    }

    for (const Clip &clip : m_clips) {
        if (clip.frames.isEmpty()) {
            out.append(QStringLiteral("%1: no frames").arg(clip.name));
            continue;
        }
        for (int i = 0; i < clip.frames.size(); ++i) {
            const Grid &grid = clip.frames.at(i);
            if (grid.columns() != m_columns || grid.rows() != m_rows) {
                out.append(QStringLiteral("%1[%2]: %3x%4, expected %5x%6")
                               .arg(clip.name)
                               .arg(i)
                               .arg(grid.columns())
                               .arg(grid.rows())
                               .arg(m_columns)
                               .arg(m_rows));
            }
            // A slot with no colour does not break the renderer -- it skips the
            // character and loses those pixels, quietly, on every surface. That
            // silence is exactly why it is worth reporting.
            QStringList unknown;
            const QList<QChar> used = grid.slotsUsed().values();
            for (QChar letter : used) {
                if (!m_palette.has(letter))
                    unknown.append(QString(letter));
            }
            if (!unknown.isEmpty()) {
                unknown.sort();
                out.append(QStringLiteral("%1[%2]: uses a slot with no colour: %3")
                               .arg(clip.name)
                               .arg(i)
                               .arg(unknown.join(QStringLiteral(", "))));
            }
        }
    }
    return out;
}

bool Document::operator==(const Document &other) const
{
    if (m_columns != other.m_columns || m_rows != other.m_rows)
        return false;
    if (m_palette.entries().size() != other.m_palette.entries().size())
        return false;
    for (int i = 0; i < m_palette.entries().size(); ++i) {
        const Palette::Slot &mine = m_palette.entries().at(i);
        const Palette::Slot &theirs = other.m_palette.entries().at(i);
        if (mine.letter != theirs.letter || mine.colour != theirs.colour)
            return false;
    }
    if (m_clips.size() != other.m_clips.size())
        return false;
    for (int i = 0; i < m_clips.size(); ++i) {
        const Clip &mine = m_clips.at(i);
        const Clip &theirs = other.m_clips.at(i);
        if (mine.name != theirs.name || mine.fps != theirs.fps
            || mine.frames != theirs.frames)
            return false;
    }
    return true;
}

} // namespace omapixel
