#include "Bridge.h"

#include <QJsonArray>
#include <QColor>

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

QString validateFrames(const QJsonValue &value, const QString &path)
{
    if (!value.isArray())
        return QStringLiteral("%1 must be an array of frames").arg(path);
    const QJsonArray frames = value.toArray();
    for (int frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        const QJsonValue frame = frames.at(frameIndex);
        if (!frame.isArray())
            return QStringLiteral("%1[%2] must be an array of rows")
                .arg(path)
                .arg(frameIndex);
        const QJsonArray rows = frame.toArray();
        for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            if (!rows.at(rowIndex).isString())
                return QStringLiteral("%1[%2][%3] must be a string")
                    .arg(path)
                    .arg(frameIndex)
                    .arg(rowIndex);
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
    const QJsonObject allSpecies =
        catalog.value(QStringLiteral("species")).toObject();
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
    QList<QPair<QString, QList<Grid>>> clips;
    for (const QString &state : states.keys())
        clips.append({state, gridsOf(states.value(state).toArray())});
    for (const QString &name : flatSequences()) {
        const QJsonArray frames = bank.value(name).toArray();
        if (!frames.isEmpty())
            clips.append({name, gridsOf(frames)});
    }
    for (const QString &name : perVariantSequences()) {
        const QJsonArray frames =
            bank.value(name).toObject().value(variant).toArray();
        if (!frames.isEmpty())
            clips.append({name, gridsOf(frames)});
    }

    if (clips.isEmpty() || clips.first().second.isEmpty()) {
        result.error = QStringLiteral("%1/%2 has no frames").arg(species, variant);
        return result;
    }

    const Grid &first = clips.first().second.first();
    Document doc = Document::empty(first.columns(), first.rows());
    doc.palette() = Palette();
    const QJsonObject palette = catalog.value(QStringLiteral("palette")).toObject();
    for (const QString &letter : palette.keys()) {
        if (letter.size() == 1)
            doc.palette().set(letter.at(0), QColor(palette.value(letter).toString()));
    }
    if (doc.palette().isEmpty())
        doc.palette() = Palette::standard();

    for (const auto &clip : clips) {
        if (!doc.addClip(clip.first))
            continue;
        Clip *target = doc.clip(clip.first);
        target->frames = clip.second;
    }

    result.document = doc;
    result.ok = true;
    return result;
}

Bridge::Result Bridge::exportInto(QJsonObject catalog, const Document &document,
                                   const QString &species, const QString &variant)
{
    Result result;

    result.error = validateExport(catalog, species, variant);
    if (!result.error.isEmpty())
        return result;

    QJsonObject allSpecies = catalog.value(QStringLiteral("species")).toObject();
    QJsonObject bank = allSpecies.value(species).toObject();
    QJsonObject fat = bank.value(QStringLiteral("fat")).toObject();
    QJsonObject states = fat.value(variant).toObject();
    const QJsonObject declared = catalog.value(QStringLiteral("states")).toObject();

    for (const Clip &clip : document.clips()) {
        if (clip.frames.isEmpty())
            continue;
        const QJsonArray frames = framesOf(clip.frames);
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
    result.ok = true;
    return result;
}

QString Bridge::validateExport(const QJsonObject &catalog, const QString &species,
                               const QString &variant)
{
    if (species.isEmpty())
        return QStringLiteral("export: species name cannot be empty");
    if (variant.isEmpty())
        return QStringLiteral("export: variant cannot be empty");

    const QJsonValue paletteValue = catalog.value(QStringLiteral("palette"));
    if (!paletteValue.isObject())
        return QStringLiteral("catalog: palette must be an object");
    const QJsonObject palette = paletteValue.toObject();
    for (auto it = palette.constBegin(); it != palette.constEnd(); ++it) {
        if (!it.value().isString())
            return QStringLiteral("catalog: palette.%1 must be a colour string")
                .arg(it.key());
        if (!QColor(it.value().toString()).isValid())
            return QStringLiteral("catalog: palette.%1 is not a valid colour")
                .arg(it.key());
    }

    const QJsonValue declarationsValue = catalog.value(QStringLiteral("states"));
    if (!declarationsValue.isObject())
        return QStringLiteral("catalog: states must be an object");
    const QJsonObject declarations = declarationsValue.toObject();
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
