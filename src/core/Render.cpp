#include "Render.h"

#include <QPainter>

namespace omapixel {
namespace render {
namespace {

QColor checkerAt(int x, int y, const Options &options)
{
    // In blocks of four sprite pixels, so the pattern zooms with the drawing
    // instead of turning into moire over the art.
    return ((x / 4) + (y / 4)) % 2 == 0 ? options.checkerDark : options.checkerLight;
}

QList<Grid> framesFor(const Document &document, const QString &clip, int frame,
                      const Options &options)
{
    const Clip *found = document.clip(clip);
    if (!found || found->frames.isEmpty())
        return {};
    if (options.sheet)
        return found->frames;
    const int at = qBound(0, frame, found->frames.size() - 1);
    return {found->frames.at(at)};
}

} // namespace

QImage toImage(const Document &document, const QString &clip, int frame,
               const Options &options)
{
    const QList<Grid> frames = framesFor(document, clip, frame, options);
    if (frames.isEmpty())
        return QImage();

    const int scale = qBound(1, options.scale, 64);
    const int gap = options.sheet ? qMax(0, options.sheetGap) : 0;
    const int cellWidth = document.columns() * scale;
    const int cellHeight = document.rows() * scale;
    const int width = frames.size() * cellWidth + (frames.size() - 1) * gap * scale;

    QImage image(width, cellHeight, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);

    for (int index = 0; index < frames.size(); ++index) {
        const Grid &grid = frames.at(index);
        const int originX = index * (cellWidth + gap * scale);
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
