#include "Render.h"

#include <algorithm>
#include <limits>
#include <QSet>
#include <vector>

namespace omapixel {
namespace render {
namespace {

struct Pixel {
    int red = 0;
    int green = 0;
    int blue = 0;
    int alpha = 0;
};

int round255(int value)
{
    return (value + 127) / 255;
}

int straight(int premultiplied, int alpha)
{
    return alpha == 0 ? 0 : qBound(0, (premultiplied * 255 + alpha / 2) / alpha, 255);
}

int blendChannel(int source, int destination, int mode)
{
    if (mode == 1)
        return round255(source * destination);
    if (mode == 2)
        return 255 - round255((255 - source) * (255 - destination));
    return source;
}

QRgb checkerAt(int x, int y, QRgb dark, QRgb light)
{
    // In blocks of four sprite pixels, so the pattern zooms with the drawing
    // instead of turning into moire over the art.
    return ((x / 4) + (y / 4)) % 2 == 0 ? dark : light;
}

bool multiply(qint64 left, qint64 right, qint64 *result)
{
    if (left != 0 && right > std::numeric_limits<qint64>::max() / left)
        return false;
    *result = left * right;
    return true;
}

bool terminalBudget(const Document &document, bool ansi, QString *error)
{
    const qint64 lines = ansi ? (qint64(document.rows()) + 1) / 2
                              : document.rows();
    qint64 cells = 0;
    if (!multiply(document.columns(), lines, &cells)
        || cells > maxTerminalCells) {
        if (error)
            *error = QStringLiteral("terminal output exceeds hard cell limit of %1")
                         .arg(maxTerminalCells);
        return false;
    }
    // A BMP slot is at most three UTF-8 bytes. ANSI adds two colour sequences,
    // a reset, and a newline; use a fixed upper bound before allocating output.
    const qint64 bytesPerCell = ansi ? 64 : 4;
    qint64 bytes = 0;
    if (!multiply(cells, bytesPerCell, &bytes)
        || bytes > std::numeric_limits<qint64>::max() - lines
        || bytes + lines > maxTerminalBytes) {
        if (error)
            *error = QStringLiteral("terminal output exceeds hard byte limit of %1 MiB")
                         .arg(maxTerminalBytes / (1024 * 1024));
        return false;
    }
    return true;
}

QStringList diagnosticsFor(const QSet<QChar> &unknown)
{
    QStringList unknownSlots;
    for (const QChar slot : unknown)
        unknownSlots.append(QString(slot));
    unknownSlots.sort();
    QStringList diagnostics;
    for (const QString &slot : unknownSlots)
        diagnostics.append(QStringLiteral("unknown palette slot `%1` skipped").arg(slot));
    return diagnostics;
}

bool composeFrame(const Document &document, const QString &clipName, int frame,
                 const Options &options, QVector<Pixel> *output,
                 QSet<QChar> *unknown, QString *error)
{
    const Clip *clip = document.clip(clipName);
    if (!clip || clip->frameCount <= 0) {
        if (error)
            *error = QStringLiteral("clip has no frames");
        return false;
    }
    if (frame < 0 || frame >= clip->frameCount) {
        if (error)
            *error = QStringLiteral("frame %1 is outside 0..%2")
                         .arg(frame).arg(clip->frameCount - 1);
        return false;
    }
    const int boundedFrame = frame;
    const Layer *isolated = nullptr;
    if (options.isolated) {
        isolated = document.layer(options.layer);
        if (!isolated) {
            if (error)
                *error = QStringLiteral("layer not found: %1").arg(options.layer);
            return false;
        }
    }

    output->resize(document.columns() * document.rows());
    const QRgb dark = options.checkerDark.rgba();
    const QRgb light = options.checkerLight.rgba();
    for (int y = 0; y < document.rows(); ++y) {
        for (int x = 0; x < document.columns(); ++x) {
            Pixel &pixel = (*output)[y * document.columns() + x];
            if (options.checker) {
                const QRgb colour = checkerAt(x, y, dark, light);
                pixel.red = qRed(colour);
                pixel.green = qGreen(colour);
                pixel.blue = qBlue(colour);
                pixel.alpha = 255;
            } else {
                pixel = Pixel();
            }
        }
    }

    std::vector<QRgb> colours(1 << 16);
    std::vector<unsigned char> known(1 << 16);
    for (const Palette::Slot &slot : document.palette().entries()) {
        const ushort letter = slot.letter.unicode();
        colours[letter] = slot.colour.rgba();
        known[letter] = 1;
    }

    for (const Layer &layer : document.layers()) {
        if (options.isolated && &layer != isolated)
            continue;
        if ((!layer.visible && !(options.isolated && options.includeHidden))
            || layer.opacity == 0)
            continue;
        const Grid grid = document.cel(layer.id, clip->id, boundedFrame);
        if (grid.columns() != document.columns() || grid.rows() != document.rows()) {
            if (error)
                *error = QStringLiteral("layer %1 has no valid cel for frame %2")
                             .arg(layer.id).arg(boundedFrame);
            return false;
        }
        const int mode = layer.mode == QLatin1String("multiply")
                             ? 1
                             : layer.mode == QLatin1String("screen") ? 2 : 0;

        const QChar *cells = grid.constData();
        for (int y = 0; y < grid.rows(); ++y) {
            for (int x = 0; x < grid.columns(); ++x) {
                const QChar letter = cells[y * grid.columns() + x];
                if (letter == Grid::Empty)
                    continue;
                if (!known[letter.unicode()]) {
                    if (unknown)
                        unknown->insert(letter);
                    continue;
                }
                const QRgb colour = colours[letter.unicode()];
                const int sourceAlpha = round255(qAlpha(colour) * layer.opacity);
                if (sourceAlpha == 0)
                    continue;
                Pixel &destination = (*output)[y * document.columns() + x];
                const int red = blendChannel(qRed(colour), destination.red, mode);
                const int green = blendChannel(qGreen(colour), destination.green, mode);
                const int blue = blendChannel(qBlue(colour), destination.blue, mode);
                if (sourceAlpha == 255) {
                    destination.red = red;
                    destination.green = green;
                    destination.blue = blue;
                    destination.alpha = 255;
                    continue;
                }
                const int sourceRed = round255(red * sourceAlpha);
                const int sourceGreen = round255(green * sourceAlpha);
                const int sourceBlue = round255(blue * sourceAlpha);
                const int inverseAlpha = 255 - sourceAlpha;
                const int destinationRed = round255(destination.red * destination.alpha);
                const int destinationGreen = round255(destination.green * destination.alpha);
                const int destinationBlue = round255(destination.blue * destination.alpha);
                const int outputAlpha = sourceAlpha
                    + round255(destination.alpha * inverseAlpha);
                destination.red = straight(sourceRed
                                             + round255(destinationRed * inverseAlpha),
                                           outputAlpha);
                destination.green = straight(sourceGreen
                                               + round255(destinationGreen * inverseAlpha),
                                             outputAlpha);
                destination.blue = straight(sourceBlue
                                              + round255(destinationBlue * inverseAlpha),
                                            outputAlpha);
                destination.alpha = outputAlpha;
            }
        }
    }
    return true;
}

void writePixels(QImage *image, const QVector<Pixel> &pixels, int columns,
                 int rows, int scale, int originX)
{
    for (int y = 0; y < rows; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image->scanLine(y * scale)) + originX;
        for (int x = 0; x < columns; ++x) {
            const Pixel &pixel = pixels[y * columns + x];
            const QRgb colour = qRgba(pixel.red, pixel.green, pixel.blue, pixel.alpha);
            std::fill_n(line + x * scale, scale, colour);
        }
        for (int offset = 1; offset < scale; ++offset)
            std::copy(line, line + columns * scale,
                      reinterpret_cast<QRgb *>(image->scanLine(y * scale + offset))
                          + originX);
    }
}

Grid flatten(const Document &document, const QVector<Pixel> &pixels,
             QuantizationReport *report)
{
    Grid grid(document.columns(), document.rows());
    if (report)
        *report = QuantizationReport();
    const QList<Palette::Slot> palette = document.palette().entries();
    for (int y = 0; y < document.rows(); ++y) {
        for (int x = 0; x < document.columns(); ++x) {
            const Pixel &pixel = pixels[y * document.columns() + x];
            if (pixel.alpha == 0)
                continue;
            if (report)
                ++report->composedPixels;
            qint64 bestDistance = std::numeric_limits<qint64>::max();
            int bestIndex = -1;
            QChar bestSlot;
            for (int index = 0; index < palette.size(); ++index) {
                const QColor &candidate = palette.at(index).colour;
                const qint64 dr = pixel.red - candidate.red();
                const qint64 dg = pixel.green - candidate.green();
                const qint64 db = pixel.blue - candidate.blue();
                const qint64 da = pixel.alpha - candidate.alpha();
                const qint64 distance = dr * dr + dg * dg + db * db + da * da;
                const QChar slot = palette.at(index).letter;
                if (distance < bestDistance
                    || (distance == bestDistance
                        && (bestIndex < 0 || index < bestIndex
                            || (index == bestIndex && slot < bestSlot)))) {
                    bestDistance = distance;
                    bestIndex = index;
                    bestSlot = slot;
                }
            }
            if (bestIndex >= 0) {
                grid.set(x, y, bestSlot);
                if (report) {
                    const QColor &selected = palette.at(bestIndex).colour;
                    if (pixel.red == selected.red() && pixel.green == selected.green()
                        && pixel.blue == selected.blue()
                        && pixel.alpha == selected.alpha())
                        ++report->exactMatches;
                    else
                        ++report->approximatedPixels;
                }
            } else if (report) {
                report->diagnostics.append(QStringLiteral(
                    "no palette slot can represent composed pixel at %1,%2")
                                                .arg(x).arg(y));
            }
        }
    }
    return grid;
}

} // namespace

QImage toImage(const Document &document, const QString &clip, int frame,
               const Options &options, QString *warning, QString *error,
               QStringList *diagnostics)
{
    if (warning)
        warning->clear();
    if (error)
        error->clear();
    if (diagnostics)
        diagnostics->clear();
    const Clip *found = document.clip(clip);
    if (!found || found->frameCount <= 0) {
        if (error)
            *error = QStringLiteral("clip has no frames");
        return QImage();
    }

    if (options.scale < 1 || options.scale > 64) {
        if (error)
            *error = QStringLiteral("render scale must be between 1 and 64");
        return QImage();
    }
    if (frame < 0 || frame >= found->frameCount) {
        if (error)
            *error = QStringLiteral("frame %1 is outside 0..%2")
                         .arg(frame).arg(found->frameCount - 1);
        return QImage();
    }
    const int scale = options.scale;
    const int gap = options.sheet ? qMax(0, options.sheetGap) : 0;
    const qint64 frameCount = options.sheet ? found->frameCount : 1;
    const qint64 cellWidth = qint64(document.columns()) * scale;
    const qint64 cellHeight = qint64(document.rows()) * scale;
    const qint64 scaledGap = qint64(gap) * scale;
    const qint64 stride = cellWidth + scaledGap;
    qint64 remainingWidth = 0;
    if (!multiply(frameCount - 1, stride, &remainingWidth)
        || remainingWidth > std::numeric_limits<qint64>::max() - cellWidth) {
        if (error)
            *error = QStringLiteral("render dimensions overflow");
        return QImage();
    }
    const qint64 width64 = cellWidth + remainingWidth;
    if (width64 > std::numeric_limits<int>::max()
        || cellHeight > std::numeric_limits<int>::max()) {
        if (error)
            *error = QStringLiteral("render dimensions exceed QImage limits");
        return QImage();
    }
    qint64 pixels = 0;
    if (!multiply(width64, cellHeight, &pixels)) {
        if (error)
            *error = QStringLiteral("render pixel count overflow");
        return QImage();
    }
    qint64 bytes = 0;
    if (!multiply(pixels, 4, &bytes)
        || pixels > maxImagePixels || bytes > maxImageBytes) {
        if (error)
            *error = QStringLiteral("render exceeds hard budget (%1 pixels, %2 MiB)")
                         .arg(maxImagePixels)
                         .arg(maxImageBytes / (1024 * 1024));
        return QImage();
    }
    if (warning && options.warningPixels > 0 && pixels > options.warningPixels) {
        *warning = QStringLiteral("render is %1 megapixels; warning threshold is %2")
                       .arg(double(pixels) / 1000000.0, 0, 'f', 1)
                       .arg(double(options.warningPixels) / 1000000.0, 0, 'f', 1);
    }

    const int width = static_cast<int>(width64);
    const int height = static_cast<int>(cellHeight);

    QImage image(width, height, QImage::Format_ARGB32);
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("could not allocate %1x%2 image").arg(width).arg(height);
        return image;
    }
    image.fill(Qt::transparent);
    QSet<QChar> unknown;
    const int firstFrame = options.sheet ? 0 : frame;
    for (int index = 0; index < frameCount; ++index) {
        QVector<Pixel> pixels;
        QString composeError;
        if (!composeFrame(document, found->id, firstFrame + index, options,
                          &pixels, &unknown, &composeError)) {
            if (error)
                *error = composeError;
            return QImage();
        }
        const int originX = static_cast<int>(qint64(index) * stride);
        writePixels(&image, pixels, document.columns(), document.rows(), scale, originX);
    }
    const QStringList foundDiagnostics = diagnosticsFor(unknown);
    if (diagnostics)
        *diagnostics = foundDiagnostics;
    if (warning && !foundDiagnostics.isEmpty()) {
        if (!warning->isEmpty())
            *warning += QStringLiteral(" · ");
        *warning += foundDiagnostics.join(QStringLiteral("; "));
    }
    return image;
}

QString toAnsi(const Document &document, const QString &clip, int frame,
               bool checker)
{
    Options options;
    options.checker = checker;
    return toAnsi(document, clip, frame, options);
}

QString toAnsi(const Document &document, const QString &clip, int frame,
               const Options &options, QStringList *diagnostics)
{
    if (diagnostics)
        diagnostics->clear();
    QString budgetError;
    if (!terminalBudget(document, true, &budgetError)) {
        if (diagnostics)
            diagnostics->append(budgetError);
        return QString();
    }
    QVector<Pixel> pixels;
    QSet<QChar> unknown;
    QString error;
    if (!composeFrame(document, clip, frame, options, &pixels, &unknown, &error))
        return QString();
    if (diagnostics)
        *diagnostics = diagnosticsFor(unknown);

    const QString reset = QStringLiteral("\x1b[0m");
    QString out;
    for (int y = 0; y < document.rows(); y += 2) {
        for (int x = 0; x < document.columns(); ++x) {
            const auto colourOf = [&](int row) -> QColor {
                if (row >= document.rows())
                    return QColor();
                const Pixel &pixel = pixels[row * document.columns() + x];
                return pixel.alpha == 0
                           ? QColor()
                           : QColor(pixel.red, pixel.green, pixel.blue, pixel.alpha);
            };
            const QColor upper = colourOf(y);
            const QColor lower = colourOf(y + 1);

            if (!upper.isValid() && !lower.isValid()) {
                out += QLatin1Char(' ');
                continue;
            }
            // The upper half block with a foreground and a background paints
            // both rows in one cell.
            if (upper.isValid()) {
                out += QStringLiteral("\x1b[38;2;%1;%2;%3m")
                           .arg(upper.red()).arg(upper.green()).arg(upper.blue());
            }
            if (lower.isValid()) {
                out += QStringLiteral("\x1b[48;2;%1;%2;%3m")
                           .arg(lower.red()).arg(lower.green()).arg(lower.blue());
            }
            out += upper.isValid() ? QStringLiteral("▀")
                                   : QStringLiteral("▄");
            if (!upper.isValid()) {
                // Only a lower half: it was painted as foreground, so undo the
                // background we never set.
                out.chop(1);
                out += QStringLiteral("\x1b[38;2;%1;%2;%3m▄")
                           .arg(lower.red()).arg(lower.green()).arg(lower.blue());
            }
            out += reset;
        }
        out += QLatin1Char('\n');
    }
    return out;
}

QString toText(const Document &document, const QString &clip, int frame)
{
    return toText(document, clip, frame, Options());
}

QString toText(const Document &document, const QString &clip, int frame,
               const Options &options, QStringList *diagnostics)
{
    if (diagnostics)
        diagnostics->clear();
    QString budgetError;
    if (!terminalBudget(document, false, &budgetError)) {
        if (diagnostics)
            diagnostics->append(budgetError);
        return QString();
    }
    Grid grid = toGrid(document, clip, frame, options, diagnostics);
    if (grid.columns() == 0 || grid.rows() == 0)
        return QString();
    return grid.toRows().join(QLatin1Char('\n')) + QLatin1Char('\n');
}

Grid toGrid(const Document &document, const QString &clip, int frame,
            const Options &options, QStringList *diagnostics,
            QuantizationReport *report)
{
    // A grid is document content, not a visual background. The checker belongs
    // only to image/ANSI surfaces, so text always preserves `.` transparency.
    Options contentOptions = options;
    contentOptions.checker = false;
    if (report)
        *report = QuantizationReport();
    QVector<Pixel> pixels;
    QSet<QChar> unknown;
    QString error;
    if (!composeFrame(document, clip, frame, contentOptions, &pixels, &unknown,
                      &error)) {
        if (diagnostics)
            *diagnostics = diagnosticsFor(unknown);
        return Grid();
    }
    if (diagnostics)
        *diagnostics = diagnosticsFor(unknown);
    const Grid grid = flatten(document, pixels, report);
    if (report)
        report->diagnostics.append(diagnosticsFor(unknown));
    return grid;
}

} // namespace render
} // namespace omapixel
