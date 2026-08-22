#pragma once

#include "Document.h"

#include <QColor>
#include <QImage>
#include <QString>

namespace omapixel {

/// Turning a document into something a person -- or an agent -- can look at.
///
/// This is the piece that takes the window out of the loop. An agent driving
/// the CLI could always change a drawing; what it could not do was SEE the
/// result, so it reached for a screenshot of a running studio. `toAnsi` puts
/// the frame in the terminal and `toImage` writes a PNG, and between them there
/// is nothing left that needs a window.
namespace render {

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
    /// Emits a warning before allocating an image above this many pixels.
    /// Zero disables the warning; it is not an allocation limit.
    qint64 warningPixels = 0;
};

QImage toImage(const Document &document, const QString &clip, int frame,
                const Options &options, QString *warning = nullptr,
                QString *error = nullptr);

/// Half-block characters, two sprite rows per terminal row, in 24-bit colour.
/// Two rows per line because a terminal cell is about twice as tall as it is
/// wide, and one row per line gives you a drawing stretched to twice its height
/// -- which is the wrong shape to judge.
QString toAnsi(const Document &document, const QString &clip, int frame,
               bool checker = false);

/// The same, without colour: one character per slot. It is what a diff and a
/// test want, and what survives being pasted somewhere.
QString toText(const Document &document, const QString &clip, int frame);

} // namespace render
} // namespace omapixel
