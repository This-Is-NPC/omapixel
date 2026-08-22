#include "Bridge.h"

#include <QJsonArray>

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

    QJsonObject allSpecies = catalog.value(QStringLiteral("species")).toObject();
    QJsonObject bank = allSpecies.value(species).toObject();
    QJsonObject fat = bank.value(QStringLiteral("fat")).toObject();
    QJsonObject states = fat.value(variant).toObject();
    const QJsonObject declared = catalog.value(QStringLiteral("states")).toObject();

    for (const Clip &clip : document.clips()) {
        if (clip.frames.isEmpty())
            continue;
        const QJsonArray frames = framesOf(clip.frames);
        if (flatSequences().contains(clip.name)) {
            bank.insert(clip.name, frames);
        } else if (perVariantSequences().contains(clip.name)) {
            QJsonObject byVariant = bank.value(clip.name).toObject();
            byVariant.insert(variant, frames);
            bank.insert(clip.name, byVariant);
        } else if (states.contains(clip.name) || declared.contains(clip.name)) {
            states.insert(clip.name, frames);
        } else {
            result.skipped.append(clip.name);
        }
    }

    fat.insert(variant, states);
    bank.insert(QStringLiteral("fat"), fat);
    allSpecies.insert(species, bank);
    catalog.insert(QStringLiteral("species"), allSpecies);

    result.catalog = catalog;
    result.ok = true;
    return result;
}

} // namespace omapixel
