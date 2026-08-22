#include "Render.h"

#include <QPainter>

#include <limits>

namespace omapixel {
namespace render {
namespace {

QColor checkerAt(int x, int y, const Options &options)
{
    // In blocks of four sprite pixels, so the pattern zooms with the drawing
    // instead of turning into moire over the art.
    return ((x / 4) + (y / 4)) % 2 == 0 ? options.checkerDark : options.checkerLight;
}

bool multiply(qint64 left, qint64 right, qint64 *result)
{
    if (left != 0 && right > std::numeric_limits<qint64>::max() / left)
        return false;
    *result = left * right;
    return true;
}

} // namespace

QImage toImage(const Document &document, const QString &clip, int frame,
               const Options &options, QString *warning, QString *error)
{
    if (warning)
        warning->clear();
    if (error)
        error->clear();
    const Clip *found = document.clip(clip);
    if (!found || found->frames.isEmpty()) {
        if (error)
            *error = QStringLiteral("clip has no frames");
        return QImage();
    }

    const int scale = qBound(1, options.scale, 64);
    const int gap = options.sheet ? qMax(0, options.sheetGap) : 0;
    const qint64 frameCount = options.sheet ? found->frames.size() : 1;
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
    QPainter painter(&image);

    const int firstFrame = options.sheet
                               ? 0
                               : qBound(0, frame, found->frames.size() - 1);
    for (int index = 0; index < frameCount; ++index) {
        const Grid &grid = found->frames.at(firstFrame + index);
        const int originX = static_cast<int>(qint64(index) * stride);
        for (int y = 0; y < grid.rows(); ++y) {
            for (int x = 0; x < grid.columns(); ++x) {
                const QChar letter = grid.at(x, y);
                const QRect cell(originX + x * scale, y * scale, scale, scale);
                if (letter == Grid::Empty) {
                    if (options.checker)
                        painter.fillRect(cell, checkerAt(x, y, options));
                    continue;
                }
                const QColor colour = document.palette().colour(letter);
                // An unknown slot is skipped rather than guessed at. It is the
                // same thing every renderer in this family does, and the
                // checker underneath is what shows you it happened.
                if (!colour.isValid()) {
                    if (options.checker)
                        painter.fillRect(cell, checkerAt(x, y, options));
                    continue;
                }
                painter.fillRect(cell, colour);
            }
        }
    }
    return image;
}

QString toAnsi(const Document &document, const QString &clip, int frame,
               bool checker)
{
    const Clip *found = document.clip(clip);
    if (!found || found->frames.isEmpty())
        return QString();
    const Grid &grid = found->frames.at(qBound(0, frame, found->frames.size() - 1));

    const QString reset = QStringLiteral("\x1b[0m");
    QString out;
    for (int y = 0; y < grid.rows(); y += 2) {
        for (int x = 0; x < grid.columns(); ++x) {
            const auto colourOf = [&](int row) -> QColor {
                if (row >= grid.rows())
                    return QColor();
                const QChar letter = grid.at(x, row);
                if (letter == Grid::Empty)
                    return checker ? checkerAt(x, row, Options()) : QColor();
                const QColor colour = document.palette().colour(letter);
                return colour.isValid()
                           ? colour
                           : (checker ? checkerAt(x, row, Options()) : QColor());
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
    const Clip *found = document.clip(clip);
    if (!found || found->frames.isEmpty())
        return QString();
    const Grid &grid = found->frames.at(qBound(0, frame, found->frames.size() - 1));
    return grid.toRows().join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace render
} // namespace omapixel
