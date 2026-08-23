#include "Codec.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include <cmath>

namespace omapixel {
namespace {

bool fail(QString *error, const QString &path, const QString &message)
{
    *error = QStringLiteral("%1: %2").arg(path, message);
    return false;
}

QString objectPath(const QString &path, const QString &key)
{
    QString escaped = key;
    escaped.replace(QChar(u'\\'), QStringLiteral("\\\\"));
    escaped.replace(QChar(u'\"'), QStringLiteral("\\\""));
    return QStringLiteral("%1[\"%2\"]").arg(path, escaped);
}

bool hasOnlyFields(const QJsonObject &object, const QStringList &allowed,
                   const QString &path, QString *error)
{
    for (const QString &key : object.keys()) {
        if (!allowed.contains(key))
            return fail(error, path + QStringLiteral(".") + key,
                        QStringLiteral("unknown field"));
    }
    return true;
}

void skipJsonSpace(const QByteArray &json, int *position)
{
    while (*position < json.size()
           && (json.at(*position) == ' ' || json.at(*position) == '\t'
               || json.at(*position) == '\n' || json.at(*position) == '\r'))
        ++*position;
}

bool scanJsonString(const QByteArray &json, int *position, QByteArray *token)
{
    if (*position >= json.size() || json.at(*position) != '"')
        return false;

    const int start = *position;
    ++*position;
    while (*position < json.size()) {
        const char character = json.at(*position);
        if (character == '\\') {
            ++*position;
            if (*position >= json.size())
                return false;
            ++*position;
            continue;
        }
        ++*position;
        if (character == '"') {
            *token = json.mid(start, *position - start);
            return true;
        }
    }
    return false;
}

QString decodedJsonKey(const QByteArray &token)
{
    QJsonParseError parse;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray("{") + token + QByteArray(":null}"),
                                &parse);
    return document.object().keys().value(0);
}

bool scanJsonValue(const QByteArray &json, int *position, const QString &path,
                   QString *error);

bool scanJsonObject(const QByteArray &json, int *position, const QString &path,
                    QString *error)
{
    ++*position; // '{'
    skipJsonSpace(json, position);
    if (*position < json.size() && json.at(*position) == '}') {
        ++*position;
        return true;
    }

    QSet<QString> keys;
    while (*position < json.size()) {
        QByteArray token;
        if (!scanJsonString(json, position, &token))
            return false;
        const QString key = decodedJsonKey(token);
        if (keys.contains(key))
            return fail(error, objectPath(path, key),
                        QStringLiteral("duplicate field"));
        keys.insert(key);

        skipJsonSpace(json, position);
        if (*position >= json.size() || json.at(*position) != ':')
            return false;
        ++*position;
        skipJsonSpace(json, position);
        if (!scanJsonValue(json, position, objectPath(path, key), error))
            return false;

        skipJsonSpace(json, position);
        if (*position < json.size() && json.at(*position) == '}') {
            ++*position;
            return true;
        }
        if (*position >= json.size() || json.at(*position) != ',')
            return false;
        ++*position;
        skipJsonSpace(json, position);
    }
    return false;
}

bool scanJsonArray(const QByteArray &json, int *position, const QString &path,
                   QString *error)
{
    ++*position; // '['
    skipJsonSpace(json, position);
    if (*position < json.size() && json.at(*position) == ']') {
        ++*position;
        return true;
    }

    int index = 0;
    while (*position < json.size()) {
        const QString childPath = QStringLiteral("%1[%2]").arg(path).arg(index);
        if (!scanJsonValue(json, position, childPath, error))
            return false;
        ++index;
        skipJsonSpace(json, position);
        if (*position < json.size() && json.at(*position) == ']') {
            ++*position;
            return true;
        }
        if (*position >= json.size() || json.at(*position) != ',')
            return false;
        ++*position;
        skipJsonSpace(json, position);
    }
    return false;
}

bool scanJsonValue(const QByteArray &json, int *position, const QString &path,
                   QString *error)
{
    if (*position >= json.size())
        return false;
    switch (json.at(*position)) {
    case '{':
        return scanJsonObject(json, position, path, error);
    case '[':
        return scanJsonArray(json, position, path, error);
    case '"': {
        QByteArray token;
        return scanJsonString(json, position, &token);
    }
    default:
        // The JSON document has already been parsed successfully. For scalar
        // values, advancing to the next structural delimiter is sufficient.
        while (*position < json.size()) {
            const char character = json.at(*position);
            if (character == ',' || character == ']' || character == '}'
                || character == ' ' || character == '\t'
                || character == '\n' || character == '\r')
                break;
            ++*position;
        }
        return true;
    }
}

bool rejectDuplicateJsonKeys(const QByteArray &json, QString *error)
{
    int position = 0;
    skipJsonSpace(json, &position);
    if (!scanJsonValue(json, &position, QStringLiteral("$"), error))
        return !error->isEmpty(); // QJsonDocument already diagnosed the syntax.
    return false;
}

bool integerValue(const QJsonValue &value, int minimum, int maximum, int *out,
                  const QString &path, QString *error)
{
    if (!value.isDouble())
        return fail(error, path, QStringLiteral("must be an integer number"));

    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < minimum || number > maximum) {
        return fail(error, path,
                    QStringLiteral("must be an integer between %1 and %2")
                        .arg(minimum)
                        .arg(maximum));
    }

    *out = static_cast<int>(number);
    return true;
}

bool required(const QJsonObject &object, const QString &key,
              const QString &path, QString *error)
{
    if (!object.contains(key))
        return fail(error, path + QStringLiteral(".") + key,
                    QStringLiteral("is required"));
    return true;
}

bool readPalette(const QJsonValue &value, Palette *palette,
                 QString *error)
{
    if (!value.isArray() && !value.isObject())
        return fail(error, QStringLiteral("$.palette"),
                    QStringLiteral("must be an array or object"));

    QSet<QChar> seen;
    if (value.isArray()) {
        const QJsonArray entries = value.toArray();
        for (int i = 0; i < entries.size(); ++i) {
            const QString path = QStringLiteral("$.palette[%1]").arg(i);
            const QJsonValue entryValue = entries.at(i);
            if (!entryValue.isObject())
                return fail(error, path, QStringLiteral("must be an object"));

            const QJsonObject entry = entryValue.toObject();
            if (!hasOnlyFields(entry, {QStringLiteral("slot"),
                                       QStringLiteral("colour")},
                               path, error)
                || !required(entry, QStringLiteral("slot"), path, error)
                || !required(entry, QStringLiteral("colour"), path, error))
                return false;

            const QString slotPath = path + QStringLiteral(".slot");
            const QJsonValue slotValue = entry.value(QStringLiteral("slot"));
            if (!slotValue.isString())
                return fail(error, slotPath,
                            QStringLiteral("must be a string containing one QChar"));
            const QString letter = slotValue.toString();
            if (letter.size() != 1)
                return fail(error, slotPath,
                            QStringLiteral("must contain exactly one QChar"));

            const QChar character = letter.at(0);
            if (character == Grid::Empty)
                return fail(error, slotPath,
                            QStringLiteral("`.` is reserved for transparency"));
            if (seen.contains(character))
                return fail(error, slotPath,
                            QStringLiteral("duplicates palette slot `%1`")
                                .arg(letter));

            const QString colourPath = path + QStringLiteral(".colour");
            const QJsonValue colourValue =
                entry.value(QStringLiteral("colour"));
            if (!colourValue.isString())
                return fail(error, colourPath,
                            QStringLiteral("must be a colour string"));
            const QColor colour(colourValue.toString());
            if (!colour.isValid())
                return fail(error, colourPath,
                            QStringLiteral("is not a valid colour"));

            seen.insert(character);
            palette->set(character, colour);
        }
        return true;
    }

    const QJsonObject entries = value.toObject();
    for (const QString &letter : entries.keys()) {
        const QString path = objectPath(QStringLiteral("$.palette"), letter);
        if (letter.size() != 1)
            return fail(error, path,
                        QStringLiteral("palette slot must contain exactly one QChar"));

        const QChar character = letter.at(0);
        if (character == Grid::Empty)
            return fail(error, path,
                        QStringLiteral("`.` is reserved for transparency"));
        if (seen.contains(character))
            return fail(error, path,
                        QStringLiteral("duplicates palette slot `%1`")
                            .arg(letter));

        const QJsonValue colourValue = entries.value(letter);
        if (!colourValue.isString())
            return fail(error, path, QStringLiteral("must be a colour string"));
        const QColor colour(colourValue.toString());
        if (!colour.isValid())
            return fail(error, path, QStringLiteral("is not a valid colour"));

        seen.insert(character);
        palette->set(character, colour);
    }
    return true;
}

bool readFrames(const QJsonValue &value, int columns, int rows,
                const Palette &palette, const QString &path,
                QList<Grid> *frames, QString *error)
{
    if (!value.isArray())
        return fail(error, path, QStringLiteral("must be an array"));

    const QJsonArray frameValues = value.toArray();
    if (frameValues.isEmpty())
        return fail(error, path, QStringLiteral("must not be empty"));

    frames->reserve(frameValues.size());
    for (int frameIndex = 0; frameIndex < frameValues.size(); ++frameIndex) {
        const QString framePath =
            QStringLiteral("%1[%2]").arg(path).arg(frameIndex);
        const QJsonValue frameValue = frameValues.at(frameIndex);
        if (!frameValue.isArray())
            return fail(error, framePath, QStringLiteral("must be an array"));

        const QJsonArray rowValues = frameValue.toArray();
        if (rowValues.size() != rows)
            return fail(error, framePath,
                        QStringLiteral("must contain exactly %1 rows").arg(rows));

        QStringList rowStrings;
        rowStrings.reserve(rows);
        for (int rowIndex = 0; rowIndex < rowValues.size(); ++rowIndex) {
            const QString rowPath =
                QStringLiteral("%1[%2]").arg(framePath).arg(rowIndex);
            const QJsonValue rowValue = rowValues.at(rowIndex);
            if (!rowValue.isString())
                return fail(error, rowPath, QStringLiteral("must be a string"));

            const QString row = rowValue.toString();
            if (row.size() != columns)
                return fail(error, rowPath,
                            QStringLiteral("must contain exactly %1 QChars")
                                .arg(columns));

            for (int column = 0; column < row.size(); ++column) {
                const QChar character = row.at(column);
                if (character != Grid::Empty && !palette.has(character)) {
                    return fail(error,
                                QStringLiteral("%1[%2]")
                                    .arg(rowPath)
                                    .arg(column),
                                QStringLiteral("uses undefined palette slot `%1`")
                                    .arg(character));
                }
            }
            rowStrings.append(row);
        }
        frames->append(Grid::fromRows(rowStrings));
    }
    return true;
}

struct ParsedClip {
    QString name;
    int fps = 0;
    QList<Grid> frames;
};

bool readClip(const QJsonObject &clip, const QString &name, int columns,
              int rows, const Palette &palette, const QString &path,
              bool hasName, ParsedClip *parsed, QString *error)
{
    const QStringList allowed = hasName
                                    ? QStringList{QStringLiteral("name"),
                                                  QStringLiteral("fps"),
                                                  QStringLiteral("frames")}
                                    : QStringList{QStringLiteral("fps"),
                                                  QStringLiteral("frames")};
    if (!hasOnlyFields(clip, allowed, path, error)
        || !required(clip, QStringLiteral("fps"), path, error)
        || !required(clip, QStringLiteral("frames"), path, error))
        return false;

    int fps = 0;
    if (!integerValue(clip.value(QStringLiteral("fps")), 1, 60, &fps,
                      path + QStringLiteral(".fps"), error))
        return false;

    QList<Grid> frames;
    if (!readFrames(clip.value(QStringLiteral("frames")), columns, rows,
                    palette, path + QStringLiteral(".frames"), &frames,
                    error))
        return false;

    parsed->name = name;
    parsed->fps = fps;
    parsed->frames = frames;
    return true;
}

QStringList resourceWarnings(const Document &document, qint64 bytes,
                             const Codec::WarningLimits &limits)
{
    QStringList warnings;
    if (limits.fileBytes > 0 && bytes > limits.fileBytes) {
        warnings << QStringLiteral("document is %1 MiB; warning threshold is %2 MiB")
                        .arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1)
                        .arg(double(limits.fileBytes) / (1024.0 * 1024.0), 0, 'f', 1);
    }
    if (limits.paletteSlots > 0
        && document.palette().entries().size() > limits.paletteSlots) {
        warnings << QStringLiteral("document has %1 palette slots; warning threshold is %2")
                        .arg(document.palette().entries().size())
                        .arg(limits.paletteSlots);
    }
    if (limits.clips > 0 && document.clips().size() > limits.clips) {
        warnings << QStringLiteral("document has %1 clips; warning threshold is %2")
                        .arg(document.clips().size())
                        .arg(limits.clips);
    }
    qint64 totalFrames = 0;
    for (const Clip &clip : document.clips()) {
        totalFrames += clip.frames.size();
        if (limits.framesPerClip > 0
            && clip.frames.size() > limits.framesPerClip) {
            warnings << QStringLiteral("clip %1 has %2 frames; warning threshold is %3")
                            .arg(clip.name)
                            .arg(clip.frames.size())
                            .arg(limits.framesPerClip);
        }
    }
    if (limits.totalFrames > 0 && totalFrames > limits.totalFrames) {
        warnings << QStringLiteral("document has %1 frames; warning threshold is %2")
                        .arg(totalFrames)
                        .arg(limits.totalFrames);
    }
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

    QJsonParseError parse;
    const QJsonDocument parsed = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError) {
        result.error = QStringLiteral("invalid JSON at offset %1: %2")
                           .arg(parse.offset)
                           .arg(parse.errorString());
        return result;
    }
    QString error;
    if (rejectDuplicateJsonKeys(json, &error)) {
        result.error = error;
        return result;
    }
    if (!parsed.isObject()) {
        result.error = QStringLiteral("$: the document has to be a JSON object");
        return result;
    }

    const QJsonObject root = parsed.object();
    if (!hasOnlyFields(root, {QStringLiteral("size"),
                              QStringLiteral("palette"),
                              QStringLiteral("clips")},
                       QStringLiteral("$"), &error)
        || !required(root, QStringLiteral("size"), QStringLiteral("$"),
                     &error)
        || !required(root, QStringLiteral("palette"), QStringLiteral("$"),
                     &error)
        || !required(root, QStringLiteral("clips"), QStringLiteral("$"),
                     &error)) {
        result.error = error;
        return result;
    }

    const QJsonValue sizeValue = root.value(QStringLiteral("size"));
    if (!sizeValue.isObject()) {
        result.error = QStringLiteral("$.size: must be an object");
        return result;
    }
    const QJsonObject size = sizeValue.toObject();
    if (!hasOnlyFields(size, {QStringLiteral("w"), QStringLiteral("h")},
                       QStringLiteral("$.size"), &error)
        || !required(size, QStringLiteral("w"), QStringLiteral("$.size"),
                     &error)
        || !required(size, QStringLiteral("h"), QStringLiteral("$.size"),
                     &error)) {
        result.error = error;
        return result;
    }

    int columns = 0;
    int rows = 0;
    if (!integerValue(size.value(QStringLiteral("w")), 1, Document::maxDimension,
                      &columns, QStringLiteral("$.size.w"), &error)
        || !integerValue(size.value(QStringLiteral("h")), 1, Document::maxDimension,
                         &rows, QStringLiteral("$.size.h"), &error)) {
        result.error = error;
        return result;
    }

    Palette palette;
    if (!readPalette(root.value(QStringLiteral("palette")), &palette, &error)) {
        result.error = error;
        return result;
    }

    const QJsonValue clipsValue = root.value(QStringLiteral("clips"));
    if (!clipsValue.isArray() && !clipsValue.isObject()) {
        result.error = QStringLiteral("$.clips: must be an array or object");
        return result;
    }

    QList<ParsedClip> clips;
    QSet<QString> names;
    if (clipsValue.isArray()) {
        const QJsonArray clipValues = clipsValue.toArray();
        if (clipValues.isEmpty()) {
            result.error = QStringLiteral("$.clips: must not be empty");
            return result;
        }

        clips.reserve(clipValues.size());
        for (int i = 0; i < clipValues.size(); ++i) {
            const QString path = QStringLiteral("$.clips[%1]").arg(i);
            const QJsonValue clipValue = clipValues.at(i);
            if (!clipValue.isObject()) {
                result.error = path + QStringLiteral(": must be an object");
                return result;
            }
            const QJsonObject clip = clipValue.toObject();
            if (!hasOnlyFields(clip, {QStringLiteral("name"),
                                      QStringLiteral("fps"),
                                      QStringLiteral("frames")},
                               path, &error)
                || !required(clip, QStringLiteral("name"), path, &error)
                || !required(clip, QStringLiteral("fps"), path, &error)
                || !required(clip, QStringLiteral("frames"), path, &error)) {
                result.error = error;
                return result;
            }

            const QJsonValue nameValue = clip.value(QStringLiteral("name"));
            if (!nameValue.isString()) {
                result.error = path + QStringLiteral(".name: must be a string");
                return result;
            }
            const QString name = nameValue.toString();
            if (name.isEmpty()) {
                result.error = path + QStringLiteral(".name: must not be empty");
                return result;
            }
            if (names.contains(name)) {
                result.error = path + QStringLiteral(".name: duplicate clip name `%1`")
                                           .arg(name);
                return result;
            }

            ParsedClip parsedClip;
            if (!readClip(clip, name, columns, rows, palette, path, true,
                          &parsedClip, &error)) {
                result.error = error;
                return result;
            }
            names.insert(name);
            clips.append(parsedClip);
        }
    } else {
        const QJsonObject clipValues = clipsValue.toObject();
        if (clipValues.isEmpty()) {
            result.error = QStringLiteral("$.clips: must not be empty");
            return result;
        }

        clips.reserve(clipValues.size());
        for (const QString &name : clipValues.keys()) {
            const QString path = objectPath(QStringLiteral("$.clips"), name);
            if (name.isEmpty()) {
                result.error = path + QStringLiteral(": clip name must not be empty");
                return result;
            }
            const QJsonValue clipValue = clipValues.value(name);
            if (!clipValue.isObject()) {
                result.error = path + QStringLiteral(": must be an object");
                return result;
            }

            ParsedClip parsedClip;
            if (!readClip(clipValue.toObject(), name, columns, rows, palette,
                          path, false, &parsedClip, &error)) {
                result.error = error;
                return result;
            }
            names.insert(name);
            clips.append(parsedClip);
        }
    }

    // All input has been checked before any Document mutation can normalize it.
    Document doc = Document::empty(columns, rows);
    doc.palette() = palette;
    for (const ParsedClip &clip : clips) {
        if (!doc.addClip(clip.name, clip.fps)) {
            result.error = QStringLiteral("$.clips: could not add validated clip `%1`")
                               .arg(clip.name);
            return result;
        }
        doc.clip(clip.name)->frames = clip.frames;
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
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        Result result;
        result.error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return result;
    }
    return read(file.readAll(), limits);
}

QByteArray Codec::write(const Document &document)
{
    QJsonObject size;
    size.insert(QStringLiteral("w"), document.columns());
    size.insert(QStringLiteral("h"), document.rows());

    QJsonArray palette;
    for (const Palette::Slot &slot : document.palette().entries()) {
        QJsonObject entry;
        entry.insert(QStringLiteral("slot"), QString(slot.letter));
        entry.insert(QStringLiteral("colour"),
                     slot.colour.name(QColor::HexRgb).toUpper());
        palette.append(entry);
    }

    QJsonArray clips;
    for (const Clip &clip : document.clips()) {
        QJsonArray frames;
        for (const Grid &grid : clip.frames) {
            QJsonArray rows;
            for (const QString &row : grid.toRows())
                rows.append(row);
            frames.append(rows);
        }
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), clip.name);
        entry.insert(QStringLiteral("fps"), clip.fps);
        entry.insert(QStringLiteral("frames"), frames);
        clips.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("size"), size);
    root.insert(QStringLiteral("palette"), palette);
    root.insert(QStringLiteral("clips"), clips);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool Codec::writeFile(const QString &path, const Document &document,
                      QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    const QByteArray encoded = write(document);
    if (file.write(encoded) != encoded.size()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace omapixel
