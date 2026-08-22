#include "DocumentModel.h"

#include "Codec.h"
#include "Ops.h"

#include <QFileInfo>
#include <QUrl>
#include <QVariantMap>

namespace omapixel {

DocumentModel::DocumentModel(QObject *parent)
    : QObject(parent), m_document(Document::blank(32, 24))
{
    m_clip = m_document.clipNames().value(0);
    m_note = QStringLiteral("new document · 32×24");
}

QVariantList DocumentModel::palette() const
{
    QVariantList out;
    for (const Palette::Slot &slot : m_document.palette().entries()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("slot"), QString(slot.letter));
        entry.insert(QStringLiteral("colour"), slot.colour.name(QColor::HexRgb).toUpper());
        out.append(entry);
    }
    return out;
}

void DocumentModel::setClip(const QString &clip)
{
    if (m_clip == clip || !m_document.clip(clip))
        return;
    m_clip = clip;
    m_frame = 0;
    emit viewChanged();
}

void DocumentModel::setFrame(int frame)
{
    const int bounded = qBound(0, frame, qMax(0, frameCount() - 1));
    if (m_frame == bounded)
        return;
    m_frame = bounded;
    emit viewChanged();
}

int DocumentModel::frameCount() const
{
    const Clip *clip = m_document.clip(m_clip);
    return clip ? clip->frames.size() : 0;
}

int DocumentModel::fps() const
{
    const Clip *clip = m_document.clip(m_clip);
    return clip ? clip->fps : 8;
}

void DocumentModel::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    emit fileChanged();
}

void DocumentModel::say(const QString &note)
{
    m_note = note;
    emit noteChanged();
}

void DocumentModel::remember(const Document &before)
{
    m_undo.append(before);
    if (m_undo.size() > HistoryDepth)
        m_undo.removeFirst();
    m_redo.clear();
    emit historyChanged();
}

void DocumentModel::reseat()
{
    if (!m_document.clip(m_clip))
        m_clip = m_document.clipNames().value(0);
    m_frame = qBound(0, m_frame, qMax(0, frameCount() - 1));
}

void DocumentModel::undo()
{
    if (m_undo.isEmpty()) {
        say(QStringLiteral("nothing to undo"));
        return;
    }
    m_redo.append(m_document);
    m_document = m_undo.takeLast();
    reseat();
    m_dirty = true;
    say(QStringLiteral("undone · %1 left").arg(m_undo.size()));
    emit changed();
    emit viewChanged();
    emit fileChanged();
    emit historyChanged();
}

void DocumentModel::redo()
{
    if (m_redo.isEmpty()) {
        say(QStringLiteral("nothing to redo"));
        return;
    }
    m_undo.append(m_document);
    m_document = m_redo.takeLast();
    reseat();
    m_dirty = true;
    say(QStringLiteral("redone"));
    emit changed();
    emit viewChanged();
    emit fileChanged();
    emit historyChanged();
}

void DocumentModel::beginStroke()
{
    m_stroke = true;
    m_strokeRemembered = false;
}

void DocumentModel::endStroke()
{
    m_stroke = false;
}

QChar DocumentModel::slotOf(const QString &text) const
{
    return text.size() == 1 ? text.at(0) : Grid::Empty;
}

// -------------------------------------------------------------------- drawing

void DocumentModel::editFrame(const std::function<void(Grid &)> &edit)
{
    Grid grid = m_document.frame(m_clip, m_frame);
    if (grid.isEmpty())
        return;
    const Grid before = grid;
    edit(grid);
    if (grid == before)
        return;
    // Snapshot taken here rather than when the stroke opened: a stroke that
    // never changes a pixel -- a click that missed, a bucket on its own colour
    // -- should not land an entry that undo then has to step through.
    if (!m_stroke || !m_strokeRemembered) {
        remember(m_document);
        m_strokeRemembered = true;
    }
    m_document.setFrame(m_clip, m_frame, grid);
    m_dirty = true;
    emit changed();
    emit fileChanged();
}

QString DocumentModel::slotAt(int x, int y) const
{
    const Grid grid = m_document.frame(m_clip, m_frame);
    return QString(grid.at(x, y));
}

void DocumentModel::paint(int x, int y, const QString &slot)
{
    editFrame([&](Grid &grid) { ops::paint(grid, x, y, slotOf(slot)); });
}

void DocumentModel::line(int x0, int y0, int x1, int y1, const QString &slot)
{
    editFrame([&](Grid &grid) {
        ops::line(grid, QPoint(x0, y0), QPoint(x1, y1), slotOf(slot));
    });
}

void DocumentModel::rect(int x0, int y0, int x1, int y1, const QString &slot,
                         bool filled)
{
    editFrame([&](Grid &grid) {
        ops::rect(grid, QPoint(x0, y0), QPoint(x1, y1), slotOf(slot), filled);
    });
}

void DocumentModel::fill(int x, int y, const QString &slot)
{
    editFrame([&](Grid &grid) { ops::fill(grid, x, y, slotOf(slot)); });
}

void DocumentModel::clearFrame()
{
    editFrame([](Grid &grid) { ops::clear(grid); });
}

void DocumentModel::shift(int dx, int dy)
{
    editFrame([&](Grid &grid) { ops::shift(grid, dx, dy); });
}

void DocumentModel::flip(const QString &axis)
{
    editFrame([&](Grid &grid) {
        if (axis == QLatin1String("x"))
            ops::flipHorizontal(grid);
        else
            ops::flipVertical(grid);
    });
}

// ------------------------------------------------------------------ structure

void DocumentModel::addClip(const QString &name)
{
    const Document before = m_document;
    if (!m_document.addClip(name)) {
        say(QStringLiteral("there is already a clip called %1").arg(name));
        return;
    }
    remember(before);
    m_clip = name;
    m_frame = 0;
    m_dirty = true;
    emit changed();
    emit viewChanged();
    emit fileChanged();
}

void DocumentModel::removeClip(const QString &name)
{
    // The refusal to remove the last clip lives in Document, so the command
    // line obeys it too.
    const Document before = m_document;
    if (!m_document.removeClip(name))
        return;
    remember(before);
    if (m_clip == name) {
        m_clip = m_document.clipNames().value(0);
        m_frame = 0;
        emit viewChanged();
    }
    m_dirty = true;
    emit changed();
    emit fileChanged();
}

void DocumentModel::renameClip(const QString &from, const QString &to)
{
    const Document before = m_document;
    if (!m_document.renameClip(from, to))
        return;
    remember(before);
    if (m_clip == from)
        m_clip = to;
    m_dirty = true;
    emit changed();
    emit viewChanged();
    emit fileChanged();
}

void DocumentModel::setFps(int fps)
{
    const Document before = m_document;
    if (!m_document.setFps(m_clip, fps))
        return;
    remember(before);
    m_dirty = true;
    emit changed();
    emit fileChanged();
}

void DocumentModel::addFrame(bool duplicate)
{
    const Document before = m_document;
    if (!m_document.addFrame(m_clip, m_frame, duplicate))
        return;
    remember(before);
    m_frame += 1;
    m_dirty = true;
    emit changed();
    emit viewChanged();
    emit fileChanged();
}

void DocumentModel::removeFrame()
{
    const Document before = m_document;
    if (!m_document.removeFrame(m_clip, m_frame))
        return;
    remember(before);
    m_frame = qBound(0, m_frame, frameCount() - 1);
    m_dirty = true;
    emit changed();
    emit viewChanged();
    emit fileChanged();
}

void DocumentModel::moveFrame(int step)
{
    const Document before = m_document;
    if (!m_document.moveFrame(m_clip, m_frame, m_frame + step))
        return;
    remember(before);
    m_frame += step;
    m_dirty = true;
    emit changed();
    emit viewChanged();
    emit fileChanged();
}

int DocumentModel::wouldLose(int columns, int rows) const
{
    return m_document.wouldLose(columns, rows);
}

void DocumentModel::resize(int columns, int rows)
{
    remember(m_document);
    m_document.resize(columns, rows);
    m_dirty = true;
    say(QStringLiteral("resized to %1×%2").arg(columns).arg(rows));
    emit changed();
    emit fileChanged();
}

void DocumentModel::reset(int columns, int rows)
{
    remember(m_document);
    m_document = Document::blank(columns, rows);
    m_clip = m_document.clipNames().value(0);
    m_frame = 0;
    m_path.clear();
    m_dirty = false;
    say(QStringLiteral("new document · %1×%2").arg(columns).arg(rows));
    emit changed();
    emit viewChanged();
    emit fileChanged();
}

void DocumentModel::setPaletteColour(const QString &slot, const QString &colour)
{
    const QColor parsed(colour);
    if (slot.size() != 1 || !parsed.isValid())
        return;
    if (m_document.palette().colour(slot.at(0)) == parsed)
        return;
    remember(m_document);
    m_document.palette().set(slot.at(0), parsed);
    m_dirty = true;
    emit changed();
    emit fileChanged();
}

QVariantList DocumentModel::sizePresets() const
{
    // Sizes that come up constantly in pixel art. Presets exist so the common
    // case costs no typing; the free fields in the panel exist because no list
    // of presets covers what somebody will want.
    const QList<QPair<QPair<int, int>, QString>> presets{
        {{16, 16}, QStringLiteral("icon")},
        {{24, 24}, QStringLiteral("large icon")},
        {{32, 32}, QStringLiteral("tile")},
        {{32, 24}, QStringLiteral("bar companion")},
        {{48, 48}, QString()},
        {{64, 48}, QStringLiteral("detail")},
        {{64, 64}, QString()},
        {{128, 128}, QStringLiteral("backdrop")},
    };
    QVariantList out;
    for (const auto &preset : presets) {
        QVariantMap entry;
        entry.insert(QStringLiteral("w"), preset.first.first);
        entry.insert(QStringLiteral("h"), preset.first.second);
        entry.insert(QStringLiteral("why"), preset.second);
        out.append(entry);
    }
    return out;
}

// ---------------------------------------------------------------------- files

bool DocumentModel::open(const QString &path)
{
    QString where = path;
    if (where.startsWith(QLatin1String("file://")))
        where = QUrl(where).toLocalFile();

    const Codec::Result read = Codec::readFile(where);
    if (!read) {
        say(read.error);
        return false;
    }
    m_document = read.document;
    m_undo.clear();
    m_redo.clear();
    emit historyChanged();
    m_clip = m_document.clipNames().value(0);
    m_frame = 0;
    m_path = where;
    m_dirty = false;
    say(QStringLiteral("%1 · %2 clip(s)")
            .arg(QFileInfo(where).fileName())
            .arg(m_document.clips().size()));
    emit changed();
    emit viewChanged();
    emit fileChanged();
    return true;
}

bool DocumentModel::save(const QString &path)
{
    const QString where = path.isEmpty() ? m_path : path;
    if (where.isEmpty()) {
        say(QStringLiteral("say where to save"));
        return false;
    }
    QString error;
    if (!Codec::writeFile(where, m_document, &error)) {
        say(error);
        return false;
    }
    m_path = where;
    m_dirty = false;
    say(QStringLiteral("saved to %1").arg(where));
    emit fileChanged();
    return true;
}

} // namespace omapixel
