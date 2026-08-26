#pragma once

#include "Grid.h"
#include "Palette.h"

#include <QList>
#include <QRect>
#include <QString>
#include <functional>

namespace omapixel {

/// One animation: stable identity, presentation name, speed, and frame count.
struct Clip {
    QString id;
    QString name;
    int fps = 8;
    int frameCount = 1;

    bool operator==(const Clip &other) const
    {
        return id == other.id && name == other.name && fps == other.fps
            && frameCount == other.frameCount;
    }
};

/// Raster storage for one layer/clip/frame address.
struct Cel {
    QString clip;
    int frame = -1;
    Grid grid;

    bool operator==(const Cel &other) const
    {
        return clip == other.clip && frame == other.frame && grid == other.grid;
    }
};

/// A clip-wide layer. The array order is bottom-to-top document content.
struct Layer {
    QString id;
    QString name;
    bool visible = true;
    bool locked = false;
    int opacity = 255;
    QString mode = QStringLiteral("normal");
    QString storage = QStringLiteral("animated");
    QList<Cel> cels;

    bool operator==(const Layer &other) const
    {
        return id == other.id && name == other.name && visible == other.visible
            && locked == other.locked && opacity == other.opacity
            && mode == other.mode && storage == other.storage
            && cels == other.cels;
    }
};

enum class EditScope {
    CurrentFrame,
    AllFrames,
};

/// A palette and a handful of animations, all at one size.
///
/// The size belongs to the DOCUMENT and not to each clip: a document is a set
/// of drawings of the same thing, and a clip of another size in the middle of
/// it is another document. Whoever wants a 16px icon and a 128px backdrop wants
/// two files and will be happier with two.
///
/// Every mutation lives here, and there is exactly one implementation of each.
/// That is the whole reason this is C++ and not two programs: before, `resize`
/// existed once in QML and once in Python, with a comment in each pointing at
/// the other. The CLI and the studio now call the same function or they do not
/// call one at all.
class Document
{
public:
    /// The largest grid a document may hold, per side. The real budget is
    /// memory: a maximal square is ~4 MB per copy, and the undo stack holds
    /// whole documents -- `history.depth` of them. 2048 keeps an 854x480
    /// pixelization (or a 1080p frame) ordinary while keeping that stack
    /// bounded on machines people actually own.
    static constexpr int maxDimension = 2048;
    static constexpr int maxPaletteSlots = Palette::maxSlots;
    static constexpr int maxClips = 64;
    static constexpr int maxLayers = 64;
    static constexpr int maxFramesPerClip = 1024;
    static constexpr qint64 maxTotalFrames = 4096;
    static constexpr qint64 maxTotalCels = 16384;
    static constexpr qint64 maxDocumentBytes = 16 * 1024 * 1024;
    static constexpr qint64 maxClipboardBytes = 4 * 1024 * 1024;
    static constexpr int maxClipboardRows = 2048;
    static constexpr int maxClipboardColumns = 2048;
    static constexpr qint64 maxClipboardCells = 1'000'000;

    Document() = default;
    static Document blank(int columns, int rows);

    /// A document with no clips at all, for a reader that is about to add
    /// them. Every other way in keeps at least one clip; this one exists
    /// because a reader has to be able to drop the placeholder before it knows
    /// what the file contains, and `removeClip` will not take the last one.
    /// Whoever calls it owns the gap: add a clip, or fall back to `blank`.
    static Document empty(int columns, int rows);

    int columns() const { return m_columns; }
    int rows() const { return m_rows; }

    const Palette &palette() const { return m_palette; }
    Palette &palette() { return m_palette; }

    const QList<Clip> &clips() const { return m_clips; }
    QStringList clipNames() const;

    /// -1 when there is no clip by that name. Callers check; nothing here
    /// throws, because a CLI has to turn a bad name into a message and not a
    /// crash.
    int indexOfClip(const QString &name) const;
    int indexOfClipId(const QString &id) const;
    int indexOfClipName(const QString &name) const;
    const Clip *clip(const QString &name) const;
    Clip *clip(const QString &name);

    QStringList clipIds() const;
    const Clip *clipById(const QString &id) const;
    Clip *clipById(const QString &id);
    const Clip *clipByName(const QString &name) const;
    Clip *clipByName(const QString &name);

    /// The frame, or a null Grid when the clip or the index is unknown.
    Grid frame(const QString &clip, int index) const;
    bool setFrame(const QString &clip, int index, const Grid &grid);

    const QList<Layer> &layers() const { return m_layers; }
    QList<Layer> &layers() { return m_layers; }
    QStringList layerIds() const;
    QStringList layerNames() const;
    int indexOfLayerId(const QString &id) const;
    int indexOfLayerName(const QString &name) const;
    const Layer *layer(const QString &idOrName) const;
    Layer *layer(const QString &idOrName);
    const Layer *layerById(const QString &id) const;
    Layer *layerById(const QString &id);
    const Layer *layerByName(const QString &name) const;
    Layer *layerByName(const QString &name);
    bool addLayer(const QString &id, const QString &name,
                  const QString &storage = QStringLiteral("animated"));
    bool removeLayer(const QString &idOrName, QString *error = nullptr);
    bool removeLayers(const QStringList &ids, QString *error = nullptr);
    bool renameLayer(const QString &idOrName, const QString &name,
                     QString *error = nullptr);
    bool moveLayer(const QString &idOrName, int index, QString *error = nullptr);
    bool moveLayers(const QStringList &ids, int index, QString *error = nullptr);
    bool duplicateLayer(const QString &idOrName, const QString &id,
                        const QString &name, QString *error = nullptr);
    bool setLayerVisible(const QString &idOrName, bool visible,
                         QString *error = nullptr);
    bool setLayerLocked(const QString &idOrName, bool locked,
                        QString *error = nullptr);
    bool setLayerOpacity(const QString &idOrName, int opacity,
                         QString *error = nullptr);
    Grid cel(const QString &layerIdOrName, const QString &clipIdOrName,
             int frame) const;
    bool setCel(const QString &layerIdOrName, const QString &clipIdOrName,
                int frame, const Grid &grid, QString *error = nullptr);

    /// Applies one raster operation to the selected layer cels. The operation
    /// is staged first, so a locked layer or malformed target never leaves a
    /// partially edited document. Shared layers always resolve to one cel.
    bool editLayer(const QString &layerIdOrName, const QString &clipIdOrName,
                   int frame, EditScope scope,
                   const std::function<void(Grid &)> &edit,
                   int *changed = nullptr, QString *error = nullptr);

    /// Changes only the blend-mode metadata. Locked layers are protected by
    /// the same core policy as raster edits.
    bool setLayerMode(const QString &layerIdOrName, const QString &mode,
                      QString *error = nullptr);

    /// Changes a palette colour only when no locked layer refers to the slot.
    /// A global palette edit otherwise changes locked content without editing
    /// that layer, which violates the same lock boundary as a raster edit.
    bool setPaletteColour(QChar slot, const QColor &colour,
                          QString *error = nullptr);

    /// Converts cel storage without silently throwing away animation. An
    /// animated layer may collapse only when every cel is identical.
    bool convertLayerStorage(const QString &layerIdOrName,
                             const QString &storage, int *lost = nullptr,
                             QString *error = nullptr, bool anyway = false);

    bool setLayerStorage(const QString &layerIdOrName, const QString &storage,
                         int *lost = nullptr, QString *error = nullptr,
                         bool anyway = false)
    {
        return convertLayerStorage(layerIdOrName, storage, lost, error, anyway);
    }

    // ------------------------------------------------------------- the clips

    /// Structural clip changes reject locked animated layers through `error`.
    bool addClip(const QString &name, int fps = 8, QString *error = nullptr);
    bool removeClip(const QString &name, QString *error = nullptr);
    /// Keeps the clip where it was in the list. Deleting and reinserting sends
    /// it to the end, and the list is the sidebar -- a clip that jumps position
    /// when renamed looks like another clip.
    bool renameClip(const QString &from, const QString &to);
    bool setFps(const QString &name, int fps);

    // ------------------------------------------------------------ the frames

    /// Structural frame changes reject locked animated layers through `error`.
    bool addFrame(const QString &clip, int after, bool duplicate,
                  QString *error = nullptr);
    bool removeFrame(const QString &clip, int index, QString *error = nullptr);
    bool moveFrame(const QString &clip, int index, int to, QString *error = nullptr);

    // -------------------------------------------------------------- the size

    /// How many drawn pixels shrinking to this size would lose. Zero means the
    /// resize is free of consequence; any other number is what a confirmation
    /// has to say before it happens.
    qint64 wouldLose(int columns, int rows) const;

    /// Resizes every frame of every clip, keeping the drawing CENTRED. What is
    /// being resized is almost always a figure, and a figure has a centre. A
    /// clip left behind would produce a document with frames of two sizes,
    /// which is a file nothing can draw.
    bool resize(int columns, int rows, QString *error = nullptr);

    /// The smallest rectangle containing every drawn pixel in one frame. An
    /// invalid rectangle means the frame is empty or does not exist.
    QRect drawnBounds(const QString &clip, int frame) const;

    /// How many drawn pixels across the document lie outside `kept`.
    qint64 wouldLoseOutside(const QRect &kept) const;

    /// Keeps exactly this rectangle in every frame of every clip. The same
    /// origin preserves animation alignment; false means invalid bounds or a
    /// rectangle that already covers the whole document.
    bool crop(const QRect &kept, QString *error = nullptr);

    /// Swaps one slot for another in every frame of every clip, and says how
    /// many pixels changed.
    qint64 replaceSlot(QChar from, QChar to, QString *error = nullptr);

    /// The same, in one frame only.
    ///
    /// Both exist because both are wanted and neither is the obvious default.
    /// Recolouring one frame of an animation leaves it flickering between two
    /// colours; recolouring all twelve when you meant to fix one is a bigger
    /// mistake and a quieter one. The caller says which.
    qint64 replaceSlotInFrame(const QString &clip, int frame, QChar from, QChar to,
                              QString *error = nullptr);

    /// Whether any pixel anywhere still draws with this slot.
    bool usesSlot(QChar slot) const;

    /// Palette removal is content mutation: a referenced slot cannot be
    /// deleted and leave every affected cel silently invalid.
    bool removePaletteSlot(QChar slot, QString *error = nullptr);

    // ------------------------------------------------------------- integrity

    /// What stops the document from being drawn, in plain sentences. Returns
    /// rather than throws: a document with a problem has to open so it can be
    /// fixed.
    QStringList problems() const;

    bool operator==(const Document &other) const;

private:
    int m_columns = 0;
    int m_rows = 0;
    Palette m_palette;
    QList<Clip> m_clips;
    QList<Layer> m_layers;
};

} // namespace omapixel
