#pragma once

#include "Document.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace omapixel {
namespace gif {

/// GIF has no useful alpha channel. Pixels with alpha < 128 are encoded as
/// transparent (palette index zero); pixels with alpha >= 128 are opaque.
static constexpr int alphaThreshold = 128;

/// Encodes every frame of `clip` as a GIF89a animation.
///
/// `scale` is the same integer nearest-neighbour scale accepted by
/// render::toImage. A zero `fpsOverride` uses the clip FPS. GIF delays are
/// distributed in centiseconds with a deterministic cumulative-floor sequence,
/// so the long-run average is the requested FPS. An effective FPS from 1 to 100
/// is required because GIF delays cannot represent less than one centisecond.
///
/// The global palette always reserves index zero for binary transparency and
/// contains at most 255 opaque colours. Quantization is global, deterministic,
/// and does not dither.
QByteArray encode(const Document &document, const QString &clip, int scale = 1,
                  int fpsOverride = 0, bool loop = true,
                  QString *error = nullptr);

/// Encodes and atomically writes a GIF. `sources` can contain paths that must
/// not be overwritten, using the same safety checks as other core outputs.
bool write(const Document &document, const QString &clip, const QString &path,
           int scale = 1, int fpsOverride = 0, bool loop = true,
           const QStringList &sources = {}, QString *error = nullptr);

} // namespace gif
} // namespace omapixel
