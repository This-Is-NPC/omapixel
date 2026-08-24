#include "Bridge.h"
#include "Render.h"
#include "TextSafety.h"

#include <QJsonArray>
#include <QColor>
#include <QRegularExpression>

namespace omapixel {
namespace {

QList<Grid> gridsOf(const QJsonArray &frames)
{
    QList<Grid> out;
    out.reserve(frames.size());
    for (const QJsonValue &frame : frames) {
        QStringList rows;
        for (const QJsonValue &row : frame.toArray())
            rows.append(row.toString());
        out.append(Grid::fromRows(rows));
    }
    return out;
}

QJsonArray framesOf(const QList<Grid> &grids)
{
    QJsonArray frames;
    for (const Grid &grid : grids) {
        QJsonArray rows;
        for (const QString &row : grid.toRows())
            rows.append(row);
        frames.append(rows);
    }
    return frames;
}

QString validateFrames(const QJsonValue &value, const QString &path,
                       int *columns = nullptr, int *rows = nullptr,
                       qint64 *cells = nullptr)
{
    if (!value.isArray())
        return QStringLiteral("%1 must be an array of frames").arg(path);
    const QJsonArray frames = value.toArray();
    if (frames.size() > Document::maxFramesPerClip)
        return QStringLiteral("%1 has more than %2 frames")
            .arg(path).arg(Document::maxFramesPerClip);
    qint64 totalCells = 0;
    int expectedColumns = -1;
    int expectedRows = -1;
    for (int frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        const QJsonValue frame = frames.at(frameIndex);
        if (!frame.isArray())
            return QStringLiteral("%1[%2] must be an array of rows")
                .arg(path)
                .arg(frameIndex);
        const QJsonArray rows = frame.toArray();
        if (rows.isEmpty() || rows.size() > Document::maxDimension)
            return QStringLiteral("%1[%2] has an invalid row count")
                .arg(path).arg(frameIndex);
        int frameColumns = -1;
        for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            if (!rows.at(rowIndex).isString())
                return QStringLiteral("%1[%2][%3] must be a string")
                    .arg(path)
                    .arg(frameIndex)
                    .arg(rowIndex);
            const QString row = rows.at(rowIndex).toString();
            if (row.size() > Document::maxDimension)
                return QStringLiteral("%1[%2][%3] is too wide")
                    .arg(path).arg(frameIndex).arg(rowIndex);
            if (frameColumns < 0)
                frameColumns = row.size();
            if (row.size() != frameColumns)
                return QStringLiteral("%1[%2] has ragged rows")
                    .arg(path).arg(frameIndex);
        }
        if (frameColumns <= 0)
            return QStringLiteral("%1[%2] has empty rows")
                .arg(path).arg(frameIndex);
        if (expectedColumns < 0) {
            expectedColumns = frameColumns;
            expectedRows = rows.size();
        }
        if (frameColumns != expectedColumns || rows.size() != expectedRows)
            return QStringLiteral("%1 has inconsistent frame dimensions").arg(path);
        totalCells += qint64(frameColumns) * rows.size();
        if (totalCells > Document::maxDocumentBytes)
            return QStringLiteral("%1 exceeds the materialization cell limit").arg(path);
    }
    if (columns && *columns >= 0 && *columns != expectedColumns)
        return QStringLiteral("%1 has inconsistent catalog width").arg(path);
    if (rows && *rows >= 0 && *rows != expectedRows)
        return QStringLiteral("%1 has inconsistent catalog height").arg(path);
    if (columns)
        *columns = expectedColumns;
    if (rows)
        *rows = expectedRows;
    if (cells)
        *cells += totalCells;
    if (cells && *cells > Document::maxDocumentBytes)
        return QStringLiteral("%1 exceeds the materialization cell limit").arg(path);
    return QString();
}

QString validateCatalogPalette(const QJsonValue &value, Palette *palette = nullptr)
{
    if (!value.isObject())
        return QStringLiteral("catalog: palette must be an object");
    const QJsonObject entries = value.toObject();
    if (entries.size() > Document::maxPaletteSlots)
        return QStringLiteral("catalog: palette has too many slots");
    const QRegularExpression colourPattern(QStringLiteral("^#[0-9A-Fa-f]{8}$"));
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QString slot = it.key();
        if (slot.size() != 1 || !Palette::validSlot(slot.at(0)))
            return QStringLiteral("catalog: palette.%1 has an invalid slot").arg(slot);
        if (!it.value().isString()
            || !colourPattern.match(it.value().toString()).hasMatch())
            return QStringLiteral("catalog: palette.%1 must be #RRGGBBAA").arg(slot);
        if (palette) {
            const QString colour = it.value().toString();
            const QColor value(colour.mid(1, 2).toInt(nullptr, 16),
                               colour.mid(3, 2).toInt(nullptr, 16),
                               colour.mid(5, 2).toInt(nullptr, 16),
                               colour.mid(7, 2).toInt(nullptr, 16));
            if (!palette->set(slot.at(0), value))
                return QStringLiteral("catalog: palette.%1 could not be added").arg(slot);
        }
    }
    return QString();
}

bool safeCatalogName(const QString &name)
{
    return !name.isEmpty() && name.size() <= 128 && text::isSafe(name, false);
}

QString validateCatalogKeys(const QJsonValue &value, const QString &path)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (!safeCatalogName(it.key()))
                return QStringLiteral("%1.%2 has an unsafe Unicode key")
                    .arg(path, it.key());
            const QString error = validateCatalogKeys(
                it.value(), path + QLatin1Char('.') + it.key());
            if (!error.isEmpty())
                return error;
        }
    } else if (value.isArray()) {
        const QJsonArray values = value.toArray();
        for (int index = 0; index < values.size(); ++index) {
            const QString error = validateCatalogKeys(
                values.at(index), QStringLiteral("%1[%2]").arg(path).arg(index));
            if (!error.isEmpty())
                return error;
        }
    }
    return QString();
}

} // namespace

QStringList Bridge::flatSequences()
{
    return {QStringLiteral("spawn"), QStringLiteral("boom")};
}

QStringList Bridge::perVariantSequences()
{
    return {QStringLiteral("launch"), QStringLiteral("land"),
            QStringLiteral("bye")};
}

Bridge::Result Bridge::importSpecies(const QJsonObject &catalog,
                                     const QString &species,
                                     const QString &variant)
{
    Result result;
    result.error = validateCatalogKeys(catalog, QStringLiteral("catalog"));
    if (!result.error.isEmpty())
        return result;
    if (!safeCatalogName(species) || !safeCatalogName(variant)) {
        result.error = QStringLiteral("catalog: species or variant has an unsafe name");
        return result;
    }
    Palette importedPalette;
    result.error = validateCatalogPalette(catalog.value(QStringLiteral("palette")),
                                          &importedPalette);
    if (!result.error.isEmpty())
        return result;
    const QJsonObject allSpecies =
        catalog.value(QStringLiteral("species")).toObject();
    if (allSpecies.size() > Document::maxClips)
        result.error = QStringLiteral("catalog: too many species");
    if (!result.error.isEmpty())
        return result;
    if (!allSpecies.contains(species)) {
        result.error = QStringLiteral("the catalog has no species %1 (it has %2)")
                           .arg(species, allSpecies.keys().join(QStringLiteral(", ")));
        return result;
    }

    const QJsonObject bank = allSpecies.value(species).toObject();
    const QJsonObject fat = bank.value(QStringLiteral("fat")).toObject();
    if (!fat.contains(variant)) {
        result.error = QStringLiteral("%1: no variant %2 (it has %3)")
                           .arg(species, variant,
                                fat.keys().join(QStringLiteral(", ")));
        return result;
    }

    // The frame size comes from the art rather than from anything declared:
    // the catalog does not say how big a sprite is, the sprite does.
    const QJsonObject states = fat.value(variant).toObject();
    if (states.size() > Document::maxClips) {
        result.error = QStringLiteral("%1/%2 has too many state clips")
                           .arg(species, variant);
        return result;
    }
    QList<QPair<QString, QList<Grid>>> clips;
    int columns = -1;
    int rows = -1;
    qint64 totalCells = 0;
    for (const QString &state : states.keys()) {
        if (!safeCatalogName(state)) {
            result.error = QStringLiteral("catalog state has an invalid name");
            return result;
        }
        const QString frameError = validateFrames(
            states.value(state), QStringLiteral("species.%1.fat.%2.%3")
                .arg(species, variant, state), &columns, &rows, &totalCells);
        if (!frameError.isEmpty()) {
            result.error = frameError;
            return result;
        }
        clips.append({state, gridsOf(states.value(state).toArray())});
    }
    for (const QString &name : flatSequences()) {
        const QJsonArray frames = bank.value(name).toArray();
        if (!frames.isEmpty()) {
            const QString frameError = validateFrames(
                frames, QStringLiteral("species.%1.%2").arg(species, name),
                &columns, &rows, &totalCells);
            if (!frameError.isEmpty()) {
                result.error = frameError;
                return result;
            }
            clips.append({name, gridsOf(frames)});
        }
    }
    for (const QString &name : perVariantSequences()) {
        const QJsonArray frames =
            bank.value(name).toObject().value(variant).toArray();
        if (!frames.isEmpty()) {
            const QString frameError = validateFrames(
                frames, QStringLiteral("species.%1.%2.%3")
                    .arg(species, name, variant), &columns, &rows, &totalCells);
            if (!frameError.isEmpty()) {
                result.error = frameError;
                return result;
            }
            clips.append({name, gridsOf(frames)});
        }
    }

    if (clips.isEmpty() || clips.first().second.isEmpty()) {
        result.error = QStringLiteral("%1/%2 has no frames").arg(species, variant);
        return result;
    }
    if (clips.size() > Document::maxClips) {
        result.error = QStringLiteral("catalog: imported clip count exceeds %1")
                           .arg(Document::maxClips);
        return result;
    }
    qint64 totalFrames = 0;
    for (const auto &clip : clips)
        totalFrames += clip.second.size();
    if (totalFrames > Document::maxTotalFrames
        || totalFrames > Document::maxTotalCels) {
        result.error = QStringLiteral("catalog: imported frame/cel count exceeds a hard limit");
        return result;
    }

    const Grid &first = clips.first().second.first();
    Document doc = Document::empty(first.columns(), first.rows());
    doc.palette() = importedPalette;
    if (importedPalette.isEmpty())
        doc.palette() = Palette::standard();

    for (const auto &clip : clips) {
        if (!doc.addClip(clip.first)) {
            result.error = QStringLiteral("catalog: could not add clip %1").arg(clip.first);
            return result;
        }
        for (int frame = 1; frame < clip.second.size(); ++frame)
            if (!doc.addFrame(clip.first, frame - 1, false)) {
                result.error = QStringLiteral("catalog: could not add frame to %1")
                                   .arg(clip.first);
                return result;
            }
        if (doc.layers().isEmpty())
            doc.addLayer(QStringLiteral("layer"), QStringLiteral("Layer"));
        for (int frame = 0; frame < clip.second.size(); ++frame)
            if (!doc.setFrame(clip.first, frame, clip.second.at(frame))) {
                result.error = QStringLiteral("catalog: could not store frame in %1")
                                   .arg(clip.first);
                return result;
            }
    }

    result.document = doc;
    result.ok = true;
    return result;
}

Bridge::Result Bridge::exportInto(QJsonObject catalog, const Document &document,
                                   const QString &species, const QString &variant)
{
    Result result;

    result.error = validateCatalogKeys(catalog, QStringLiteral("catalog"));
    if (!result.error.isEmpty())
        return result;

    result.error = validateExport(catalog, species, variant);
    if (!result.error.isEmpty())
        return result;
    const QStringList problems = document.problems();
    if (!problems.isEmpty()) {
        result.error = QStringLiteral("document is invalid: %1").arg(problems.first());
        return result;
    }

    QJsonObject allSpecies = catalog.value(QStringLiteral("species")).toObject();
    QJsonObject bank = allSpecies.value(species).toObject();
    QJsonObject fat = bank.value(QStringLiteral("fat")).toObject();
    QJsonObject states = fat.value(variant).toObject();
    const QJsonObject declared = catalog.value(QStringLiteral("states")).toObject();

    for (const Clip &clip : document.clips()) {
        if (clip.frameCount <= 0)
            continue;
        QJsonArray frames;
        for (int frame = 0; frame < clip.frameCount; ++frame) {
            QStringList diagnostics;
            const Grid composed = render::toGrid(document, clip.id, frame,
                                                 render::Options(), &diagnostics);
            if (!diagnostics.isEmpty()) {
                result.error = diagnostics.join(QStringLiteral("; "));
                return result;
            }
            frames.append(framesOf({composed}).at(0));
        }
        if (flatSequences().contains(clip.name) && bank.contains(clip.name)) {
            bank.insert(clip.name, frames);
            result.exported += 1;
        } else if (perVariantSequences().contains(clip.name)
                   && bank.contains(clip.name)) {
            QJsonObject byVariant = bank.value(clip.name).toObject();
            byVariant.insert(variant, frames);
            bank.insert(clip.name, byVariant);
            result.exported += 1;
        } else if (states.contains(clip.name) || declared.contains(clip.name)) {
            states.insert(clip.name, frames);
            result.exported += 1;
        } else {
            result.skipped.append(clip.name);
        }
    }

    if (result.exported == 0) {
        result.error = QStringLiteral("the document has no known non-empty sequence to export");
        return result;
    }

    fat.insert(variant, states);
    bank.insert(QStringLiteral("fat"), fat);
    allSpecies.insert(species, bank);
    catalog.insert(QStringLiteral("species"), allSpecies);

    result.catalog = catalog;
    const QByteArray serialized = QJsonDocument(result.catalog).toJson(QJsonDocument::Compact);
    if (serialized.size() > Document::maxDocumentBytes) {
        result.error = QStringLiteral("catalog: serialized output exceeds the hard limit of %1 MiB")
                           .arg(Document::maxDocumentBytes / (1024 * 1024));
        result.catalog = QJsonObject();
        return result;
    }
    const QString finalError = validateExport(result.catalog, species, variant);
    if (!finalError.isEmpty()) {
        result.error = finalError;
        result.catalog = QJsonObject();
        return result;
    }
    const Result importerCheck = importSpecies(result.catalog, species, variant);
    if (!importerCheck.ok) {
        result.error = QStringLiteral("catalog: exported data is not importable: %1")
                           .arg(importerCheck.error);
        result.catalog = QJsonObject();
        return result;
    }
    result.ok = true;
    return result;
}

QString Bridge::validateExport(const QJsonObject &catalog, const QString &species,
                               const QString &variant)
{
    if (!safeCatalogName(species))
        return QStringLiteral("export: species name cannot be empty");
    if (!safeCatalogName(variant))
        return QStringLiteral("export: variant cannot be empty");

    const QString paletteError = validateCatalogPalette(
        catalog.value(QStringLiteral("palette")));
    if (!paletteError.isEmpty())
        return paletteError;

    const QJsonValue declarationsValue = catalog.value(QStringLiteral("states"));
    if (!declarationsValue.isObject())
        return QStringLiteral("catalog: states must be an object");
    const QJsonObject declarations = declarationsValue.toObject();
    if (declarations.size() > Document::maxClips)
        return QStringLiteral("catalog: states has too many entries");
    for (auto it = declarations.constBegin(); it != declarations.constEnd(); ++it) {
        if (!it.value().isDouble())
            return QStringLiteral("catalog: states.%1 must be a number").arg(it.key());
    }

    const QJsonValue speciesValue = catalog.value(QStringLiteral("species"));
    if (!speciesValue.isObject())
        return QStringLiteral("catalog: species must be an object");
    const QJsonObject allSpecies = speciesValue.toObject();
    if (!allSpecies.contains(species))
        return QStringLiteral("the catalog has no species %1 (it has %2)")
            .arg(species, allSpecies.keys().join(QStringLiteral(", ")));
    if (!allSpecies.value(species).isObject())
        return QStringLiteral("species.%1 must be an object").arg(species);

    const QJsonObject bank = allSpecies.value(species).toObject();
    if (!bank.value(QStringLiteral("fat")).isObject())
        return QStringLiteral("species.%1.fat must be an object").arg(species);
    const QJsonObject fat = bank.value(QStringLiteral("fat")).toObject();
    if (!fat.contains(variant))
        return QStringLiteral("%1: no variant %2 (it has %3)")
            .arg(species, variant, fat.keys().join(QStringLiteral(", ")));
    if (!fat.value(variant).isObject())
        return QStringLiteral("species.%1.fat.%2 must be an object")
            .arg(species, variant);

    const QJsonObject states = fat.value(variant).toObject();
    if (states.size() > Document::maxClips)
        return QStringLiteral("catalog: variant has too many states");
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        const QString error = validateFrames(
            it.value(), QStringLiteral("species.%1.fat.%2.%3")
                .arg(species, variant, it.key()));
        if (!error.isEmpty())
            return error;
    }

    for (const QString &name : flatSequences()) {
        if (!bank.contains(name))
            continue;
        const QString error = validateFrames(
            bank.value(name), QStringLiteral("species.%1.%2").arg(species, name));
        if (!error.isEmpty())
            return error;
    }
    for (const QString &name : perVariantSequences()) {
        if (!bank.contains(name))
            continue;
        if (!bank.value(name).isObject())
            return QStringLiteral("species.%1.%2 must be an object")
                .arg(species, name);
        const QJsonObject byVariant = bank.value(name).toObject();
        if (!byVariant.contains(variant))
            continue;
        const QString error = validateFrames(
            byVariant.value(variant),
            QStringLiteral("species.%1.%2.%3").arg(species, name, variant));
        if (!error.isEmpty())
            return error;
    }
    return QString();
}

} // namespace omapixel
