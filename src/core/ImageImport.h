#pragma once

#include "Document.h"
#include "Quantization.h"

#include <QSize>
#include <QStringList>

namespace omapixel {

/// Safe PNG/JPEG/WebP raster import into the character-grid document model.
class ImageImport
{
public:
    enum class ResizeMode {
        Contain,
        Cover,
        Stretch,
    };

    struct Options {
        /// Zero means unset. `scale` and targetResolution are mutually exclusive.
        /// A value of N represents N source pixels per logical pixel.
        int scale = 0;
        QSize targetResolution;
        ResizeMode resizeMode = ResizeMode::Contain;

        QString clip;
        int frame = 0;
        QString clipName = QStringLiteral("Imported");
        QString layerId;
        QString layerName = QStringLiteral("Imported");

        /// These are safety limits, not allocation requests. They cannot exceed
        /// the hard limits below.
        qint64 maxInputBytes = 256 * 1024 * 1024;
        int maxSourceDimension = 16384;
        qint64 maxSourcePixels = 64 * 1000 * 1000;
        int maxPaletteSlots = Palette::maxSlots;
    };

    static constexpr qint64 hardMaxInputBytes = 256 * 1024 * 1024;
    static constexpr int hardMaxSourceDimension = 16384;
    static constexpr qint64 hardMaxSourcePixels = 64 * 1000 * 1000;

    struct Report {
        QString format;
        QSize sourceSize;
        QSize decodedSize;
        QSize logicalSize;
        bool orientationApplied = false;
        ResizeMode resizeMode = ResizeMode::Stretch;
        qint64 sourcePixels = 0;
        qint64 logicalPixels = 0;
        qint64 transparentPixels = 0;
        qint64 representedPixels = 0;
        qint64 exactMatches = 0;
        qint64 approximatedPixels = 0;
        qint64 clippedPixels = 0;
        int paletteSlotsBefore = 0;
        int paletteSlotsAfter = 0;
        int newPaletteSlots = 0;
        QString clipId;
        QString layerId;
        QStringList diagnostics;
    };

    struct Result {
        bool ok = false;
        Document document;
        Grid grid;
        Palette palette;
        Report report;
        QString error;

        explicit operator bool() const { return ok; }
    };

    /// Reads and prepares a raster without changing a document. The returned
    /// grid and palette are useful to callers that need their own transaction.
    static Result load(const QString &path, const Options &options,
                       const Palette &fixed);
    static Result load(const QString &path, const Options &options);
    static Result load(const QString &path);

    /// Creates a new one-clip, one-layer document from the raster.
    static Result createDocument(const QString &path, const Options &options);
    static Result createDocument(const QString &path);

    /// Stages the import, layer creation, palette update, and cel write on a
    /// copy. `document` is assigned only after every operation succeeds.
    static bool addLayer(Document *document, const QString &path,
                         const Options &options, Report *report, QString *error);
    static bool addLayer(Document *document, const QString &path,
                         const Options &options);
    static bool addLayer(Document *document, const QString &path);
};

} // namespace omapixel
