#include "DocumentModel.h"

#include "Codec.h"
#include "Ops.h"
#include "Render.h"

#include <QFileInfo>
#include <QDir>
#include <QRandomGenerator>
#include <QUrl>
#include <QVariantMap>

namespace omapixel {

DocumentModel::DocumentModel(QObject *parent)
    : QObject(parent), m_document(Document::blank(32, 24))
{
    m_paletteRows.sync(m_document.palette());
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

void DocumentModel::paletteMoved()
{
    m_paletteRevision += 1;
    m_paletteRows.sync(m_document.palette());
    emit paletteChanged();
}

void DocumentModel::remember(const Document &before)
{
    // Inside a stroke only the first change is filed. It is what makes a drag
    // one undo step, and it is also what lets an operation built from two
    // edits -- a colour added and then painted with -- cost one snapshot
    // instead of two. A document is not small; copying it twice per keypress
    // is felt.
    if (m_stroke) {
        if (m_strokeRemembered)
            return;
        m_strokeRemembered = true;
    }
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
    paletteMoved();
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
    paletteMoved();
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
    remember(m_document);
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

QColor DocumentModel::colourOf(const QString &slot) const
{
    if (slot.size() != 1)
        return QColor();
    return m_document.palette().colour(slot.at(0));
}

QVariantList DocumentModel::findColours(const QString &query) const
{
    const QString wanted = query.trimmed();
    QVariantList out;

    const auto add = [&out](const QString &name, const QColor &colour) {
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("colour"), colour.name(QColor::HexRgb).toUpper());
        out.append(entry);
    };

    // A hex, or anything else Qt can read as a colour, first and under the
    // text that was typed.
    const QColor literal(wanted);
    if (!wanted.isEmpty() && literal.isValid())
        add(wanted, literal);

    for (const QString &name : QColor::colorNames()) {
        if (out.size() >= 80)
            break;
        if (!wanted.isEmpty() && !name.contains(wanted, Qt::CaseInsensitive))
            continue;
        // `transparent` is a colour name Qt knows and this format has no use
        // for: emptiness here is the absence of a slot, not a slot that
        // happens to be see-through.
        if (name == QLatin1String("transparent"))
            continue;
        add(name, QColor(name));
    }
    return out;
}

int DocumentModel::countSlot(const QString &slot, bool everywhere) const
{
    if (slot.size() != 1)
        return 0;
    const QChar wanted = slot.at(0);
    int found = 0;

    const auto tally = [&](const Grid &grid) {
        for (int y = 0; y < grid.rows(); ++y) {
            for (int x = 0; x < grid.columns(); ++x) {
                if (grid.at(x, y) == wanted)
                    found += 1;
            }
        }
    };

    if (!everywhere) {
        tally(m_document.frame(m_clip, m_frame));
        return found;
    }

    for (const Clip &clip : m_document.clips()) {
        for (const Grid &grid : clip.frames) {
            tally(grid);
        }
    }
    return found;
}

void DocumentModel::replaceColour(const QString &fromSlot, const QString &hex,
                                  bool everywhere)
{
    const QColor colour(hex);
    if (fromSlot.size() != 1 || !colour.isValid())
        return;
    const QChar from = fromSlot.at(0);

    // A slot that already holds this colour, or a new one. Reusing keeps the
    // palette from growing an identical entry every time.
    QChar to;
    for (const Palette::Slot &entry : m_document.palette().entries()) {
        if (entry.colour == colour) {
            to = entry.letter;
            break;
        }
    }
    if (to.isNull()) {
        const QString fresh = freeSlot();
        if (fresh.isEmpty()) {
            say(QStringLiteral("the palette is full — remove a slot first"));
            return;
        }
        to = fresh.at(0);
    }
    if (to == from)
        return;

    const Document before = m_document;
    if (!m_document.palette().colour(to).isValid())
        m_document.palette().set(to, colour);
    const int moved = everywhere
                          ? m_document.replaceSlot(from, to)
                          : m_document.replaceSlotInFrame(m_clip, m_frame, from, to);
    if (moved == 0) {
        m_document = before;
        say(QStringLiteral("nothing was drawn with %1").arg(from));
        return;
    }
    // Nothing refers to the old slot any more, so it is clutter.
    if (!m_document.usesSlot(from))
        m_document.palette().remove(from);

    remember(before);
    m_dirty = true;
    m_clip = m_document.clip(m_clip) ? m_clip : m_document.clipNames().value(0);
    paletteMoved();
    say(QStringLiteral("replaced %1 pixel(s) of %2%3")
            .arg(moved)
            .arg(from)
            .arg(everywhere ? QStringLiteral(", everywhere")
                            : QStringLiteral(", in this frame")));
    emit changed();
    emit viewChanged();
    emit fileChanged();
}

QString DocumentModel::freeSlot() const
{
    const auto usable = [](char16_t c) {
        // `.` is emptiness and can never be a slot. The quote and the backslash
        // are legal in the file -- JSON escapes them -- but they turn a row of
        // pixels into a row of escapes for anyone reading it, and this format
        // is meant to be read.
        //
        // The digits are reserved for the studio's colour keys. A slot called
        // `3` whose colour is not what pressing 3 draws is a contradiction
        // sitting on screen, and one nobody can be talked out of reading. A
        // document that already uses a digit still works -- the format allows
        // any character -- this only declines to hand one out.
        return c != u'.' && c != u'"' && c != u'\\' && !(c >= u'0' && c <= u'9');
    };

    // Letters first, so a palette that stays small stays legible.
    const QString preferred = QStringLiteral(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    for (const QChar c : preferred) {
        if (!m_document.palette().colour(c).isValid())
            return QString(c);
    }
    for (char16_t c = u'!'; c <= u'~'; ++c) {
        if (usable(c) && !m_document.palette().colour(QChar(c)).isValid())
            return QString(QChar(c));
    }
    // Latin-1 and Latin Extended-A after that: accented letters that still
    // read as letters in a wall of pixels, unlike box-drawing or arrows.
    // Soft hyphen and the non-breaking space are skipped -- a slot you cannot
    // see is a slot you cannot edit by hand.
    for (char16_t c = 0xA1; c <= 0x24F; ++c) {
        if (c == 0xAD)
            continue;
        if (usable(c) && !m_document.palette().colour(QChar(c)).isValid())
            return QString(QChar(c));
    }
    return QString();
}

QString DocumentModel::randomColour() const
{
    auto *chance = QRandomGenerator::global();
    return QColor::fromRgb(chance->bounded(256), chance->bounded(256),
                           chance->bounded(256))
        .name(QColor::HexRgb)
        .toUpper();
}

QColor DocumentModel::contrastAt(int x, int y) const
{
    const Grid grid = m_document.frame(m_clip, m_frame);
    const QChar slot = grid.at(x, y);
    if (slot == Grid::Empty)
        return QColor();

    const QColor under = m_document.palette().colour(slot);
    if (!under.isValid())
        return QColor();

    const auto luminance = [](const QColor &c) {
        return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
    };
    const QColor inverted(255 - under.red(), 255 - under.green(), 255 - under.blue());
    if (qAbs(luminance(inverted) - luminance(under)) > 0.28)
        return inverted;
    return luminance(under) > 0.5 ? QColor(Qt::black) : QColor(Qt::white);
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
    paletteMoved();
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
    paletteMoved();
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
    paletteMoved();
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

bool DocumentModel::exportImage(const QString &path, int scale, bool sheet,
                                bool checker)
{
    QString where = path;
    if (where.startsWith(QLatin1String("file://")))
        where = QUrl(where).toLocalFile();
    if (where.isEmpty()) {
        say(QStringLiteral("say where to export"));
        return false;
    }

    render::Options options;
    options.scale = qBound(1, scale, 64);
    options.sheet = sheet;
    options.checker = checker;
    const QImage image = render::toImage(m_document, m_clip, m_frame, options);
    if (image.isNull() || !image.save(where)) {
        say(QStringLiteral("could not write %1").arg(where));
        return false;
    }
    say(QStringLiteral("exported %1×%2 to %3")
            .arg(image.width())
            .arg(image.height())
            .arg(QFileInfo(where).fileName()));
    return true;
}

QString DocumentModel::suggestedExportPath(bool sheet) const
{
    const QString stem = m_path.isEmpty()
                             ? QStringLiteral("untitled")
                             : QFileInfo(m_path).completeBaseName();
    const QString directory = m_path.isEmpty() ? QDir::currentPath()
                                               : QFileInfo(m_path).absolutePath();
    return QStringLiteral("%1/%2%3.png")
        .arg(directory, stem, sheet ? QStringLiteral("-sheet") : QString());
}

} // namespace omapixel
