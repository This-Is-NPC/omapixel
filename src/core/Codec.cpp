#include "Codec.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace omapixel {
namespace {

QStringList rowsOf(const QJsonArray &array)
{
    QStringList rows;
    rows.reserve(array.size());
    for (const QJsonValue &row : array)
        rows.append(row.toString());
    return rows;
}

/// Reads the palette from either shape: the array this version writes, or the
/// object the Python version wrote. Accepting both costs eight lines and means
/// nobody's file stops opening because the program grew up.
Palette readPalette(const QJsonValue &value)
{
    Palette palette;
    if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray()) {
            const QJsonObject slot = entry.toObject();
            const QString letter = slot.value(QStringLiteral("slot")).toString();
            if (letter.size() != 1)
                continue;
            palette.set(letter.at(0),
                        QColor(slot.value(QStringLiteral("colour")).toString()));
        }
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        // Sorted, because that is all an object can give us. It only affects
        // legacy files, and only the order of the strip.
        for (const QString &letter : object.keys()) {
            if (letter.size() != 1)
                continue;
            palette.set(letter.at(0), QColor(object.value(letter).toString()));
        }
    }
    return palette;
}

} // namespace

Codec::Result Codec::read(const QByteArray &json)
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
    if (!parsed.isObject()) {
        result.error = QStringLiteral("the document has to be a JSON object");
        return result;
    }

    const QJsonObject root = parsed.object();
    const QJsonObject size = root.value(QStringLiteral("size")).toObject();
    const int columns = size.value(QStringLiteral("w")).toInt();
    const int rows = size.value(QStringLiteral("h")).toInt();
    if (columns <= 0 || rows <= 0) {
        result.error = QStringLiteral("size.w and size.h have to be positive");
        return result;
    }

    // Starts with no clips so the file's own come out in the file's order,
    // rather than after a placeholder that would then have to be removed.
    Document doc = Document::empty(columns, rows);
    doc.palette() = readPalette(root.value(QStringLiteral("palette")));

    const QJsonValue clips = root.value(QStringLiteral("clips"));
    if (clips.isArray()) {
        for (const QJsonValue &entry : clips.toArray()) {
            const QJsonObject clip = entry.toObject();
            const QString name = clip.value(QStringLiteral("name")).toString();
            if (name.isEmpty() || !doc.addClip(name))
                continue;
            doc.setFps(name, clip.value(QStringLiteral("fps")).toInt(8));
            // addClip seeded one blank frame; the file's frames replace it.
            Clip *target = doc.clip(name);
            target->frames.clear();
            for (const QJsonValue &frame :
                 clip.value(QStringLiteral("frames")).toArray()) {
                target->frames.append(Grid::fromRows(rowsOf(frame.toArray())));
            }
            if (target->frames.isEmpty())
                target->frames.append(Grid(columns, rows));
        }
    } else if (clips.isObject()) {
        const QJsonObject object = clips.toObject();
        for (const QString &name : object.keys()) {
            if (!doc.addClip(name))
                continue;
            const QJsonObject clip = object.value(name).toObject();
            doc.setFps(name, clip.value(QStringLiteral("fps")).toInt(8));
            Clip *target = doc.clip(name);
            target->frames.clear();
            for (const QJsonValue &frame :
                 clip.value(QStringLiteral("frames")).toArray()) {
                target->frames.append(Grid::fromRows(rowsOf(frame.toArray())));
            }
            if (target->frames.isEmpty())
                target->frames.append(Grid(columns, rows));
        }
    }

    // A file whose clips are missing, empty, or all unreadable still has to
    // open -- it is usually a file somebody is midway through writing by hand,
    // and refusing it is refusing the one thing that would show them why.
    if (doc.clips().isEmpty())
        doc.addClip(QStringLiteral("idle"));

    result.document = doc;
    result.ok = true;
    return result;
}

Codec::Result Codec::readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        Result result;
        result.error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return result;
    }
    return read(file.readAll());
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
    file.write(write(document));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace omapixel
