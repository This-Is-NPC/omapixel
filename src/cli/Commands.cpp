#include "Commands.h"

#include "LayerOperations.h"
#include "Ops.h"
#include "Render.h"

#include <QCommandLineParser>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPoint>
#include <QRegularExpression>

namespace omapixel {
namespace cli {
namespace {

// --------------------------------------------------------------- parsing bits

bool parseSize(const QString &text, int *columns, int *rows, QString *error)
{
    const QStringList parts = text.toLower().split(QLatin1Char('x'));
    bool okColumns = false;
    bool okRows = false;
    if (parts.size() == 2) {
        *columns = parts.at(0).toInt(&okColumns);
        *rows = parts.at(1).toInt(&okRows);
    }
    if (!okColumns || !okRows || *columns <= 0 || *rows <= 0) {
        *error = QStringLiteral("--size: %1 is not COLUMNSxROWS").arg(text);
        return false;
    }
    return true;
}

bool parsePoint(const QString &text, QPoint *point, QString *error)
{
    const QStringList parts = text.split(QLatin1Char(','));
    bool okX = false;
    bool okY = false;
    if (parts.size() == 2) {
        point->setX(parts.at(0).trimmed().toInt(&okX));
        point->setY(parts.at(1).trimmed().toInt(&okY));
    }
    if (!okX || !okY) {
        *error = QStringLiteral("%1 is not X,Y").arg(text);
        return false;
    }
    return true;
}

bool parseSlot(const QString &text, QChar *slot, QString *error)
{
    if (text.size() != 1 || !Palette::validSlot(text.at(0))) {
        *error = QStringLiteral("--slot: %1 is not a single character").arg(text);
        return false;
    }
    *slot = text.at(0);
    return true;
}

bool parseBoundedInteger(const QString &text, const QString &option,
                         int minimum, int maximum, int *result, QString *error)
{
    if (!QRegularExpression(QStringLiteral("^[+-]?[0-9]+$")).match(text).hasMatch()) {
        *error = QStringLiteral("%1 must be an integer").arg(option);
        return false;
    }
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok || value < minimum || value > maximum) {
        *error = QStringLiteral("%1 must be an integer from %2 to %3")
                     .arg(option).arg(minimum).arg(maximum);
        return false;
    }
    *result = value;
    return true;
}

/// The clip and frame a command works on. Defaults to the first clip and frame
/// 0, so the common case -- a document with one clip -- needs no flags at all.
QString value(const QCommandLineParser &parser, const char *name);
bool isSet(const QCommandLineParser &parser, const char *name);

struct Target {
    QString clip;
    int frame = 0;
    QString layer;
    bool allFrames = false;
};

bool resolveLayerTarget(const Document &doc, const QString &layerIdOption,
                       const QString &layerNameOption, bool requireExplicit,
                       QString *layer, QString *error)
{
    if (doc.layers().isEmpty()) {
        *error = QStringLiteral("E_LAYER_NOT_FOUND: document has no layers");
        return false;
    }
    const Layer *byId = layerIdOption.isEmpty() ? nullptr : doc.layerById(layerIdOption);
    const Layer *byName = layerNameOption.isEmpty() ? nullptr : doc.layerByName(layerNameOption);
    if (!layerIdOption.isEmpty() && !byId) {
        *error = QStringLiteral("E_LAYER_NOT_FOUND: --layer-id=%1").arg(layerIdOption);
        return false;
    }
    if (!layerNameOption.isEmpty() && !byName) {
        *error = QStringLiteral("E_LAYER_NOT_FOUND: --layer=%1").arg(layerNameOption);
        return false;
    }
    if (byId && byName && byId != byName) {
        *error = QStringLiteral("E_LAYER_TARGET_CONFLICT: --layer-id=%1 names `%2`, "
                                "but --layer=%3 names `%4`")
                     .arg(layerIdOption, byId->name, layerNameOption, byName->name);
        return false;
    }
    const Layer *found = byId ? byId : byName;
    if (!found) {
        if (requireExplicit && doc.layers().size() > 1) {
            *error = QStringLiteral(
                "E_LAYER_TARGET_REQUIRED: multilayer mutation requires --layer-id=ID "
                "or --layer=EXACT_NAME");
            return false;
        }
        found = &doc.layers().first();
    }
    *layer = found->id;
    return true;
}

bool parseScope(const QCommandLineParser &parser, bool *allFrames, QString *error)
{
    const bool legacyAll = isSet(parser, "all-frames");
    const QString scope = value(parser, "scope");
    if (scope.isEmpty()) {
        *allFrames = legacyAll;
        return true;
    }
    if (scope != QLatin1String("frame") && scope != QLatin1String("all-frames")) {
        *error = QStringLiteral("--scope: %1 is not frame or all-frames").arg(scope);
        return false;
    }
    if (legacyAll && scope == QLatin1String("frame")) {
        *error = QStringLiteral("E_SCOPE_CONFLICT: --scope=frame conflicts with --all-frames");
        return false;
    }
    *allFrames = scope == QLatin1String("all-frames");
    return true;
}

bool resolveTarget(const Document &doc, const QString &clipOption,
                   const QString &frameOption, Target *target, QString *error,
                   const QString &layerIdOption = QString(),
                   const QString &layerNameOption = QString(),
                   bool requireLayer = false, bool allFrames = false,
                   bool requireFrame = false)
{
    if (doc.clips().isEmpty()) {
        *error = QStringLiteral("the document has no clips");
        return false;
    }
    target->clip = clipOption.isEmpty() ? doc.clips().first().name : clipOption;
    const Clip *clip = doc.clip(target->clip);
    if (!clip) {
        *error = QStringLiteral("no clip %1 (there is %2)")
                     .arg(target->clip, doc.clipNames().join(QStringLiteral(", ")));
        return false;
    }
    bool okFrame = true;
    target->frame = frameOption.isEmpty() ? 0 : frameOption.toInt(&okFrame);
    if (!okFrame || target->frame < 0 || target->frame >= clip->frameCount) {
        *error = QStringLiteral("E_FRAME_OUT_OF_RANGE: --frame=%1 (valid range 0..%2)")
                     .arg(frameOption).arg(clip->frameCount - 1);
        return false;
    }
    if (!resolveLayerTarget(doc, layerIdOption, layerNameOption, requireLayer,
                            &target->layer, error))
        return false;
    target->allFrames = allFrames;
    const Layer *layer = doc.layerById(target->layer);
    if (requireFrame && !allFrames && frameOption.isEmpty() && layer
        && layer->storage == QStringLiteral("animated")) {
        *error = QStringLiteral(
            "E_FRAME_SCOPE_REQUIRED: animated layer edits require --frame=N or --all-frames");
        return false;
    }
    return true;
}

QString value(const QCommandLineParser &parser, const char *name)
{
    return parser.value(QLatin1String(name));
}

bool isSet(const QCommandLineParser &parser, const char *name)
{
    return parser.isSet(QLatin1String(name));
}

bool renderOptions(const Document &doc, const QCommandLineParser &parser,
                   render::Options *options, QString *error)
{
    options->layer.clear();
    options->checker = isSet(parser, "checker");
    options->isolated = isSet(parser, "isolated");
    if (!options->isolated && (isSet(parser, "layer-id") || isSet(parser, "layer"))) {
        *error = QStringLiteral("--layer-id/--layer require --isolated");
        return false;
    }
    if (options->isolated
        && !resolveLayerTarget(doc, value(parser, "layer-id"), value(parser, "layer"),
                               true, &options->layer, error)) {
        return false;
    }
    if (options->isolated && options->layer.isEmpty()) {
        *error = QStringLiteral("--isolated requires --layer-id=ID or --layer=EXACT_NAME");
        return false;
    }
    return true;
}

bool hiddenAllowed(const Layer &layer, const QCommandLineParser &parser, QString *error)
{
    if (layer.visible || isSet(parser, "include-hidden"))
        return true;
    *error = QStringLiteral("E_LAYER_HIDDEN: --layer-id=%1 is hidden; pass --include-hidden")
                 .arg(layer.id);
    return false;
}

bool parseBool(const QString &text, bool *result)
{
    const QString lower = text.toLower();
    if (lower == QLatin1String("true") || lower == QLatin1String("yes")
        || lower == QLatin1String("on") || lower == QLatin1String("1")) {
        *result = true;
        return true;
    }
    if (lower == QLatin1String("false") || lower == QLatin1String("no")
        || lower == QLatin1String("off") || lower == QLatin1String("0")) {
        *result = false;
        return true;
    }
    return false;
}

QString layerReport(const LayerOperationReport &report)
{
    return QStringLiteral("frames=%1 affected-pixels=%2 exact-pixels=%3 "
                          "approximated-pixels=%4 new-slots=%5 removed-layers=%6\n")
        .arg(report.frames)
        .arg(report.affectedPixels)
        .arg(report.exactMatches)
        .arg(report.approximatedPixels)
        .arg(report.newSlots)
        .arg(report.removedLayers);
}

// ------------------------------------------------------------------- commands

Outcome doInfo(const Document &doc)
{
    QJsonObject size;
    size.insert(QStringLiteral("w"), doc.columns());
    size.insert(QStringLiteral("h"), doc.rows());

    QJsonArray palette;
    for (const Palette::Slot &slot : doc.palette().entries()) {
        QJsonObject entry;
        entry.insert(QStringLiteral("slot"), QString(slot.letter));
        entry.insert(QStringLiteral("colour"),
                     slot.colour.name(QColor::HexRgb).toUpper());
        palette.append(entry);
    }

    QJsonArray clips;
    for (const Clip &clip : doc.clips()) {
        qint64 drawn = 0;
        for (const Layer &layer : doc.layers())
            for (const Cel &cel : layer.cels)
                if (layer.storage == QStringLiteral("shared")
                    || cel.clip == clip.id)
                    drawn += cel.grid.drawnCount();
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), clip.name);
        entry.insert(QStringLiteral("fps"), clip.fps);
        entry.insert(QStringLiteral("frames"), clip.frameCount);
        entry.insert(QStringLiteral("drawn"), drawn);
        clips.append(entry);
    }

    QJsonArray layers;
    for (const Layer &layer : doc.layers()) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), layer.id);
        entry.insert(QStringLiteral("name"), layer.name);
        entry.insert(QStringLiteral("visible"), layer.visible);
        entry.insert(QStringLiteral("locked"), layer.locked);
        entry.insert(QStringLiteral("storage"), layer.storage);
        layers.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("size"), size);
    root.insert(QStringLiteral("palette"), palette);
    root.insert(QStringLiteral("clips"), clips);
    root.insert(QStringLiteral("layers"), layers);
    return Outcome::ok(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

Outcome doCheck(const Document &doc)
{
    const QStringList problems = doc.problems();
    QString text;
    for (const QString &line : problems)
        text += line + QLatin1Char('\n');
    text += problems.isEmpty() ? QStringLiteral("nothing to report\n")
                               : QStringLiteral("%1 problem(s)\n").arg(problems.size());
    Outcome outcome = Outcome::ok(text);
    // Exit 1 on a finding, so `check` drops into a shell `if` or a CI step.
    outcome.code = problems.isEmpty() ? 0 : 1;
    return outcome;
}

Outcome doResize(Document &doc, const QCommandLineParser &parser)
{
    int columns = 0;
    int rows = 0;
    QString error;
    if (!isSet(parser, "size"))
        return Outcome::wrong(QStringLiteral("resize: say --size COLUMNSxROWS"));
    if (!parseSize(value(parser, "size"), &columns, &rows, &error))
        return Outcome::wrong(error);

    const qint64 lost = doc.wouldLose(columns, rows);
    if (lost > 0 && !isSet(parser, "anyway")) {
        return Outcome::refused(
            QStringLiteral("this would crop %1 drawn pixel(s). Pass --anyway if "
                           "that is what you want.")
                .arg(lost));
    }
    const int wasColumns = doc.columns();
    const int wasRows = doc.rows();
    QString resizeError;
    if (!doc.resize(columns, rows, &resizeError))
        return Outcome::refused(resizeError);

    QString note = QStringLiteral("%1x%2 → %3x%4")
                       .arg(wasColumns)
                       .arg(wasRows)
                       .arg(columns)
                       .arg(rows);
    if (lost > 0)
        note += QStringLiteral(", %1 pixel(s) cropped").arg(lost);
    return Outcome::edited(note + QLatin1Char('\n'));
}

Outcome doTrim(Document &doc, const QCommandLineParser &parser)
{
    Target target;
    QString error;
    if (!resolveTarget(doc, value(parser, "clip"), value(parser, "frame"), &target,
                       &error))
        return Outcome::wrong(error);

    const QRect bounds = doc.drawnBounds(target.clip, target.frame);
    if (!bounds.isValid()) {
        return Outcome::refused(
            QStringLiteral("trim: %1 frame %2 is empty").arg(target.clip).arg(target.frame));
    }

    const QRect whole(0, 0, doc.columns(), doc.rows());
    if (bounds == whole)
        return Outcome::ok(QStringLiteral("already tight\n"));

    const qint64 lost = doc.wouldLoseOutside(bounds);
    if (lost > 0 && !isSet(parser, "anyway")) {
        return Outcome::refused(
            QStringLiteral("this would crop %1 drawn pixel(s) outside %2 frame %3's "
                           "content bounds. Pass --anyway if that is what you want.")
                .arg(lost)
                .arg(target.clip)
                .arg(target.frame));
    }

    const int wasColumns = doc.columns();
    const int wasRows = doc.rows();
    QString cropError;
    if (!doc.crop(bounds, &cropError))
        return Outcome::refused(QStringLiteral("trim: content bounds are outside the canvas"));

    QString note = QStringLiteral("%1x%2 → %3x%4")
                       .arg(wasColumns)
                       .arg(wasRows)
                       .arg(bounds.width())
                       .arg(bounds.height());
    if (lost > 0)
        note += QStringLiteral(", %1 pixel(s) cropped").arg(lost);
    return Outcome::edited(note + QLatin1Char('\n'));
}

Outcome doClip(Document &doc, QStringList &words, const QCommandLineParser &parser)
{
    if (words.isEmpty())
        return Outcome::wrong(QStringLiteral("clip: add, rm, rename or fps"));
    const QString action = words.takeFirst();
    const QString name = isSet(parser, "name")
                             ? value(parser, "name")
                             : (words.isEmpty() ? QString() : words.takeFirst());
    QString error;

    if (action == QLatin1String("add")) {
        int fps = 8;
        if (isSet(parser, "fps")
            && !parseBoundedInteger(value(parser, "fps"), QStringLiteral("--fps"),
                                    1, 60, &fps, &error))
            return Outcome::wrong(error);
        if (!doc.addClip(name, fps, &error)) {
            return Outcome::refused(
                error.isEmpty()
                    ? QStringLiteral("clip add: %1 is empty or already there").arg(name)
                    : error);
        }
    } else if (action == QLatin1String("rm")) {
        if (!doc.removeClip(name, &error)) {
            // Two different refusals, and saying "no clip walk" for both sends
            // somebody hunting for a typo that is not there.
            return Outcome::refused(
                error.isEmpty()
                    ? (doc.clip(name)
                           ? QStringLiteral("clip rm: a document keeps its last clip")
                           : QStringLiteral("clip rm: no clip %1").arg(name))
                    : error);
        }
    } else if (action == QLatin1String("rename")) {
        if (words.isEmpty())
            return Outcome::wrong(QStringLiteral("clip rename: say the new name"));
        if (!doc.renameClip(name, words.first())) {
            return Outcome::refused(QStringLiteral("clip rename: %1 -> %2 refused")
                                        .arg(name, words.first()));
        }
    } else if (action == QLatin1String("fps")) {
        if (!isSet(parser, "fps"))
            return Outcome::wrong(QStringLiteral("clip fps: say --fps N"));
        int fps = 0;
        if (!parseBoundedInteger(value(parser, "fps"), QStringLiteral("--fps"),
                                 1, 60, &fps, &error))
            return Outcome::wrong(error);
        if (!doc.setFps(name, fps))
            return Outcome::refused(QStringLiteral("clip fps: no clip %1").arg(name));
    } else {
        return Outcome::wrong(
            QStringLiteral("clip: add, rm, rename or fps — not %1").arg(action));
    }
    return Outcome::edited();
}

Outcome doFrame(Document &doc, QStringList &words, const QCommandLineParser &parser)
{
    if (words.isEmpty())
        return Outcome::wrong(QStringLiteral("frame: add, dup, rm or move"));
    const QString action = words.takeFirst();
    Target target;
    QString error;
    if (!resolveTarget(doc, value(parser, "clip"), value(parser, "frame"), &target,
                       &error))
        return Outcome::wrong(error);

    if (action == QLatin1String("add") || action == QLatin1String("dup")) {
        if (!doc.addFrame(target.clip, target.frame,
                          action == QLatin1String("dup"), &error))
            return Outcome::refused(error);
    } else if (action == QLatin1String("rm")) {
        if (!doc.removeFrame(target.clip, target.frame, &error))
            return Outcome::refused(error.isEmpty()
                                         ? QStringLiteral("frame rm: a clip keeps its last frame")
                                         : error);
    } else if (action == QLatin1String("move")) {
        if (!isSet(parser, "index"))
            return Outcome::wrong(QStringLiteral("frame move: say --index N"));
        const Clip *clip = doc.clip(target.clip);
        int index = 0;
        if (!clip || !parseBoundedInteger(value(parser, "index"), QStringLiteral("--index"),
                                          0, clip->frameCount - 1, &index, &error))
            return Outcome::wrong(error);
        if (!doc.moveFrame(target.clip, target.frame, index, &error))
            return Outcome::refused(error.isEmpty()
                                         ? QStringLiteral("frame move: out of range")
                                         : error);
    } else {
        return Outcome::wrong(
            QStringLiteral("frame: add, dup, rm or move — not %1").arg(action));
    }
    return Outcome::edited();
}

Outcome doPalette(Document &doc, QStringList &words, const QCommandLineParser &parser)
{
    if (words.isEmpty())
        return Outcome::wrong(QStringLiteral("palette: list, set or rm"));
    const QString action = words.takeFirst();

    if (action == QLatin1String("list")) {
        QString text;
        for (const Palette::Slot &slot : doc.palette().entries()) {
            text += QStringLiteral("%1  %2\n")
                        .arg(slot.letter)
                        .arg(slot.colour.name(QColor::HexRgb).toUpper());
        }
        return Outcome::ok(text);
    }

    QChar slot;
    QString error;
    if (!isSet(parser, "slot") || !parseSlot(value(parser, "slot"), &slot, &error)) {
        return Outcome::wrong(error.isEmpty()
                                  ? QStringLiteral("palette: say --slot LETTER")
                                  : error);
    }
    if (action == QLatin1String("set")) {
        const QColor colour(value(parser, "colour"));
        if (!colour.isValid())
            return Outcome::wrong(QStringLiteral("palette set: --colour has to be #RRGGBB"));
        if (!doc.setPaletteColour(slot, colour, &error))
            return Outcome::refused(error);
    } else if (action == QLatin1String("rm")) {
        QString paletteError;
        if (!doc.removePaletteSlot(slot, &paletteError)) {
            if (!paletteError.isEmpty())
                return Outcome::refused(paletteError);
            return Outcome::refused(QStringLiteral("palette rm: no slot %1").arg(slot));
        }
    } else {
        return Outcome::wrong(
            QStringLiteral("palette: list, set or rm — not %1").arg(action));
    }
    return Outcome::edited();
}

Outcome doLayer(Document &doc, QStringList &words, const QCommandLineParser &parser)
{
    const QString action = words.isEmpty() ? QStringLiteral("list") : words.takeFirst();
    if (action == QLatin1String("list")) {
        QJsonArray layers;
        for (int index = 0; index < doc.layers().size(); ++index) {
            const Layer &layer = doc.layers().at(index);
            QJsonObject entry;
            entry.insert(QStringLiteral("index"), index);
            entry.insert(QStringLiteral("id"), layer.id);
            entry.insert(QStringLiteral("name"), layer.name);
            entry.insert(QStringLiteral("visible"), layer.visible);
            entry.insert(QStringLiteral("locked"), layer.locked);
            entry.insert(QStringLiteral("opacity"), layer.opacity);
            entry.insert(QStringLiteral("mode"), layer.mode);
            entry.insert(QStringLiteral("storage"), layer.storage);
            layers.append(entry);
        }
        return Outcome::ok(QJsonDocument(layers).toJson(QJsonDocument::Indented));
    }

    if (action == QLatin1String("add")) {
        QString id = value(parser, "id");
        QString name = value(parser, "name");
        if (id.isEmpty() && !words.isEmpty())
            id = words.takeFirst();
        if (name.isEmpty() && !words.isEmpty())
            name = words.takeFirst();
        if (id.isEmpty() || name.isEmpty())
            return Outcome::wrong(QStringLiteral("layer add: say --id ID --name NAME"));
        const QString storage = value(parser, "storage").isEmpty()
                                     ? QStringLiteral("animated")
                                     : value(parser, "storage");
        if (!doc.addLayer(id, name, storage))
            return Outcome::refused(QStringLiteral("E_LAYER_IDENTITY: invalid or duplicate layer identity"));
        return Outcome::edited(QStringLiteral("added %1 (%2)\n").arg(id, name));
    }

    QString target;
    QString error;
    if (!resolveLayerTarget(doc, value(parser, "layer-id"), value(parser, "layer"),
                            true, &target, &error))
        return Outcome::wrong(error);
    Layer *found = doc.layerById(target);
    if (!found)
        return Outcome::wrong(QStringLiteral("E_LAYER_NOT_FOUND: %1").arg(target));

    if (action == QLatin1String("rm")) {
        if (!hiddenAllowed(*found, parser, &error))
            return Outcome::refused(error);
        if (found->locked)
            return Outcome::refused(QStringLiteral("E_LAYER_LOCKED: --layer-id=%1").arg(found->id));
        if (doc.layers().size() <= 1)
            return Outcome::refused(QStringLiteral("E_LAYER_LAST: a document keeps its last layer"));
        if (!doc.removeLayer(found->id, &error))
            return Outcome::refused(QStringLiteral("E_LAYER_REMOVE: could not remove --layer-id=%1")
                                        .arg(found->id));
        return Outcome::edited(QStringLiteral("removed %1\n").arg(target));
    }

    if (action == QLatin1String("rename")) {
        QString name = value(parser, "name");
        if (name.isEmpty() && !words.isEmpty())
            name = words.first();
        if (name.isEmpty())
            return Outcome::wrong(QStringLiteral("layer rename: say --name NAME"));
        if (!doc.renameLayer(found->id, name, &error))
            return Outcome::refused(error);
        return Outcome::edited(QStringLiteral("renamed %1 to %2\n").arg(found->id, name));
    }

    if (action == QLatin1String("move")) {
        if (!isSet(parser, "index"))
            return Outcome::wrong(QStringLiteral("layer move: say --index N"));
        bool ok = false;
        const int index = value(parser, "index").toInt(&ok);
        if (!ok)
            return Outcome::wrong(QStringLiteral("E_LAYER_INDEX: --index must be an integer"));
        if (!hiddenAllowed(*found, parser, &error))
            return Outcome::refused(error);
        if (!doc.moveLayer(found->id, index, &error))
            return Outcome::refused(error);
        return Outcome::edited(QStringLiteral("moved %1 to index %2\n").arg(target).arg(index));
    }

    if (action == QLatin1String("dup")) {
        QString id = value(parser, "id");
        QString name = value(parser, "name");
        if (id.isEmpty() || name.isEmpty())
            return Outcome::wrong(QStringLiteral("layer dup: say --id ID --name NAME"));
        if (!hiddenAllowed(*found, parser, &error))
            return Outcome::refused(error);
        if (!doc.duplicateLayer(found->id, id, name, &error))
            return Outcome::refused(error);
        return Outcome::edited(QStringLiteral("duplicated %1 as %2\n").arg(target, id));
    }

    if (action == QLatin1String("mode")) {
        if (!isSet(parser, "mode"))
            return Outcome::wrong(QStringLiteral("layer mode: say --mode normal|multiply|screen"));
        if (!doc.setLayerMode(found->id, value(parser, "mode"), &error))
            return Outcome::refused(error);
        return Outcome::edited(QStringLiteral("%1: mode=%2\n").arg(found->id, value(parser, "mode")));
    }

    if (action == QLatin1String("set")) {
        const bool haveVisible = isSet(parser, "visible");
        const bool haveLocked = isSet(parser, "locked");
        const bool haveOpacity = isSet(parser, "opacity");
        if (!haveVisible && !haveLocked && !haveOpacity)
            return Outcome::wrong(QStringLiteral(
                "layer set: say --visible BOOL, --locked BOOL, or --opacity 0..255"));

        Document next = doc;
        Layer *nextLayer = next.layerById(found->id);
        if (nextLayer->locked && (haveVisible || haveOpacity))
            return Outcome::refused(QStringLiteral("E_LAYER_LOCKED: --layer-id=%1")
                                        .arg(found->id));
        if (haveVisible) {
            bool visible = false;
            if (!parseBool(value(parser, "visible"), &visible))
                return Outcome::wrong(QStringLiteral("E_LAYER_VALUE: --visible must be true or false"));
            if (!next.setLayerVisible(found->id, visible, &error))
                return Outcome::refused(error);
        }
        if (haveLocked) {
            bool locked = false;
            if (!parseBool(value(parser, "locked"), &locked))
                return Outcome::wrong(QStringLiteral("E_LAYER_VALUE: --locked must be true or false"));
            if (!next.setLayerLocked(found->id, locked, &error))
                return Outcome::refused(error);
        }
        if (haveOpacity) {
            bool ok = false;
            const int opacity = value(parser, "opacity").toInt(&ok);
            if (!ok)
                return Outcome::wrong(QStringLiteral("E_LAYER_OPACITY: opacity must be 0..255"));
            if (!next.setLayerOpacity(found->id, opacity, &error))
                return Outcome::refused(error);
        }
        doc = next;
        return Outcome::edited(QStringLiteral("updated %1\n").arg(target));
    }

    if (action == QLatin1String("merge-down")) {
        if (!hiddenAllowed(*found, parser, &error))
            return Outcome::refused(error);
        const int sourceIndex = doc.indexOfLayerId(found->id);
        if (sourceIndex > 0
            && !hiddenAllowed(doc.layers().at(sourceIndex - 1), parser, &error))
            return Outcome::refused(error);
        LayerOperationReport report;
        if (!applyMergeDown(&doc, found->id, &report, &error))
            return Outcome::refused(error);
        return Outcome::edited(layerReport(report));
    }

    return Outcome::wrong(QStringLiteral(
        "layer: list, add, rm, rename, move, set, mode, dup or merge-down — not %1")
                              .arg(action));
}

/// The drawing commands. They all work on one frame, so they share the fetch,
/// the edit and the store.
Outcome doDrawing(Document &doc, const QString &command, QStringList &words,
                  const QCommandLineParser &parser)
{
    Target target;
    QString error;
    bool allFrames = false;
    if (!parseScope(parser, &allFrames, &error))
        return Outcome::wrong(error);
    if (!resolveTarget(doc, value(parser, "clip"), value(parser, "frame"), &target,
                       &error, value(parser, "layer-id"), value(parser, "layer"), true,
                       allFrames, !isSet(parser, "scope")))
        return Outcome::wrong(error);
    const Layer *targetLayer = doc.layerById(target.layer);
    if (targetLayer && !hiddenAllowed(*targetLayer, parser, &error))
        return Outcome::refused(error);

    QChar slot = Grid::Empty;
    if (isSet(parser, "slot") && !parseSlot(value(parser, "slot"), &slot, &error))
        return Outcome::wrong(error);

    QPoint at;
    QPoint from;
    QPoint to;
    const bool haveAt =
        isSet(parser, "at") && parsePoint(value(parser, "at"), &at, &error);
    const bool haveFrom =
        isSet(parser, "from") && parsePoint(value(parser, "from"), &from, &error);
    const bool haveTo =
        isSet(parser, "to") && parsePoint(value(parser, "to"), &to, &error);

    QString action;
    QPoint by;
    QChar into;
    if (command == QLatin1String("edit")) {
        if (words.isEmpty())
            return Outcome::wrong(QStringLiteral("edit: clear, shift, flip or swap"));
        action = words.first();
        if (action == QLatin1String("shift")) {
            if (!isSet(parser, "by") || !parsePoint(value(parser, "by"), &by, &error))
                return Outcome::wrong(QStringLiteral("edit shift: say --by DX,DY"));
        } else if (action == QLatin1String("flip")) {
            if (value(parser, "axis") != QLatin1String("x")
                && value(parser, "axis") != QLatin1String("y"))
                return Outcome::wrong(QStringLiteral("edit flip: --axis x or y"));
        } else if (action == QLatin1String("swap")) {
            if (!isSet(parser, "slot")
                || !parseSlot(value(parser, "slot"), &slot, &error)
                || !isSet(parser, "to")
                || !parseSlot(value(parser, "to"), &into, &error))
                return Outcome::wrong(QStringLiteral("edit swap: say --slot FROM --to INTO"));
        } else if (action != QLatin1String("clear")) {
            return Outcome::wrong(
                QStringLiteral("edit: clear, shift, flip or swap — not %1").arg(action));
        }
    }

    const auto edit = [&](Grid &grid) {
        if (command == QLatin1String("paint")) {
            ops::paint(grid, at.x(), at.y(), slot);
        } else if (command == QLatin1String("line")) {
            ops::line(grid, from, to, slot);
        } else if (command == QLatin1String("rect")) {
            ops::rect(grid, from, to, slot, isSet(parser, "filled"));
        } else if (command == QLatin1String("fill")) {
            ops::fill(grid, at.x(), at.y(), slot);
        } else if (action == QLatin1String("clear")) {
            ops::clear(grid, isSet(parser, "slot") ? slot : Grid::Empty);
        } else if (action == QLatin1String("shift")) {
            ops::shift(grid, by.x(), by.y());
        } else if (action == QLatin1String("flip")) {
            if (value(parser, "axis") == QLatin1String("x"))
                ops::flipHorizontal(grid);
            else
                ops::flipVertical(grid);
        } else if (action == QLatin1String("swap")) {
            ops::swapSlot(grid, slot, into);
        }
    };
    if ((command == QLatin1String("paint") && !haveAt)
        || (command == QLatin1String("line") && (!haveFrom || !haveTo))
        || (command == QLatin1String("rect") && (!haveFrom || !haveTo))
        || (command == QLatin1String("fill") && !haveAt))
        return Outcome::wrong(command == QLatin1String("paint")
                                  ? QStringLiteral("paint: say --at X,Y")
                                  : command == QLatin1String("fill")
                                        ? QStringLiteral("fill: say --at X,Y")
                                        : command == QLatin1String("rect")
                                              ? QStringLiteral("rect: say --from X,Y --to X,Y")
                                              : QStringLiteral("line: say --from X,Y --to X,Y"));
    int changed = 0;
    QString editError;
    if (!doc.editLayer(target.layer, target.clip, target.frame,
                       target.allFrames ? EditScope::AllFrames : EditScope::CurrentFrame,
                       edit, &changed, &editError))
        return Outcome::refused(editError);
    return Outcome::edited();
}

} // namespace

void addOptions(QCommandLineParser &parser)
{
    parser.addOptions({
        {QStringLiteral("size"), QStringLiteral("COLUMNSxROWS"), QStringLiteral("size")},
        {QStringLiteral("clip"), QStringLiteral("which clip (default: the first)"),
          QStringLiteral("name")},
        {QStringLiteral("layer-id"), QStringLiteral("stable layer ID"), QStringLiteral("id")},
        {QStringLiteral("layer"), QStringLiteral("exact layer name"), QStringLiteral("name")},
        {QStringLiteral("frame"), QStringLiteral("which frame (default: 0)"),
         QStringLiteral("index")},
        {QStringLiteral("slot"), QStringLiteral("the palette letter to draw with"),
         QStringLiteral("letter")},
        {QStringLiteral("at"), QStringLiteral("X,Y"), QStringLiteral("point")},
        {QStringLiteral("from"), QStringLiteral("X,Y"), QStringLiteral("point")},
        {QStringLiteral("to"), QStringLiteral("X,Y"), QStringLiteral("point")},
        {QStringLiteral("by"), QStringLiteral("DX,DY"), QStringLiteral("offset")},
        {{QStringLiteral("o"), QStringLiteral("out")}, QStringLiteral("where to write"),
         QStringLiteral("path")},
        {QStringLiteral("scale"), QStringLiteral("pixels per sprite pixel (default 1)"),
          QStringLiteral("n")},
        {QStringLiteral("resolution"), QStringLiteral("import resolution WIDTHxHEIGHT"),
          QStringLiteral("size")},
        {QStringLiteral("fit"), QStringLiteral("import fit: contain, cover, or stretch"),
          QStringLiteral("mode")},
        {QStringLiteral("into"), QStringLiteral("document to receive an imported layer"),
          QStringLiteral("path")},
        {QStringLiteral("layer-name"), QStringLiteral("name for an imported layer"),
          QStringLiteral("name")},
        {QStringLiteral("format"), QStringLiteral("render format: png or gif"),
          QStringLiteral("format")},
        {QStringLiteral("fps"), QStringLiteral("frames per second"), QStringLiteral("n")},
        {QStringLiteral("name"), QStringLiteral("a name"), QStringLiteral("text")},
        {QStringLiteral("colour"), QStringLiteral("#RRGGBB"), QStringLiteral("hex")},
        {QStringLiteral("axis"), QStringLiteral("x or y"), QStringLiteral("axis")},
        {QStringLiteral("index"), QStringLiteral("an index"), QStringLiteral("n")},
        {QStringLiteral("script"),
         QStringLiteral("a file of commands for `batch`, or - for stdin"),
         QStringLiteral("path")},
        {QStringLiteral("filled"), QStringLiteral("fill the rectangle")},
        {QStringLiteral("sheet"), QStringLiteral("every frame side by side")},
         {QStringLiteral("checker"), QStringLiteral("checkerboard behind transparency")},
         {QStringLiteral("loop"), QStringLiteral("loop an animated GIF (default)")},
         {QStringLiteral("no-loop"), QStringLiteral("play an animated GIF once")},
         {QStringLiteral("isolated"), QStringLiteral("render only the explicit layer target")},
         {QStringLiteral("anyway"), QStringLiteral("do it even though it crops drawing")},
         {QStringLiteral("all-frames"), QStringLiteral("edit every frame of the target layer")},
         {QStringLiteral("scope"), QStringLiteral("content scope: frame or all-frames"),
          QStringLiteral("scope")},
         {QStringLiteral("include-hidden"), QStringLiteral("allow operations on hidden layers")},
         {QStringLiteral("id"), QStringLiteral("new layer ID"), QStringLiteral("id")},
         {QStringLiteral("storage"), QStringLiteral("layer storage: shared or animated"),
          QStringLiteral("storage")},
         {QStringLiteral("mode"), QStringLiteral("blend mode: normal, multiply, or screen"),
          QStringLiteral("mode")},
         {QStringLiteral("visible"), QStringLiteral("set layer visibility"),
          QStringLiteral("bool")},
         {QStringLiteral("locked"), QStringLiteral("set layer lock state"),
          QStringLiteral("bool")},
         {QStringLiteral("opacity"), QStringLiteral("layer opacity, 0..255"),
          QStringLiteral("n")},
         {QStringLiteral("dup"), QStringLiteral("copy the current frame")},
         {QStringLiteral("param"), QStringLiteral("plugin parameter KEY=VALUE"),
          QStringLiteral("key=value")},
    });
}

bool isDocumentCommand(const QString &command)
{
    static const QStringList known{
        QStringLiteral("info"),  QStringLiteral("check"), QStringLiteral("show"),
        QStringLiteral("text"),  QStringLiteral("resize"), QStringLiteral("trim"),
        QStringLiteral("clip"),  QStringLiteral("frame"), QStringLiteral("palette"),
        QStringLiteral("layer"),
        QStringLiteral("paint"),
        QStringLiteral("line"),  QStringLiteral("rect"),  QStringLiteral("fill"),
        QStringLiteral("edit"),
    };
    return known.contains(command);
}

Outcome applyCommand(Document &doc, const QString &command, QStringList words,
                     const QCommandLineParser &parser)
{
    if (command == QLatin1String("info"))
        return doInfo(doc);
    if (command == QLatin1String("check"))
        return doCheck(doc);

    if (command == QLatin1String("show") || command == QLatin1String("text")) {
        Target target;
        QString error;
        if (!resolveTarget(doc, value(parser, "clip"), value(parser, "frame"), &target,
                           &error))
            return Outcome::wrong(error);
        render::Options options;
        if (!renderOptions(doc, parser, &options, &error))
            return Outcome::wrong(error);
        QStringList diagnostics;
        Outcome outcome = Outcome::ok(
            command == QLatin1String("show")
                ? render::toAnsi(doc, target.clip, target.frame, options, &diagnostics)
                : render::toText(doc, target.clip, target.frame, options, &diagnostics));
        if (!diagnostics.isEmpty())
            outcome.error = diagnostics.join(QStringLiteral("; "));
        return outcome;
    }

    if (command == QLatin1String("resize"))
        return doResize(doc, parser);
    if (command == QLatin1String("trim"))
        return doTrim(doc, parser);
    if (command == QLatin1String("clip"))
        return doClip(doc, words, parser);
    if (command == QLatin1String("frame"))
        return doFrame(doc, words, parser);
    if (command == QLatin1String("palette"))
        return doPalette(doc, words, parser);
    if (command == QLatin1String("layer"))
        return doLayer(doc, words, parser);

    if (command == QLatin1String("paint") || command == QLatin1String("line")
        || command == QLatin1String("rect") || command == QLatin1String("fill")
        || command == QLatin1String("edit")) {
        return doDrawing(doc, command, words, parser);
    }

    return Outcome::wrong(
        QStringLiteral("no command %1 — try `omapixel --help`").arg(command));
}

} // namespace cli
} // namespace omapixel
