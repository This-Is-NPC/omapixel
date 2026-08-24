#pragma once

#include "Document.h"

#include <QColor>
#include <QImage>
#include <QString>
#include <QStringList>

namespace omapixel {

/// Turning a document into something a person -- or an agent -- can look at.
///
/// This is the piece that takes the window out of the loop. An agent driving
/// the CLI could always change a drawing; what it could not do was SEE the
/// result, so it reached for a screenshot of a running studio. `toAnsi` puts
/// the frame in the terminal and `toImage` writes a PNG, and between them there
/// is nothing left that needs a window.
namespace render {

static constexpr qint64 maxImagePixels = 64 * 1000 * 1000;
static constexpr qint64 maxImageBytes = 256 * 1024 * 1024;
static constexpr qint64 maxTerminalCells = 1'000'000;
static constexpr qint64 maxTerminalBytes = 16 * 1024 * 1024;

/// Consequences of converting the composed RGBA surface back to palette slots.
/// `newSlots` stays zero under the v2 contract: quantization selects an existing
/// palette entry and never invents a colour.
struct QuantizationReport {
    qint64 composedPixels = 0;
    qint64 exactMatches = 0;
    qint64 approximatedPixels = 0;
    qint64 newSlots = 0;
    QStringList diagnostics;
};

struct Options {
    int scale = 1;
    /// Draws the checkerboard behind transparent pixels. Without it there is no
    /// telling an empty pixel from one the colour of the background.
    ///
    /// The two tones are passed in rather than fixed, because the studio tunes
    /// them to the desktop theme: a dark chequer under a light theme reads as a
    /// hole in the drawing rather than as absence.
    bool checker = false;
    QColor checkerDark{"#1B1C26"};
    QColor checkerLight{"#22242F"};
    /// Lays every frame of the clip side by side instead of drawing one.
    bool sheet = false;
    int sheetGap = 2;
    /// Composite all visible layers (the default), or render only `layer`.
    /// Isolation still respects the layer's visibility and opacity.
    bool isolated = false;
    QString layer;
    /// Emits a warning before allocating an image above this many pixels.
    /// Zero disables the warning; it is not an allocation limit.
    qint64 warningPixels = 0;
};

QImage toImage(const Document &document, const QString &clip, int frame,
                const Options &options, QString *warning = nullptr,
                QString *error = nullptr,
                QStringList *diagnostics = nullptr);

/// The composed surface flattened back to the document's palette. This is the
/// representation used by text output; ties are resolved by palette order.
Grid toGrid(const Document &document, const QString &clip, int frame,
            const Options &options = Options(),
            QStringList *diagnostics = nullptr,
            QuantizationReport *report = nullptr);

/// Half-block characters, two sprite rows per terminal row, in 24-bit colour.
/// Two rows per line because a terminal cell is about twice as tall as it is
/// wide, and one row per line gives you a drawing stretched to twice its height
/// -- which is the wrong shape to judge.
QString toAnsi(const Document &document, const QString &clip, int frame,
               bool checker = false);

QString toAnsi(const Document &document, const QString &clip, int frame,
               const Options &options, QStringList *diagnostics = nullptr);

/// The same, without colour: one character per slot. It is what a diff and a
/// test want, and what survives being pasted somewhere.
QString toText(const Document &document, const QString &clip, int frame);

QString toText(const Document &document, const QString &clip, int frame,
               const Options &options, QStringList *diagnostics = nullptr);

} // namespace render
} // namespace omapixel
