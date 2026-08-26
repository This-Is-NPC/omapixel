#include "Document.h"

#include "Ops.h"
#include "TextSafety.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <numeric>

namespace omapixel {

namespace {

bool validId(const QString &id)
{
    return QRegularExpression(QStringLiteral("^[a-z][a-z0-9-]{0,63}$"))
        .match(id)
        .hasMatch();
}

bool validName(const QString &name)
{
    if (name.isEmpty() || name.size() > 128)
        return false;
    return text::isSafe(name, false);
}

bool validMode(const QString &mode)
{
    return QStringList{QStringLiteral("normal"), QStringLiteral("multiply"),
                       QStringLiteral("screen")}
        .contains(mode);
}

bool rejectLocked(const Layer &layer, QString *error)
{
    if (!layer.locked)
        return false;
    if (error)
        *error = QStringLiteral("E_LAYER_LOCKED: --layer=%1").arg(layer.id);
    return true;
}

bool rejectLockedAnimatedLayers(const QList<Layer> &layers, QString *error)
{
    for (const Layer &layer : layers) {
        if (layer.storage == QStringLiteral("animated") && rejectLocked(layer, error))
            return true;
    }
    return false;
}

qint64 gridDifferenceCount(const Grid &left, const Grid &right)
{
    return qint64(ops::diff(left, right).size());
}

} // namespace

Document Document::blank(int columns, int rows)
{
    Document doc;
    doc.m_columns = qBound(1, columns, maxDimension);
    doc.m_rows = qBound(1, rows, maxDimension);
    doc.m_palette = Palette::standard();
    doc.addClip(QStringLiteral("idle"));
    doc.addLayer(QStringLiteral("layer"), QStringLiteral("Layer"));
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

QStringList Document::clipIds() const
{
    QStringList ids;
    ids.reserve(m_clips.size());
    for (const Clip &clip : m_clips)
        ids.append(clip.id);
    return ids;
}

int Document::indexOfClip(const QString &name) const
{
    for (int i = 0; i < m_clips.size(); ++i) {
        if (m_clips.at(i).name == name || m_clips.at(i).id == name)
            return i;
    }
    return -1;
}

int Document::indexOfClipId(const QString &id) const
{
    for (int i = 0; i < m_clips.size(); ++i) {
        if (m_clips.at(i).id == id)
            return i;
    }
    return -1;
}

int Document::indexOfClipName(const QString &name) const
{
    for (int i = 0; i < m_clips.size(); ++i) {
        if (m_clips.at(i).name == name)
            return i;
    }
    return -1;
}

const Clip *Document::clipById(const QString &id) const
{
    for (const Clip &clip : m_clips) {
        if (clip.id == id)
            return &clip;
    }
    return nullptr;
}

Clip *Document::clipById(const QString &id)
{
    for (Clip &clip : m_clips) {
        if (clip.id == id)
            return &clip;
    }
    return nullptr;
}

const Clip *Document::clipByName(const QString &name) const
{
    const int at = indexOfClipName(name);
    return at < 0 ? nullptr : &m_clips.at(at);
}

Clip *Document::clipByName(const QString &name)
{
    const int at = indexOfClipName(name);
    return at < 0 ? nullptr : &m_clips[at];
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
    if (!found || index < 0 || index >= found->frameCount || m_layers.isEmpty())
        return Grid();
    const Layer &layer = m_layers.first();
    for (const Cel &cel : layer.cels) {
        if ((layer.storage == QStringLiteral("shared") && cel.frame < 0)
            || (cel.clip == found->id && cel.frame == index))
            return cel.grid;
    }
    return Grid();
}

bool Document::setFrame(const QString &name, int index, const Grid &grid)
{
    const Clip *found = clip(name);
    if (!found || index < 0 || index >= found->frameCount || m_layers.isEmpty())
        return false;
    return setCel(m_layers.first().id, found->id, index, grid);
}

QStringList Document::layerIds() const
{
    QStringList ids;
    ids.reserve(m_layers.size());
    for (const Layer &layer : m_layers)
        ids.append(layer.id);
    return ids;
}

QStringList Document::layerNames() const
{
    QStringList names;
    names.reserve(m_layers.size());
    for (const Layer &layer : m_layers)
        names.append(layer.name);
    return names;
}

int Document::indexOfLayerId(const QString &id) const
{
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers.at(i).id == id)
            return i;
    }
    return -1;
}

int Document::indexOfLayerName(const QString &name) const
{
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers.at(i).name == name)
            return i;
    }
    return -1;
}

const Layer *Document::layer(const QString &idOrName) const
{
    const Layer *byId = layerById(idOrName);
    return byId ? byId : layerByName(idOrName);
}

Layer *Document::layer(const QString &idOrName)
{
    Layer *byId = layerById(idOrName);
    return byId ? byId : layerByName(idOrName);
}

const Layer *Document::layerById(const QString &id) const
{
    const int at = indexOfLayerId(id);
    return at < 0 ? nullptr : &m_layers.at(at);
}

Layer *Document::layerById(const QString &id)
{
    const int at = indexOfLayerId(id);
    return at < 0 ? nullptr : &m_layers[at];
}

const Layer *Document::layerByName(const QString &name) const
{
    const int at = indexOfLayerName(name);
    return at < 0 ? nullptr : &m_layers.at(at);
}

Layer *Document::layerByName(const QString &name)
{
    const int at = indexOfLayerName(name);
    return at < 0 ? nullptr : &m_layers[at];
}

bool Document::addLayer(const QString &id, const QString &name, const QString &storage)
{
    if (m_layers.size() >= maxLayers || !validId(id) || !validName(name)
        || layerById(id) || layerByName(name)
        || (storage != QStringLiteral("shared")
            && storage != QStringLiteral("animated")))
        return false;
    qint64 newCels = storage == QStringLiteral("shared") ? 1 : 0;
    if (storage == QStringLiteral("animated")) {
        for (const Clip &clip : m_clips)
            newCels += clip.frameCount;
    }
    qint64 existingCels = 0;
    for (const Layer &existing : m_layers)
        existingCels += existing.cels.size();
    if (newCels > maxTotalCels || existingCels > maxTotalCels - newCels)
        return false;
    Layer layer;
    layer.id = id;
    layer.name = name;
    layer.storage = storage;
    if (storage == QStringLiteral("shared")) {
        layer.cels.append({QString(), -1, Grid(m_columns, m_rows)});
    } else {
        for (const Clip &clip : m_clips) {
            for (int frame = 0; frame < clip.frameCount; ++frame)
                layer.cels.append({clip.id, frame, Grid(m_columns, m_rows)});
        }
    }
    m_layers.append(layer);
    return true;
}

bool Document::removeLayer(const QString &idOrName, QString *error)
{
    const int at = indexOfLayerId(idOrName) >= 0
                       ? indexOfLayerId(idOrName)
                       : indexOfLayerName(idOrName);
    if (at < 0 || m_layers.size() <= 1)
        return false;
    if (rejectLocked(m_layers.at(at), error))
        return false;
    m_layers.removeAt(at);
    return true;
}

bool Document::removeLayers(const QStringList &ids, QString *error)
{
    QList<int> indexes;
    for (const QString &id : ids) {
        const int at = indexOfLayerId(id);
        if (at < 0 || indexes.contains(at))
            continue;
        if (rejectLocked(m_layers.at(at), error))
            return false;
        indexes.append(at);
    }
    if (indexes.isEmpty() || indexes.size() >= m_layers.size()) {
        if (error)
            *error = QStringLiteral("E_LAYER_LAST: a document keeps its last layer");
        return false;
    }
    std::sort(indexes.begin(), indexes.end(), std::greater<int>());
    for (const int at : indexes)
        m_layers.removeAt(at);
    return true;
}

bool Document::renameLayer(const QString &idOrName, const QString &name,
                           QString *error)
{
    Layer *found = layer(idOrName);
    if (!found) {
        if (error)
            *error = QStringLiteral("E_LAYER_TARGET: layer not found: %1").arg(idOrName);
        return false;
    }
    if (!validName(name) || layerByName(name)) {
        if (error)
            *error = QStringLiteral("E_LAYER_NAME: invalid or duplicate name `%1`").arg(name);
        return false;
    }
    if (rejectLocked(*found, error))
        return false;
    found->name = name;
    return true;
}

bool Document::moveLayer(const QString &idOrName, int index, QString *error)
{
    const int from = indexOfLayerId(idOrName) >= 0
                         ? indexOfLayerId(idOrName)
                         : indexOfLayerName(idOrName);
    if (from < 0 || index < 0 || index >= m_layers.size()) {
        if (error)
            *error = QStringLiteral("E_LAYER_INDEX: target or index is out of range");
        return false;
    }
    if (rejectLocked(m_layers.at(from), error))
        return false;
    if (from == index)
        return true;
    Layer moved = m_layers.takeAt(from);
    m_layers.insert(index, moved);
    return true;
}

bool Document::moveLayers(const QStringList &ids, int index, QString *error)
{
    QList<Layer> moving;
    for (const Layer &layer : m_layers) {
        if (!ids.contains(layer.id))
            continue;
        if (rejectLocked(layer, error))
            return false;
        moving.append(layer);
    }
    if (moving.isEmpty()) {
        if (error)
            *error = QStringLiteral("E_LAYER_TARGET: no layer selected");
        return false;
    }
    for (const Layer &layer : moving)
        m_layers.removeAll(layer);
    const int insertion = qBound(0, index, m_layers.size());
    for (int offset = 0; offset < moving.size(); ++offset)
        m_layers.insert(insertion + offset, moving.at(offset));
    return true;
}

bool Document::duplicateLayer(const QString &idOrName, const QString &id,
                              const QString &name, QString *error)
{
    const Layer *source = layer(idOrName);
    if (!source) {
        if (error)
            *error = QStringLiteral("E_LAYER_TARGET: layer not found: %1").arg(idOrName);
        return false;
    }
    if (rejectLocked(*source, error))
        return false;
    if (!validId(id) || !validName(name) || layerById(id) || layerByName(name)) {
        if (error)
            *error = QStringLiteral("E_LAYER_IDENTITY: invalid or duplicate layer identity");
        return false;
    }
    qint64 existingCels = 0;
    for (const Layer &layer : m_layers)
        existingCels += layer.cels.size();
    if (existingCels > maxTotalCels - source->cels.size()) {
        if (error)
            *error = QStringLiteral("E_LAYER_LIMIT: duplicating %1 would exceed %2 cels")
                         .arg(source->id).arg(maxTotalCels);
        return false;
    }
    if (m_layers.size() >= maxLayers) {
        if (error)
            *error = QStringLiteral("E_LAYER_LIMIT: a document keeps at most %1 layers")
                         .arg(maxLayers);
        return false;
    }
    Layer copy = *source;
    copy.id = id;
    copy.name = name;
    const int at = indexOfLayerId(source->id);
    m_layers.insert(at + 1, copy);
    return true;
}

bool Document::setLayerVisible(const QString &idOrName, bool visible,
                               QString *error)
{
    Layer *found = layer(idOrName);
    if (!found) {
        if (error)
            *error = QStringLiteral("E_LAYER_TARGET: layer not found: %1").arg(idOrName);
        return false;
    }
    if (rejectLocked(*found, error))
        return false;
    found->visible = visible;
    return true;
}

bool Document::setLayerLocked(const QString &idOrName, bool locked,
                              QString *error)
{
    Layer *found = layer(idOrName);
    if (!found) {
        if (error)
            *error = QStringLiteral("E_LAYER_TARGET: layer not found: %1").arg(idOrName);
        return false;
    }
    found->locked = locked;
    return true;
}

bool Document::setLayerOpacity(const QString &idOrName, int opacity,
                               QString *error)
{
    Layer *found = layer(idOrName);
    if (!found || opacity < 0 || opacity > 255) {
        if (error)
            *error = QStringLiteral("E_LAYER_OPACITY: opacity must be 0..255");
        return false;
    }
    if (rejectLocked(*found, error))
        return false;
    found->opacity = opacity;
    return true;
}

Grid Document::cel(const QString &layerIdOrName, const QString &clipIdOrName,
                   int frameIndex) const
{
    const Layer *foundLayer = layer(layerIdOrName);
    const Clip *foundClip = clip(clipIdOrName);
    if (!foundLayer || !foundClip || frameIndex < 0
        || frameIndex >= foundClip->frameCount)
        return Grid();
    for (const Cel &cel : foundLayer->cels) {
        if ((foundLayer->storage == QStringLiteral("shared") && cel.frame < 0)
            || (cel.clip == foundClip->id && cel.frame == frameIndex))
            return cel.grid;
    }
    return Grid();
}

bool Document::setCel(const QString &layerIdOrName, const QString &clipIdOrName,
                      int frameIndex, const Grid &grid, QString *error)
{
    Layer *foundLayer = layer(layerIdOrName);
    const Clip *foundClip = clip(clipIdOrName);
    if (!foundLayer || !foundClip || grid.columns() != m_columns
        || grid.rows() != m_rows || frameIndex < 0
        || frameIndex >= foundClip->frameCount)
        return false;
    if (rejectLocked(*foundLayer, error))
        return false;
    for (Cel &cel : foundLayer->cels) {
        if ((foundLayer->storage == QStringLiteral("shared") && cel.frame < 0)
            || (cel.clip == foundClip->id && cel.frame == frameIndex)) {
            cel.grid = grid;
            return true;
        }
    }
    return false;
}

bool Document::editLayer(const QString &layerIdOrName, const QString &clipIdOrName,
                         int frameIndex, EditScope scope,
                         const std::function<void(Grid &)> &edit, int *changed,
                         QString *error)
{
    if (changed)
        *changed = 0;
    Layer *foundLayer = layer(layerIdOrName);
    const Clip *foundClip = clip(clipIdOrName);
    if (!foundLayer || !foundClip || frameIndex < 0
        || frameIndex >= foundClip->frameCount)
        return false;
    if (rejectLocked(*foundLayer, error))
        return false;

    QList<int> targets;
    if (foundLayer->storage == QStringLiteral("shared")) {
        for (int i = 0; i < foundLayer->cels.size(); ++i) {
            if (foundLayer->cels.at(i).frame < 0)
                targets.append(i);
        }
    } else {
        for (int i = 0; i < foundLayer->cels.size(); ++i) {
            const Cel &cel = foundLayer->cels.at(i);
            if (cel.clip == foundClip->id
                && (scope == EditScope::AllFrames || cel.frame == frameIndex))
                targets.append(i);
        }
    }
    if (targets.isEmpty())
        return false;

    QList<Grid> next;
    next.reserve(targets.size());
    int totalChanged = 0;
    for (const int target : targets) {
        Grid grid = foundLayer->cels.at(target).grid;
        const Grid before = grid;
        edit(grid);
        if (grid != before)
            totalChanged += gridDifferenceCount(before, grid);
        next.append(grid);
    }
    if (totalChanged == 0)
        return true;
    for (int i = 0; i < targets.size(); ++i)
        foundLayer->cels[targets.at(i)].grid = next.at(i);
    if (changed)
        *changed = totalChanged;
    return true;
}

bool Document::setLayerMode(const QString &layerIdOrName, const QString &mode,
                            QString *error)
{
    Layer *foundLayer = layer(layerIdOrName);
    if (!foundLayer || !validMode(mode)) {
        if (error)
            *error = QStringLiteral("E_LAYER_TARGET: invalid mode or layer");
        return false;
    }
    if (rejectLocked(*foundLayer, error))
        return false;
    if (foundLayer->mode == mode)
        return true;
    foundLayer->mode = mode;
    return true;
}

bool Document::convertLayerStorage(const QString &layerIdOrName,
                                   const QString &storage, int *lost,
                                   QString *error, bool anyway)
{
    if (lost)
        *lost = 0;
    Layer *foundLayer = layer(layerIdOrName);
    if (!foundLayer || (storage != QStringLiteral("shared")
                        && storage != QStringLiteral("animated"))) {
        if (error)
            *error = QStringLiteral("E_LAYER_TARGET: invalid layer storage");
        return false;
    }
    if (foundLayer->storage == storage)
        return true;
    if (rejectLocked(*foundLayer, error))
        return false;

    if (storage == QStringLiteral("shared")) {
        if (foundLayer->cels.isEmpty())
            return false;
        const Grid reference = foundLayer->cels.first().grid;
        int differences = 0;
        for (int i = 1; i < foundLayer->cels.size(); ++i)
            differences += gridDifferenceCount(reference,
                                                foundLayer->cels.at(i).grid);
        if (differences > 0 && !anyway) {
            if (lost)
                *lost = differences;
            if (error)
                *error = QStringLiteral(
                             "E_LAYER_DATA_LOSS: animated layer %1 has %2 differing pixel(s)")
                             .arg(foundLayer->id)
                             .arg(differences);
            return false;
        }
        foundLayer->cels = {Cel{QString(), -1, reference}};
        foundLayer->storage = storage;
        return true;
    }

    qint64 existingCels = 0;
    for (const Layer &layer : m_layers)
        existingCels += layer.cels.size();
    const qint64 newCels = [&] {
        qint64 count = 0;
        for (const Clip &clip : m_clips)
            count += clip.frameCount;
        return count;
    }();
    if (newCels > maxTotalCels || existingCels - foundLayer->cels.size()
            > maxTotalCels - newCels) {
        if (error)
            *error = QStringLiteral("E_LAYER_LIMIT: converting %1 would exceed %2 cels")
                         .arg(foundLayer->id).arg(maxTotalCels);
        return false;
    }

    const Grid shared = foundLayer->cels.first().grid;
    QList<Cel> cels;
    for (const Clip &clip : m_clips) {
        for (int frame = 0; frame < clip.frameCount; ++frame)
            cels.append({clip.id, frame, shared});
    }
    foundLayer->cels = cels;
    foundLayer->storage = storage;
    return true;
}

// ----------------------------------------------------------------- the clips

bool Document::addClip(const QString &name, int fps, QString *error)
{
    if (error)
        error->clear();
    if (m_clips.size() >= maxClips || !validName(name) || indexOfClip(name) >= 0)
        return false;
    if (rejectLockedAnimatedLayers(m_layers, error))
        return false;
    qint64 totalFrames = 1;
    for (const Clip &clip : m_clips)
        totalFrames += clip.frameCount;
    if (totalFrames > maxTotalFrames)
        return false;
    qint64 existingCels = 0;
    for (const Layer &layer : m_layers)
        existingCels += layer.cels.size();
    qint64 addedCels = 0;
    for (const Layer &layer : m_layers)
        if (layer.storage == QStringLiteral("animated"))
            ++addedCels;
    if (existingCels > maxTotalCels - addedCels)
        return false;
    Clip clip;
    QString id = name.toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9-]+")), QStringLiteral("-"));
    while (id.startsWith(QLatin1Char('-')))
        id.remove(0, 1);
    if (id.isEmpty() || !id.at(0).isLetter())
        id.prepend(QStringLiteral("clip-"));
    const QString base = id;
    int suffix = 2;
    while (clipById(id))
        id = QStringLiteral("%1-%2").arg(base).arg(suffix++);
    clip.id = id;
    clip.name = name;
    clip.fps = qBound(1, fps, 60);
    m_clips.append(clip);
    for (Layer &layer : m_layers) {
        if (layer.storage != QStringLiteral("animated"))
            continue;
        layer.cels.append({clip.id, 0, Grid(m_columns, m_rows)});
    }
    return true;
}

bool Document::removeClip(const QString &name, QString *error)
{
    if (error)
        error->clear();
    // The last clip stays, for the same reason a clip keeps its last frame: a
    // document with no clips has nothing to draw and nothing to select. The
    // studio opens it on an empty frame with no way back, and every command
    // that takes a frame answers "the document has no clips". Guarding it in
    // the front end only, as this once did, leaves the command line able to
    // write a file that neither front end can then edit.
    const int at = indexOfClip(name);
    if (at < 0 || m_clips.size() <= 1)
        return false;
    if (rejectLockedAnimatedLayers(m_layers, error))
        return false;
    const QString id = m_clips.at(at).id;
    m_clips.removeAt(at);
    for (Layer &layer : m_layers) {
        for (int i = layer.cels.size() - 1; i >= 0; --i) {
            if (layer.cels.at(i).clip == id)
                layer.cels.removeAt(i);
        }
    }
    return true;
}

bool Document::renameClip(const QString &from, const QString &to)
{
    const int at = indexOfClip(from);
    if (at < 0 || !validName(to) || indexOfClip(to) >= 0)
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

bool Document::addFrame(const QString &name, int after, bool duplicate, QString *error)
{
    if (error)
        error->clear();
    Document next = *this;
    Clip *found = next.clip(name);
    if (!found) {
        if (error)
            *error = QStringLiteral("E_CLIP_NOT_FOUND: no clip %1").arg(name);
        return false;
    }
    if (rejectLockedAnimatedLayers(next.m_layers, error))
        return false;
    if (found->frameCount >= maxFramesPerClip) {
        if (error)
            *error = QStringLiteral("E_FRAME_LIMIT: clip %1 keeps at most %2 frames")
                         .arg(found->id).arg(maxFramesPerClip);
        return false;
    }
    qint64 totalFrames = 0;
    for (const Clip &clip : next.m_clips)
        totalFrames += clip.frameCount;
    if (totalFrames >= maxTotalFrames) {
        if (error)
            *error = QStringLiteral("E_FRAME_LIMIT: document keeps at most %1 total frames")
                         .arg(maxTotalFrames);
        return false;
    }
    qint64 existingCels = 0;
    for (const Layer &layer : next.m_layers)
        existingCels += layer.cels.size();
    qint64 animatedLayers = 0;
    for (const Layer &layer : next.m_layers)
        if (layer.storage == QStringLiteral("animated"))
            ++animatedLayers;
    if (existingCels >= maxTotalCels - animatedLayers) {
        if (error)
            *error = QStringLiteral("E_CEL_LIMIT: adding a frame would exceed %1 total cels")
                         .arg(maxTotalCels);
        return false;
    }
    const int at = qBound(-1, after, found->frameCount - 1);
    for (Layer &layer : next.m_layers) {
        if (layer.storage != QStringLiteral("animated"))
            continue;
        QList<int> positions;
        QSet<int> frames;
        for (int i = 0; i < layer.cels.size(); ++i)
            if (layer.cels.at(i).clip == found->id) {
                positions.append(i);
                frames.insert(layer.cels.at(i).frame);
                if (layer.cels.at(i).frame < 0
                    || layer.cels.at(i).frame >= found->frameCount) {
                    if (error)
                        *error = QStringLiteral("E_CEL_INTEGRITY: clip %1 has an invalid cel")
                                     .arg(found->id);
                    return false;
                }
            }
        if (positions.size() != found->frameCount
            || frames.size() != found->frameCount
            || (at >= 0 && !frames.contains(at))) {
            if (error)
                *error = QStringLiteral("E_CEL_INTEGRITY: clip %1 does not cover every frame")
                             .arg(found->id);
            return false;
        }
        int position = positions.first();
        Grid copy(m_columns, m_rows);
        if (at >= 0) {
            for (const int candidate : positions) {
                if (layer.cels.at(candidate).frame == at) {
                    position = candidate + 1;
                    if (duplicate)
                        copy = layer.cels.at(candidate).grid;
                    break;
                }
            }
        }
        layer.cels.insert(position, {found->id, at + 1, copy});
        for (int i = 0; i < layer.cels.size(); ++i) {
            if (i != position && layer.cels.at(i).clip == found->id
                && layer.cels.at(i).frame >= at + 1)
                ++layer.cels[i].frame;
        }
    }
    ++found->frameCount;
    *this = std::move(next);
    return true;
}

bool Document::removeFrame(const QString &name, int index, QString *error)
{
    if (error)
        error->clear();
    Document next = *this;
    Clip *found = next.clip(name);
    // A clip with no frames is a clip that cannot be drawn, so the last one stays.
    if (!found || found->frameCount <= 1 || index < 0
        || index >= found->frameCount)
        return false;
    if (rejectLockedAnimatedLayers(next.m_layers, error))
        return false;
    for (Layer &layer : next.m_layers) {
        if (layer.storage != QStringLiteral("animated"))
            continue;
        QSet<int> frames;
        for (const Cel &cel : layer.cels) {
            if (cel.clip == found->id) {
                frames.insert(cel.frame);
                if (cel.frame < 0 || cel.frame >= found->frameCount)
                    return false;
            }
        }
        if (frames.size() != found->frameCount)
            return false;
        int selected = -1;
        for (int i = layer.cels.size() - 1; i >= 0; --i) {
            if (layer.cels.at(i).clip == found->id
                && layer.cels.at(i).frame == index) {
                selected = i;
                layer.cels.removeAt(i);
            }
        }
        if (selected < 0)
            return false;
        for (Cel &cel : layer.cels) {
            if (cel.clip == found->id && cel.frame > index)
                --cel.frame;
        }
    }
    --found->frameCount;
    *this = std::move(next);
    return true;
}

bool Document::moveFrame(const QString &name, int index, int to, QString *error)
{
    if (error)
        error->clear();
    Document next = *this;
    Clip *found = next.clip(name);
    if (!found || index < 0 || index >= found->frameCount || to < 0
        || to >= found->frameCount)
        return false;
    if (index == to)
        return true;
    if (rejectLockedAnimatedLayers(next.m_layers, error))
        return false;
    for (Layer &layer : next.m_layers) {
        if (layer.storage != QStringLiteral("animated"))
            continue;
        QList<Cel> selected;
        QList<int> positions;
        QSet<int> frames;
        for (const Cel &cel : layer.cels)
            if (cel.clip == found->id) {
                selected.append(cel);
                frames.insert(cel.frame);
                if (cel.frame < 0 || cel.frame >= found->frameCount)
                    return false;
            }
        if (selected.size() != found->frameCount)
            return false;
        if (frames.size() != found->frameCount || !frames.contains(index)
            || !frames.contains(to))
            return false;
        for (int i = 0; i < layer.cels.size(); ++i)
            if (layer.cels.at(i).clip == found->id)
                positions.append(i);
        std::sort(selected.begin(), selected.end(), [](const Cel &left, const Cel &right) {
            return left.frame < right.frame;
        });
        selected.move(index, to);
        int at = 0;
        for (const int position : positions) {
            layer.cels[position] = selected.at(at);
            layer.cels[position].frame = at++;
        }
    }
    *this = std::move(next);
    return true;
}

// ------------------------------------------------------------------ the size

qint64 Document::wouldLose(int columns, int rows) const
{
    const int dx = (columns - m_columns) / 2;
    const int dy = (rows - m_rows) / 2;
    qint64 lost = 0;
    for (const Layer &layer : m_layers) {
        for (const Cel &cel : layer.cels) {
            const Grid &grid = cel.grid;
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

bool Document::resize(int columns, int rows, QString *error)
{
    columns = qBound(1, columns, maxDimension);
    rows = qBound(1, rows, maxDimension);

    for (const Layer &layer : m_layers) {
        if (rejectLocked(layer, error))
            return false;
    }

    Document next = *this;

    // No early return when the size already matches. A hand-written file can
    // hold a frame whose rows are not the size the document declares, and this
    // is the operation somebody reaches for to repair it; returning early left
    // `check` reporting a problem that nothing could fix.
    for (Layer &layer : next.m_layers) {
        for (Cel &cel : layer.cels) {
            Grid &grid = cel.grid;
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

    next.m_columns = columns;
    next.m_rows = rows;
    *this = std::move(next);
    return true;
}

QRect Document::drawnBounds(const QString &clipName, int frameIndex) const
{
    const Clip *found = clip(clipName);
    if (!found || frameIndex < 0 || frameIndex >= found->frameCount)
        return QRect();

    int left = m_columns;
    int top = m_rows;
    int right = -1;
    int bottom = -1;
    for (const Layer &layer : m_layers) {
        const Grid grid = cel(layer.id, found->id, frameIndex);
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
    }
    return right < left ? QRect()
                        : QRect(left, top, right - left + 1, bottom - top + 1);
}

qint64 Document::wouldLoseOutside(const QRect &kept) const
{
    qint64 lost = 0;
    for (const Layer &layer : m_layers) {
        for (const Cel &cel : layer.cels) {
            const Grid &grid = cel.grid;
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

bool Document::crop(const QRect &kept, QString *error)
{
    const QRect whole(0, 0, m_columns, m_rows);
    if (!kept.isValid() || !whole.contains(kept) || kept == whole)
        return false;

    for (const Layer &layer : m_layers) {
        if (rejectLocked(layer, error))
            return false;
    }

    Document next = *this;

    for (Layer &layer : next.m_layers) {
        for (Cel &cel : layer.cels) {
            Grid &grid = cel.grid;
            Grid next(kept.width(), kept.height());
            for (int y = 0; y < kept.height(); ++y) {
                for (int x = 0; x < kept.width(); ++x)
                    next.set(x, y, grid.at(x + kept.x(), y + kept.y()));
            }
            grid = next;
        }
    }
    next.m_columns = kept.width();
    next.m_rows = kept.height();
    *this = std::move(next);
    return true;
}

qint64 Document::replaceSlot(QChar from, QChar to, QString *error)
{
    if (from == to)
        return 0;
    for (const Layer &layer : m_layers) {
        if (rejectLocked(layer, error))
            return 0;
    }
    qint64 changed = 0;
    for (Layer &layer : m_layers) {
        for (Cel &cel : layer.cels)
            changed += ops::swapSlot(cel.grid, from, to);
    }
    return changed;
}

qint64 Document::replaceSlotInFrame(const QString &clip, int frame, QChar from,
                                    QChar to, QString *error)
{
    if (from == to)
        return 0;
    const Clip *foundClip = this->clip(clip);
    if (!foundClip || frame < 0 || frame >= foundClip->frameCount)
        return 0;
    qint64 changed = 0;
    for (const Layer &layer : m_layers) {
        if (rejectLocked(layer, error))
            return 0;
    }
    Document next = *this;
    for (Layer &layer : next.m_layers) {
        Grid grid = next.cel(layer.id, foundClip->id, frame);
        changed += ops::swapSlot(grid, from, to);
        if (changed > 0)
            next.setCel(layer.id, foundClip->id, frame, grid);
    }
    if (changed > 0)
        *this = std::move(next);
    return changed;
}

bool Document::usesSlot(QChar slot) const
{
    for (const Layer &layer : m_layers) {
        for (const Cel &cel : layer.cels) {
            const Grid &grid = cel.grid;
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

bool Document::setPaletteColour(QChar slot, const QColor &colour, QString *error)
{
    if (!Palette::validSlot(slot) || !colour.isValid()) {
        if (error)
            *error = QStringLiteral("E_PALETTE_VALUE: invalid slot or colour");
        return false;
    }
    const QColor current = m_palette.colour(slot);
    if (current == colour)
        return true;
    if (m_palette.has(slot)) {
        for (const Layer &layer : m_layers) {
            if (!layer.locked)
                continue;
            for (const Cel &cel : layer.cels) {
                if (cel.grid.slotsUsed().contains(slot)) {
                    if (error)
                        *error = QStringLiteral(
                            "E_PALETTE_LOCKED: slot %1 is used by locked layer %2")
                                     .arg(slot).arg(layer.id);
                    return false;
                }
            }
        }
    }
    if (!m_palette.set(slot, colour)) {
        if (error)
            *error = QStringLiteral("E_PALETTE_LIMIT: a document keeps at most %1 slots")
                         .arg(maxPaletteSlots);
        return false;
    }
    return true;
}

bool Document::removePaletteSlot(QChar slot, QString *error)
{
    if (!m_palette.has(slot))
        return false;
    if (usesSlot(slot)) {
        if (error)
            *error = QStringLiteral("E_PALETTE_IN_USE: slot %1 is still used")
                         .arg(slot);
        return false;
    }
    return m_palette.remove(slot);
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
        if (!Palette::validSlot(slot.letter))
            out.append(QStringLiteral("palette slot %1 is invalid").arg(slot.letter));
        if (!slot.colour.isValid()) {
            out.append(QStringLiteral("slot %1: not a colour")
                           .arg(slot.letter));
        }
    }

    QSet<QString> clipIds;
    QSet<QString> clipNames;
    for (const Clip &clip : m_clips) {
        if (!validId(clip.id) || clipIds.contains(clip.id))
            out.append(QStringLiteral("%1: duplicate or empty clip id").arg(clip.name));
        if (!validName(clip.name) || clipNames.contains(clip.name))
            out.append(QStringLiteral("%1: duplicate or empty clip name").arg(clip.name));
        if (clip.fps < 1 || clip.fps > 60)
            out.append(QStringLiteral("%1: FPS is outside 1..60").arg(clip.name));
        if (clip.frameCount <= 0)
            out.append(QStringLiteral("%1: no frames").arg(clip.name));
        clipIds.insert(clip.id);
        clipNames.insert(clip.name);
    }
    QSet<QString> layerIds;
    QSet<QString> layerNames;
    for (const Layer &layer : m_layers) {
        if (layer.cels.size() > maxTotalCels) {
            out.append(QStringLiteral("%1: cel count exceeds %2")
                           .arg(layer.name).arg(maxTotalCels));
        }
        if (!validId(layer.id) || layerIds.contains(layer.id))
            out.append(QStringLiteral("%1: duplicate or empty layer id").arg(layer.name));
        if (!validName(layer.name) || layerNames.contains(layer.name))
            out.append(QStringLiteral("%1: duplicate or empty layer name").arg(layer.name));
        if (layer.opacity < 0 || layer.opacity > 255)
            out.append(QStringLiteral("%1: opacity is outside 0..255").arg(layer.name));
        if (!QStringList{QStringLiteral("normal"), QStringLiteral("multiply"),
                         QStringLiteral("screen")}.contains(layer.mode))
            out.append(QStringLiteral("%1: unknown blend mode").arg(layer.name));
        const int expected = layer.storage == QStringLiteral("shared")
                                 ? 1
                                 : std::accumulate(m_clips.cbegin(), m_clips.cend(), 0,
                                     [](int total, const Clip &clip) {
                                         return total + clip.frameCount;
                                     });
        if ((layer.storage != QStringLiteral("shared")
             && layer.storage != QStringLiteral("animated"))
             || layer.cels.size() != expected)
            out.append(QStringLiteral("%1: invalid cel count").arg(layer.name));
        layerIds.insert(layer.id);
        layerNames.insert(layer.name);
        QSet<QString> celAddresses;
        for (int i = 0; i < layer.cels.size(); ++i) {
            const Cel &cel = layer.cels.at(i);
            const Grid &grid = cel.grid;
            if (grid.columns() != m_columns || grid.rows() != m_rows) {
                out.append(QStringLiteral("%1[%2]: %3x%4, expected %5x%6")
                               .arg(layer.name).arg(i).arg(grid.columns())
                               .arg(grid.rows()).arg(m_columns).arg(m_rows));
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
                               .arg(layer.name).arg(i)
                               .arg(unknown.join(QStringLiteral(", "))));
            }
            if (layer.storage == QStringLiteral("shared")) {
                if (!cel.clip.isEmpty() || cel.frame != -1)
                    out.append(QStringLiteral("%1[%2]: shared cel must use all frames")
                                   .arg(layer.name).arg(i));
            } else {
                const Clip *clip = clipById(cel.clip);
                if (!clip || cel.frame < 0 || cel.frame >= clip->frameCount)
                    out.append(QStringLiteral("%1[%2]: cel has an invalid clip/frame address")
                                   .arg(layer.name).arg(i));
                const QString address = QStringLiteral("%1:%2").arg(cel.clip).arg(cel.frame);
                if (celAddresses.contains(address))
                    out.append(QStringLiteral("%1[%2]: duplicate cel address")
                                   .arg(layer.name).arg(i));
                celAddresses.insert(address);
            }
        }
    }
    if (m_palette.entries().size() > maxPaletteSlots)
        out.append(QStringLiteral("palette: more than %1 slots").arg(maxPaletteSlots));
    if (m_clips.size() > maxClips)
        out.append(QStringLiteral("document: more than %1 clips").arg(maxClips));
    qint64 totalFrames = 0;
    for (const Clip &clip : m_clips) {
        totalFrames += clip.frameCount;
        if (clip.frameCount > maxFramesPerClip)
            out.append(QStringLiteral("%1: more than %2 frames")
                           .arg(clip.name).arg(maxFramesPerClip));
    }
    if (totalFrames > maxTotalFrames)
        out.append(QStringLiteral("document: more than %1 frames").arg(maxTotalFrames));
    if (m_layers.size() > maxLayers)
        out.append(QStringLiteral("document: more than %1 layers").arg(maxLayers));
    qint64 totalCels = 0;
    for (const Layer &layer : m_layers)
        totalCels += layer.cels.size();
    if (totalCels > maxTotalCels)
        out.append(QStringLiteral("document: more than %1 cels").arg(maxTotalCels));
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
        if (!(mine == theirs))
            return false;
    }
    return m_layers == other.m_layers;
}

} // namespace omapixel
