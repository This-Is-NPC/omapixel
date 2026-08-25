#include "GifExport.h"

#include "Output.h"
#include "Render.h"

#include <QHash>
#include <QImage>
#include <QVector>

#include <algorithm>
#include <limits>
#include <utility>

namespace omapixel {
namespace gif {
namespace {

constexpr qint64 maxHistogramColours = 2'000'000;
constexpr qint64 maxTotalPixels = 256'000'000;
constexpr qint64 maxGifBytes = 256 * 1024 * 1024;

struct ColourPoint
{
    quint32 key = 0;
    int red = 0;
    int green = 0;
    int blue = 0;
    qint64 count = 0;
};

struct ColourBox
{
    QVector<int> points;
    int minRed = 0;
    int maxRed = 0;
    int minGreen = 0;
    int maxGreen = 0;
    int minBlue = 0;
    int maxBlue = 0;
    qint64 population = 0;
};

bool multiply(qint64 left, qint64 right, qint64 *result)
{
    if (left < 0 || right < 0
        || (left != 0 && right > std::numeric_limits<qint64>::max() / left))
        return false;
    *result = left * right;
    return true;
}

bool add(qint64 left, qint64 right, qint64 *result)
{
    if (right < 0 || left > std::numeric_limits<qint64>::max() - right)
        return false;
    *result = left + right;
    return true;
}

QString frameError(int frame, const QString &error)
{
    return QStringLiteral("GIF frame %1: %2").arg(frame).arg(error);
}

ColourBox makeBox(const QVector<ColourPoint> &colours, QVector<int> points)
{
    ColourBox box;
    box.points = std::move(points);
    const ColourPoint &first = colours.at(box.points.constFirst());
    box.minRed = box.maxRed = first.red;
    box.minGreen = box.maxGreen = first.green;
    box.minBlue = box.maxBlue = first.blue;
    for (const int pointIndex : box.points) {
        const ColourPoint &point = colours.at(pointIndex);
        box.minRed = qMin(box.minRed, point.red);
        box.maxRed = qMax(box.maxRed, point.red);
        box.minGreen = qMin(box.minGreen, point.green);
        box.maxGreen = qMax(box.maxGreen, point.green);
        box.minBlue = qMin(box.minBlue, point.blue);
        box.maxBlue = qMax(box.maxBlue, point.blue);
        box.population += point.count;
    }
    return box;
}

int rangeOf(const ColourBox &box, int channel)
{
    if (channel == 0)
        return box.maxRed - box.minRed;
    if (channel == 1)
        return box.maxGreen - box.minGreen;
    return box.maxBlue - box.minBlue;
}

int channelValue(const ColourPoint &point, int channel)
{
    if (channel == 0)
        return point.red;
    if (channel == 1)
        return point.green;
    return point.blue;
}

QVector<QRgb> makePalette(const QVector<ColourPoint> &colours)
{
    QVector<QRgb> palette;
    palette.reserve(256);
    // Index zero is intentionally never used for an opaque colour.
    palette.append(qRgb(0, 0, 0));

    if (colours.size() <= 255) {
        QVector<ColourPoint> ordered = colours;
        std::sort(ordered.begin(), ordered.end(), [](const ColourPoint &left,
                                                     const ColourPoint &right) {
            if (left.count != right.count)
                return left.count > right.count;
            return left.key < right.key;
        });
        for (const ColourPoint &colour : ordered)
            palette.append(qRgb(colour.red, colour.green, colour.blue));
        return palette;
    }

    QVector<int> all;
    all.reserve(colours.size());
    for (int index = 0; index < colours.size(); ++index)
        all.append(index);

    QVector<ColourBox> boxes;
    boxes.reserve(255);
    boxes.append(makeBox(colours, std::move(all)));
    while (boxes.size() < 255) {
        int selected = -1;
        int selectedRange = -1;
        qint64 selectedPopulation = -1;
        quint32 selectedKey = std::numeric_limits<quint32>::max();
        for (int index = 0; index < boxes.size(); ++index) {
            const ColourBox &box = boxes.at(index);
            if (box.points.size() < 2)
                continue;
            const int range = qMax(rangeOf(box, 0),
                                   qMax(rangeOf(box, 1), rangeOf(box, 2)));
            const quint32 firstKey = colours.at(box.points.constFirst()).key;
            if (range > selectedRange
                || (range == selectedRange && box.population > selectedPopulation)
                || (range == selectedRange && box.population == selectedPopulation
                    && firstKey < selectedKey)) {
                selected = index;
                selectedRange = range;
                selectedPopulation = box.population;
                selectedKey = firstKey;
            }
        }
        if (selected < 0)
            break;

        const ColourBox original = boxes.takeAt(selected);
        int channel = 0;
        if (rangeOf(original, 1) > rangeOf(original, channel))
            channel = 1;
        if (rangeOf(original, 2) > rangeOf(original, channel))
            channel = 2;
        QVector<int> ordered = original.points;
        std::sort(ordered.begin(), ordered.end(), [&](int left, int right) {
            const ColourPoint &a = colours.at(left);
            const ColourPoint &b = colours.at(right);
            const int aChannel = channelValue(a, channel);
            const int bChannel = channelValue(b, channel);
            if (aChannel != bChannel)
                return aChannel < bChannel;
            return a.key < b.key;
        });

        const qint64 wanted = (original.population + 1) / 2;
        qint64 accumulated = 0;
        int split = 1;
        for (int index = 0; index < ordered.size() - 1; ++index) {
            accumulated += colours.at(ordered.at(index)).count;
            if (accumulated >= wanted) {
                split = index + 1;
                break;
            }
        }
        QVector<int> left;
        QVector<int> right;
        left.reserve(split);
        right.reserve(ordered.size() - split);
        for (int index = 0; index < ordered.size(); ++index) {
            if (index < split)
                left.append(ordered.at(index));
            else
                right.append(ordered.at(index));
        }
        boxes.insert(selected, makeBox(colours, std::move(left)));
        boxes.insert(selected + 1, makeBox(colours, std::move(right)));
    }

    for (const ColourBox &box : boxes) {
        qint64 red = 0;
        qint64 green = 0;
        qint64 blue = 0;
        for (const int pointIndex : box.points) {
            const ColourPoint &point = colours.at(pointIndex);
            red += point.red * point.count;
            green += point.green * point.count;
            blue += point.blue * point.count;
        }
        const qint64 divisor = qMax<qint64>(1, box.population);
        palette.append(qRgb(int((red + divisor / 2) / divisor),
                            int((green + divisor / 2) / divisor),
                            int((blue + divisor / 2) / divisor)));
    }
    return palette;
}

int tableBitsFor(int colourCount)
{
    int bits = 0;
    while ((1 << bits) < colourCount)
        ++bits;
    return qMax(1, bits);
}

int colourDistance(QRgb left, const ColourPoint &right)
{
    const int red = qRed(left) - right.red;
    const int green = qGreen(left) - right.green;
    const int blue = qBlue(left) - right.blue;
    return red * red + green * green + blue * blue;
}

QHash<quint32, quint8> makeColourIndex(const QVector<ColourPoint> &colours,
                                       const QVector<QRgb> &palette)
{
    QHash<quint32, quint8> indices;
    indices.reserve(colours.size());
    for (const ColourPoint &colour : colours) {
        int best = 1;
        int distance = std::numeric_limits<int>::max();
        for (int index = 1; index < palette.size(); ++index) {
            const int candidateDistance = colourDistance(palette.at(index), colour);
            if (candidateDistance < distance) {
                distance = candidateDistance;
                best = index;
            }
        }
        indices.insert(colour.key, quint8(best));
    }
    return indices;
}

class Writer
{
public:
    explicit Writer(QByteArray *output, QString *error)
        : m_output(output), m_error(error)
    {
    }

    bool byte(int value)
    {
        const char value8 = char(value & 0xff);
        return bytes(&value8, 1);
    }

    bool littleEndian16(int value)
    {
        return byte(value) && byte(value >> 8);
    }

    bool bytes(const char *data, qint64 count)
    {
        if (!m_output || count < 0 || count > std::numeric_limits<int>::max()
            || m_output->size() > maxGifBytes - count) {
            fail(QStringLiteral("GIF output exceeds hard limit of %1 MiB")
                     .arg(maxGifBytes / (1024 * 1024)));
            return false;
        }
        m_output->append(data, int(count));
        return true;
    }

    bool text(const char *data, int count)
    {
        return bytes(data, count);
    }

    void fail(const QString &message)
    {
        if (m_error && m_error->isEmpty())
            *m_error = message;
    }

private:
    QByteArray *m_output = nullptr;
    QString *m_error = nullptr;
};

class SubBlocks
{
public:
    explicit SubBlocks(Writer *writer) : m_writer(writer) {}

    bool append(int value)
    {
        m_block.append(char(value & 0xff));
        if (m_block.size() == 255)
            return flush();
        return true;
    }

    bool finish()
    {
        return m_block.isEmpty() || flush();
    }

private:
    bool flush()
    {
        if (!m_writer->byte(m_block.size())
            || !m_writer->bytes(m_block.constData(), m_block.size()))
            return false;
        m_block.clear();
        return true;
    }

    Writer *m_writer = nullptr;
    QByteArray m_block;
};

class Bits
{
public:
    explicit Bits(SubBlocks *blocks) : m_blocks(blocks) {}

    bool put(int code, int width)
    {
        m_buffer |= quint32(code) << m_bits;
        m_bits += width;
        while (m_bits >= 8) {
            if (!m_blocks->append(int(m_buffer & 0xff)))
                return false;
            m_buffer >>= 8;
            m_bits -= 8;
        }
        return true;
    }

    bool finish()
    {
        if (m_bits == 0)
            return true;
        return m_blocks->append(int(m_buffer & 0xff));
    }

private:
    SubBlocks *m_blocks = nullptr;
    quint32 m_buffer = 0;
    int m_bits = 0;
};

bool writeLzw(Writer *writer, const QByteArray &indices, int tableSize)
{
    int minimumCodeSize = 0;
    while ((1 << minimumCodeSize) < tableSize)
        ++minimumCodeSize;
    minimumCodeSize = qMax(2, minimumCodeSize);
    const int clearCode = 1 << minimumCodeSize;
    const int endCode = clearCode + 1;

    if (!writer->byte(minimumCodeSize))
        return false;
    SubBlocks blocks(writer);
    Bits bits(&blocks);
    QHash<quint32, int> dictionary;
    dictionary.reserve(4096);
    int codeSize = minimumCodeSize + 1;
    int nextCode = endCode + 1;
    auto reset = [&]() {
        dictionary.clear();
        dictionary.reserve(4096);
        codeSize = minimumCodeSize + 1;
        nextCode = endCode + 1;
    };

    if (indices.isEmpty()
        || !bits.put(clearCode, codeSize))
        return false;
    int prefix = uchar(indices.at(0));
    for (int index = 1; index < indices.size(); ++index) {
        const int symbol = uchar(indices.at(index));
        const quint32 key = (quint32(prefix) << 8) | quint32(symbol);
        const auto found = dictionary.constFind(key);
        if (found != dictionary.constEnd()) {
            prefix = found.value();
            continue;
        }
        if (!bits.put(prefix, codeSize))
            return false;
        if (nextCode < 4096) {
            dictionary.insert(key, nextCode++);
            // The decoder creates an entry one emitted code later than the
            // encoder. Grow only after crossing the boundary; growing exactly
            // at it makes that next code one bit wider than strict decoders
            // expect.
            if (nextCode > (1 << codeSize) && codeSize < 12)
                ++codeSize;
        } else {
            if (!bits.put(clearCode, codeSize))
                return false;
            reset();
        }
        prefix = symbol;
    }
    return bits.put(prefix, codeSize) && bits.put(endCode, codeSize)
        && bits.finish() && blocks.finish() && writer->byte(0);
}

bool renderFrame(const Document &document, const QString &clip, int frame, int scale,
                 QImage *image, QString *error)
{
    render::Options options;
    options.scale = scale;
    QString renderError;
    *image = render::toImage(document, clip, frame, options, nullptr, &renderError);
    if (image->isNull()) {
        if (error)
            *error = frameError(frame, renderError.isEmpty()
                                          ? QStringLiteral("render returned an empty image")
                                          : renderError);
        return false;
    }
    return true;
}

bool collectColours(const Document &document, const QString &clip, int scale,
                    int frameCount, QHash<quint32, qint64> *histogram,
                    qint64 *totalPixels, QString *error)
{
    for (int frame = 0; frame < frameCount; ++frame) {
        QImage image;
        if (!renderFrame(document, clip, frame, scale, &image, error))
            return false;
        const qint64 pixels = qint64(image.width()) * image.height();
        if (!add(*totalPixels, pixels, totalPixels)
            || *totalPixels > maxTotalPixels) {
            if (error)
                *error = QStringLiteral(
                    "GIF animation exceeds hard limit of %1 rendered pixels")
                             .arg(maxTotalPixels);
            return false;
        }
        for (int y = 0; y < image.height(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QRgb pixel = line[x];
                if (qAlpha(pixel) < alphaThreshold)
                    continue;
                const quint32 key = (quint32(qRed(pixel)) << 16)
                    | (quint32(qGreen(pixel)) << 8) | quint32(qBlue(pixel));
                auto found = histogram->find(key);
                if (found == histogram->end()) {
                    if (histogram->size() >= maxHistogramColours) {
                        if (error)
                            *error = QStringLiteral(
                                "GIF has more than %1 distinct opaque colours")
                                     .arg(maxHistogramColours);
                        return false;
                    }
                    histogram->insert(key, 1);
                } else {
                    ++found.value();
                }
            }
        }
    }
    return true;
}

bool writeFrame(Writer *writer, const Document &document, const QString &clip,
                int frame, int scale, int delay, const QHash<quint32, quint8> &colourIndices,
                int tableSize, int width, int height, QString *error)
{
    QImage image;
    if (!renderFrame(document, clip, frame, scale, &image, error))
        return false;
    if (image.width() != width || image.height() != height) {
        if (error)
            *error = frameError(frame, QStringLiteral("render dimensions changed"));
        return false;
    }

    QByteArray indices;
    const qint64 pixelCount = qint64(width) * height;
    if (pixelCount > std::numeric_limits<int>::max()) {
        if (error)
            *error = frameError(frame, QStringLiteral("pixel count exceeds QByteArray limits"));
        return false;
    }
    indices.resize(int(pixelCount));
    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const QRgb pixel = line[x];
            if (qAlpha(pixel) < alphaThreshold) {
                indices[y * width + x] = char(0);
                continue;
            }
            const quint32 key = (quint32(qRed(pixel)) << 16)
                | (quint32(qGreen(pixel)) << 8) | quint32(qBlue(pixel));
            const auto found = colourIndices.constFind(key);
            if (found == colourIndices.constEnd()) {
                if (error)
                    *error = frameError(frame, QStringLiteral(
                        "rendered colour was absent from the global palette"));
                return false;
            }
            indices[y * width + x] = char(found.value());
        }
    }

    // Restore the frame rectangle to the transparent background before the
    // next frame. Keeping the previous pixels would leave trails wherever the
    // next frame is transparent.
    if (!writer->byte(0x21) || !writer->byte(0xf9) || !writer->byte(4)
        || !writer->byte(0x09) || !writer->littleEndian16(delay)
        || !writer->byte(0) || !writer->byte(0) || !writer->byte(0x2c)
        || !writer->littleEndian16(0) || !writer->littleEndian16(0)
        || !writer->littleEndian16(width) || !writer->littleEndian16(height)
        || !writer->byte(0))
        return false;
    return writeLzw(writer, indices, tableSize);
}

bool estimatedSize(qint64 pixels, int frameCount, int tableSize, bool loop,
                   qint64 *estimate)
{
    qint64 compressed = 0;
    if (!multiply(pixels, 2, &compressed)
        || !add(compressed, pixels / 255 + 2, &compressed))
        return false;
    qint64 perFrame = 21;
    if (!add(perFrame, compressed, &perFrame))
        return false;
    qint64 frames = 0;
    if (!multiply(perFrame, frameCount, &frames))
        return false;
    qint64 result = 13 + qint64(tableSize) * 3 + (loop ? 19 : 0);
    return add(result, frames, estimate);
}

} // namespace

QByteArray encode(const Document &document, const QString &clip, int scale,
                  int fpsOverride, bool loop, QString *error)
{
    if (error)
        error->clear();
    const Clip *found = document.clip(clip);
    if (!found) {
        if (error)
            *error = QStringLiteral("clip not found: %1").arg(clip);
        return {};
    }
    if (found->frameCount <= 0 || found->frameCount > Document::maxFramesPerClip) {
        if (error)
            *error = QStringLiteral("clip frame count must be between 1 and %1")
                         .arg(Document::maxFramesPerClip);
        return {};
    }
    if (scale < 1 || scale > 64) {
        if (error)
            *error = QStringLiteral("GIF scale must be between 1 and 64");
        return {};
    }
    if (fpsOverride < 0) {
        if (error)
            *error = QStringLiteral("GIF FPS override cannot be negative");
        return {};
    }
    const int fps = fpsOverride == 0 ? found->fps : fpsOverride;
    if (fps < 1 || fps > 100) {
        if (error)
            *error = QStringLiteral("GIF FPS must be between 1 and 100");
        return {};
    }
    if (document.columns() <= 0 || document.rows() <= 0
        || document.columns() > Document::maxDimension
        || document.rows() > Document::maxDimension) {
        if (error)
            *error = QStringLiteral("document dimensions are invalid for GIF export");
        return {};
    }

    const qint64 width64 = qint64(document.columns()) * scale;
    const qint64 height64 = qint64(document.rows()) * scale;
    if (width64 > 65535 || height64 > 65535) {
        if (error)
            *error = QStringLiteral("scaled GIF dimensions exceed the 65535-pixel GIF limit");
        return {};
    }
    qint64 framePixels = 0;
    if (!multiply(width64, height64, &framePixels)
        || framePixels > render::maxImagePixels) {
        if (error)
            *error = QStringLiteral("scaled GIF frame exceeds the render pixel limit of %1")
                         .arg(render::maxImagePixels);
        return {};
    }
    qint64 totalPixels = 0;
    if (!multiply(framePixels, found->frameCount, &totalPixels)
        || totalPixels > maxTotalPixels) {
        if (error)
            *error = QStringLiteral(
                "GIF animation exceeds hard limit of %1 rendered pixels")
                         .arg(maxTotalPixels);
        return {};
    }

    QHash<quint32, qint64> histogram;
    histogram.reserve(4096);
    totalPixels = 0;
    if (!collectColours(document, clip, scale, found->frameCount, &histogram,
                        &totalPixels, error))
        return {};

    QVector<ColourPoint> colours;
    colours.reserve(histogram.size());
    for (auto iterator = histogram.constBegin(); iterator != histogram.constEnd();
         ++iterator) {
        const quint32 key = iterator.key();
        colours.append(ColourPoint{key, int((key >> 16) & 0xff),
                                   int((key >> 8) & 0xff), int(key & 0xff),
                                   iterator.value()});
    }
    std::sort(colours.begin(), colours.end(), [](const ColourPoint &left,
                                                 const ColourPoint &right) {
        return left.key < right.key;
    });
    const QVector<QRgb> palette = makePalette(colours);
    const int tableBits = tableBitsFor(palette.size());
    const int tableSize = 1 << tableBits;
    qint64 estimate = 0;
    if (!estimatedSize(framePixels, found->frameCount, tableSize, loop, &estimate)
        || estimate > maxGifBytes) {
        if (error)
            *error = QStringLiteral("estimated GIF output exceeds hard limit of %1 MiB")
                         .arg(maxGifBytes / (1024 * 1024));
        return {};
    }
    const QHash<quint32, quint8> colourIndices = makeColourIndex(colours, palette);

    QByteArray result;
    result.reserve(int(qMin<qint64>(estimate, std::numeric_limits<int>::max())));
    Writer writer(&result, error);
    if (!writer.text("GIF89a", 6) || !writer.littleEndian16(int(width64))
        || !writer.littleEndian16(int(height64))
        || !writer.byte(0x80 | (7 << 4) | (tableBits - 1))
        || !writer.byte(0) || !writer.byte(0))
        return {};
    for (int index = 0; index < tableSize; ++index) {
        const QRgb colour = index < palette.size() ? palette.at(index) : qRgb(0, 0, 0);
        if (!writer.byte(qRed(colour)) || !writer.byte(qGreen(colour))
            || !writer.byte(qBlue(colour)))
            return {};
    }
    if (loop && (!writer.byte(0x21) || !writer.byte(0xff) || !writer.byte(11)
                 || !writer.text("NETSCAPE2.0", 11) || !writer.byte(3)
                 || !writer.byte(1) || !writer.littleEndian16(0) || !writer.byte(0)))
        return {};

    for (int frame = 0; frame < found->frameCount; ++frame) {
        // floor((frame + 1) * 100 / fps) - floor(frame * 100 / fps).
        const int delay = ((frame + 1) * 100) / fps - (frame * 100) / fps;
        if (!writeFrame(&writer, document, clip, frame, scale, delay, colourIndices,
                        tableSize, int(width64), int(height64), error))
            return {};
    }
    if (!writer.byte(0x3b))
        return {};
    return result;
}

bool write(const Document &document, const QString &clip, const QString &path,
           int scale, int fpsOverride, bool loop, const QStringList &sources,
           QString *error)
{
    const QByteArray bytes = encode(document, clip, scale, fpsOverride, loop, error);
    if (bytes.isEmpty())
        return false;
    return output::writeAtomically(path, bytes, sources, error);
}

} // namespace gif
} // namespace omapixel
