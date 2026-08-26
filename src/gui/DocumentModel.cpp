#include "DocumentModel.h"

#include "ChangeLog.h"
#include "Codec.h"
#include "Config.h"
#include "Differences.h"
#include "GifExport.h"
#include "ImageImport.h"
#include "LayerOperations.h"
#include "Ops.h"
#include "Output.h"
#include "Render.h"
#include "Sessions.h"
#include "Strings.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMimeData>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QProcess>
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

QString freeSlotIn(const Palette &palette)
{
    if (palette.size() >= Palette::maxSlots)
        return QString();

    // Keep generated rows readable: letters first, then visible punctuation
    // and extended Unicode. Dot is transparency; digits belong to colour keys.
    const auto usable = [](char16_t c) {
        return c != u'.' && c != u'"' && c != u'\\'
               && !(c >= u'0' && c <= u'9');
    };

    const QString preferred = QStringLiteral(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    for (const QChar c : preferred) {
        if (!palette.colour(c).isValid())
            return QString(c);
    }
    for (char16_t c = u'!'; c <= u'~'; ++c) {
        if (usable(c) && !palette.colour(QChar(c)).isValid())
            return QString(QChar(c));
    }
    for (char16_t c = 0xA1; c <= 0x3FF; ++c) {
        // Soft hyphen is invisible, so it is no better as a slot than a space.
        if (c != 0xAD && usable(c) && !palette.colour(QChar(c)).isValid())
            return QString(QChar(c));
    }
    return QString();
}

QString localPath(const QString &path)
{
    const QUrl url(path);
    return url.isLocalFile() ? url.toLocalFile() : path;
}

bool importOptions(const QString &fit, int scale, int width, int height,
                   ImageImport::Options *options, QString *error)
{
    if (!options)
        return false;
    if (scale < 0) {
        if (error)
            *error = QStringLiteral("import scale must be zero or a positive integer");
        return false;
    }
    if (scale > 0 && (width != 0 || height != 0)) {
        if (error)
            *error = QStringLiteral("import scale and resolution are mutually exclusive");
        return false;
    }
    if (scale == 0 && (width <= 0 || height <= 0
                       || width > Document::maxDimension
                       || height > Document::maxDimension)) {
        if (error)
            *error = QStringLiteral("import resolution must be positive and no larger than %1x%1")
                         .arg(Document::maxDimension);
        return false;
    }

    const QString mode = fit.toLower();
    if (mode == QLatin1String("contain"))
        options->resizeMode = ImageImport::ResizeMode::Contain;
    else if (mode == QLatin1String("cover"))
        options->resizeMode = ImageImport::ResizeMode::Cover;
    else if (mode == QLatin1String("stretch"))
        options->resizeMode = ImageImport::ResizeMode::Stretch;
    else {
        if (error)
            *error = QStringLiteral("import fit must be contain, cover, or stretch");
        return false;
    }
    options->scale = scale;
    options->targetResolution = scale == 0 ? QSize(width, height) : QSize();
    return true;
}

bool samePalette(const Palette &left, const Palette &right)
{
    if (left.size() != right.size())
        return false;
    for (int index = 0; index < left.entries().size(); ++index) {
        const Palette::Slot &a = left.entries().at(index);
        const Palette::Slot &b = right.entries().at(index);
        if (a.letter != b.letter || a.colour != b.colour)
            return false;
    }
    return true;
}

QVariantMap layerReport(const LayerOperationReport &report)
{
    return {{QStringLiteral("frames"), report.frames},
            {QStringLiteral("affectedPixels"), report.affectedPixels},
            {QStringLiteral("exactMatches"), report.exactMatches},
            {QStringLiteral("approximatedPixels"), report.approximatedPixels},
            {QStringLiteral("newSlots"), report.newSlots},
            {QStringLiteral("removedLayers"), report.removedLayers},
            {QStringLiteral("diagnostics"), report.diagnostics}};
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
    m_activeLayerId = m_document.layers().value(0).id;
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

QVariantList DocumentModel::clips() const
{
    QVariantList out;
    for (const Clip &clip : m_document.clips()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), clip.id);
        entry.insert(QStringLiteral("name"), clip.name);
        out.append(entry);
    }
    return out;
}

QString DocumentModel::activeClipId() const
{
    const Clip *clip = m_document.clip(m_clip);
    return clip ? clip->id : QString();
}

void DocumentModel::selectClip(const QString &id)
{
    const Clip *clip = m_document.clipById(id);
    if (clip)
        setClip(clip->name);
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
    return clip ? clip->frameCount : 0;
}

int DocumentModel::fps() const
{
    const Clip *clip = m_document.clip(m_clip);
    return clip ? clip->fps : 8;
}

QVariantList DocumentModel::layers() const
{
    QVariantList out;
    for (const Layer &layer : m_document.layers()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), layer.id);
        entry.insert(QStringLiteral("name"), layer.name);
        entry.insert(QStringLiteral("visible"), layer.visible);
        entry.insert(QStringLiteral("locked"), layer.locked);
        entry.insert(QStringLiteral("opacity"), layer.opacity);
        entry.insert(QStringLiteral("mode"), layer.mode);
        entry.insert(QStringLiteral("storage"), layer.storage);
        entry.insert(QStringLiteral("shared"), layer.storage == QLatin1String("shared"));
        entry.insert(QStringLiteral("animated"), layer.storage == QLatin1String("animated"));
        entry.insert(QStringLiteral("active"), layer.id == m_activeLayerId);
        out.append(entry);
    }
    return out;
}

QString DocumentModel::activeLayerName() const
{
    const Layer *layer = m_document.layerById(m_activeLayerId);
    return layer ? layer->name : QString();
}

bool DocumentModel::activeLayerLocked() const
{
    const Layer *layer = activeLayer();
    return layer && layer->locked;
}

QString DocumentModel::activeLayerStorage() const
{
    const Layer *layer = activeLayer();
    return layer ? layer->storage : QString();
}

void DocumentModel::setActiveLayerId(const QString &id)
{
    if (id == m_activeLayerId || !m_document.layerById(id))
        return;
    m_activeLayerId = id;
    emit viewChanged();
    emit activeLayerChanged();
}

void DocumentModel::setEditScope(const QString &scope)
{
    const QString normalized = scope == QLatin1String("all-frames")
                                   ? QStringLiteral("all-frames")
                                   : QStringLiteral("frame");
    if (m_editScope == normalized)
        return;
    m_editScope = normalized;
    emit viewChanged();
    emit activeLayerChanged();
}

void DocumentModel::setPickerScope(const QString &scope)
{
    const QString normalized = scope == QLatin1String("composite")
                                   ? QStringLiteral("composite")
                                   : QStringLiteral("active");
    if (m_pickerScope == normalized)
        return;
    m_pickerScope = normalized;
    emit viewChanged();
}

const Layer *DocumentModel::activeLayer() const
{
    return m_document.layerById(m_activeLayerId);
}

void DocumentModel::reportError(const QString &error)
{
    if (error.startsWith(QStringLiteral("E_LAYER_LOCKED"))) {
        QString id = m_activeLayerId;
        const QRegularExpression match(QStringLiteral("--layer=([^ ]+)"));
        const QRegularExpressionMatch found = match.match(error);
        if (found.hasMatch())
            id = found.captured(1);
        const Layer *layer = m_document.layerById(id);
        const QString label = layer ? layer->name : id;
        say(Strings::shared().t(QStringLiteral("note.layerLocked")).arg(label));
    } else if (!error.isEmpty()) {
        say(error);
    }
}

void DocumentModel::emitRasterChanged()
{
    const Layer *layer = activeLayer();
    if (m_editScope == QLatin1String("all-frames")
        || (layer && layer->storage == QLatin1String("shared")))
        emit renderChanged(QString(), -1);
    else
        emit renderChanged(m_clip, m_frame);
}

bool DocumentModel::commitLayerChange(const Document &before, const QString &error,
                                      const QString &previousActiveId, bool notifyActive)
{
    if (!error.isEmpty()) {
        reportError(error);
        return false;
    }
    if (m_document == before)
        return false;
    remember(before);
    m_dirty = true;
    emit changed();
    const QString oldId = previousActiveId.isEmpty() ? m_activeLayerId : previousActiveId;
    const Layer *oldActive = before.layerById(oldId);
    const Layer *newActive = m_document.layerById(m_activeLayerId);
    if (notifyActive && (oldId != m_activeLayerId || !oldActive || !newActive
        || oldActive->name != newActive->name
        || oldActive->locked != newActive->locked
        || oldActive->storage != newActive->storage))
        emit activeLayerChanged();
    emitRasterChanged();
    emit fileChanged();
    return true;
}

void DocumentModel::preserveActiveLayer(const Document &before)
{
    if (m_document.layerById(m_activeLayerId))
        return;
    const int oldIndex = before.indexOfLayerId(m_activeLayerId);
    if (!m_document.layers().isEmpty()) {
        const int fallback = oldIndex < 0
                                 ? 0
                                 : qBound(0, oldIndex, m_document.layers().size() - 1);
        m_activeLayerId = m_document.layers().at(fallback).id;
    } else {
        m_activeLayerId.clear();
    }
}

bool DocumentModel::addLayer(const QString &id, const QString &name,
                             const QString &storage)
{
    const Document before = m_document;
    QString error;
    if (!m_document.addLayer(id, name, storage))
        error = QStringLiteral("E_LAYER_IDENTITY: invalid or duplicate layer identity");
    return commitLayerChange(before, error);
}

bool DocumentModel::removeLayer(const QString &id)
{
    const QString target = id.isEmpty() ? m_activeLayerId : id;
    const Document before = m_document;
    QString error;
    if (!m_document.removeLayer(target, &error)) {
        reportError(error);
        return false;
    }
    const QString previousActiveId = m_activeLayerId;
    const bool changed = commitLayerChange(before, QString(), QString(), false);
    preserveActiveLayer(before);
    if (changed && previousActiveId != m_activeLayerId)
        emit activeLayerChanged();
    if (changed)
        emit viewChanged();
    return changed;
}

bool DocumentModel::renameLayer(const QString &id, const QString &name)
{
    const Document before = m_document;
    QString error;
    if (!m_document.renameLayer(id, name, &error))
        return reportError(error), false;
    return commitLayerChange(before);
}

bool DocumentModel::moveLayer(const QString &id, int index)
{
    const Document before = m_document;
    QString error;
    if (!m_document.moveLayer(id, index, &error))
        return reportError(error), false;
    return commitLayerChange(before);
}

bool DocumentModel::duplicateLayer(const QString &id, const QString &newId,
                                   const QString &name)
{
    const Document before = m_document;
    QString error;
    if (!m_document.duplicateLayer(id, newId, name, &error))
        return reportError(error), false;
    return commitLayerChange(before);
}

bool DocumentModel::setLayerVisible(const QString &id, bool visible)
{
    const Document before = m_document;
    QString error;
    if (!m_document.setLayerVisible(id, visible, &error))
        return reportError(error), false;
    return commitLayerChange(before);
}

bool DocumentModel::setLayerLocked(const QString &id, bool locked)
{
    const Document before = m_document;
    QString error;
    if (!m_document.setLayerLocked(id, locked, &error))
        return reportError(error), false;
    return commitLayerChange(before);
}

bool DocumentModel::setLayerOpacity(const QString &id, int opacity)
{
    const Document before = m_document;
    QString error;
    if (!m_document.setLayerOpacity(id, opacity, &error))
        return reportError(error), false;
    return commitLayerChange(before);
}

bool DocumentModel::setLayerMode(const QString &id, const QString &mode)
{
    const Document before = m_document;
    QString error;
    if (!m_document.setLayerMode(id, mode, &error))
        return reportError(error), false;
    return commitLayerChange(before);
}

bool DocumentModel::setLayerStorage(const QString &id, const QString &storage,
                                    bool anyway)
{
    const Document before = m_document;
    QString error;
    if (!m_document.setLayerStorage(id, storage, nullptr, &error, anyway))
        return reportError(error), false;
    return commitLayerChange(before);
}

bool DocumentModel::clearLayer(const QString &id, bool allFrames)
{
    const Document before = m_document;
    int changedPixels = 0;
    QString error;
    if (!m_document.editLayer(id, m_clip, m_frame,
                              allFrames ? EditScope::AllFrames
                                        : EditScope::CurrentFrame,
                              [](Grid &grid) { ops::clear(grid); },
                              &changedPixels, &error)) {
        reportError(error);
        return false;
    }
    return changedPixels > 0 && commitLayerChange(before);
}

bool DocumentModel::setLayersVisible(const QStringList &ids, bool visible)
{
    const Document before = m_document;
    for (const QString &id : ids) {
        QString error;
        if (!m_document.setLayerVisible(id, visible, &error)) {
            reportError(error);
            m_document = before;
            return false;
        }
    }
    return commitLayerChange(before);
}

bool DocumentModel::setLayersLocked(const QStringList &ids, bool locked)
{
    const Document before = m_document;
    for (const QString &id : ids) {
        QString error;
        if (!m_document.setLayerLocked(id, locked, &error)) {
            reportError(error);
            m_document = before;
            return false;
        }
    }
    return commitLayerChange(before);
}

bool DocumentModel::removeLayers(const QStringList &ids)
{
    const Document before = m_document;
    QString error;
    if (!m_document.removeLayers(ids, &error)) {
        reportError(error);
        return false;
    }
    const QString previousActiveId = m_activeLayerId;
    const bool changed = commitLayerChange(before, QString(), QString(), false);
    preserveActiveLayer(before);
    if (changed && previousActiveId != m_activeLayerId)
        emit activeLayerChanged();
    if (changed) {
        emit viewChanged();
    }
    return changed;
}

bool DocumentModel::moveLayers(const QStringList &ids, int index)
{
    const Document before = m_document;
    QString error;
    if (!m_document.moveLayers(ids, index, &error)) {
        reportError(error);
        return false;
    }
    return commitLayerChange(before);
}

QString DocumentModel::nextLayerId() const
{
    QString id = QStringLiteral("layer-%1").arg(m_document.layers().size() + 1);
    int suffix = 2;
    while (m_document.layerById(id))
        id = QStringLiteral("layer-%1-%2").arg(m_document.layers().size() + 1).arg(suffix++);
    return id;
}

QString DocumentModel::nextLayerName() const
{
    int number = m_document.layers().size() + 1;
    QString name;
    do {
        name = QStringLiteral("Layer %1").arg(number++);
    } while (m_document.layerByName(name));
    return name;
}

QVariantMap DocumentModel::layerStoragePreview(const QString &id,
                                               const QString &storage) const
{
    Document staged = m_document;
    int lost = 0;
    QString error;
    const bool ok = staged.convertLayerStorage(id, storage, &lost, &error);
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("lost"), lost},
            {QStringLiteral("error"), error},
            {QStringLiteral("id"), id}, {QStringLiteral("storage"), storage}};
}

QVariantMap DocumentModel::mergeDownPreview(const QString &id) const
{
    const LayerOperationResult result = previewMergeDown(m_document, id);
    QVariantMap out = layerReport(result.report);
    out.insert(QStringLiteral("ok"), result.ok);
    out.insert(QStringLiteral("error"), result.error);
    return out;
}

bool DocumentModel::mergeDown(const QString &id)
{
    const Document before = m_document;
    const QString previousActiveId = m_activeLayerId;
    const int sourceIndex = m_document.indexOfLayerId(id);
    if (sourceIndex <= 0)
        return reportError(QStringLiteral("E_LAYER_TARGET: source layer has no layer below")), false;
    const QString targetId = m_document.layers().at(sourceIndex - 1).id;
    LayerOperationReport report;
    QString error;
    if (!applyMergeDown(&m_document, id, &report, &error)) {
        reportError(error);
        return false;
    }
    if (m_activeLayerId == id)
        m_activeLayerId = targetId;
    const bool changed = commitLayerChange(before, QString(), previousActiveId);
    if (changed) {
        say(Strings::shared().t(QStringLiteral("note.layerMerged"))
                .arg(report.affectedPixels).arg(report.removedLayers));
        emit viewChanged();
    }
    return changed;
}

QVariantMap DocumentModel::flattenPreview() const
{
    const LayerOperationResult result = previewFlattenVisible(m_document);
    QVariantMap out = layerReport(result.report);
    out.insert(QStringLiteral("ok"), result.ok);
    out.insert(QStringLiteral("error"), result.error);
    return out;
}

bool DocumentModel::flatten()
{
    const Document before = m_document;
    const QString previousActiveId = m_activeLayerId;
    LayerOperationReport report;
    QString error;
    if (!applyFlattenVisible(&m_document, &report, &error)) {
        reportError(error);
        return false;
    }
    m_activeLayerId = m_document.layers().value(0).id;
    const bool changed = commitLayerChange(before, QString(), previousActiveId);
    if (changed) {
        say(Strings::shared().t(QStringLiteral("note.layerFlattened"))
                .arg(report.affectedPixels).arg(report.removedLayers));
        emit viewChanged();
    }
    return changed;
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

bool DocumentModel::copySelection()
{
    if (!m_selection.isValid())
        return false;

    const qint64 width = m_selection.width();
    const qint64 height = m_selection.height();
    const qint64 cells = width * height;
    // A clipboard payload is JSON colour strings, not one byte per cell. Keep
    // the estimate conservative so a large selection is rejected before Qt
    // serializes or materializes it in the desktop clipboard.
    if (width > Document::maxClipboardColumns
        || height > Document::maxClipboardRows
        || cells > Document::maxClipboardCells
        || cells > (Document::maxClipboardBytes - 32) / 12) {
        say(Strings::shared().t(QStringLiteral("note.invalidPixelClipboard")));
        return false;
    }

    const Grid grid = m_document.cel(m_activeLayerId,
                                     m_document.clip(m_clip)->id, m_frame);
    QJsonArray rows;
    for (int y = m_selection.top(); y <= m_selection.bottom(); ++y) {
        QJsonArray row;
        for (int x = m_selection.left(); x <= m_selection.right(); ++x) {
            const QChar slot = grid.at(x, y);
            if (slot == Grid::Empty) {
                row.append(QJsonValue::Null);
            } else {
                row.append(m_document.palette().colour(slot)
                               .name(QColor::HexRgb).toUpper());
            }
        }
        rows.append(row);
    }

    const QByteArray encoded = QJsonDocument(rows).toJson(QJsonDocument::Indented);
    if (encoded.size() > Document::maxClipboardBytes) {
        say(Strings::shared().t(QStringLiteral("note.invalidPixelClipboard")));
        return false;
    }
    // Qt's clipboard API materializes the selected MIME payload here; there is
    // no size-only query in QClipboard/QMimeData. The pre-serialization bound
    // above is therefore the last enforceable guard before that API call.
    QGuiApplication::clipboard()->setText(QString::fromUtf8(encoded));
    say(Strings::shared().t(QStringLiteral("note.copiedPixels"))
            .arg(m_selection.width()).arg(m_selection.height()));
    return true;
}

bool DocumentModel::pastePixels(int x, int y)
{
    const QByteArray clipboardBytes =
        QGuiApplication::clipboard()->mimeData()->data(QStringLiteral("text/plain"));
    if (clipboardBytes.size() > Document::maxClipboardBytes) {
        say(Strings::shared().t(QStringLiteral("note.invalidPixelClipboard")));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(clipboardBytes, &parseError);
    const QJsonArray rows = json.array();
    if (parseError.error != QJsonParseError::NoError || !json.isArray()
        || rows.isEmpty() || !rows.first().isArray()
        || rows.first().toArray().isEmpty()) {
        say(Strings::shared().t(QStringLiteral("note.invalidPixelClipboard")));
        return false;
    }

    const int width = rows.first().toArray().size();
    if (rows.size() > Document::maxClipboardRows
        || width > Document::maxClipboardColumns
        || qint64(rows.size()) * width > Document::maxClipboardCells) {
        say(Strings::shared().t(QStringLiteral("note.invalidPixelClipboard")));
        return false;
    }
    const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    QList<QList<QColor>> pixels;
    pixels.reserve(rows.size());
    for (const QJsonValue &rowValue : rows) {
        if (!rowValue.isArray() || rowValue.toArray().size() != width) {
            say(Strings::shared().t(QStringLiteral("note.invalidPixelClipboard")));
            return false;
        }
        QList<QColor> row;
        row.reserve(width);
        for (const QJsonValue &pixel : rowValue.toArray()) {
            if (pixel.isNull()) {
                row.append(QColor());
            } else if (pixel.isString() && hex.match(pixel.toString()).hasMatch()) {
                row.append(QColor(pixel.toString()));
            } else {
                say(Strings::shared().t(QStringLiteral("note.invalidPixelClipboard")));
                return false;
            }
        }
        pixels.append(row);
    }

    const int left = qBound(0, x, m_document.columns() - 1);
    const int top = qBound(0, y, m_document.rows() - 1);
    const int pastedWidth = qMin(width, m_document.columns() - left);
    const int pastedHeight = qMin(rows.size(), m_document.rows() - top);

    Document next = m_document;
    QHash<QRgb, QChar> slotsByColour;
    for (const Palette::Slot &slot : next.palette().entries())
        slotsByColour.insert(slot.colour.rgb(), slot.letter);

    bool paletteFull = false;
    const auto paste = [&](Grid &grid) {
        for (int py = 0; py < pastedHeight; ++py) {
            for (int px = 0; px < pastedWidth; ++px) {
                const QColor colour = pixels.at(py).at(px);
                QChar slot = Grid::Empty;
                if (colour.isValid()) {
                    const auto known = slotsByColour.constFind(colour.rgb());
                    if (known != slotsByColour.constEnd()) {
                        slot = known.value();
                    } else {
                        const QString fresh = freeSlotIn(next.palette());
                        if (fresh.isEmpty()) {
                            paletteFull = true;
                            return;
                        }
                        slot = fresh.at(0);
                        next.palette().set(slot, colour);
                        slotsByColour.insert(colour.rgb(), slot);
                    }
                }
                grid.set(left + px, top + py, slot);
            }
        }
    };

    const Document before = m_document;
    int changedPixels = 0;
    QString error;
    if (!next.editLayer(m_activeLayerId, m_clip, m_frame,
                        m_editScope == QLatin1String("all-frames")
                            ? EditScope::AllFrames
                            : EditScope::CurrentFrame,
                        paste, &changedPixels, &error)) {
        reportError(error);
        return false;
    }
    if (paletteFull) {
        say(Strings::shared().t(QStringLiteral("note.paletteFull")));
        return false;
    }

    const bool paletteChanged = next.palette().size() != before.palette().size();
    if (!(next == before) && changedPixels > 0) {
        remember(before);
        m_document = next;
        m_dirty = true;
        if (paletteChanged)
            paletteMoved();
        emit changed();
        emitRasterChanged();
        emit fileChanged();
    }
    setSelection(left, top, left + pastedWidth - 1, top + pastedHeight - 1);
    const bool cropped = pastedWidth != width || pastedHeight != rows.size();
    say(Strings::shared().t(cropped ? QStringLiteral("note.pastedPixelsCropped")
                                   : QStringLiteral("note.pastedPixels"))
            .arg(pastedWidth).arg(pastedHeight).arg(left).arg(top));
    return true;
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
    m_undoActiveLayerIds.append(m_activeLayerId);
    while (m_undo.size() > historyDepth())
        m_undo.removeFirst();
    while (m_undoActiveLayerIds.size() > m_undo.size())
        m_undoActiveLayerIds.removeFirst();
    m_redo.clear();
    m_redoActiveLayerIds.clear();
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
    const QString oldId = m_activeLayerId;
    const Layer *oldLayer = m_document.layerById(oldId);
    if (!m_document.clip(m_clip))
        m_clip = m_document.clipNames().value(0);
    m_frame = qBound(0, m_frame, qMax(0, frameCount() - 1));
    if (!m_document.layerById(m_activeLayerId))
        m_activeLayerId = m_document.layers().value(0).id;
    const Layer *newLayer = activeLayer();
    if (oldId != m_activeLayerId
        || (oldLayer && newLayer
            && (oldLayer->name != newLayer->name
                || oldLayer->locked != newLayer->locked
                || oldLayer->storage != newLayer->storage)))
        emit activeLayerChanged();
}

void DocumentModel::undo()
{
    if (m_undo.isEmpty()) {
        say(Strings::shared().t(QStringLiteral("note.nothingToUndo")));
        return;
    }
    m_lastChangeExternal = false;
    m_redo.append(m_document);
    m_redoActiveLayerIds.append(m_activeLayerId);
    m_document = m_undo.takeLast();
    m_activeLayerId = m_undoActiveLayerIds.takeLast();
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
    m_undoActiveLayerIds.append(m_activeLayerId);
    m_document = m_redo.takeLast();
    m_activeLayerId = m_redoActiveLayerIds.takeLast();
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
    if (!activeLayer())
        return;
    const Document before = m_document;
    int changedPixels = 0;
    QString error;
    const EditScope scope = m_editScope == QLatin1String("all-frames")
                                ? EditScope::AllFrames
                                : EditScope::CurrentFrame;
    if (!m_document.editLayer(m_activeLayerId, m_clip, m_frame, scope, edit,
                              &changedPixels, &error)) {
        reportError(error);
        return;
    }
    if (changedPixels == 0)
        return;
    // Snapshot taken here rather than when the stroke opened: a stroke that
    // never changes a pixel -- a click that missed, a bucket on its own colour
    // -- should not land an entry that undo then has to step through.
    remember(before);
    m_dirty = true;
    emit changed();
    emitRasterChanged();
    emit fileChanged();
}

QString DocumentModel::slotAt(int x, int y) const
{
    const Clip *clip = m_document.clip(m_clip);
    if (!activeLayer() || !clip)
        return QStringLiteral(".");
    const Grid grid = m_document.cel(m_activeLayerId, clip->id, m_frame);
    return QString(grid.at(x, y));
}

QString DocumentModel::compositeSlotAt(int x, int y) const
{
    const Grid grid = render::toGrid(m_document, m_clip, m_frame);
    return QString(grid.at(x, y));
}

QString DocumentModel::pickSlot(int x, int y, bool composite) const
{
    return composite ? compositeSlotAt(x, y) : slotAt(x, y);
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
        const Clip *clip = m_document.clip(m_clip);
        if (!clip)
            return found;
        for (const Layer &layer : m_document.layers())
            tally(m_document.cel(layer.id, clip->id, m_frame));
        return found;
    }

    for (const Layer &layer : m_document.layers())
        for (const Clip &clip : m_document.clips())
            for (int frame = 0; frame < clip.frameCount; ++frame)
                tally(m_document.cel(layer.id, clip.id, frame));
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
    if (!m_document.palette().colour(to).isValid()) {
        QString paletteError;
        if (!m_document.setPaletteColour(to, colour, &paletteError)) {
            reportError(paletteError);
            return;
        }
    }
    const qint64 moved = everywhere
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
    return freeSlotIn(m_document.palette());
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
    const Clip *clip = m_document.clip(m_clip);
    if (!activeLayer() || !clip)
        return QColor();
    const Grid grid = m_document.cel(m_activeLayerId, clip->id, m_frame);
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
    QString error;
    if (!m_document.addClip(name, 8, &error)) {
        if (error.isEmpty())
            say(Strings::shared().t(QStringLiteral("note.clipExists")).arg(name));
        else
            reportError(error);
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
    QString error;
    if (!m_document.removeClip(name, &error)) {
        reportError(error);
        return;
    }
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
    QString error;
    if (!m_document.addFrame(m_clip, m_frame, duplicate, &error)) {
        reportError(error);
        return;
    }
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
    QString error;
    if (!m_document.removeFrame(m_clip, m_frame, &error)) {
        reportError(error);
        return;
    }
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
    QString error;
    if (!m_document.moveFrame(m_clip, m_frame, m_frame + step, &error)) {
        reportError(error);
        return;
    }
    remember(before);
    m_frame += step;
    m_dirty = true;
    emit changed();
    emit renderChanged(QString(), -1);
    emit viewChanged();
    emit fileChanged();
}

qint64 DocumentModel::wouldLose(int columns, int rows) const
{
    return m_document.wouldLose(columns, rows);
}

void DocumentModel::resize(int columns, int rows)
{
    clearSelection();
    const Document before = m_document;
    QString error;
    if (!m_document.resize(columns, rows, &error)) {
        reportError(error);
        return;
    }
    remember(before);
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

    const qint64 lost = m_document.wouldLoseOutside(bounds);
    if (lost > 0 && !anyway) {
        say(Strings::shared().t(QStringLiteral("note.trimWouldCrop")).arg(lost));
        return false;
    }

    const Document before = m_document;
    QString cropError;
    if (!m_document.crop(bounds, &cropError)) {
        if (!cropError.isEmpty())
            say(cropError);
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
    m_activeLayerId = m_document.layers().value(0).id;
    m_editScope = QStringLiteral("frame");
    m_pickerScope = QStringLiteral("active");
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
    emit activeLayerChanged();
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
    const Document before = m_document;
    QString error;
    if (!m_document.setPaletteColour(slot.at(0), parsed, &error)) {
        reportError(error);
        return;
    }
    remember(before);
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
    const QString where = localPath(path);

    const Codec::Result read = Codec::readFile(where, warningLimits());
    if (!read) {
        say(read.error);
        return false;
    }
    // The document has a name now; its scratch backing is a liability, not
    // an address.
    retireScratch();
    const Document before = m_document;
    m_document = read.document;
    m_undo.clear();
    m_redo.clear();
    m_undoActiveLayerIds.clear();
    m_redoActiveLayerIds.clear();
    emit historyChanged();
    m_clip = m_document.clipNames().value(0);
    m_frame = 0;
    preserveActiveLayer(before);
    m_editScope = QStringLiteral("frame");
    m_pickerScope = QStringLiteral("active");
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
    emit activeLayerChanged();
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
        m_redoActiveLayerIds.clear();
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
    const QString where = localPath(path.isEmpty() ? m_path : path);
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
    const QString where = localPath(path);
    if (where.isEmpty()) {
        say(Strings::shared().t(QStringLiteral("note.sayWhereToExport")));
        return false;
    }

    render::Options options;
    options.scale = scale;
    options.sheet = sheet;
    options.checker = checker;
    options.warningPixels = renderWarningPixels();
    QString warning;
    QString error;
    if (!output::validate(where, {followedPath()}, &error)) {
        say(error);
        return false;
    }
    const QImage image =
        render::toImage(m_document, m_clip, m_frame, options, &warning, &error);
    QBuffer encoded;
    encoded.open(QIODevice::WriteOnly);
    if (!image.isNull() && image.save(&encoded, "PNG")
        && output::writeAtomically(where, encoded.data(), {followedPath()}, &error)) {
        QString note = Strings::shared().t(QStringLiteral("note.exported"))
                           .arg(image.width())
                           .arg(image.height())
                           .arg(QFileInfo(where).fileName());
        if (!warning.isEmpty())
            note += QStringLiteral(" · warning: ") + warning;
        say(note);
        return true;
    } else {
        say(error.isEmpty()
                ? Strings::shared().t(QStringLiteral("note.couldNotWrite")).arg(where)
                : error);
        return false;
    }
}

bool DocumentModel::importImage(const QString &path, const QString &destination,
                                int scale, int width, int height,
                                const QString &fit, const QString &layerName)
{
    const QString where = localPath(path);
    if (where.isEmpty()) {
        say(QStringLiteral("image import path is empty"));
        return false;
    }

    ImageImport::Options options;
    QString error;
    if (!importOptions(fit, scale, width, height, &options, &error)) {
        say(error);
        return false;
    }
    options.layerName = layerName;

    if (destination == QLatin1String("document")) {
        const ImageImport::Result result = ImageImport::createDocument(where, options);
        if (!result) {
            say(result.error);
            return false;
        }

        retireScratch();
        m_document = result.document;
        m_undo.clear();
        m_redo.clear();
        m_undoActiveLayerIds.clear();
        m_redoActiveLayerIds.clear();
        emit historyChanged();
        m_clip = m_document.clipNames().value(0);
        m_frame = 0;
        m_activeLayerId = result.report.layerId;
        if (m_activeLayerId.isEmpty())
            m_activeLayerId = m_document.layers().value(0).id;
        m_editScope = QStringLiteral("frame");
        m_pickerScope = QStringLiteral("active");
        m_path.clear();
        m_dirty = true;
        m_reloadPending = false;
        m_lastChangeExternal = false;
        watch();
        openScratch();
        paletteMoved();
        m_changes->sync();
        say(QStringLiteral("imported %1").arg(QFileInfo(where).fileName()));
        emit changed();
        emit activeLayerChanged();
        emit renderChanged(QString(), -1);
        emit viewChanged();
        emit fileChanged();
        emit documentReplaced();
        return true;
    }

    if (destination != QLatin1String("layer")) {
        say(QStringLiteral("image import destination must be document or layer"));
        return false;
    }

    options.clip = m_clip;
    options.frame = m_frame;
    const Document before = m_document;
    Document next = m_document;
    ImageImport::Report report;
    if (!ImageImport::addLayer(&next, where, options, &report, &error)) {
        say(error);
        return false;
    }

    const bool paletteChanged = !samePalette(next.palette(), before.palette());
    remember(before);
    m_document = next;
    m_activeLayerId = report.layerId;
    m_dirty = true;
    if (paletteChanged)
        paletteMoved();
    QString note = QStringLiteral("imported %1 as %2")
                       .arg(QFileInfo(where).fileName(), activeLayerName());
    if (report.clippedPixels > 0)
        note += QStringLiteral(" · %1 pixel(s) clipped at the canvas edge")
                    .arg(report.clippedPixels);
    say(note);
    emit changed();
    emit activeLayerChanged();
    emit renderChanged(QString(), -1);
    emit viewChanged();
    emit fileChanged();
    return true;
}

bool DocumentModel::importImageInNewWindow(const QString &path, int scale,
                                           int width, int height,
                                           const QString &fit)
{
    const QString where = localPath(path);
    if (where.isEmpty()) {
        say(QStringLiteral("image import path is empty"));
        return false;
    }

    ImageImport::Options options;
    QString error;
    if (!importOptions(fit, scale, width, height, &options, &error)) {
        say(error);
        return false;
    }

    QStringList arguments{QStringLiteral("--import-image"), where};
    if (scale > 0) {
        arguments << QStringLiteral("--import-scale") << QString::number(scale);
    } else {
        arguments << QStringLiteral("--import-resolution")
                  << QStringLiteral("%1x%2").arg(width).arg(height);
    }
    arguments << QStringLiteral("--import-fit") << fit.toLower();
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments)) {
        say(QStringLiteral("could not open imported image in a new window"));
        return false;
    }
    return true;
}

bool DocumentModel::exportGif(const QString &path, int scale, int fps, bool loop)
{
    const QString where = localPath(path);
    if (where.isEmpty()) {
        say(Strings::shared().t(QStringLiteral("note.sayWhereToExport")));
        return false;
    }

    QString error;
    if (!output::validate(where, {followedPath()}, &error)) {
        say(error);
        return false;
    }
    if (!gif::write(m_document, m_clip, where, scale, fps, loop,
                    {followedPath()}, &error)) {
        say(error);
        return false;
    }
    say(QStringLiteral("exported GIF to %1").arg(QFileInfo(where).fileName()));
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

QString DocumentModel::suggestedGifPath() const
{
    const QString stem = m_path.isEmpty()
                             ? QStringLiteral("untitled")
                             : QFileInfo(m_path).completeBaseName();
    const QString directory = m_path.isEmpty() ? QDir::currentPath()
                                               : QFileInfo(m_path).absolutePath();
    return QUrl::fromLocalFile(QStringLiteral("%1/%2.gif").arg(directory, stem))
        .toString();
}

} // namespace omapixel
