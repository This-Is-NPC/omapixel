#include "Quantization.h"

#include <QMap>
#include <QHash>
#include <QVector>

#include <algorithm>
#include <limits>

namespace omapixel {
namespace {

struct ColourCount {
    QRgb rgba = 0;
    qint64 count = 0;
};

struct Box {
    QVector<int> indices;
};

int channel(QRgb rgba, int index)
{
    switch (index) {
    case 0: return qRed(rgba);
    case 1: return qGreen(rgba);
    case 2: return qBlue(rgba);
    default: return qAlpha(rgba);
    }
}

bool sameColour(QRgb rgba, const QColor &colour)
{
    return qRed(rgba) == colour.red()
        && qGreen(rgba) == colour.green()
        && qBlue(rgba) == colour.blue()
        && qAlpha(rgba) == colour.alpha();
}

QChar nextSlot(const Palette &palette)
{
    // This order is stable and keeps imported rows readable. The fallback is
    // deliberately bounded to BMP so one slot always remains one QChar.
    const QString preferred = QStringLiteral(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    for (const QChar candidate : preferred) {
        if (!palette.has(candidate) && Palette::validSlot(candidate))
            return candidate;
    }
    for (ushort code = 0x21; code <= 0x7e; ++code) {
        const QChar candidate(code);
        if (!palette.has(candidate) && Palette::validSlot(candidate))
            return candidate;
    }
    for (ushort code = 0xa1; code < 0xd800; ++code) {
        const QChar candidate(code);
        if (!palette.has(candidate) && Palette::validSlot(candidate))
            return candidate;
    }
    return QChar();
}

int bestSplitBox(const QVector<Box> &boxes, const QVector<ColourCount> &histogram,
                 int *channelOut)
{
    int selected = -1;
    int selectedChannel = -1;
    int selectedRange = -1;
    qint64 selectedWeight = -1;
    for (int boxIndex = 0; boxIndex < boxes.size(); ++boxIndex) {
        const Box &box = boxes.at(boxIndex);
        if (box.indices.size() < 2)
            continue;
        qint64 weight = 0;
        for (const int index : box.indices)
            weight += histogram.at(index).count;
        for (int dimension = 0; dimension < 4; ++dimension) {
            int low = 255;
            int high = 0;
            for (const int index : box.indices) {
                const int value = channel(histogram.at(index).rgba, dimension);
                low = qMin(low, value);
                high = qMax(high, value);
            }
            const int range = high - low;
            if (range > selectedRange
                || (range == selectedRange && weight > selectedWeight)) {
                selected = boxIndex;
                selectedChannel = dimension;
                selectedRange = range;
                selectedWeight = weight;
            }
        }
    }
    if (channelOut)
        *channelOut = selectedChannel;
    return selected;
}

QColor representative(const Box &box, const QVector<ColourCount> &histogram)
{
    qint64 red = 0;
    qint64 green = 0;
    qint64 blue = 0;
    qint64 alpha = 0;
    qint64 weight = 0;
    for (const int index : box.indices) {
        const ColourCount &entry = histogram.at(index);
        weight += entry.count;
        red += qint64(qRed(entry.rgba)) * entry.count;
        green += qint64(qGreen(entry.rgba)) * entry.count;
        blue += qint64(qBlue(entry.rgba)) * entry.count;
        alpha += qint64(qAlpha(entry.rgba)) * entry.count;
    }
    if (weight <= 0)
        return QColor();
    return QColor(int((red + weight / 2) / weight),
                  int((green + weight / 2) / weight),
                  int((blue + weight / 2) / weight),
                  int((alpha + weight / 2) / weight));
}

qint64 distance(QRgb rgba, const QColor &colour)
{
    const qint64 red = qRed(rgba) - colour.red();
    const qint64 green = qGreen(rgba) - colour.green();
    const qint64 blue = qBlue(rgba) - colour.blue();
    const qint64 alpha = qAlpha(rgba) - colour.alpha();
    return red * red + green * green + blue * blue + alpha * alpha;
}

} // namespace

Quantization::Result Quantization::run(const QImage &image, const Palette &fixed,
                                        const Options &options)
{
    Result result;
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        result.error = QStringLiteral("E_QUANTIZE_IMAGE: image is empty");
        return result;
    }
    if (options.maxColors < 1 || options.maxColors > Palette::maxSlots) {
        result.error = QStringLiteral("E_QUANTIZE_LIMIT: maxColors must be between 1 and %1")
                           .arg(Palette::maxSlots);
        return result;
    }
    if (fixed.size() > options.maxColors) {
        result.error = QStringLiteral("E_QUANTIZE_LIMIT: fixed palette has %1 slots, limit is %2")
                           .arg(fixed.size()).arg(options.maxColors);
        return result;
    }
    if (options.maxNewSlots < -1) {
        result.error = QStringLiteral("E_QUANTIZE_LIMIT: maxNewSlots cannot be negative");
        return result;
    }

    const qint64 pixels = qint64(image.width()) * image.height();
    result.report.inputPixels = pixels;
    result.report.fixedSlots = fixed.size();
    result.palette = fixed;

    const QImage rgbaImage = image.format() == QImage::Format_RGBA8888
                                 ? image
                                 : image.convertToFormat(QImage::Format_RGBA8888);
    if (rgbaImage.isNull()) {
        result.error = QStringLiteral("E_QUANTIZE_IMAGE: could not convert image to RGBA");
        return result;
    }

    QMap<quint32, qint64> counts;
    for (int y = 0; y < rgbaImage.height(); ++y) {
        const uchar *line = rgbaImage.constScanLine(y);
        for (int x = 0; x < rgbaImage.width(); ++x) {
            const QRgb colour = qRgba(line[x * 4], line[x * 4 + 1],
                                      line[x * 4 + 2], line[x * 4 + 3]);
            if (qAlpha(colour) == 0) {
                ++result.report.transparentPixels;
                continue;
            }
            ++counts[quint32(colour)];
        }
    }

    QVector<ColourCount> unmatched;
    unmatched.reserve(counts.size());
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        bool found = false;
        for (const Palette::Slot &slot : fixed.entries()) {
            if (sameColour(QRgb(it.key()), slot.colour)) {
                found = true;
                break;
            }
        }
        if (!found)
            unmatched.append({QRgb(it.key()), it.value()});
    }

    int capacity = options.maxColors - fixed.size();
    if (options.maxNewSlots >= 0)
        capacity = qMin(capacity, options.maxNewSlots);
    QVector<Box> boxes;
    if (!unmatched.isEmpty() && capacity > 0) {
        Box initial;
        initial.indices.reserve(unmatched.size());
        for (int i = 0; i < unmatched.size(); ++i)
            initial.indices.append(i);
        boxes.append(initial);
        while (boxes.size() < capacity) {
            int splitChannel = -1;
            const int selected = bestSplitBox(boxes, unmatched, &splitChannel);
            if (selected < 0)
                break;
            QVector<int> indices = boxes.at(selected).indices;
            std::sort(indices.begin(), indices.end(), [&](int left, int right) {
                const int leftChannel = channel(unmatched.at(left).rgba, splitChannel);
                const int rightChannel = channel(unmatched.at(right).rgba, splitChannel);
                if (leftChannel != rightChannel)
                    return leftChannel < rightChannel;
                return unmatched.at(left).rgba < unmatched.at(right).rgba;
            });
            qint64 total = 0;
            for (const int index : indices)
                total += unmatched.at(index).count;
            const qint64 halfway = (total + 1) / 2;
            qint64 accumulated = 0;
            int split = 1;
            for (int i = 0; i < indices.size(); ++i) {
                accumulated += unmatched.at(indices.at(i)).count;
                if (accumulated >= halfway) {
                    split = i + 1;
                    break;
                }
            }
            if (split >= indices.size())
                split = indices.size() / 2;
            if (split <= 0 || split >= indices.size())
                break;
            Box left;
            Box right;
            left.indices = indices.mid(0, split);
            right.indices = indices.mid(split);
            boxes[selected] = left;
            boxes.insert(selected + 1, right);
        }
    }

    for (const Box &box : boxes) {
        const QColor colour = representative(box, unmatched);
        if (!colour.isValid())
            continue;
        bool duplicate = false;
        for (const Palette::Slot &slot : result.palette.entries()) {
            if (slot.colour == colour) {
                duplicate = true;
                break;
            }
        }
        if (duplicate || result.palette.size() >= options.maxColors)
            continue;
        const QChar slot = nextSlot(result.palette);
        if (slot.isNull() || !result.palette.set(slot, colour)) {
            result.error = QStringLiteral("E_QUANTIZE_PALETTE: no valid free palette slot");
            return result;
        }
    }
    result.report.newSlots = result.palette.size() - fixed.size();

    result.grid = Grid(rgbaImage.width(), rgbaImage.height());
    const QList<Palette::Slot> candidates = result.palette.entries();
    if (!counts.isEmpty() && candidates.isEmpty()) {
        result.error = QStringLiteral("E_QUANTIZE_PALETTE: no palette slot can represent pixels");
        return result;
    }
    QHash<quint32, QChar> mappedColours;
    mappedColours.reserve(counts.size());
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        const QRgb source = QRgb(it.key());
        qint64 best = std::numeric_limits<qint64>::max();
        int bestIndex = -1;
        for (int index = 0; index < candidates.size(); ++index) {
            const qint64 candidateDistance = distance(source, candidates.at(index).colour);
            if (candidateDistance < best) {
                best = candidateDistance;
                bestIndex = index;
            }
        }
        if (bestIndex < 0) {
            result.error = QStringLiteral("E_QUANTIZE_PALETTE: colour has no representative");
            return result;
        }
        mappedColours.insert(it.key(), candidates.at(bestIndex).letter);
        result.report.representedPixels += it.value();
        if (sameColour(source, candidates.at(bestIndex).colour))
            result.report.exactMatches += it.value();
        else
            result.report.approximatedPixels += it.value();
    }
    for (int y = 0; y < rgbaImage.height(); ++y) {
        const uchar *line = rgbaImage.constScanLine(y);
        for (int x = 0; x < rgbaImage.width(); ++x) {
            const QRgb source = qRgba(line[x * 4], line[x * 4 + 1],
                                     line[x * 4 + 2], line[x * 4 + 3]);
            if (qAlpha(source) == 0)
                continue;
            const auto mapped = mappedColours.constFind(quint32(source));
            if (mapped == mappedColours.constEnd()) {
                result.error = QStringLiteral("E_QUANTIZE_PALETTE: pixel has no representative");
                return result;
            }
            result.grid.set(x, y, mapped.value());
        }
    }
    if (!unmatched.isEmpty() && result.report.newSlots == 0)
        result.report.diagnostics.append(QStringLiteral(
            "all imported colours were approximated using the fixed palette"));
    result.ok = true;
    return result;
}

Quantization::Result Quantization::run(const QImage &image, const Palette &fixed)
{
    return run(image, fixed, Options());
}

Quantization::Result Quantization::run(const QImage &image)
{
    return run(image, Palette(), Options());
}

} // namespace omapixel
