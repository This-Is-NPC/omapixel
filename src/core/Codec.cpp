#include "Codec.h"
#include "Output.h"
#include "TextSafety.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <limits>

namespace omapixel {
namespace {

bool fail(QString *error, const QString &path, const QString &message)
{
    *error = QStringLiteral("%1: %2").arg(path, message);
    return false;
}

bool hasOnlyFields(const QJsonObject &object, const QStringList &allowed,
                   const QString &path, QString *error)
{
    for (const QString &key : object.keys()) {
        if (!allowed.contains(key))
            return fail(error, path + QLatin1Char('.') + key,
                        QStringLiteral("unknown field"));
    }
    return true;
}

bool required(const QJsonObject &object, const QString &key,
              const QString &path, QString *error)
{
    if (!object.contains(key))
        return fail(error, path + QLatin1Char('.') + key,
                    QStringLiteral("is required"));
    return true;
}

bool integerValue(const QJsonValue &value, int minimum, int maximum, int *out,
                  const QString &path, QString *error)
{
    if (!value.isDouble())
        return fail(error, path, QStringLiteral("must be an integer number"));
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < minimum || number > maximum)
        return fail(error, path,
                    QStringLiteral("must be an integer between %1 and %2")
                        .arg(minimum)
                        .arg(maximum));
    *out = static_cast<int>(number);
    return true;
}

bool checkId(const QJsonValue &value, const QString &path, QString *error)
{
    if (!value.isString()
        || !QRegularExpression(QStringLiteral("^[a-z][a-z0-9-]{0,63}$"))
                .match(value.toString())
                .hasMatch())
        return fail(error, path,
                    QStringLiteral("must match [a-z][a-z0-9-]{0,63}"));
    return true;
}

bool checkName(const QJsonValue &value, const QString &path, QString *error)
{
    if (!value.isString() || value.toString().isEmpty()
        || value.toString().size() > 128)
        return fail(error, path,
                    QStringLiteral("must be a non-empty string of at most 128 characters"));
    if (!text::isSafe(value.toString(), false))
        return fail(error, path,
                    QStringLiteral("must not contain terminal control or unsafe Unicode characters"));
    return true;
}

bool readRows(const QJsonValue &value, int columns, int rows,
              const Palette &palette, const QString &path, Grid *grid,
              QString *error)
{
    if (!value.isArray())
        return fail(error, path, QStringLiteral("must be an array"));
    const QJsonArray values = value.toArray();
    if (values.size() != rows)
        return fail(error, path,
                    QStringLiteral("must contain exactly %1 rows").arg(rows));
    QStringList lines;
    lines.reserve(rows);
    for (int y = 0; y < rows; ++y) {
        const QString rowPath = QStringLiteral("%1[%2]").arg(path).arg(y);
        if (!values.at(y).isString())
            return fail(error, rowPath, QStringLiteral("must be a string"));
        const QString line = values.at(y).toString();
        if (line.size() != columns)
            return fail(error, rowPath,
                        QStringLiteral("must contain exactly %1 characters")
                            .arg(columns));
        for (int x = 0; x < line.size(); ++x) {
            const QChar slot = line.at(x);
            if (slot != Grid::Empty && !palette.has(slot))
                return fail(error, QStringLiteral("%1[%2]").arg(rowPath).arg(x),
                            QStringLiteral("uses undefined palette slot `%1`")
                                .arg(slot));
        }
        lines.append(line);
    }
    *grid = Grid::fromRows(lines);
    return true;
}

bool readPalette(const QJsonValue &value, Palette *palette, QString *error)
{
    const QString path = QStringLiteral("$.palette");
    if (!value.isArray())
        return fail(error, path, QStringLiteral("must be an array"));
    QSet<QChar> seen;
    const QJsonArray entries = value.toArray();
    for (int i = 0; i < entries.size(); ++i) {
        const QString entryPath = QStringLiteral("%1[%2]").arg(path).arg(i);
        if (!entries.at(i).isObject())
            return fail(error, entryPath, QStringLiteral("must be an object"));
        const QJsonObject entry = entries.at(i).toObject();
        if (!hasOnlyFields(entry, {QStringLiteral("slot"), QStringLiteral("colour")},
                           entryPath, error)
            || !required(entry, QStringLiteral("slot"), entryPath, error)
            || !required(entry, QStringLiteral("colour"), entryPath, error))
            return false;
        const QString slotPath = entryPath + QStringLiteral(".slot");
        const QString slot = entry.value(QStringLiteral("slot")).toString();
        if (!entry.value(QStringLiteral("slot")).isString() || slot.size() != 1
            || !Palette::validSlot(slot.at(0)))
            return fail(error, slotPath,
                        QStringLiteral("must be one character and not `.`, `\"`, or `\\`"));
        if (seen.contains(slot.at(0)))
            return fail(error, slotPath,
                        QStringLiteral("duplicates palette slot `%1`").arg(slot));
        const QString colourPath = entryPath + QStringLiteral(".colour");
        const QString colour = entry.value(QStringLiteral("colour")).toString();
        if (!entry.value(QStringLiteral("colour")).isString()
            || !QRegularExpression(QStringLiteral("^#[0-9A-Fa-f]{8}$"))
                    .match(colour)
                    .hasMatch())
            return fail(error, colourPath, QStringLiteral("must be #RRGGBBAA"));
        const int red = colour.mid(1, 2).toInt(nullptr, 16);
        const int green = colour.mid(3, 2).toInt(nullptr, 16);
        const int blue = colour.mid(5, 2).toInt(nullptr, 16);
        const int alpha = colour.mid(7, 2).toInt(nullptr, 16);
        if (!palette->set(slot.at(0), QColor(red, green, blue, alpha)))
            return fail(error, slotPath, QStringLiteral("could not add palette slot"));
        seen.insert(slot.at(0));
    }
    return true;
}

QJsonArray rowsOf(const Grid &grid)
{
    QJsonArray rows;
    for (const QString &row : grid.toRows())
        rows.append(row);
    return rows;
}

struct ScanLimits {
    int depth = 0;
    qint64 tokens = 0;
    qint64 stringBytes = 0;
    qint64 objectMembers = 0;
    static constexpr int maxDepth = 64;
    static constexpr int maxArrayItems = 65536;
    static constexpr qint64 maxObjectMembers = 262144;
    static constexpr qint64 maxTokens = 2'000'000;
    static constexpr qint64 maxStringBytes = 4096;
    static constexpr qint64 maxTotalStringBytes = Document::maxDocumentBytes;
};

void skipSpace(const QByteArray &json, int *position)
{
    while (*position < json.size()
           && QByteArray(" \t\n\r").contains(json.at(*position)))
        ++*position;
}

bool scanString(const QByteArray &json, int *position, QByteArray *token,
                QString *error, ScanLimits *limits)
{
    if (*position >= json.size() || json.at(*position) != '"')
        return false;
    const int start = *position;
    ++*position;
    while (*position < json.size()) {
        if (*position - start >= ScanLimits::maxStringBytes)
            return fail(error, QStringLiteral("$"),
                        QStringLiteral("string token exceeds %1 bytes")
                            .arg(ScanLimits::maxStringBytes));
        const char character = json.at(*position);
        if (character == '\\') {
            if (++*position >= json.size())
                return false;
            ++*position;
        } else {
            ++*position;
            if (character == '"') {
                *token = json.mid(start, *position - start);
                limits->stringBytes += token->size();
                if (limits->stringBytes > ScanLimits::maxTotalStringBytes)
                    return fail(error, QStringLiteral("$"),
                                QStringLiteral("string tokens exceed the total limit"));
                if (++limits->tokens > ScanLimits::maxTokens)
                    return fail(error, QStringLiteral("$"),
                                QStringLiteral("JSON token count exceeds %1")
                                    .arg(ScanLimits::maxTokens));
                return true;
            }
        }
    }
    return false;
}

QString decodedKey(const QByteArray &token)
{
    QJsonParseError parse;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray("{") + token + QByteArray(":null}"), &parse);
    return document.object().keys().value(0);
}

bool scanValue(const QByteArray &json, int *position, const QString &path,
               QString *error, ScanLimits *limits);

bool scanObject(const QByteArray &json, int *position, const QString &path,
                QString *error, ScanLimits *limits)
{
    if (++limits->depth > ScanLimits::maxDepth)
        return fail(error, path, QStringLiteral("nesting depth exceeds %1")
                                   .arg(ScanLimits::maxDepth));
    ++*position;
    skipSpace(json, position);
    QSet<QString> keys;
    if (*position < json.size() && json.at(*position) == '}') {
        ++*position;
        --limits->depth;
        return true;
    }
    while (*position < json.size()) {
        QByteArray token;
        if (++limits->objectMembers > ScanLimits::maxObjectMembers)
            return fail(error, path, QStringLiteral("object has too many members"));
        if (!scanString(json, position, &token, error, limits))
            return false;
        const QString key = decodedKey(token);
        if (keys.contains(key))
            return fail(error, path + QStringLiteral(".") + key,
                        QStringLiteral("duplicate field"));
        keys.insert(key);
        skipSpace(json, position);
        if (*position >= json.size() || json.at(*position) != ':')
            return false;
        ++*position;
        skipSpace(json, position);
        if (!scanValue(json, position, path + QStringLiteral(".") + key, error,
                       limits))
            return false;
        skipSpace(json, position);
        if (*position < json.size() && json.at(*position) == '}') {
            ++*position;
            --limits->depth;
            return true;
        }
        if (*position >= json.size() || json.at(*position) != ',')
            return false;
        ++*position;
        skipSpace(json, position);
    }
    return false;
}

bool scanArray(const QByteArray &json, int *position, const QString &path,
               QString *error, ScanLimits *limits)
{
    if (++limits->depth > ScanLimits::maxDepth)
        return fail(error, path, QStringLiteral("nesting depth exceeds %1")
                                   .arg(ScanLimits::maxDepth));
    ++*position;
    skipSpace(json, position);
    if (*position < json.size() && json.at(*position) == ']') {
        ++*position;
        --limits->depth;
        return true;
    }
    int index = 0;
    while (*position < json.size()) {
        if (index >= ScanLimits::maxArrayItems)
            return fail(error, path, QStringLiteral("array has too many items"));
        if (!scanValue(json, position, QStringLiteral("%1[%2]").arg(path).arg(index++),
                       error, limits))
            return false;
        skipSpace(json, position);
        if (*position < json.size() && json.at(*position) == ']') {
            ++*position;
            --limits->depth;
            return true;
        }
        if (*position >= json.size() || json.at(*position) != ',')
            return false;
        ++*position;
        skipSpace(json, position);
    }
    return false;
}

bool scanValue(const QByteArray &json, int *position, const QString &path,
               QString *error, ScanLimits *limits)
{
    if (*position >= json.size())
        return false;
    if (json.at(*position) == '{')
        return scanObject(json, position, path, error, limits);
    if (json.at(*position) == '[')
        return scanArray(json, position, path, error, limits);
    if (json.at(*position) == '"') {
        QByteArray token;
        return scanString(json, position, &token, error, limits);
    }
    const int start = *position;
    while (*position < json.size()
           && !QByteArray(",]} \t\n\r").contains(json.at(*position)))
        ++*position;
    if (*position == start)
        return false;
    if (*position - start > ScanLimits::maxStringBytes)
        return fail(error, path, QStringLiteral("scalar token exceeds %1 bytes")
                                      .arg(ScanLimits::maxStringBytes));
    if (++limits->tokens > ScanLimits::maxTokens)
        return fail(error, path, QStringLiteral("JSON token count exceeds %1")
                                      .arg(ScanLimits::maxTokens));
    return true;
}

bool rejectDuplicateJsonKeysImpl(const QByteArray &json, QString *error)
{
    int position = 0;
    skipSpace(json, &position);
    ScanLimits limits;
    if (!scanValue(json, &position, QStringLiteral("$"), error, &limits)) {
        if (error->isEmpty())
            *error = QStringLiteral("$: malformed JSON");
        return true;
    }
    skipSpace(json, &position);
    if (position != json.size())
        return fail(error, QStringLiteral("$"), QStringLiteral("trailing JSON data"));
    return false;
}

QString colourString(const QColor &colour)
{
    return QStringLiteral("#%1%2%3%4")
        .arg(colour.red(), 2, 16, QLatin1Char('0'))
        .arg(colour.green(), 2, 16, QLatin1Char('0'))
        .arg(colour.blue(), 2, 16, QLatin1Char('0'))
        .arg(colour.alpha(), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QStringList resourceWarnings(const Document &document, qint64 bytes,
                             const Codec::WarningLimits &limits)
{
    QStringList warnings;
    if (limits.fileBytes > 0 && bytes > limits.fileBytes)
        warnings << QStringLiteral("document is %1 MiB; warning threshold is %2 MiB")
                        .arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1)
                        .arg(double(limits.fileBytes) / (1024.0 * 1024.0), 0, 'f', 1);
    if (limits.paletteSlots > 0
        && document.palette().entries().size() > limits.paletteSlots)
        warnings << QStringLiteral("document has %1 palette slots; warning threshold is %2")
                        .arg(document.palette().entries().size())
                        .arg(limits.paletteSlots);
    if (limits.clips > 0 && document.clips().size() > limits.clips)
        warnings << QStringLiteral("document has %1 clips; warning threshold is %2")
                        .arg(document.clips().size()).arg(limits.clips);
    qint64 totalFrames = 0;
    for (const Clip &clip : document.clips()) {
        totalFrames += clip.frameCount;
        if (limits.framesPerClip > 0 && clip.frameCount > limits.framesPerClip)
            warnings << QStringLiteral("clip %1 has %2 frames; warning threshold is %3")
                            .arg(clip.name).arg(clip.frameCount)
                            .arg(limits.framesPerClip);
    }
    if (limits.totalFrames > 0 && totalFrames > limits.totalFrames)
        warnings << QStringLiteral("document has %1 frames; warning threshold is %2")
                        .arg(totalFrames).arg(limits.totalFrames);
    return warnings;
}

} // namespace

Codec::Result Codec::read(const QByteArray &json)
{
    return read(json, WarningLimits());
}

Codec::Result Codec::read(const QByteArray &json, const WarningLimits &limits)
{
    Result result;
    if (json.size() > Document::maxDocumentBytes) {
        result.error = QStringLiteral("$: document exceeds hard limit of %1 MiB")
                           .arg(Document::maxDocumentBytes / (1024 * 1024));
        return result;
    }
    QString scanError;
    if (Codec::rejectDuplicateJsonKeys(json, &scanError)) {
        result.error = scanError;
        return result;
    }
    QJsonParseError parse;
    const QJsonDocument parsed = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError) {
        result.error = QStringLiteral("invalid JSON at offset %1: %2")
                           .arg(parse.offset).arg(parse.errorString());
        return result;
    }
    QString error;
    if (!parsed.isObject()) {
        result.error = QStringLiteral("$: the document has to be a JSON object");
        return result;
    }
    const QJsonObject root = parsed.object();
    if (!hasOnlyFields(root, {QStringLiteral("version"), QStringLiteral("canvas"),
                              QStringLiteral("palette"), QStringLiteral("clips"),
                              QStringLiteral("layers")}, QStringLiteral("$"), &error)
        || !required(root, QStringLiteral("version"), QStringLiteral("$"), &error)
        || !required(root, QStringLiteral("canvas"), QStringLiteral("$"), &error)
        || !required(root, QStringLiteral("palette"), QStringLiteral("$"), &error)
        || !required(root, QStringLiteral("clips"), QStringLiteral("$"), &error)
        || !required(root, QStringLiteral("layers"), QStringLiteral("$"), &error)) {
        result.error = error;
        return result;
    }
    int version = 0;
    if (!integerValue(root.value(QStringLiteral("version")), 2, 2,
                      &version, QStringLiteral("$.version"), &error)) {
        result.error = error;
        return result;
    }
    Q_UNUSED(version);

    const QJsonValue canvasValue = root.value(QStringLiteral("canvas"));
    if (!canvasValue.isObject()) {
        result.error = QStringLiteral("$.canvas: must be an object");
        return result;
    }
    const QJsonObject canvas = canvasValue.toObject();
    if (!hasOnlyFields(canvas, {QStringLiteral("width"), QStringLiteral("height")},
                       QStringLiteral("$.canvas"), &error)
        || !required(canvas, QStringLiteral("width"), QStringLiteral("$.canvas"), &error)
        || !required(canvas, QStringLiteral("height"), QStringLiteral("$.canvas"), &error)) {
        result.error = error;
        return result;
    }
    int columns = 0;
    int rows = 0;
    if (!integerValue(canvas.value(QStringLiteral("width")), 1, Document::maxDimension,
                      &columns, QStringLiteral("$.canvas.width"), &error)
        || !integerValue(canvas.value(QStringLiteral("height")), 1, Document::maxDimension,
                         &rows, QStringLiteral("$.canvas.height"), &error)
        || !root.value(QStringLiteral("palette")).isArray()
        || root.value(QStringLiteral("palette")).toArray().size() > Document::maxPaletteSlots
        || !readPalette(root.value(QStringLiteral("palette")), &result.document.palette(),
                        &error)) {
        if (error.isEmpty())
            error = QStringLiteral("$.palette: exceeds hard limit of %1 slots")
                        .arg(Document::maxPaletteSlots);
        result.error = error;
        return result;
    }

    const QJsonValue clipsValue = root.value(QStringLiteral("clips"));
    if (!clipsValue.isArray() || clipsValue.toArray().isEmpty()
        || clipsValue.toArray().size() > Document::maxClips) {
        result.error = QStringLiteral("$.clips: must be a non-empty array");
        return result;
    }
    QList<Clip> clips;
    QSet<QString> clipIds;
    QSet<QString> clipNames;
    for (int i = 0; i < clipsValue.toArray().size(); ++i) {
        const QString path = QStringLiteral("$.clips[%1]").arg(i);
        const QJsonValue value = clipsValue.toArray().at(i);
        if (!value.isObject()) {
            result.error = path + QStringLiteral(": must be an object");
            return result;
        }
        const QJsonObject object = value.toObject();
        if (!hasOnlyFields(object, {QStringLiteral("id"), QStringLiteral("name"),
                                    QStringLiteral("fps"), QStringLiteral("frameCount")},
                           path, &error)
            || !required(object, QStringLiteral("id"), path, &error)
            || !required(object, QStringLiteral("name"), path, &error)
            || !required(object, QStringLiteral("fps"), path, &error)
            || !required(object, QStringLiteral("frameCount"), path, &error)
            || !checkId(object.value(QStringLiteral("id")), path + QStringLiteral(".id"), &error)
            || !checkName(object.value(QStringLiteral("name")), path + QStringLiteral(".name"), &error)) {
            result.error = error;
            return result;
        }
        const QString id = object.value(QStringLiteral("id")).toString();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (clipIds.contains(id)) {
            result.error = QStringLiteral("%1.id: duplicates clip id `%2`").arg(path).arg(id);
            return result;
        }
        if (clipNames.contains(name)) {
            result.error = QStringLiteral("%1.name: duplicates clip name `%2`").arg(path).arg(name);
            return result;
        }
        Clip clip;
        clip.id = id;
        clip.name = name;
        if (!integerValue(object.value(QStringLiteral("fps")), 1, 60, &clip.fps,
                          path + QStringLiteral(".fps"), &error)
            || !integerValue(object.value(QStringLiteral("frameCount")), 1,
                             Document::maxFramesPerClip, &clip.frameCount,
                             path + QStringLiteral(".frameCount"), &error)) {
            result.error = error;
            return result;
        }
        clips.append(clip);
        clipIds.insert(id);
        clipNames.insert(name);
    }

    const QJsonValue layersValue = root.value(QStringLiteral("layers"));
    if (!layersValue.isArray() || layersValue.toArray().isEmpty()
        || layersValue.toArray().size() > Document::maxLayers) {
        result.error = QStringLiteral("$.layers: must be a non-empty array");
        return result;
    }
    QList<Layer> layers;
    QSet<QString> layerIds;
    QSet<QString> layerNames;
    const qint64 expectedAnimated = [&clips] {
        qint64 count = 0;
        for (const Clip &clip : clips)
            count += clip.frameCount;
        return count;
    }();
    if (expectedAnimated > Document::maxTotalFrames) {
        result.error = QStringLiteral("$.clips: total frame count exceeds hard limit of %1")
                           .arg(Document::maxTotalFrames);
        return result;
    }
    qint64 expectedCels = 0;
    for (const QJsonValue &layerValue : layersValue.toArray()) {
        if (!layerValue.isObject())
            continue;
        const QJsonObject layerObject = layerValue.toObject();
        const QJsonValue celsValue = layerObject.value(QStringLiteral("cels"));
        if (!celsValue.isArray())
            continue;
        expectedCels += layerObject.value(QStringLiteral("storage")).toString()
                            == QStringLiteral("shared")
                        ? 1
                        : expectedAnimated;
        if (expectedCels > Document::maxTotalCels) {
            result.error = QStringLiteral("$.layers: total cel count exceeds hard limit of %1")
                               .arg(Document::maxTotalCels);
            return result;
        }
    }
    for (int i = 0; i < layersValue.toArray().size(); ++i) {
        const QString path = QStringLiteral("$.layers[%1]").arg(i);
        const QJsonValue value = layersValue.toArray().at(i);
        if (!value.isObject()) {
            result.error = path + QStringLiteral(": must be an object");
            return result;
        }
        const QJsonObject object = value.toObject();
        const QStringList fields{QStringLiteral("id"), QStringLiteral("name"),
                                 QStringLiteral("visible"), QStringLiteral("locked"),
                                 QStringLiteral("opacity"), QStringLiteral("mode"),
                                 QStringLiteral("storage"), QStringLiteral("cels")};
        if (!hasOnlyFields(object, fields, path, &error)) {
            result.error = error;
            return result;
        }
        for (const QString &field : fields) {
            if (!required(object, field, path, &error)) {
                result.error = error;
                return result;
            }
        }
        if (!checkId(object.value(QStringLiteral("id")), path + QStringLiteral(".id"), &error)
            || !checkName(object.value(QStringLiteral("name")), path + QStringLiteral(".name"), &error)) {
            result.error = error;
            return result;
        }
        Layer layer;
        layer.id = object.value(QStringLiteral("id")).toString();
        layer.name = object.value(QStringLiteral("name")).toString();
        if (layerIds.contains(layer.id)) {
            result.error = QStringLiteral("%1.id: duplicates layer id `%2`").arg(path).arg(layer.id);
            return result;
        }
        if (layerNames.contains(layer.name)) {
            result.error = QStringLiteral("%1.name: duplicates layer name `%2`").arg(path).arg(layer.name);
            return result;
        }
        if (!object.value(QStringLiteral("visible")).isBool()
            || !object.value(QStringLiteral("locked")).isBool()) {
            result.error = path + QStringLiteral(".visible: must be boolean");
            return result;
        }
        layer.visible = object.value(QStringLiteral("visible")).toBool();
        layer.locked = object.value(QStringLiteral("locked")).toBool();
        if (!integerValue(object.value(QStringLiteral("opacity")), 0, 255, &layer.opacity,
                          path + QStringLiteral(".opacity"), &error)) {
            result.error = error;
            return result;
        }
        layer.mode = object.value(QStringLiteral("mode")).toString();
        if (!QStringList{QStringLiteral("normal"), QStringLiteral("multiply"),
                         QStringLiteral("screen")}.contains(layer.mode)) {
            result.error = path + QStringLiteral(".mode: must be normal, multiply, or screen");
            return result;
        }
        layer.storage = object.value(QStringLiteral("storage")).toString();
        if (layer.storage != QStringLiteral("shared")
            && layer.storage != QStringLiteral("animated")) {
            result.error = path + QStringLiteral(".storage: must be shared or animated");
            return result;
        }
        const QJsonValue celsValue = object.value(QStringLiteral("cels"));
        if (!celsValue.isArray()) {
            result.error = path + QStringLiteral(".cels: must be an array");
            return result;
        }
        const QJsonArray cels = celsValue.toArray();
        const qint64 expected = layer.storage == QStringLiteral("shared") ? 1 : expectedAnimated;
        if (cels.size() != expected) {
            result.error = QStringLiteral("%1.cels: %2 storage requires exactly %3 cels")
                               .arg(path).arg(layer.storage).arg(expected);
            return result;
        }
        QSet<QString> seen;
        for (int celIndex = 0; celIndex < cels.size(); ++celIndex) {
            const QString celPath = QStringLiteral("%1.cels[%2]").arg(path).arg(celIndex);
            if (!cels.at(celIndex).isObject()) {
                result.error = celPath + QStringLiteral(": must be an object");
                return result;
            }
            const QJsonObject celObject = cels.at(celIndex).toObject();
            Cel cel;
            if (layer.storage == QStringLiteral("shared")) {
                if (!hasOnlyFields(celObject, {QStringLiteral("scope"), QStringLiteral("rows")},
                                   celPath, &error)
                    || !required(celObject, QStringLiteral("scope"), celPath, &error)
                    || !required(celObject, QStringLiteral("rows"), celPath, &error)
                    || celObject.value(QStringLiteral("scope")).toString() != QStringLiteral("all")) {
                    result.error = error.isEmpty() ? celPath + QStringLiteral(".scope: must be `all`") : error;
                    return result;
                }
                if (!readRows(celObject.value(QStringLiteral("rows")), columns, rows,
                              result.document.palette(), celPath + QStringLiteral(".rows"),
                              &cel.grid, &error)) {
                    result.error = error;
                    return result;
                }
            } else {
                if (!hasOnlyFields(celObject, {QStringLiteral("clip"), QStringLiteral("frame"),
                                               QStringLiteral("rows")}, celPath, &error)
                    || !required(celObject, QStringLiteral("clip"), celPath, &error)
                    || !required(celObject, QStringLiteral("frame"), celPath, &error)
                    || !required(celObject, QStringLiteral("rows"), celPath, &error)
                    || !checkId(celObject.value(QStringLiteral("clip")), celPath + QStringLiteral(".clip"), &error)) {
                    result.error = error;
                    return result;
                }
                cel.clip = celObject.value(QStringLiteral("clip")).toString();
                const Clip *clip = nullptr;
                for (const Clip &candidate : clips)
                    if (candidate.id == cel.clip)
                        clip = &candidate;
                if (!clip) {
                    result.error = celPath + QStringLiteral(".clip: unknown clip `%1`").arg(cel.clip);
                    return result;
                }
                if (!integerValue(celObject.value(QStringLiteral("frame")), 0,
                                  clip->frameCount - 1, &cel.frame,
                                  celPath + QStringLiteral(".frame"), &error)
                    || !readRows(celObject.value(QStringLiteral("rows")), columns, rows,
                                 result.document.palette(), celPath + QStringLiteral(".rows"),
                                 &cel.grid, &error)) {
                    result.error = error;
                    return result;
                }
                const QString key = QStringLiteral("%1:%2").arg(cel.clip).arg(cel.frame);
                if (seen.contains(key)) {
                    result.error = celPath + QStringLiteral(".frame: duplicates an animated cel");
                    return result;
                }
                seen.insert(key);
            }
            layer.cels.append(cel);
        }
        if (layer.storage == QStringLiteral("animated")) {
            for (const Clip &clip : clips) {
                for (int frame = 0; frame < clip.frameCount; ++frame) {
                    if (!seen.contains(QStringLiteral("%1:%2").arg(clip.id).arg(frame))) {
                        result.error = path + QStringLiteral(".cels: must cover every clip/frame pair exactly once");
                        return result;
                    }
                }
            }
        }
        layers.append(layer);
        layerIds.insert(layer.id);
        layerNames.insert(layer.name);
    }

    Document doc = Document::empty(columns, rows);
    doc.palette() = result.document.palette();
    for (const Clip &clip : clips) {
        if (!doc.addClip(clip.name, clip.fps)) {
            result.error = QStringLiteral("$.clips: could not add validated clip `%1`").arg(clip.name);
            return result;
        }
        doc.clip(clip.name)->id = clip.id;
        doc.clip(clip.name)->frameCount = clip.frameCount;
    }
    for (const Layer &layer : layers) {
        if (!doc.addLayer(layer.id, layer.name, layer.storage)) {
            result.error = QStringLiteral("$.layers: could not add validated layer `%1`").arg(layer.name);
            return result;
        }
        Layer *target = doc.layerById(layer.id);
        *target = layer;
    }
    result.document = doc;
    result.warnings = resourceWarnings(doc, json.size(), limits);
    result.ok = true;
    return result;
}

Codec::Result Codec::readFile(const QString &path)
{
    return readFile(path, WarningLimits());
}

Codec::Result Codec::readFile(const QString &path, const WarningLimits &limits)
{
    QByteArray bytes;
    QString inputError;
    if (!input::readRegularFile(path, Document::maxDocumentBytes, &bytes,
                                &inputError)) {
        Result result;
        result.error = inputError;
        return result;
    }
    if (bytes.size() > Document::maxDocumentBytes) {
        Result result;
        result.error = QStringLiteral("%1: document exceeds hard limit of %2 MiB")
                           .arg(path)
                           .arg(Document::maxDocumentBytes / (1024 * 1024));
        return result;
    }
    return read(bytes, limits);
}

bool Codec::rejectDuplicateJsonKeys(const QByteArray &json, QString *error)
{
    QString ignoredError;
    return rejectDuplicateJsonKeysImpl(json, error ? error : &ignoredError);
}

QByteArray Codec::write(const Document &document, QString *error)
{
    const QStringList problems = document.problems();
    if (!problems.isEmpty()) {
        if (error)
            *error = QStringLiteral("document is invalid: %1")
                         .arg(problems.first());
        return QByteArray();
    }
    QJsonArray palette;
    for (const Palette::Slot &slot : document.palette().entries()) {
        QJsonObject entry;
        entry.insert(QStringLiteral("slot"), QString(slot.letter));
        entry.insert(QStringLiteral("colour"), colourString(slot.colour));
        palette.append(entry);
    }
    QJsonArray clips;
    for (const Clip &clip : document.clips()) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), clip.id);
        entry.insert(QStringLiteral("name"), clip.name);
        entry.insert(QStringLiteral("fps"), clip.fps);
        entry.insert(QStringLiteral("frameCount"), clip.frameCount);
        clips.append(entry);
    }
    QJsonArray layers;
    for (const Layer &layer : document.layers()) {
        QJsonArray cels;
        for (const Cel &cel : layer.cels) {
            QJsonObject entry;
            if (layer.storage == QStringLiteral("shared")) {
                entry.insert(QStringLiteral("scope"), QStringLiteral("all"));
            } else {
                entry.insert(QStringLiteral("clip"), cel.clip);
                entry.insert(QStringLiteral("frame"), cel.frame);
            }
            entry.insert(QStringLiteral("rows"), rowsOf(cel.grid));
            cels.append(entry);
        }
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), layer.id);
        entry.insert(QStringLiteral("name"), layer.name);
        entry.insert(QStringLiteral("visible"), layer.visible);
        entry.insert(QStringLiteral("locked"), layer.locked);
        entry.insert(QStringLiteral("opacity"), layer.opacity);
        entry.insert(QStringLiteral("mode"), layer.mode);
        entry.insert(QStringLiteral("storage"), layer.storage);
        entry.insert(QStringLiteral("cels"), cels);
        layers.append(entry);
    }
    QJsonObject canvas;
    canvas.insert(QStringLiteral("width"), document.columns());
    canvas.insert(QStringLiteral("height"), document.rows());
    QJsonObject root;
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("canvas"), canvas);
    root.insert(QStringLiteral("palette"), palette);
    root.insert(QStringLiteral("clips"), clips);
    root.insert(QStringLiteral("layers"), layers);
    const QByteArray encoded = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (encoded.size() > Document::maxDocumentBytes) {
        if (error)
            *error = QStringLiteral("serialized document exceeds the hard limit of %1 MiB")
                         .arg(Document::maxDocumentBytes / (1024 * 1024));
        return QByteArray();
    }
    return encoded;
}

bool Codec::writeFile(const QString &path, const Document &document, QString *error)
{
    return writeFile(path, document, {}, error);
}

bool Codec::writeFile(const QString &path, const Document &document,
                      const QStringList &sources, QString *error)
{
    const QByteArray encoded = write(document, error);
    if (encoded.isEmpty())
        return false;
    return output::writeAtomically(path, encoded, sources, error);
}

} // namespace omapixel
