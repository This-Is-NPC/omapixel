#include "Commands.h"

#include "Ops.h"
#include "Render.h"

#include <QCommandLineParser>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPoint>

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
    if (text.size() != 1) {
        *error = QStringLiteral("--slot: %1 is not a single character").arg(text);
        return false;
    }
    *slot = text.at(0);
    return true;
}

/// The clip and frame a command works on. Defaults to the first clip and frame
/// 0, so the common case -- a document with one clip -- needs no flags at all.
struct Target {
    QString clip;
    int frame = 0;
};

bool resolveTarget(const Document &doc, const QString &clipOption,
                   const QString &frameOption, Target *target, QString *error)
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
    if (!okFrame || target->frame < 0 || target->frame >= clip->frames.size()) {
        *error = QStringLiteral("%1 has frames 0..%2, not %3")
                     .arg(target->clip)
                     .arg(clip->frames.size() - 1)
                     .arg(frameOption);
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
        int drawn = 0;
        for (const Grid &grid : clip.frames)
            drawn += grid.drawnCount();
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), clip.name);
        entry.insert(QStringLiteral("fps"), clip.fps);
        entry.insert(QStringLiteral("frames"), clip.frames.size());
        entry.insert(QStringLiteral("drawn"), drawn);
        clips.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("size"), size);
    root.insert(QStringLiteral("palette"), palette);
    root.insert(QStringLiteral("clips"), clips);
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

    const int lost = doc.wouldLose(columns, rows);
    if (lost > 0 && !isSet(parser, "anyway")) {
        return Outcome::refused(
            QStringLiteral("this would crop %1 drawn pixel(s). Pass --anyway if "
                           "that is what you want.")
                .arg(lost));
    }
    const int wasColumns = doc.columns();
    const int wasRows = doc.rows();
    doc.resize(columns, rows);

    QString note = QStringLiteral("%1x%2 → %3x%4")
                       .arg(wasColumns)
                       .arg(wasRows)
                       .arg(columns)
                       .arg(rows);
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

    if (action == QLatin1String("add")) {
        const int fps = isSet(parser, "fps") ? value(parser, "fps").toInt() : 8;
        if (!doc.addClip(name, fps)) {
            return Outcome::refused(
                QStringLiteral("clip add: %1 is empty or already there").arg(name));
        }
    } else if (action == QLatin1String("rm")) {
        if (!doc.removeClip(name)) {
            // Two different refusals, and saying "no clip walk" for both sends
            // somebody hunting for a typo that is not there.
            return Outcome::refused(
                doc.clip(name)
                    ? QStringLiteral("clip rm: a document keeps its last clip")
                    : QStringLiteral("clip rm: no clip %1").arg(name));
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
        if (!doc.setFps(name, value(parser, "fps").toInt()))
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
        doc.addFrame(target.clip, target.frame, action == QLatin1String("dup"));
    } else if (action == QLatin1String("rm")) {
        if (!doc.removeFrame(target.clip, target.frame))
            return Outcome::refused(QStringLiteral("frame rm: a clip keeps its last frame"));
    } else if (action == QLatin1String("move")) {
        if (!isSet(parser, "index"))
            return Outcome::wrong(QStringLiteral("frame move: say --index N"));
        if (!doc.moveFrame(target.clip, target.frame, value(parser, "index").toInt()))
            return Outcome::refused(QStringLiteral("frame move: out of range"));
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
        doc.palette().set(slot, colour);
    } else if (action == QLatin1String("rm")) {
        if (!doc.palette().remove(slot))
            return Outcome::refused(QStringLiteral("palette rm: no slot %1").arg(slot));
    } else {
        return Outcome::wrong(
            QStringLiteral("palette: list, set or rm — not %1").arg(action));
    }
    return Outcome::edited();
}

/// The drawing commands. They all work on one frame, so they share the fetch,
/// the edit and the store.
Outcome doDrawing(Document &doc, const QString &command, QStringList &words,
                  const QCommandLineParser &parser)
{
    Target target;
    QString error;
    if (!resolveTarget(doc, value(parser, "clip"), value(parser, "frame"), &target,
                       &error))
        return Outcome::wrong(error);

    Grid grid = doc.frame(target.clip, target.frame);
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

    if (command == QLatin1String("paint")) {
        if (!haveAt)
            return Outcome::wrong(QStringLiteral("paint: say --at X,Y"));
        ops::paint(grid, at.x(), at.y(), slot);
    } else if (command == QLatin1String("line")) {
        if (!haveFrom || !haveTo)
            return Outcome::wrong(QStringLiteral("line: say --from X,Y --to X,Y"));
        ops::line(grid, from, to, slot);
    } else if (command == QLatin1String("rect")) {
        if (!haveFrom || !haveTo)
            return Outcome::wrong(QStringLiteral("rect: say --from X,Y --to X,Y"));
        ops::rect(grid, from, to, slot, isSet(parser, "filled"));
    } else if (command == QLatin1String("fill")) {
        if (!haveAt)
            return Outcome::wrong(QStringLiteral("fill: say --at X,Y"));
        ops::fill(grid, at.x(), at.y(), slot);
    } else if (command == QLatin1String("edit")) {
        if (words.isEmpty())
            return Outcome::wrong(QStringLiteral("edit: clear, shift, flip or swap"));
        const QString action = words.takeFirst();
        if (action == QLatin1String("clear")) {
            ops::clear(grid, isSet(parser, "slot") ? slot : Grid::Empty);
        } else if (action == QLatin1String("shift")) {
            QPoint by;
            if (!isSet(parser, "by") || !parsePoint(value(parser, "by"), &by, &error))
                return Outcome::wrong(QStringLiteral("edit shift: say --by DX,DY"));
            ops::shift(grid, by.x(), by.y());
        } else if (action == QLatin1String("flip")) {
            const QString axis = value(parser, "axis");
            if (axis == QLatin1String("x"))
                ops::flipHorizontal(grid);
            else if (axis == QLatin1String("y"))
                ops::flipVertical(grid);
            else
                return Outcome::wrong(QStringLiteral("edit flip: --axis x or y"));
        } else if (action == QLatin1String("swap")) {
            QChar into;
            if (!isSet(parser, "to") || !parseSlot(value(parser, "to"), &into, &error))
                return Outcome::wrong(QStringLiteral("edit swap: say --slot FROM --to INTO"));
            ops::swapSlot(grid, slot, into);
        } else {
            return Outcome::wrong(
                QStringLiteral("edit: clear, shift, flip or swap — not %1").arg(action));
        }
    }

    doc.setFrame(target.clip, target.frame, grid);
    return Outcome::edited();
}

} // namespace

void addOptions(QCommandLineParser &parser)
{
    parser.addOptions({
        {QStringLiteral("size"), QStringLiteral("COLUMNSxROWS"), QStringLiteral("size")},
        {QStringLiteral("clip"), QStringLiteral("which clip (default: the first)"),
         QStringLiteral("name")},
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
        {QStringLiteral("anyway"), QStringLiteral("do it even though it crops drawing")},
        {QStringLiteral("dup"), QStringLiteral("copy the current frame")},
    });
}

bool isDocumentCommand(const QString &command)
{
    static const QStringList known{
        QStringLiteral("info"),  QStringLiteral("check"), QStringLiteral("show"),
        QStringLiteral("text"),  QStringLiteral("resize"), QStringLiteral("trim"),
        QStringLiteral("clip"),  QStringLiteral("frame"), QStringLiteral("palette"),
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
        return Outcome::ok(command == QLatin1String("show")
                               ? render::toAnsi(doc, target.clip, target.frame,
                                                isSet(parser, "checker"))
                               : render::toText(doc, target.clip, target.frame));
    }

    if (command == QLatin1String("resize"))
        return doResize(doc, parser);
    if (command == QLatin1String("clip"))
        return doClip(doc, words, parser);
    if (command == QLatin1String("frame"))
        return doFrame(doc, words, parser);
    if (command == QLatin1String("palette"))
        return doPalette(doc, words, parser);

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
