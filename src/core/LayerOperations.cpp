#include "LayerOperations.h"

#include "Render.h"

namespace omapixel {
namespace {

void addQuantization(LayerOperationReport *total,
                     const render::QuantizationReport &report)
{
    total->exactMatches += report.exactMatches;
    total->approximatedPixels += report.approximatedPixels;
    total->newSlots += report.newSlots;
    total->diagnostics.append(report.diagnostics);
}

qint64 differingPixels(const Grid &left, const Grid &right)
{
    if (left.columns() != right.columns() || left.rows() != right.rows())
        return qint64(left.columns()) * left.rows()
            + qint64(right.columns()) * right.rows();

    qint64 differences = 0;
    for (int y = 0; y < left.rows(); ++y)
        for (int x = 0; x < left.columns(); ++x)
            if (left.at(x, y) != right.at(x, y))
                ++differences;
    return differences;
}

bool validGrid(const Grid &grid, const Document &document)
{
    return grid.columns() == document.columns() && grid.rows() == document.rows();
}

Document pairForMerge(const Document &source, const Layer &target,
                      const Layer &overlay)
{
    Document pair = source;
    pair.layers().clear();
    pair.layers().append(target);
    pair.layers().append(overlay);
    return pair;
}

bool addFrameGrid(Document *document, Layer *layer, const Clip &clip, int frame,
                  const Grid &grid, QString *error)
{
    if (!validGrid(grid, *document)) {
        if (error)
            *error = QStringLiteral("E_LAYER_COMPOSE: invalid result for %1 frame %2")
                         .arg(clip.id).arg(frame);
        return false;
    }
    if (layer->storage == QStringLiteral("shared")) {
        if (layer->cels.isEmpty())
            layer->cels.append({QString(), -1, grid});
        else
            layer->cels.first().grid = grid;
        return true;
    }
    for (Cel &cel : layer->cels) {
        if (cel.clip == clip.id && cel.frame == frame) {
            cel.grid = grid;
            return true;
        }
    }
    if (error)
        *error = QStringLiteral("E_LAYER_COMPOSE: missing result cel for %1 frame %2")
                     .arg(clip.id).arg(frame);
    return false;
}

void normalizeFlattenedLayer(Layer *layer)
{
    layer->visible = true;
    layer->locked = false;
    layer->opacity = 255;
    layer->mode = QStringLiteral("normal");
}

QString nextFlattenedId(const Document &document)
{
    QString id = QStringLiteral("flattened");
    int suffix = 2;
    while (document.layerById(id))
        id = QStringLiteral("flattened-%1").arg(suffix++);
    return id;
}

QString flattenedIdFor(const Document &document)
{
    if (document.layers().size() == 1
        && document.layers().first().id.startsWith(QStringLiteral("flattened")))
        return document.layers().first().id;
    return nextFlattenedId(document);
}

} // namespace

LayerOperationResult previewMergeDown(const Document &source,
                                      const QString &sourceLayer)
{
    LayerOperationResult result;
    const int sourceIndex = source.indexOfLayerId(sourceLayer) >= 0
                                ? source.indexOfLayerId(sourceLayer)
                                : source.indexOfLayerName(sourceLayer);
    if (sourceIndex <= 0) {
        result.error = QStringLiteral(
            "E_LAYER_TARGET: source layer must have a layer immediately below it");
        return result;
    }

    const Layer &overlay = source.layers().at(sourceIndex);
    const Layer &target = source.layers().at(sourceIndex - 1);
    if (!overlay.visible) {
        result.error = QStringLiteral("E_LAYER_VISIBILITY: hidden source layer %1")
                           .arg(overlay.id);
        return result;
    }
    if (overlay.locked || target.locked) {
        result.error = QStringLiteral("E_LAYER_LOCKED: --layer=%1")
                           .arg(overlay.locked ? overlay.id : target.id);
        return result;
    }
    if (!target.visible) {
        result.error = QStringLiteral(
            "E_LAYER_VISIBILITY: visible source %1 would be removed into hidden target %2")
                           .arg(overlay.id, target.id);
        return result;
    }

    const QString storage = target.storage == QStringLiteral("shared")
                                 && overlay.storage == QStringLiteral("shared")
                             ? QStringLiteral("shared")
                             : QStringLiteral("animated");
    Document pair = pairForMerge(source, target, overlay);
    Document merged = source;
    Layer mergedLayer = target;
    mergedLayer.storage = storage;
    mergedLayer.cels.clear();
    const bool targetVisible = target.visible;
    normalizeFlattenedLayer(&mergedLayer);
    mergedLayer.visible = targetVisible;
    if (storage == QStringLiteral("shared"))
        mergedLayer.cels.append({QString(), -1,
                                 Grid(source.columns(), source.rows())});
    else {
        for (const Clip &clip : source.clips())
            for (int frame = 0; frame < clip.frameCount; ++frame)
                mergedLayer.cels.append({clip.id, frame,
                                         Grid(source.columns(), source.rows())});
    }

    for (const Clip &clip : source.clips()) {
        for (int frame = 0; frame < clip.frameCount; ++frame) {
            render::QuantizationReport quantization;
            QStringList diagnostics;
            const Grid grid = render::toGrid(
                pair, clip.id, frame, render::Options(), &diagnostics, &quantization);
            if (!validGrid(grid, source)) {
                result.error = QStringLiteral("E_LAYER_COMPOSE: could not merge %1")
                                   .arg(overlay.id);
                return result;
            }
            result.report.frames += 1;
            addQuantization(&result.report, quantization);
            result.report.affectedPixels += differingPixels(
                source.cel(target.id, clip.id, frame), grid);
            if (!addFrameGrid(&merged, &mergedLayer, clip, frame, grid,
                              &result.error))
                return result;
        }
    }

    merged.layers()[sourceIndex - 1] = mergedLayer;
    merged.layers().removeAt(sourceIndex);
    result.document = merged;
    result.report.removedLayers = 1;
    result.ok = true;
    return result;
}

bool applyMergeDown(Document *document, const QString &sourceLayer,
                    LayerOperationReport *report, QString *error)
{
    if (!document) {
        if (error)
            *error = QStringLiteral("E_LAYER_TARGET: no document");
        return false;
    }
    const LayerOperationResult staged = previewMergeDown(*document, sourceLayer);
    if (!staged) {
        if (error)
            *error = staged.error;
        return false;
    }
    *document = staged.document;
    if (report)
        *report = staged.report;
    return true;
}

LayerOperationResult flattenVisible(const Document &source, bool anyway)
{
    LayerOperationResult result;
    Q_UNUSED(anyway);
    for (const Layer &layer : source.layers()) {
        if (layer.locked) {
            result.error = QStringLiteral(
                "E_LAYER_LOCKED: flatten would remove or change locked layer %1")
                               .arg(layer.id);
            return result;
        }
    }
    Document composed = source;
    QList<Layer> visible;
    bool allShared = true;
    for (const Layer &layer : source.layers()) {
        if (!layer.visible)
            continue;
        visible.append(layer);
        allShared = allShared && layer.storage == QStringLiteral("shared");
    }
    composed.layers() = visible;

    const QString storage = allShared ? QStringLiteral("shared")
                                      : QStringLiteral("animated");
    Document flattened = source;
    flattened.layers().clear();
    const QString flattenedId = flattenedIdFor(source);
    const QString flattenedName = source.layers().size() == 1
                                      && source.layers().first().id == flattenedId
                                  ? source.layers().first().name
                                  : QStringLiteral("Flattened");
    if (!flattened.addLayer(flattenedId, flattenedName,
                            storage)) {
        result.error = QStringLiteral("E_LAYER_COMPOSE: could not create flattened layer");
        return result;
    }
    Layer *flattenedLayer = flattened.layerById(flattenedId);
    normalizeFlattenedLayer(flattenedLayer);

    for (const Clip &clip : source.clips()) {
        for (int frame = 0; frame < clip.frameCount; ++frame) {
            render::QuantizationReport quantization;
            QStringList diagnostics;
            const Grid grid = render::toGrid(composed, clip.id, frame,
                                             render::Options(), &diagnostics,
                                             &quantization);
            if (!validGrid(grid, source)) {
                result.error = QStringLiteral("E_LAYER_COMPOSE: could not flatten %1")
                                   .arg(clip.id);
                return result;
            }
            result.report.frames += 1;
            addQuantization(&result.report, quantization);
            result.report.affectedPixels += grid.drawnCount();
            if (!addFrameGrid(&flattened, flattenedLayer, clip, frame, grid,
                              &result.error))
                return result;
        }
    }

    result.document = flattened;
    result.report.removedLayers = qMax(0, source.layers().size() - 1);
    result.ok = true;
    return result;
}

LayerOperationResult previewFlattenVisible(const Document &source, bool anyway)
{
    return flattenVisible(source, anyway);
}

bool applyFlattenVisible(Document *document, LayerOperationReport *report,
                         QString *error)
{
    if (!document) {
        if (error)
            *error = QStringLiteral("E_LAYER_TARGET: no document");
        return false;
    }
    const LayerOperationResult staged = flattenVisible(*document);
    if (!staged) {
        if (error)
            *error = staged.error;
        return false;
    }
    *document = staged.document;
    if (report)
        *report = staged.report;
    return true;
}

} // namespace omapixel
