#include "DocumentModel.h"

#include "ChangeLog.h"
#include "Codec.h"
#include "Config.h"
#include "Differences.h"
#include "Ops.h"
#include "Render.h"
#include "Sessions.h"
#include "Strings.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRandomGenerator>
#include <QUrl>
#include <QVariantMap>

namespace omapixel {
namespace {

Codec::WarningLimits warningLimits()
{
    const Config &config = Config::shared();
    Codec::WarningLimits limits;
    limits.fileBytes = qint64(config.number(QStringLiteral("warnings.file_mib")))
                       * 1024 * 1024;
    limits.clips = config.number(QStringLiteral("warnings.clips"));
    limits.framesPerClip =
        config.number(QStringLiteral("warnings.frames_per_clip"));
    limits.totalFrames = config.number(QStringLiteral("warnings.frames_total"));
    limits.paletteSlots = config.number(QStringLiteral("warnings.palette_slots"));
    return limits;
}

qint64 renderWarningPixels()
{
    return qint64(Config::shared().number(
                      QStringLiteral("warnings.render_megapixels")))
           * 1000000;
}

Document newDocument(int columns, int rows)
{
    Document document = Document::blank(columns, rows);
    document.setFps(document.clipNames().first(),
                    qBound(1, Config::shared().number(
                                  QStringLiteral("document.fps")),
                           60));
    return document;
}

} // namespace

DocumentModel::DocumentModel(QObject *parent)
    : QObject(parent),
      // The size the config file calls a new document, so opening the studio
      // with nothing to open gives you the canvas you usually work on.
      m_document(newDocument(
          qBound(1, Config::shared().number(QStringLiteral("document.width")), 512),
          qBound(1, Config::shared().number(QStringLiteral("document.height")), 512))),
      m_changes(new ChangeLog(this))
{
    m_paletteRows.sync(m_document.palette());
    m_clip = m_document.clipNames().value(0);
    m_note = QStringLiteral("new document · %1×%2")
                 .arg(m_document.columns())
                 .arg(m_document.rows());
    m_changes->follow(this);
    connect(this, &DocumentModel::viewChanged, this,
            &DocumentModel::clearSelection);

    // The live loop, watched the way Config and Theme watch their own files:
    // both the file and its directory. A CLI write renames over the target,
    // which drops a file-only watch -- the directory signal is what re-arms
    // us for every write after the first.
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        // Re-arm here as well, even though the directory signal usually
        // arrives to do it: a rename-over drops our watch on the file, and
        // on backends where the directory signal is lost or late (FUSE,
        // network mounts) this is the only chance to stay alive. Idempotent.
        watch();
        reloadFromDisk();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        // Re-arm BEFORE reading: the rename is why this fired, and the path
        // we were watching is no longer the file on disk.
        watch();
        reloadFromDisk();
    });

    // An untitled window still gets a real address on disk, so the command
    // line can find it and draw into it.
    openScratch();
}

QString DocumentModel::followedPath() const
{
    return m_path.isEmpty() ? m_scratch : m_path;
}

bool DocumentModel::isScratchBacked() const
{
    return !m_scratch.isEmpty() && m_path.isEmpty();
}

void DocumentModel::retireScratch()
{
    if (!m_scratch.isEmpty())
        QFile::remove(m_scratch);
    m_scratch.clear();
}

void DocumentModel::openScratch()
{
    if (!Config::shared().flag(QStringLiteral("studio.scratch")))
        return;
    m_scratch = sessions::scratchPath(QCoreApplication::applicationPid());
    if (m_scratch.isEmpty()) {
        // Nowhere sane to put it: fall back to the old invisibility rather
        // than pretending to be addressable.
        return;
    }
    writeScratchSeed();
    watch();
}

void DocumentModel::writeScratchSeed()
{
    QDir().mkpath(sessions::scratchDirectory());
    QString error;
    if (!Codec::writeFile(m_scratch, m_document, &error)) {
        // Unwritable is as good as absent: the window falls back quietly.
        m_scratch.clear();
    }
}

QAbstractListModel *DocumentModel::changes() const { return m_changes; }

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

void DocumentModel::setSelection(int x0, int y0, int x1, int y1)
{
    const int left = qBound(0, qMin(x0, x1), m_document.columns() - 1);
    const int right = qBound(0, qMax(x0, x1), m_document.columns() - 1);
    const int top = qBound(0, qMin(y0, y1), m_document.rows() - 1);
    const int bottom = qBound(0, qMax(y0, y1), m_document.rows() - 1);
    const QRect next(QPoint(left, top), QPoint(right, bottom));
    if (m_selection == next)
        return;
    m_selection = next;
    emit selectionChanged();
}

void DocumentModel::clearSelection()
{
    if (!m_selection.isValid())
        return;
    m_selection = QRect();
    emit selectionChanged();
}

void DocumentModel::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    watch();
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
    // Whatever happens next came from the user's hand, not from disk.
    m_lastChangeExternal = false;
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
    while (m_undo.size() > historyDepth())
        m_undo.removeFirst();
    m_redo.clear();
    emit historyChanged();
}

int DocumentModel::historyDepth()
{
    // One is the floor: a stack with nothing in it turns Ctrl+Z into a key
    // that does nothing, which reads as broken rather than as configured.
    return qMax(1, Config::shared().number(QStringLiteral("history.depth")));
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
        say(Strings::shared().t(QStringLiteral("note.nothingToUndo")));
        return;
    }
    m_lastChangeExternal = false;
    m_redo.append(m_document);
    m_document = m_undo.takeLast();
    reseat();
    m_dirty = true;
    say(Strings::shared().t(QStringLiteral("note.undone")).arg(m_undo.size()));
    paletteMoved();
    emit changed();
    emit renderChanged(QString(), -1);
    emit viewChanged();
    emit fileChanged();
    emit historyChanged();
}

void DocumentModel::redo()
{
    if (m_redo.isEmpty()) {
        say(Strings::shared().t(QStringLiteral("note.nothingToRedo")));
        return;
    }
    m_lastChangeExternal = false;
    m_undo.append(m_document);
    m_document = m_redo.takeLast();
    reseat();
    m_dirty = true;
    say(Strings::shared().t(QStringLiteral("note.redone")));
    paletteMoved();
    emit changed();
    emit renderChanged(QString(), -1);
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
    if (m_reloadPending) {
        m_reloadPending = false;
        reloadFromDisk();
    }
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
    emit renderChanged(m_clip, m_frame);
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
            say(Strings::shared().t(QStringLiteral("note.paletteFull")));
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
        say(Strings::shared().t(QStringLiteral("note.nothingDrawnWith")).arg(from));
        return;
    }
    // Nothing refers to the old slot any more, so it is clutter.
    if (!m_document.usesSlot(from))
        m_document.palette().remove(from);

    remember(before);
    m_dirty = true;
    m_clip = m_document.clip(m_clip) ? m_clip : m_document.clipNames().value(0);
    paletteMoved();
    say(Strings::shared().t(QStringLiteral("note.replaced"))
            .arg(moved)
            .arg(from)
            .arg(everywhere ? Strings::shared().t(QStringLiteral("note.everywhere"))
                            : Strings::shared().t(QStringLiteral("note.thisFrame"))));
    emit changed();
    emit renderChanged(QString(), -1);
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
    editFrame([&](Grid &grid) {
        if (m_selection.isValid()) {
            ops::rect(grid, m_selection.topLeft(), m_selection.bottomRight(),
                      slotOf(slot), true);
        } else {
            ops::paint(grid, x, y, slotOf(slot));
        }
    });
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
        say(Strings::shared().t(QStringLiteral("note.clipExists")).arg(name));
        return;
    }
    remember(before);
    m_clip = name;
    m_frame = 0;
    m_dirty = true;
    emit changed();
    emit renderChanged(QString(), -1);
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
    emit renderChanged(QString(), -1);
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
    emit renderChanged(QString(), -1);
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
    emit renderChanged(QString(), -1);
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
    emit renderChanged(QString(), -1);
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
    emit renderChanged(QString(), -1);
    emit viewChanged();
    emit fileChanged();
}

int DocumentModel::wouldLose(int columns, int rows) const
{
    return m_document.wouldLose(columns, rows);
}

void DocumentModel::resize(int columns, int rows)
{
    clearSelection();
    remember(m_document);
    m_document.resize(columns, rows);
    m_dirty = true;
    say(Strings::shared().t(QStringLiteral("note.resized")).arg(columns).arg(rows));
    emit changed();
    emit renderChanged(QString(), -1);
    emit fileChanged();
}

QVariantMap DocumentModel::trimPreview() const
{
    const QRect bounds = m_document.drawnBounds(m_clip, m_frame);
    if (!bounds.isValid())
        return {{QStringLiteral("empty"), true}};

    const QRect whole(0, 0, m_document.columns(), m_document.rows());
    return {{QStringLiteral("empty"), false},
            {QStringLiteral("changed"), bounds != whole && whole.contains(bounds)},
            {QStringLiteral("x"), bounds.x()},
            {QStringLiteral("y"), bounds.y()},
            {QStringLiteral("columns"), bounds.width()},
            {QStringLiteral("rows"), bounds.height()},
            {QStringLiteral("lost"), m_document.wouldLoseOutside(bounds)}};
}

bool DocumentModel::trim(bool anyway)
{
    const QRect bounds = m_document.drawnBounds(m_clip, m_frame);
    if (!bounds.isValid()) {
        say(Strings::shared().t(QStringLiteral("note.nothingToTrim")));
        return false;
    }

    const QRect whole(0, 0, m_document.columns(), m_document.rows());
    if (bounds == whole) {
        say(Strings::shared().t(QStringLiteral("note.alreadyTight")));
        return false;
    }

    const int lost = m_document.wouldLoseOutside(bounds);
    if (lost > 0 && !anyway) {
        say(Strings::shared().t(QStringLiteral("note.trimWouldCrop")).arg(lost));
        return false;
    }

    const Document before = m_document;
    if (!m_document.crop(bounds)) {
        say(Strings::shared().t(QStringLiteral("note.nothingToTrim")));
        return false;
    }
    clearSelection();
    remember(before);
    m_dirty = true;
    say(Strings::shared().t(QStringLiteral("note.trimmed"))
            .arg(bounds.width())
            .arg(bounds.height()));
    emit changed();
    emit renderChanged(QString(), -1);
    emit fileChanged();
    return true;
}

void DocumentModel::reset(int columns, int rows)
{
    remember(m_document);
    m_document = newDocument(columns, rows);
    m_clip = m_document.clipNames().value(0);
    m_frame = 0;
    m_path.clear();
    m_dirty = false;
    watch();
    say(Strings::shared().t(QStringLiteral("note.newDocument")).arg(columns).arg(rows));
    paletteMoved();
    m_changes->sync();
    // A new untitled document: fresh backing, so the command line can reach
    // this one too.
    openScratch();
    emit changed();
    emit renderChanged(QString(), -1);
    emit viewChanged();
    emit fileChanged();
    emit documentReplaced();
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

    const Codec::Result read = Codec::readFile(where, warningLimits());
    if (!read) {
        say(read.error);
        return false;
    }
    // The document has a name now; its scratch backing is a liability, not
    // an address.
    retireScratch();
    m_document = read.document;
    m_undo.clear();
    m_redo.clear();
    emit historyChanged();
    m_clip = m_document.clipNames().value(0);
    m_frame = 0;
    m_path = where;
    m_dirty = false;
    watch();
    paletteMoved();
    QString note = Strings::shared().t(QStringLiteral("note.opened"))
                       .arg(QFileInfo(where).fileName())
                       .arg(m_document.clips().size());
    if (!read.warnings.isEmpty())
        note += QStringLiteral(" · warning: ") + read.warnings.join(QStringLiteral("; "));
    say(note);
    // A whole-document swap, not a change to a drawing: the log rebases
    // rather than filing a diff between two different documents.
    m_changes->sync();
    emit changed();
    emit renderChanged(QString(), -1);
    emit viewChanged();
    emit fileChanged();
    emit documentReplaced();
    return true;
}

void DocumentModel::watch()
{
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    const QString followed = followedPath();
    if (followed.isEmpty())
        return;
    m_watcher.addPath(followed);
    const QString directory = QFileInfo(followed).absolutePath();
    if (!directory.isEmpty())
        m_watcher.addPath(directory);
}

bool DocumentModel::reloadFromDisk()
{
    if (followedPath().isEmpty())
        return false;

    // A stroke spans the whole press-to-release drag (`beginStroke` to
    // `endStroke`). Swapping `m_document` under a live drag makes the rest of
    // the stroke paint onto the file's document with a stale remembered flag
    // -- that corrupts rather than surprises. Queue it; `endStroke` applies.
    if (m_stroke) {
        m_reloadPending = true;
        return false;
    }

    const Codec::Result read = Codec::readFile(followedPath(), warningLimits());
    if (!read) {
        // The directory re-arm means sibling noise -- an export elsewhere, an
        // rm, an editor's swapfile -- can knock here with a missing or
        // half-written file. What is on screen is the only copy the user
        // has; it stays, and the failure is said out loud.
        say(read.error);
        return false;
    }

    // Content is the only test that counts. A written-flag would race the
    // writer; equality makes our own save, a touch and a reformat all
    // correctly nothing -- and collapses the double fire one atomic rename
    // produces from watching both the file and its directory.
    //
    // Note what this makes true: Document::operator== now defines what counts
    // as an external change. A field added to Document but left out of that
    // operator is an edit the studio will never show.
    if (read.document == m_document)
        return false;

    const Document before = m_document;
    // Captured before the flag falls, because it is about to: this boolean is
    // the data-loss guard. The reload replaces whatever the user had not
    // saved, and the note has to say so while there is still something to
    // describe.
    const bool replacedUnsaved = m_dirty;
    m_document = read.document;
    reseat();
    // Memory equals disk after a clean reload, so adopting does not set
    // dirty. Whether it CLEARS dirty depends on what the disk is: a file the
    // user named is a save, and the flag falls. A scratch backing is tmpfs
    // that dies at logout -- adopting from it is not saving either, so the
    // unsaved truth stands and the close guard keeps doing its job.
    if (!isScratchBacked())
        m_dirty = false;

    // An agent loop can rewrite the file dozens of times a second, and every
    // snapshot here is a whole document on a stack capped at history.depth.
    // Filing one per write evicts the user's own history within seconds and
    // clears their redo with it. So while the document on screen IS the state
    // some earlier external write left -- nothing of theirs drawn on top
    // since -- the single entry standing for "your version" keeps standing,
    // however many writes land. The first user edit dirties the document and
    // the next outside write snapshots again. Redo goes either way: what it
    // held was undone into a world that no longer exists.
    if (!replacedUnsaved && !m_undo.isEmpty()) {
        m_redo.clear();
        emit historyChanged();
    } else {
        remember(before);
    }
    // The change log reads this when `changed()` reaches it, which with a
    // direct connection is inside the emit below.
    m_lastChangeExternal = true;

    paletteMoved();
    // Two notes, not one. The ordinary case says what changed; when unsaved
    // work was replaced, the note also says how to get it back -- without
    // that sentence the next Ctrl+S destroys the agent's edit and the work
    // with it.
    say(Strings::shared()
            .t(replacedUnsaved ? QStringLiteral("note.reloadedDirty")
                               : QStringLiteral("note.reloaded"))
            .arg(QFileInfo(m_path).fileName())
            .arg(describeDifferences(before)));
    emit changed();
    emit renderChanged(QString(), -1);
    emit viewChanged();
    emit fileChanged();
    return true;
}

QString DocumentModel::describeDifferences(const Document &before) const
{
    // One summarizer, shared with the change log: a sentence that claims
    // more behind it means it everywhere it is shown.
    return summarizeDifferences(documentDifferences(before, m_document,
                                                   QStringLiteral("before"),
                                                   QStringLiteral("after")))
        .join(QStringLiteral("; "));
}

bool DocumentModel::save(const QString &path)
{
    QString where = path.isEmpty() ? m_path : path;
    if (where.startsWith(QLatin1String("file://")))
        where = QUrl(where).toLocalFile();
    if (where.isEmpty()) {
        say(Strings::shared().t(QStringLiteral("note.sayWhereToSave")));
        return false;
    }
    QString error;
    if (!Codec::writeFile(where, m_document, &error)) {
        say(error);
        return false;
    }
    m_path = where;
    m_dirty = false;
    watch();
    // The document has a home; the scratch backing would only be a second,
    // stale copy of it.
    retireScratch();
    say(Strings::shared().t(QStringLiteral("note.savedTo")).arg(where));
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
        say(Strings::shared().t(QStringLiteral("note.sayWhereToExport")));
        return false;
    }

    render::Options options;
    options.scale = qBound(1, scale, 64);
    options.sheet = sheet;
    options.checker = checker;
    options.warningPixels = renderWarningPixels();
    QString warning;
    QString error;
    const QImage image =
        render::toImage(m_document, m_clip, m_frame, options, &warning, &error);
    if (image.isNull() || !image.save(where)) {
        say(error.isEmpty()
                ? Strings::shared().t(QStringLiteral("note.couldNotWrite")).arg(where)
                : error);
        return false;
    }
    QString note = Strings::shared().t(QStringLiteral("note.exported"))
                       .arg(image.width())
                       .arg(image.height())
                       .arg(QFileInfo(where).fileName());
    if (!warning.isEmpty())
        note += QStringLiteral(" · warning: ") + warning;
    say(note);
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
