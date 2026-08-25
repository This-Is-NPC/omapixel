#include "ImageImport.h"

#include "Output.h"

#include <QBuffer>
#include <QImageIOHandler>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>

#include <utility>

namespace omapixel {
namespace {

QString failText(const QString &code, const QString &message)
{
    return code + QStringLiteral(": ") + message;
}

bool checkedPixels(const QSize &size, qint64 *pixels)
{
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
        return false;
    *pixels = qint64(size.width()) * size.height();
    return *pixels > 0;
}

bool validMode(ImageImport::ResizeMode mode)
{
    return mode == ImageImport::ResizeMode::Contain
        || mode == ImageImport::ResizeMode::Cover
        || mode == ImageImport::ResizeMode::Stretch;
}

bool sourceLimits(const QSize &size, const ImageImport::Options &options,
                  QString *error)
{
    const int dimensionLimit = qBound(1, qMin(options.maxSourceDimension,
                                              ImageImport::hardMaxSourceDimension),
                                      ImageImport::hardMaxSourceDimension);
    const qint64 pixelLimit = qBound<qint64>(1,
        qMin(options.maxSourcePixels, ImageImport::hardMaxSourcePixels),
        ImageImport::hardMaxSourcePixels);
    qint64 pixels = 0;
    if (!checkedPixels(size, &pixels)) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_DIMENSIONS"),
                              QStringLiteral("decoder returned invalid dimensions"));
        return false;
    }
    if (size.width() > dimensionLimit || size.height() > dimensionLimit) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_DIMENSIONS"),
                              QStringLiteral("source dimensions %1x%2 exceed limit %3")
                                  .arg(size.width()).arg(size.height()).arg(dimensionLimit));
        return false;
    }
    if (pixels > pixelLimit) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_PIXELS"),
                              QStringLiteral("source has %1 pixels; limit is %2")
                                  .arg(pixels).arg(pixelLimit));
        return false;
    }
    return true;
}

bool readImage(const QString &path, const ImageImport::Options &options,
               QImage *image, ImageImport::Report *report, QString *error)
{
    if (!image)
        return false;
    const qint64 inputLimit = qMin(options.maxInputBytes,
                                   ImageImport::hardMaxInputBytes);
    QByteArray bytes;
    QString readError;
    if (!input::readRegularFile(path, inputLimit, &bytes, &readError)) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_READ"), readError);
        return false;
    }
    if (bytes.size() > inputLimit) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_BYTES"),
                              QStringLiteral("input exceeds the %1 byte limit").arg(inputLimit));
        return false;
    }

    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_READ"),
                              QStringLiteral("could not open image buffer"));
        return false;
    }
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_FORMAT"),
                              reader.errorString().isEmpty()
                                  ? QStringLiteral("file is not a readable raster image")
                                  : reader.errorString());
        return false;
    }
    QByteArray formatBytes = reader.format().toLower();
    if (formatBytes == QByteArrayLiteral("jpg"))
        formatBytes = QByteArrayLiteral("jpeg");
    if (formatBytes != QByteArrayLiteral("png")
        && formatBytes != QByteArrayLiteral("jpeg")
        && formatBytes != QByteArrayLiteral("webp")) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_FORMAT"),
                              QStringLiteral("format `%1` is not supported")
                                  .arg(QString::fromLatin1(formatBytes)));
        return false;
    }
    const QSize announced = reader.size();
    if (!sourceLimits(announced, options, error))
        return false;

    const bool orientation = reader.transformation()
                             != QImageIOHandler::TransformationNone;
    const QImage decoded = reader.read();
    if (decoded.isNull()) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_DECODE"), reader.errorString());
        return false;
    }
    if (!sourceLimits(decoded.size(), options, error))
        return false;
    *image = decoded.convertToFormat(QImage::Format_RGBA8888);
    if (image->isNull()) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_DECODE"),
                              QStringLiteral("could not convert decoded image to RGBA"));
        return false;
    }
    if (report) {
        report->format = QString::fromLatin1(formatBytes);
        report->sourceSize = announced;
        report->decodedSize = image->size();
        report->orientationApplied = orientation;
        checkedPixels(image->size(), &report->sourcePixels);
    }
    return true;
}

bool prepareImage(const QImage &source, const ImageImport::Options &options,
                  QImage *prepared, QSize *logicalSize, QString *error)
{
    if (!prepared || !logicalSize)
        return false;
    // QSize's default value is (-1,-1), so test the sentinel explicitly. A
    // partially invalid user value still enters the target-resolution branch
    // and receives a useful validation error below.
    const bool targetSet = options.targetResolution.width() >= 0
                           || options.targetResolution.height() >= 0;
    if (options.scale < 0) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_OPTIONS"),
                              QStringLiteral("scale must be zero or a positive integer"));
        return false;
    }
    if (targetSet && options.scale > 0) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_OPTIONS"),
                              QStringLiteral("scale and targetResolution are mutually exclusive"));
        return false;
    }
    if (!validMode(options.resizeMode)) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_OPTIONS"),
                              QStringLiteral("unknown resize mode"));
        return false;
    }

    QSize target;
    if (targetSet) {
        target = options.targetResolution;
        if (!target.isValid() || target.width() <= 0 || target.height() <= 0) {
            if (error)
                *error = failText(QStringLiteral("E_IMPORT_RESOLUTION"),
                                  QStringLiteral("targetResolution must be positive"));
            return false;
        }
    } else {
        const int scale = options.scale > 0 ? options.scale : 1;
        const int width = int((qint64(source.width()) + scale - 1) / scale);
        const int height = int((qint64(source.height()) + scale - 1) / scale);
        target = QSize(width, height);
    }
    if (target.width() > Document::maxDimension
        || target.height() > Document::maxDimension) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_RESOLUTION"),
                              QStringLiteral("logical dimensions %1x%2 exceed document limit %3")
                                  .arg(target.width()).arg(target.height())
                                  .arg(Document::maxDimension));
        return false;
    }
    qint64 targetPixels = 0;
    if (!checkedPixels(target, &targetPixels)
        || targetPixels > qint64(Document::maxDimension) * Document::maxDimension) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_PIXELS"),
                              QStringLiteral("logical image exceeds document pixel limit"));
        return false;
    }

    if (!targetSet || options.resizeMode == ImageImport::ResizeMode::Stretch) {
        *prepared = source.scaled(target, Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation);
    } else if (options.resizeMode == ImageImport::ResizeMode::Contain) {
        const QImage fitted = source.scaled(target, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
        *prepared = QImage(target, QImage::Format_RGBA8888);
        prepared->fill(Qt::transparent);
        QPainter painter(prepared);
        const int x = (target.width() - fitted.width()) / 2;
        const int y = (target.height() - fitted.height()) / 2;
        painter.drawImage(x, y, fitted);
    } else {
        const QImage fitted = source.scaled(target, Qt::KeepAspectRatioByExpanding,
                                            Qt::SmoothTransformation);
        const int x = (fitted.width() - target.width()) / 2;
        const int y = (fitted.height() - target.height()) / 2;
        *prepared = fitted.copy(x, y, target.width(), target.height());
    }
    if (prepared->isNull()) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_RESOLUTION"),
                              QStringLiteral("could not allocate logical image"));
        return false;
    }
    *logicalSize = target;
    return true;
}

ImageImport::Result loadImpl(const QString &path, const ImageImport::Options &options,
                             const Palette &fixed)
{
    ImageImport::Result result;
    if (options.maxInputBytes <= 0 || options.maxSourceDimension <= 0
        || options.maxSourcePixels <= 0) {
        result.error = failText(QStringLiteral("E_IMPORT_LIMIT"),
                                QStringLiteral("input, dimension, and pixel limits must be positive"));
        return result;
    }
    if (options.maxPaletteSlots < 1
        || options.maxPaletteSlots > Palette::maxSlots) {
        result.error = failText(QStringLiteral("E_IMPORT_PALETTE"),
                                QStringLiteral("maxPaletteSlots must be between 1 and %1")
                                    .arg(Palette::maxSlots));
        return result;
    }
    QImage source;
    QString error;
    if (!readImage(path, options, &source, &result.report, &error)) {
        result.error = error;
        return result;
    }
    QImage prepared;
    QSize logicalSize;
    if (!prepareImage(source, options, &prepared, &logicalSize, &error)) {
        result.error = error;
        return result;
    }
    result.report.logicalSize = logicalSize;
    result.report.logicalPixels = qint64(logicalSize.width()) * logicalSize.height();
    result.report.resizeMode = options.resizeMode;
    result.report.paletteSlotsBefore = fixed.size();

    Quantization::Options quantizationOptions;
    quantizationOptions.maxColors = options.maxPaletteSlots;
    const Quantization::Result quantized = Quantization::run(prepared, fixed,
                                                             quantizationOptions);
    if (!quantized) {
        result.error = quantized.error;
        return result;
    }
    result.grid = quantized.grid;
    result.palette = quantized.palette;
    result.document.palette() = result.palette;
    result.report.transparentPixels = quantized.report.transparentPixels;
    result.report.representedPixels = quantized.report.representedPixels;
    result.report.exactMatches = quantized.report.exactMatches;
    result.report.approximatedPixels = quantized.report.approximatedPixels;
    result.report.paletteSlotsAfter = quantized.palette.size();
    result.report.newPaletteSlots = quantized.report.newSlots;
    result.report.diagnostics = quantized.report.diagnostics;
    result.ok = true;
    return result;
}

QString makeLayerId(const Document &document, const ImageImport::Options &options,
                    QString *error)
{
    if (!options.layerId.isEmpty()) {
        if (!QRegularExpression(QStringLiteral("^[a-z][a-z0-9-]{0,63}$"))
                .match(options.layerId).hasMatch()) {
            if (error)
                *error = failText(QStringLiteral("E_IMPORT_LAYER"),
                                  QStringLiteral("layerId is not a valid document id"));
            return QString();
        }
        if (document.layerById(options.layerId)) {
            if (error)
                *error = failText(QStringLiteral("E_IMPORT_LAYER"),
                                  QStringLiteral("layerId `%1` already exists").arg(options.layerId));
            return QString();
        }
        return options.layerId;
    }
    QString id = options.layerName.toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9-]+")), QStringLiteral("-"));
    while (id.startsWith(QLatin1Char('-')))
        id.remove(0, 1);
    if (id.isEmpty() || !id.at(0).isLetter())
        id.prepend(QStringLiteral("imported"));
    if (id.size() > 55)
        id.truncate(55);
    const QString base = id;
    int suffix = 2;
    while (document.layerById(id)) {
        id = QStringLiteral("%1-%2").arg(base).arg(suffix++);
    }
    return id;
}

QString makeLayerName(const Document &document, const ImageImport::Options &options)
{
    QString name = options.layerName.isEmpty() ? QStringLiteral("Imported")
                                               : options.layerName;
    if (!document.layerByName(name))
        return name;
    const QString base = name;
    int suffix = 2;
    do {
        name = QStringLiteral("%1 %2").arg(base).arg(suffix++);
    } while (document.layerByName(name));
    return name;
}

Grid centredGrid(const Grid &source, const Document &document, qint64 *clipped)
{
    Grid output(document.columns(), document.rows());
    const int originX = (document.columns() - source.columns()) / 2;
    const int originY = (document.rows() - source.rows()) / 2;
    for (int y = 0; y < source.rows(); ++y) {
        for (int x = 0; x < source.columns(); ++x) {
            const QChar slot = source.at(x, y);
            const int targetX = x + originX;
            const int targetY = y + originY;
            if (slot != Grid::Empty
                && (targetX < 0 || targetY < 0
                    || targetX >= document.columns() || targetY >= document.rows())) {
                if (clipped)
                    ++*clipped;
                continue;
            }
            if (slot != Grid::Empty)
                output.set(targetX, targetY, slot);
        }
    }
    return output;
}

} // namespace

ImageImport::Result ImageImport::load(const QString &path, const Options &options,
                                      const Palette &fixed)
{
    return loadImpl(path, options, fixed);
}

ImageImport::Result ImageImport::load(const QString &path, const Options &options)
{
    return load(path, options, Palette());
}

ImageImport::Result ImageImport::load(const QString &path)
{
    return load(path, Options(), Palette());
}

ImageImport::Result ImageImport::createDocument(const QString &path,
                                                const Options &options)
{
    Result result = loadImpl(path, options, Palette());
    if (!result)
        return result;
    if (options.frame != 0) {
        result.ok = false;
        result.error = failText(QStringLiteral("E_IMPORT_FRAME"),
                                QStringLiteral("a new document starts at frame 0"));
        return result;
    }
    const QString clipName = options.clipName.isEmpty()
                                 ? QStringLiteral("Imported") : options.clipName;
    Document document = Document::empty(result.grid.columns(), result.grid.rows());
    document.palette() = result.palette;
    if (!document.addClip(clipName)) {
        result.ok = false;
        result.error = failText(QStringLiteral("E_IMPORT_DOCUMENT"),
                                QStringLiteral("could not create clip `%1`").arg(clipName));
        return result;
    }
    const QString layerId = makeLayerId(document, options, &result.error);
    if (layerId.isEmpty() || !document.addLayer(layerId,
                                                options.layerName.isEmpty()
                                                    ? QStringLiteral("Imported")
                                                    : options.layerName,
                                                QStringLiteral("animated"))) {
        result.ok = false;
        if (result.error.isEmpty())
            result.error = failText(QStringLiteral("E_IMPORT_DOCUMENT"),
                                    QStringLiteral("could not create imported layer"));
        return result;
    }
    const Clip *clip = document.clip(clipName);
    if (!clip || !document.setCel(layerId, clip->id, 0, result.grid, &result.error)) {
        result.ok = false;
        if (result.error.isEmpty())
            result.error = failText(QStringLiteral("E_IMPORT_DOCUMENT"),
                                    QStringLiteral("could not write imported cel"));
        return result;
    }
    result.document = document;
    result.report.clipId = clip->id;
    result.report.layerId = layerId;
    result.ok = true;
    return result;
}

ImageImport::Result ImageImport::createDocument(const QString &path)
{
    return createDocument(path, Options());
}

bool ImageImport::addLayer(Document *document, const QString &path,
                           const Options &options, Report *report, QString *error)
{
    if (report)
        *report = Report();
    if (error)
        error->clear();
    if (!document) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_DOCUMENT"),
                              QStringLiteral("document pointer is null"));
        return false;
    }
    if (document->palette().size() > options.maxPaletteSlots
        || options.maxPaletteSlots < 1
        || options.maxPaletteSlots > Palette::maxSlots) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_PALETTE"),
                              QStringLiteral("document palette cannot fit the requested limit"));
        return false;
    }
    const QString clipName = options.clip.isEmpty()
                                 ? (document->clips().isEmpty()
                                        ? QString() : document->clips().first().id)
                                 : options.clip;
    const Clip *clip = document->clip(clipName);
    if (!clip) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_CLIP"),
                              QStringLiteral("clip `%1` was not found").arg(clipName));
        return false;
    }
    if (options.frame < 0 || options.frame >= clip->frameCount) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_FRAME"),
                              QStringLiteral("frame %1 is outside the selected clip").arg(options.frame));
        return false;
    }

    const Result loaded = loadImpl(path, options, document->palette());
    if (!loaded) {
        if (error)
            *error = loaded.error;
        if (report)
            *report = loaded.report;
        return false;
    }
    Document next = *document;
    next.palette() = loaded.palette;
    QString identityError;
    const QString layerId = makeLayerId(next, options, &identityError);
    if (layerId.isEmpty()) {
        if (error)
            *error = identityError;
        return false;
    }
    const QString layerName = makeLayerName(next, options);
    if (!next.addLayer(layerId, layerName, QStringLiteral("animated"))) {
        if (error)
            *error = failText(QStringLiteral("E_IMPORT_LAYER"),
                              QStringLiteral("could not create layer `%1`").arg(layerName));
        return false;
    }
    qint64 clipped = 0;
    const Grid placed = centredGrid(loaded.grid, next, &clipped);
    const Clip *nextClip = next.clip(clipName);
    if (!nextClip || !next.setCel(layerId, nextClip->id, options.frame, placed, error)) {
        if (error && error->isEmpty())
            *error = failText(QStringLiteral("E_IMPORT_LAYER"),
                              QStringLiteral("could not write imported cel"));
        return false;
    }
    if (report)
        *report = loaded.report;
    if (report) {
        report->clippedPixels = clipped;
        report->clipId = nextClip->id;
        report->layerId = layerId;
        if (clipped > 0)
            report->diagnostics.append(QStringLiteral("%1 drawn pixels clipped at document bounds")
                                           .arg(clipped));
    }
    *document = std::move(next);
    return true;
}

bool ImageImport::addLayer(Document *document, const QString &path,
                           const Options &options)
{
    return addLayer(document, path, options, nullptr, nullptr);
}

bool ImageImport::addLayer(Document *document, const QString &path)
{
    return addLayer(document, path, Options(), nullptr, nullptr);
}

} // namespace omapixel
