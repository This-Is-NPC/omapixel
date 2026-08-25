#pragma once

#include "Grid.h"
#include "Palette.h"

#include <QImage>
#include <QStringList>

namespace omapixel {

/// Deterministic RGBA quantization for raster import.
///
/// Existing palette entries are fixed candidates and are never recoloured or
/// reordered. Remaining capacity is filled with median-cut representatives.
/// The alpha channel participates in both the histogram and the distance, so
/// translucent pixels remain translucent instead of being treated as opaque.
class Quantization
{
public:
    struct Options {
        int maxColors = Palette::maxSlots;
        /// Negative means all capacity left after the fixed palette.
        int maxNewSlots = -1;
    };

    struct Report {
        qint64 inputPixels = 0;
        qint64 transparentPixels = 0;
        qint64 representedPixels = 0;
        qint64 exactMatches = 0;
        qint64 approximatedPixels = 0;
        int fixedSlots = 0;
        int newSlots = 0;
        QStringList diagnostics;
    };

    struct Result {
        bool ok = false;
        Grid grid;
        Palette palette;
        Report report;
        QString error;

        explicit operator bool() const { return ok; }
    };

    /// Quantizes a valid image in row-major order. Fully transparent pixels
    /// are always written as Grid::Empty and never consume a palette slot.
    static Result run(const QImage &image, const Palette &fixed,
                      const Options &options);
    static Result run(const QImage &image, const Palette &fixed);
    static Result run(const QImage &image);
};

} // namespace omapixel
