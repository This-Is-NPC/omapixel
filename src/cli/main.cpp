// omapixel — the command line.
//
// Every operation the studio can do is here, and that is deliberate: an agent
// driving this has to be able to build a whole animation without a window, and
// a human has to be able to script one. The studio is a second front end over
// the same core, never a place where a capability lives alone.
//
// Two commands make the difference between "can change a drawing" and "can work
// on a drawing": `show`, which puts the frame in the terminal, and `render`,
// which writes a PNG. Without a way to SEE the result, a complete command set
// still leaves you taking screenshots of a running window.
//
// This file is argument handling and files. What a command DOES lives in
// Commands.cpp, so that one command and the same command inside a `batch`
// cannot drift apart.

#include "Bridge.h"
#include "Codec.h"
#include "Commands.h"
#include "Config.h"
#include "Differences.h"
#include "Document.h"
#include "LayerOperations.h"
#include "ImageImport.h"
#include "GifExport.h"
#include "Ops.h"
#include "Output.h"
#include "PluginCommands.h"
#include "Sessions.h"
#include "Strings.h"
#include "Render.h"
#include "TextSafety.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QBuffer>
#include <QTextStream>

#include "version.h"

#include <cstdio>

using namespace omapixel;

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

QString safeDiagnostic(const QString &text)
{
    return text::escapeForTerminal(text);
}

constexpr qsizetype maxBatchLines = 8192;
constexpr qsizetype maxBatchCommands = 4096;
constexpr qsizetype maxBatchOutputBytes = 4 * 1024 * 1024;

void diagnostic(const QString &text)
{
    err() << safeDiagnostic(text) << "\n";
}

bool hasOmittedPluginParameter(int argc, char **argv)
{
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) != QLatin1String("--param"))
            continue;
        if (index + 1 == argc
            || QString::fromLocal8Bit(argv[index + 1]).startsWith(QLatin1Char('-')))
            return true;
    }
    return false;
}

Codec::WarningLimits warningLimits()
{
    const Config &config = Config::shared();
    Codec::WarningLimits limits;
    limits.fileBytes = qint64(config.number(QStringLiteral("warnings.file_mib")))
                       * 1024 * 1024;
    limits.clips = config.number(QStringLiteral("warnings.clips"));
    limits.framesPerClip =
        config.number(QStringLiteral("warnings.frames_per_clip"));
    limits.totalFrames = config.number(QStringLiteral("warnings.frames_total"));
    limits.paletteSlots = config.number(QStringLiteral("warnings.palette_slots"));
    return limits;
}

qint64 renderWarningPixels()
{
    return qint64(Config::shared().number(
                      QStringLiteral("warnings.render_megapixels")))
           * 1000000;
}

/// Reads a batch script: a file, or standard input when the path is `-`.
bool readScript(const QString &path, QStringList *lines, QString *error)
{
    if (path == QLatin1String("-")) {
        QTextStream in(stdin);
        const QString text = in.read(Document::maxDocumentBytes + 1);
        if (text.toUtf8().size() > Document::maxDocumentBytes) {
            *error = QStringLiteral("batch script exceeds the hard input limit");
            return false;
        }
        const qsizetype count = text.isEmpty() ? 0 : text.count(QLatin1Char('\n'))
            + (text.endsWith(QLatin1Char('\n')) ? 0 : 1);
        if (count > maxBatchLines) {
            *error = QStringLiteral("batch script exceeds the hard line limit");
            return false;
        }
        *lines = text.split(QLatin1Char('\n'));
        return true;
    }
    QByteArray bytes;
    if (!input::readRegularFile(path, Document::maxDocumentBytes, &bytes, error))
        return false;
    if (bytes.size() > Document::maxDocumentBytes) {
        *error = QStringLiteral("%1: batch script exceeds the hard input limit").arg(path);
        return false;
    }
    const qsizetype count = bytes.isEmpty() ? 0 : bytes.count('\n')
        + (bytes.endsWith('\n') ? 0 : 1);
    if (count > maxBatchLines) {
        *error = QStringLiteral("%1: batch script exceeds the hard line limit").arg(path);
        return false;
    }
    *lines = QString::fromUtf8(bytes).split(QLatin1Char('\n'));
    return true;
}

/// `batch` — many commands over one document, in one pass.
///
/// The point is not tidiness, it is time. Every separate invocation starts a Qt
/// application, parses the whole document, changes a few pixels and writes the
/// file back. Generating a 160x90 picture that way took three and a half
/// thousand processes and four and a half minutes, and almost none of that was
/// drawing. Here the document is read once and written once.
///
/// A batch is all or nothing. If a line fails, nothing is saved: a script that
/// half-applied would leave a document nobody could reason about, and running it
/// again would not mean the same as running it once.
int runBatch(Document &doc, const QString &path, const QCommandLineParser &parser)
{
    if (!parser.isSet(QStringLiteral("script"))) {
        err() << "batch: say --script FILE, or --script - to read stdin\n";
        return 2;
    }
    QStringList lines;
    QString error;
    if (!readScript(parser.value(QStringLiteral("script")), &lines, &error)) {
        diagnostic(error);
        return 1;
    }

    int applied = 0;
    int changes = 0;
    qsizetype commands = 0;
    qsizetype outputBytes = 0;
    for (int number = 1; number <= lines.size(); ++number) {
        const QString line = lines.at(number - 1).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        QStringList words = QProcess::splitCommand(line);
        if (words.isEmpty())
            continue;
        if (++commands > maxBatchCommands) {
            diagnostic(QStringLiteral("line %1: batch script exceeds the hard command limit")
                           .arg(number));
            return 2;
        }
        const QString command = words.takeFirst();

        if (!cli::isDocumentCommand(command)) {
            diagnostic(QStringLiteral("line %1: %2 cannot run in a batch — a batch works "
                                     "on the one document it opened")
                           .arg(number).arg(command));
            return 2;
        }

        // A fresh parser per line: the flags belong to that line, and reusing
        // the outer one would let a --slot from line 3 leak into line 4.
        QCommandLineParser lineParser;
        cli::addOptions(lineParser);
        if (!lineParser.parse(QStringList{QStringLiteral("omapixel")} + words)) {
            diagnostic(QStringLiteral("line %1: %2")
                           .arg(number).arg(lineParser.errorText()));
            return 2;
        }

        const cli::Outcome outcome = cli::applyCommand(
            doc, command, lineParser.positionalArguments(), lineParser);
        outputBytes += outcome.output.toUtf8().size();
        if (outputBytes > maxBatchOutputBytes) {
            diagnostic(QStringLiteral("line %1: batch output exceeds the hard limit of %2 MiB")
                           .arg(number).arg(maxBatchOutputBytes / (1024 * 1024)));
            return 2;
        }
        const QStringList lineWords = lineParser.positionalArguments();
        const bool machineOutput = command == QLatin1String("info")
            || ((command == QLatin1String("palette") || command == QLatin1String("layer"))
                && (lineWords.isEmpty() || lineWords.first() == QLatin1String("list")));
        if (!outcome.output.isEmpty())
            out() << (command == QLatin1String("show") || machineOutput
                          ? outcome.output
                          : safeDiagnostic(outcome.output));
        if (outcome.code != 0) {
            diagnostic(QStringLiteral("line %1: %2").arg(number).arg(outcome.error));
            diagnostic(QStringLiteral("nothing was saved"));
            return outcome.code;
        }
        applied += 1;
        if (outcome.changed)
            changes += 1;
    }

    if (changes > 0 && !Codec::writeFile(path, doc, &error)) {
        diagnostic(error);
        return 1;
    }
    out() << safeDiagnostic(path) << ": " << applied << " command(s), " << changes
           << " change(s)\n";
    return 0;
}

bool resolveRenderLayer(const Document &doc, const QCommandLineParser &parser,
                        QString *layer, QString *error)
{
    const QString idOption = parser.value(QStringLiteral("layer-id"));
    const QString nameOption = parser.value(QStringLiteral("layer"));
    const Layer *byId = idOption.isEmpty() ? nullptr : doc.layerById(idOption);
    const Layer *byName = nameOption.isEmpty() ? nullptr : doc.layerByName(nameOption);
    if (!idOption.isEmpty() && !byId) {
        *error = QStringLiteral("E_LAYER_NOT_FOUND: --layer-id=%1").arg(idOption);
        return false;
    }
    if (!nameOption.isEmpty() && !byName) {
        *error = QStringLiteral("E_LAYER_NOT_FOUND: --layer=%1").arg(nameOption);
        return false;
    }
    if (byId && byName && byId != byName) {
        *error = QStringLiteral("E_LAYER_TARGET_CONFLICT: --layer-id=%1 conflicts with --layer=%2")
                     .arg(idOption, nameOption);
        return false;
    }
    const Layer *found = byId ? byId : byName;
    if (!found) {
        *error = QStringLiteral("E_LAYER_TARGET_REQUIRED: --isolated requires "
                                "--layer-id=ID or --layer=EXACT_NAME");
        return false;
    }
    *layer = found->id;
    return true;
}

int flattenTo(const Document &source, const QString &inputPath,
              const QCommandLineParser &parser)
{
    if (!parser.isSet(QStringLiteral("out"))) {
        err() << "flatten: say where to write, with -o\n";
        return 2;
    }
    const QString outputPath = parser.value(QStringLiteral("out"));
    QString outputError;
    if (!output::validate(outputPath, {inputPath}, &outputError)) {
        diagnostic(QStringLiteral("E_FLATTEN_OUTPUT: %1").arg(outputError));
        return 2;
    }

    // Stage through the permissive path so an existing palette-loss report is
    // preserved as the primary refusal before the structural lock policy.
    const LayerOperationResult staged = previewFlattenVisible(source);
    if (!staged) {
        diagnostic(staged.error);
        return 1;
    }
    const LayerOperationReport &report = staged.report;
    const QString summary = QStringLiteral(
        "frames=%1 affected-pixels=%2 exact-pixels=%3 approximated-pixels=%4 "
        "new-slots=%5 removed-layers=%6")
                                .arg(report.frames)
                                .arg(report.affectedPixels)
                                .arg(report.exactMatches)
                                .arg(report.approximatedPixels)
                                .arg(report.newSlots)
                                .arg(report.removedLayers);
    if (report.hasPaletteLoss() && !parser.isSet(QStringLiteral("anyway"))) {
        diagnostic(QStringLiteral("E_FLATTEN_PALETTE_LOSS: %1; pass --anyway to write")
                       .arg(summary));
        return 1;
    }
    QString error;
    if (!Codec::writeFile(outputPath, staged.document, {inputPath}, &error)) {
        diagnostic(error);
        return 1;
    }
    out() << safeDiagnostic(outputPath) << ": " << summary << "\n";
    return 0;
}

/// `where` -- which live studios hold a document and their view state.
///
/// The read side of the session files the studio publishes. An explicit ask
/// rather than a silent default: if `--frame` ever fell back to the session,
/// the CLI would stop being a function of its arguments and the same script
/// would hit different frames depending on whether a window happened to be
/// open. Here the agent asks, then passes the number itself.
///
/// Exit codes are the contract: 0 when something is printed, 1 when the
/// honest answer is "nobody" -- both for a named document nobody holds and
/// for no live sessions at all, so "is anyone looking?" has one negative.
int where(const QStringList &words)
{
    // Absolute, because the publisher writes absolute paths and a relative
    // spelling from another directory must still match.
    const QString wanted = words.isEmpty()
                               ? QString()
                               : QFileInfo(words.first()).absoluteFilePath();
    const QList<sessions::Entry> found = sessions::live(wanted);

    if (found.isEmpty()) {
        const QString message = wanted.isEmpty()
                                    ? Strings::shared().t(QStringLiteral("error.whereNoSessions"))
                                    : Strings::shared().t(QStringLiteral("error.whereNobody"))
                                          .arg(wanted);
        err() << safeDiagnostic(message) << "\n";
        return 1;
    }

    QJsonArray list;
    for (const sessions::Entry &entry : found) {
        QJsonObject one;
        one.insert(QStringLiteral("pid"), entry.pid);
        one.insert(QStringLiteral("started"), entry.started);
        one.insert(QStringLiteral("path"), entry.path);
        one.insert(QStringLiteral("dirty"), entry.dirty);
        one.insert(QStringLiteral("view"), QJsonObject{
                                             {QStringLiteral("clip"), entry.clip},
                                             {QStringLiteral("frame"), entry.frame},
                                             {QStringLiteral("layerId"), entry.layerId},
                                             {QStringLiteral("layerName"), entry.layerName},
                                             {QStringLiteral("scope"), entry.scope}});
        if (entry.selection.isValid()) {
            QJsonObject selection;
            selection.insert(QStringLiteral("clip"), entry.selectionClip);
            selection.insert(QStringLiteral("frame"), entry.selectionFrame);
            selection.insert(QStringLiteral("layerId"), entry.selectionLayerId);
            selection.insert(QStringLiteral("layerName"), entry.selectionLayerName);
            selection.insert(QStringLiteral("x"), entry.selection.x());
            selection.insert(QStringLiteral("y"), entry.selection.y());
            selection.insert(QStringLiteral("width"), entry.selection.width());
            selection.insert(QStringLiteral("height"), entry.selection.height());
            selection.insert(QStringLiteral("count"),
                             entry.selection.width() * entry.selection.height());
            one.insert(QStringLiteral("selection"), selection);
        } else {
            one.insert(QStringLiteral("selection"), QJsonValue::Null);
        }
        list.append(one);
    }
    QJsonObject report;
    report.insert(QStringLiteral("sessions"), list);
    out() << QJsonDocument(report).toJson(QJsonDocument::Compact) << "\n";
    return 0;
}

/// `i18n` -- what a language catalogue is still missing.
///
/// Here rather than in a script beside the catalogues, because this project is
/// C++ and a helper in another language is a second toolchain to install, a
/// second thing to keep working, and a second place to look.
int checkCatalogues(const QString &wanted)
{
    const QString home = Strings::catalogueDir();
    const auto read = [](const QString &path, QJsonObject *into, QString *error) {
        QByteArray bytes;
        if (!input::readRegularFile(path, Document::maxDocumentBytes, &bytes, error))
            return false;
        if (bytes.size() > Document::maxDocumentBytes) {
            *error = QStringLiteral("%1: catalog exceeds the hard input limit").arg(path);
            return false;
        }
        QString scanError;
        if (Codec::rejectDuplicateJsonKeys(bytes, &scanError)) {
            *error = QStringLiteral("%1: %2").arg(path, scanError);
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
            *error = QStringLiteral("%1: invalid catalog JSON").arg(path);
            return false;
        }
        *into = parsed.object();
        return true;
    };

    QJsonObject english;
    QString readError;
    if (!read(home + QStringLiteral("/en.json"), &english, &readError)) {
        diagnostic(readError);
        return 1;
    }

    if (wanted.isEmpty()) {
        out() << "catalogues in " << safeDiagnostic(home) << ":\n";
        const QFileInfoList files =
            QDir(home).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo &file : files) {
            QJsonObject one;
            if (!read(file.absoluteFilePath(), &one, &readError))
                continue;
            out() << safeDiagnostic(QStringLiteral("  %1 %2 strings")
                                        .arg(file.completeBaseName(), -8)
                                        .arg(one.size()));
            if (file.completeBaseName() != QLatin1String("en"))
                out() << "  (" << one.size() * 100 / english.size() << "% of English)";
            out() << "\n";
        }
        out() << "\nto start one:  cp i18n/en.json i18n/pt.json"
              << "  &&  omapixel i18n pt\n";
        return 0;
    }

    QJsonObject them;
    if (!read(home + QLatin1Char('/') + wanted + QStringLiteral(".json"),
              &them, &readError)) {
        diagnostic(readError);
        return 1;
    }

    QStringList missing;
    QStringList untouched;
    for (auto it = english.constBegin(); it != english.constEnd(); ++it) {
        const QString theirs = them.value(it.key()).toString().trimmed();
        if (theirs.isEmpty())
            missing << it.key();
        else if (theirs == it.value().toString())
            untouched << it.key();
    }
    QStringList stale;
    for (auto it = them.constBegin(); it != them.constEnd(); ++it) {
        if (!english.contains(it.key()))
            stale << it.key();
    }
    missing.sort();
    stale.sort();

    out() << safeDiagnostic(wanted) << ": " << (english.size() - missing.size()) << " of "
          << english.size() << " translated\n";
    if (!missing.isEmpty()) {
        out() << "\n  " << missing.size() << " still to do:\n";
        for (int i = 0; i < missing.size() && i < 40; ++i) {
            out() << safeDiagnostic(QStringLiteral("    %1 %2\n")
                                        .arg(missing.at(i), -38)
                                        .arg(english.value(missing.at(i)).toString()));
        }
        if (missing.size() > 40)
            out() << "    ... and " << missing.size() - 40 << " more\n";
    }
    if (!stale.isEmpty()) {
        out() << "\n  " << stale.size()
              << " the program no longer asks for, safe to delete:\n";
        for (const QString &key : stale)
            out() << "    " << safeDiagnostic(key) << "\n";
    }
    if (!untouched.isEmpty() && missing.isEmpty()) {
        out() << "\n  " << untouched.size()
              << " still read as the English, which may be deliberate\n";
    }
    return missing.isEmpty() && stale.isEmpty() ? 0 : 1;
}

/// `config` -- the settings file, and what is wrong with it.
///
/// The same three questions omarchy's own programs answer about their config:
/// where it is, whether it parses, and how to get one. `herdr config check`
/// and `voxtype`'s commented default are the shape being followed here; a
/// studio that invented its own would be one more thing to learn on a desktop
/// where every other program already works this way.
int inspectConfig(const QString &what)
{
    Config &config = Config::shared();
    const QString path = Config::file();

    if (what == QLatin1String("check")) {
        const QStringList problems = config.problems();
        if (!QFile::exists(path)) {
            out() << safeDiagnostic(path)
                  << ": not there — omapixel runs on its defaults\n";
            return 0;
        }
        if (problems.isEmpty()) {
            out() << safeDiagnostic(path) << ": good\n";
            return 0;
        }
        err() << safeDiagnostic(path) << ":\n";
        for (const QString &problem : problems)
            err() << "  " << safeDiagnostic(problem) << "\n";
        return 1;
    }

    if (what == QLatin1String("write")) {
        const QString text = Config::defaultText();
        if (text.isEmpty()) {
            err() << "the default config is not installed — looked in:\n";
            for (const QString &place : Config::defaultSearchPath())
                err() << "  " << safeDiagnostic(place) << "\n";
            return 1;
        }
        if (QFile::exists(path)) {
            err() << safeDiagnostic(path)
                  << " is already there — delete it first, or edit it\n";
            return 1;
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            err() << "could not write " << safeDiagnostic(path) << "\n";
            return 1;
        }
        file.write(text.toUtf8());
        out() << "wrote " << safeDiagnostic(path) << "\n";
        return 0;
    }

    if (!what.isEmpty()) {
        err() << "config: say `check`, `write`, or nothing at all\n";
        return 2;
    }

    out() << "file      " << safeDiagnostic(path);
    out() << (QFile::exists(path) ? "\n" : "   (not there — running on the defaults)\n");
    const QString speaking = Strings::shared().language();
    const QString wanted = Strings::preferredLanguage();
    out() << "language  " << safeDiagnostic(speaking);
    // Asked for one and speaking another means there is no catalogue for it.
    // Saying only the one it settled on reads as the setting being ignored.
    if (!wanted.startsWith(speaking))
        out() << "   (asked for " << safeDiagnostic(wanted)
              << ", no catalogue — `omapixel i18n`)";
    out() << "\n";

    // What the file actually changed. A config file you cannot diff against
    // the defaults is a config file you stop trusting.
    QStringList differs;
    for (const auto &setting : Config::settings()) {
        if (config.value(setting.first) != setting.second) {
            differs << QStringLiteral("  %1 = %2 (default %3)")
                           .arg(setting.first, -20)
                           .arg(config.value(setting.first).toString(),
                                setting.second.toString());
        }
    }
    for (const auto &action : Config::actions()) {
        const QStringList now = config.bindings(action.first);
        QString was = action.second;
        if (was.startsWith(QLatin1Char('[')))
            was = was.remove(QLatin1Char('[')).remove(QLatin1Char(']'))
                      .remove(QLatin1Char('"')).remove(QLatin1Char(' '));
        else
            was = was.toLower();
        if (now.join(QLatin1Char(',')) != was) {
            differs << QStringLiteral("  %1 = %2 (default %3)")
                           .arg(QStringLiteral("keys.") + action.first, -20)
                           .arg(now.isEmpty() ? QStringLiteral("nothing")
                                              : now.join(QStringLiteral(", ")),
                                was.isEmpty() ? QStringLiteral("nothing") : was);
        }
    }
    if (differs.isEmpty()) {
        out() << "\neverything is at its default\n";
    } else {
        out() << "\nchanged from the defaults:\n";
        for (const QString &line : differs)
            out() << safeDiagnostic(line) << "\n";
    }

    const QStringList problems = config.problems();
    if (!problems.isEmpty()) {
        out() << "\nand " << problems.size() << " problem(s) — run `omapixel config check`\n";
        return 1;
    }
    return 0;
}

} // namespace

// --------------------------------------------------------------- the commands

int main(int argc, char *argv[])
{
    // QGuiApplication and not QCoreApplication: `render` paints with QPainter,
    // which needs the GUI stack up. It never opens a window, and the offscreen
    // platform keeps it working with no display at all.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qunsetenv("QT_QPA_PLATFORMTHEME");
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("omapixel"));
    // The words, so the command line speaks whatever the window speaks. Both
    // front ends over one core, catalogues included.
    // The settings first: the language the words are read in is one of them.
    Config::shared().load();
    Strings::shared().load(Strings::preferredLanguage());

    // The one flag rather than a sub-command, because this is the form
    // omarchy's own tooling reads a program's defaults with -- it is how
    // `omarchy-menu-herdr-keybindings` learns what the bindings are.
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) != QLatin1String("--default-config"))
            continue;
        const QString text = Config::defaultText();
        if (text.isEmpty()) {
            err() << "the default config is not installed\n";
            return 1;
        }
        out() << text::escapeForTerminal(text);
        out().flush();
        return 0;
    }
    QCoreApplication::setApplicationVersion(QStringLiteral(OMAPIXEL_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "omapixel — a pixel art and animation studio, from the command line.\n"
        "\n"
        "  new      create an empty document\n"
        "  info     what is in a document, as JSON\n"
        "  check    what stops a document from being drawn\n"
        "  show     draw a frame in the terminal\n"
        "  text     the frame as letters, one per pixel\n"
        "  render   write a PNG or animated GIF\n"
        "  flatten  write a separate composite document\n"
        "  resize   change the frame, keeping the drawing centred\n"
        "  trim     crop empty borders around one frame's content\n"
        "  clip     add | rm | rename | fps\n"
        "  frame    add | dup | rm | move\n"
        "  layer    list | add | rm | rename | move | set | mode | dup | merge-down\n"
        "  paint    one pixel\n"
        "  line     from one point to another\n"
        "  rect     an outline or a filled block\n"
        "  fill     flood the contiguous run under a point\n"
        "  edit     clear | shift | flip | swap\n"
        "  palette  list | set | rm\n"
        "  batch    many commands over one document, read once and written once\n"
        "  i18n     what a language catalogue is still missing\n"
        "  config   the settings file: check | write\n"
        "  plugin   list | check | run local plugins\n"
        "  where    which live studios hold a document\n"
        "  diff     what differs between two documents\n"
        "  import   pull one sprite set out of a catalog\n"
        "  import-image  turn PNG, JPEG, or WebP into a document or layer\n"
        "  export   put the clips back into one\n"
        "\n"
        "--default-config prints the annotated settings file to standard output.\n"
        "\n"
        "The file comes second: omapixel <command> <file> [sub-command] [flags].\n"
        "Run `omapixel <command> --help` for the flags of one command."));
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("what to do"));
    parser.addHelpOption();
    parser.addVersionOption();
    cli::addOptions(parser);
    parser.addOption(QCommandLineOption(QStringLiteral("json"),
                                        QStringLiteral("print machine-readable JSON")));
    if (hasOmittedPluginParameter(argc, argv)) {
        err() << "plugin run: --param must be KEY=VALUE with a value\n";
        return 2;
    }
    parser.process(app);

    QStringList words = parser.positionalArguments();
    if (words.isEmpty()) {
        err() << "say what to do — try `omapixel --help`\n";
        return 2;
    }

    const QString command = words.takeFirst();

    // -------------------------------------------------------------- new

    if (command == QLatin1String("i18n"))
        return checkCatalogues(words.value(0));

    if (command == QLatin1String("where"))
        return where(words);

    if (command == QLatin1String("config"))
        return inspectConfig(words.value(0));

    if (command == QLatin1String("plugin")) {
        const cli::Outcome outcome = cli::runPluginCommand(words, parser);
        if (!outcome.output.isEmpty())
            out() << outcome.output;
        if (!outcome.error.isEmpty())
            err() << safeDiagnostic(outcome.error);
        return outcome.code;
    }

    if (command == QLatin1String("new")) {
        if (words.isEmpty()) {
            err() << "new: say where to write\n";
            return 2;
        }
        int columns = 32;
        int rows = 24;
        if (parser.isSet(QStringLiteral("size"))) {
            const QString text = parser.value(QStringLiteral("size"));
            const QStringList parts = text.toLower().split(QLatin1Char('x'));
            bool okColumns = false;
            bool okRows = false;
            if (parts.size() == 2) {
                columns = parts.at(0).toInt(&okColumns);
                rows = parts.at(1).toInt(&okRows);
            }
            if (!okColumns || !okRows || columns <= 0 || rows <= 0) {
        err() << "--size: " << safeDiagnostic(text) << " is not COLUMNSxROWS\n";
                return 2;
            }
        }
        const Document doc = Document::blank(columns, rows);
        QString error;
        if (!Codec::writeFile(words.first(), doc, &error)) {
        err() << safeDiagnostic(error) << "\n";
            return 1;
        }
        out() << safeDiagnostic(words.first()) << ": " << columns << "x" << rows
              << ", one clip\n";
        return 0;
    }

    // -------------------------------------------------------- import-image
    // A raster is not an omapixel document, so this also comes before the
    // ordinary document load. With --into it stages a new layer on that file;
    // without it the raster becomes a new one-frame document.

    if (command == QLatin1String("import-image")) {
        if (words.isEmpty()) {
            err() << "import-image: say which image to import\n";
            return 2;
        }
        if (words.size() != 1) {
            err() << "import-image: say exactly one image to import\n";
            return 2;
        }
        if (!parser.isSet(QStringLiteral("out"))) {
            err() << "import-image: say -o where to write\n";
            return 2;
        }
        if (parser.isSet(QStringLiteral("scale"))
            && parser.isSet(QStringLiteral("resolution"))) {
            err() << "import-image: --scale and --resolution are mutually exclusive\n";
            return 2;
        }
        if (parser.isSet(QStringLiteral("fit"))
            && !parser.isSet(QStringLiteral("resolution"))) {
            err() << "import-image: --fit requires --resolution\n";
            return 2;
        }
        const bool intoDocument = parser.isSet(QStringLiteral("into"));
        if (!intoDocument
            && (parser.isSet(QStringLiteral("clip"))
                || parser.isSet(QStringLiteral("frame"))
                || parser.isSet(QStringLiteral("layer-id"))
                || parser.isSet(QStringLiteral("layer-name")))) {
            err() << "import-image: layer target options require --into\n";
            return 2;
        }

        ImageImport::Options options;
        if (parser.isSet(QStringLiteral("scale"))) {
            bool ok = false;
            options.scale = parser.value(QStringLiteral("scale")).toInt(&ok);
            if (!ok || options.scale < 1 || options.scale > 64) {
                err() << "import-image: --scale must be an integer between 1 and 64\n";
                return 2;
            }
        }
        if (parser.isSet(QStringLiteral("resolution"))) {
            const QString value = parser.value(QStringLiteral("resolution"));
            const QStringList parts = value.toLower().split(QLatin1Char('x'));
            bool widthOk = false;
            bool heightOk = false;
            const int width = parts.size() == 2 ? parts.at(0).toInt(&widthOk) : 0;
            const int height = parts.size() == 2 ? parts.at(1).toInt(&heightOk) : 0;
            if (!widthOk || !heightOk || width < 1 || height < 1
                || width > Document::maxDimension || height > Document::maxDimension) {
                err() << "import-image: --resolution must be WIDTHxHEIGHT, with each side between 1 and "
                      << Document::maxDimension << "\n";
                return 2;
            }
            options.targetResolution = QSize(width, height);
        }
        const QString fit = parser.isSet(QStringLiteral("fit"))
                                ? parser.value(QStringLiteral("fit")).toLower()
                                : QStringLiteral("contain");
        if (fit == QLatin1String("contain"))
            options.resizeMode = ImageImport::ResizeMode::Contain;
        else if (fit == QLatin1String("cover"))
            options.resizeMode = ImageImport::ResizeMode::Cover;
        else if (fit == QLatin1String("stretch"))
            options.resizeMode = ImageImport::ResizeMode::Stretch;
        else {
            err() << "import-image: --fit must be contain, cover, or stretch\n";
            return 2;
        }
        if (parser.isSet(QStringLiteral("clip")))
            options.clip = parser.value(QStringLiteral("clip"));
        if (parser.isSet(QStringLiteral("layer-id")))
            options.layerId = parser.value(QStringLiteral("layer-id"));
        if (parser.isSet(QStringLiteral("layer-name")))
            options.layerName = parser.value(QStringLiteral("layer-name"));
        if (parser.isSet(QStringLiteral("frame"))) {
            bool ok = false;
            options.frame = parser.value(QStringLiteral("frame")).toInt(&ok);
            if (!ok || options.frame < 0) {
                err() << "import-image: --frame must be a non-negative integer\n";
                return 2;
            }
        }

        const QString imagePath = words.first();
        const QString outputPath = parser.value(QStringLiteral("out"));
        const QString intoPath = parser.value(QStringLiteral("into"));
        QStringList sources{imagePath};
        Document imported;
        ImageImport::Report report;
        QString importError;
        if (!intoPath.isEmpty()) {
            sources.append(intoPath);
            const Codec::Result target = Codec::readFile(intoPath, warningLimits());
            if (!target) {
                diagnostic(target.error);
                return 1;
            }
            imported = target.document;
            if (!ImageImport::addLayer(&imported, imagePath, options, &report,
                                       &importError)) {
                diagnostic(QStringLiteral("import-image: %1").arg(importError));
                return 1;
            }
        } else {
            const ImageImport::Result result = ImageImport::createDocument(imagePath,
                                                                           options);
            if (!result) {
                diagnostic(QStringLiteral("import-image: %1").arg(result.error));
                return 1;
            }
            imported = result.document;
            report = result.report;
        }
        QString outputError;
        if (!output::validate(outputPath, sources, &outputError)) {
            diagnostic(QStringLiteral("import-image: %1").arg(outputError));
            return 2;
        }
        if (!Codec::writeFile(outputPath, imported, sources, &outputError)) {
            diagnostic(QStringLiteral("import-image: %1").arg(outputError));
            return 1;
        }
        out() << safeDiagnostic(outputPath) << ": " << report.logicalSize.width()
              << "x" << report.logicalSize.height() << ", "
              << report.paletteSlotsAfter << " colour(s), "
              << report.approximatedPixels << " approximated pixel(s), "
              << report.clippedPixels << " clipped pixel(s)\n";
        return 0;
    }

    // -------------------------------------------------------------- import
    // Reads a catalog rather than a document, so it comes before the load.

    if (command == QLatin1String("import")) {
        if (words.isEmpty()) {
            err() << "import: say which catalog\n";
            return 2;
        }
        // Each is required on its own. Asking for both to be missing meant
        // `import x --name p` fell through to writing an empty path, and the
        // error somebody got was ": No such file or directory".
        if (!parser.isSet(QStringLiteral("out"))) {
            err() << "import: say -o where to write\n";
            return 2;
        }
        if (!parser.isSet(QStringLiteral("name"))) {
            err() << "import: say --name which set to pull out\n";
            return 2;
        }
        QString outputError;
        if (!output::validate(parser.value(QStringLiteral("out")), {words.first()},
                              &outputError)) {
            diagnostic(QStringLiteral("import: %1").arg(outputError));
            return 2;
         }
         QByteArray catalogBytes;
         if (!input::readRegularFile(words.first(), Document::maxDocumentBytes,
                                     &catalogBytes, &outputError)) {
             diagnostic(outputError);
             return 1;
         }
         if (catalogBytes.size() > Document::maxDocumentBytes) {
             diagnostic(QStringLiteral("%1: catalog exceeds the hard input limit")
                            .arg(words.first()));
             return 1;
         }
         QString scanError;
         if (Codec::rejectDuplicateJsonKeys(catalogBytes, &scanError)) {
             diagnostic(QStringLiteral("%1: %2").arg(words.first(), scanError));
             return 1;
         }
         QJsonParseError catalogParse;
        const QJsonDocument catalogDocument =
            QJsonDocument::fromJson(catalogBytes, &catalogParse);
        if (catalogParse.error != QJsonParseError::NoError
            || !catalogDocument.isObject()) {
            diagnostic(QStringLiteral("%1: catalog must be a JSON object (%2)")
                           .arg(words.first(), catalogParse.errorString()));
            return 1;
        }
        const QJsonObject catalog = catalogDocument.object();
        const QString species = parser.value(QStringLiteral("name"));
        const QString variant = parser.isSet(QStringLiteral("index"))
                                    ? parser.value(QStringLiteral("index"))
                                    : QStringLiteral("0");
        const Bridge::Result pulled = Bridge::importSpecies(catalog, species, variant);
        if (!pulled) {
            diagnostic(pulled.error);
            return 1;
        }
        QString error;
         if (!Codec::writeFile(parser.value(QStringLiteral("out")), pulled.document,
                               {words.first()}, &error)) {
            diagnostic(error);
            return 1;
        }
        int frames = 0;
        for (const Clip &clip : pulled.document.clips())
            frames += clip.frameCount;
        out() << safeDiagnostic(parser.value(QStringLiteral("out"))) << ": "
              << pulled.document.clips().size() << " clip(s), " << frames << " frames\n";
        return 0;
    }

    // Everything below reads a document first.
    if (words.isEmpty()) {
        err() << safeDiagnostic(command) << ": say which document\n";
        return 2;
    }
    const QString path = words.takeFirst();
    Codec::Result loaded = Codec::readFile(path, warningLimits());
    if (!loaded) {
        diagnostic(loaded.error);
        return 1;
    }
    for (const QString &warning : loaded.warnings)
        diagnostic(QStringLiteral("warning: %1").arg(warning));
    Document doc = loaded.document;
    QString error;

    // ------------------------------------------------ commands over two files

    if (command == QLatin1String("render")) {
        if (!parser.isSet(QStringLiteral("out"))) {
            err() << "render: say where to write, with -o\n";
            return 2;
        }
        if (doc.clips().isEmpty()) {
            diagnostic(QStringLiteral("the document has no clips"));
            return 1;
        }
        const QString clipName = parser.value(QStringLiteral("clip")).isEmpty()
                                     ? doc.clips().first().name
                                     : parser.value(QStringLiteral("clip"));
        const bool frameProvided = parser.isSet(QStringLiteral("frame"));
        const QString frameText = parser.value(QStringLiteral("frame"));
        bool frameOk = true;
        const int frame = !frameProvided ? 0 : frameText.toInt(&frameOk);
        const Clip *selectedClip = doc.clip(clipName);
        if (!selectedClip) {
            diagnostic(QStringLiteral("E_CLIP_NOT_FOUND: --clip=%1").arg(clipName));
            return 1;
        }
        if (!frameOk || frame < 0 || frame >= selectedClip->frameCount) {
            diagnostic(QStringLiteral("E_FRAME_OUT_OF_RANGE: --frame=%1").arg(frameText));
            return 2;
        }
        const QString requestedFormat = parser.value(QStringLiteral("format")).toLower();
        QString format = requestedFormat;
        if (format.isEmpty())
            format = QFileInfo(parser.value(QStringLiteral("out"))).suffix().toLower()
                         == QLatin1String("gif") ? QStringLiteral("gif")
                                                 : QStringLiteral("png");
        if (format != QLatin1String("png") && format != QLatin1String("gif")) {
            diagnostic(QStringLiteral("render: --format must be png or gif"));
            return 2;
        }
        QString outputError;
        if (!output::validate(parser.value(QStringLiteral("out")), {path},
                              &outputError)) {
            diagnostic(QStringLiteral("render: %1").arg(outputError));
            return 2;
        }
        render::Options options;
        bool scaleOk = true;
        options.scale = parser.isSet(QStringLiteral("scale"))
                            ? parser.value(QStringLiteral("scale")).toInt(&scaleOk)
                            : 1;
        if (!scaleOk || options.scale < 1 || options.scale > 64) {
            diagnostic(QStringLiteral("E_RENDER_SCALE: --scale must be an integer between 1 and 64"));
            return 2;
        }
        if (format == QLatin1String("gif")) {
            if (frameProvided || parser.isSet(QStringLiteral("sheet"))
                || parser.isSet(QStringLiteral("checker"))
                || parser.isSet(QStringLiteral("isolated"))
                || parser.isSet(QStringLiteral("layer-id"))
                || parser.isSet(QStringLiteral("layer"))) {
                diagnostic(QStringLiteral(
                    "render: GIF exports the complete composed clip; --frame, --sheet, "
                    "--checker, and isolated-layer options are not supported"));
                return 2;
            }
            if (parser.isSet(QStringLiteral("loop"))
                && parser.isSet(QStringLiteral("no-loop"))) {
                diagnostic(QStringLiteral("render: --loop and --no-loop conflict"));
                return 2;
            }
            int fps = 0;
            if (parser.isSet(QStringLiteral("fps"))) {
                bool fpsOk = false;
                fps = parser.value(QStringLiteral("fps")).toInt(&fpsOk);
                if (!fpsOk || fps < 1 || fps > 100) {
                    diagnostic(QStringLiteral(
                        "render: --fps must be an integer between 1 and 100"));
                    return 2;
                }
            }
            QString gifError;
            if (!gif::write(doc, clipName, parser.value(QStringLiteral("out")),
                            options.scale, fps,
                            !parser.isSet(QStringLiteral("no-loop")), {path},
                            &gifError)) {
                diagnostic(QStringLiteral("render: %1").arg(gifError));
                return 1;
            }
            out() << safeDiagnostic(parser.value(QStringLiteral("out"))) << ": "
                  << doc.columns() * options.scale << "x"
                  << doc.rows() * options.scale << ", "
                  << selectedClip->frameCount << " frame(s)\n";
            return 0;
        }
        if (parser.isSet(QStringLiteral("fps"))
            || parser.isSet(QStringLiteral("loop"))
            || parser.isSet(QStringLiteral("no-loop"))) {
            diagnostic(QStringLiteral("render: --fps and loop options require GIF output"));
            return 2;
        }
        options.checker = parser.isSet(QStringLiteral("checker"));
        options.sheet = parser.isSet(QStringLiteral("sheet"));
        options.isolated = parser.isSet(QStringLiteral("isolated"));
        QString layerError;
        if (!options.isolated
            && (parser.isSet(QStringLiteral("layer-id"))
                || parser.isSet(QStringLiteral("layer")))) {
            diagnostic(QStringLiteral("--layer-id/--layer require --isolated"));
            return 2;
        }
        if (options.isolated
            && !resolveRenderLayer(doc, parser, &options.layer, &layerError)) {
            diagnostic(layerError);
            return layerError.startsWith(QStringLiteral("E_LAYER_TARGET_REQUIRED")) ? 2 : 1;
        }
        options.warningPixels = renderWarningPixels();
        QString warning;
        QString renderError;
        QStringList diagnostics;
        const QImage image =
            render::toImage(doc, clipName, frame, options, &warning, &renderError,
                            &diagnostics);
        if (!warning.isEmpty())
            diagnostic(QStringLiteral("warning: %1").arg(warning));
        if (image.isNull()) {
            diagnostic(QStringLiteral("render: %1").arg(renderError));
            return 1;
        }
        QBuffer encoded;
        encoded.open(QIODevice::WriteOnly);
        if (!image.save(&encoded, "PNG")) {
            diagnostic(QStringLiteral("render: could not encode PNG"));
            return 1;
        }
        if (!output::writeAtomically(parser.value(QStringLiteral("out")),
                                     encoded.data(), {path}, &outputError)) {
            diagnostic(QStringLiteral("render: %1").arg(outputError));
            return 1;
        }
        out() << safeDiagnostic(parser.value(QStringLiteral("out"))) << ": "
              << image.width() << "x"
              << image.height() << "\n";
        return 0;
    }

    if (command == QLatin1String("flatten"))
        return flattenTo(doc, path, parser);

    if (command == QLatin1String("export")) {
        if (words.isEmpty()) {
            err() << "export: say which catalog to write into\n";
            return 2;
        }
        if (!parser.isSet(QStringLiteral("name"))
            || parser.value(QStringLiteral("name")).isEmpty()) {
            err() << "export: say --name which set to export\n";
            return 2;
        }
        const QStringList problems = doc.problems();
        if (!problems.isEmpty()) {
            for (const QString &line : problems)
            err() << "error: " << safeDiagnostic(line) << "\n";
            err() << "did not export: fix the document first\n";
            return 1;
        }
        const QString into = words.first();
        QString outputError;
        if (!output::validate(into, {}, &outputError)) {
            err() << "export: " << safeDiagnostic(outputError) << "\n";
            return 2;
        }
        QByteArray catalogBytes;
        if (!input::readRegularFile(into, Document::maxDocumentBytes, &catalogBytes,
                                    &outputError)) {
            diagnostic(outputError);
            return 1;
        }
        if (catalogBytes.size() > Document::maxDocumentBytes) {
            diagnostic(QStringLiteral("%1: catalog exceeds the hard input limit").arg(into));
            return 1;
        }
        QString scanError;
        if (Codec::rejectDuplicateJsonKeys(catalogBytes, &scanError)) {
            diagnostic(QStringLiteral("%1: %2").arg(into, scanError));
            return 1;
        }
        QJsonParseError parse;
        const QJsonDocument parsed = QJsonDocument::fromJson(catalogBytes, &parse);
        if (parse.error != QJsonParseError::NoError) {
            diagnostic(QStringLiteral("%1: invalid JSON at offset %2: %3")
                           .arg(into).arg(parse.offset).arg(parse.errorString()));
            return 1;
        }
        if (!parsed.isObject()) {
            diagnostic(QStringLiteral("%1: the catalog has to be a JSON object").arg(into));
            return 1;
        }
        const QJsonObject catalog = parsed.object();
        const QString species = parser.value(QStringLiteral("name"));
        const QString variant = parser.isSet(QStringLiteral("index"))
                                    ? parser.value(QStringLiteral("index"))
                                    : QStringLiteral("0");
        const Bridge::Result pushed = Bridge::exportInto(catalog, doc, species, variant);
        if (!pushed) {
            diagnostic(pushed.error);
            return 1;
        }
        for (const QString &name : pushed.skipped) {
            diagnostic(QStringLiteral("warning: the catalog does not know the sequence %1 — skipped")
                           .arg(name));
        }
        const QByteArray serialized =
            QJsonDocument(pushed.catalog).toJson(QJsonDocument::Compact);
        if (serialized.size() > Document::maxDocumentBytes) {
            diagnostic(QStringLiteral("export: serialized catalog exceeds the hard limit of %1 MiB")
                           .arg(Document::maxDocumentBytes / (1024 * 1024)));
            return 1;
        }
        if (!output::writeAtomically(into, serialized, {}, &error)) {
            diagnostic(error);
            return 1;
        }
        out() << safeDiagnostic(into) << ": " << safeDiagnostic(species) << "/"
              << safeDiagnostic(variant) << " — "
              << pushed.exported << " sequence(s)\n";
        return 0;
    }

    if (command == QLatin1String("diff")) {
        if (words.isEmpty()) {
            err() << "diff: say which document to compare against\n";
            return 2;
        }
        Codec::Result other = Codec::readFile(words.first(), warningLimits());
        if (!other) {
            err() << safeDiagnostic(other.error) << "\n";
            return 1;
        }
        for (const QString &warning : other.warnings)
            err() << "warning: " << safeDiagnostic(warning) << "\n";
        const QStringList differences =
            documentDifferences(doc, other.document, path, words.first());
        for (const QString &difference : differences)
            out() << safeDiagnostic(difference) << "\n";
        out() << differences.size() << " difference(s)\n";
        return differences.isEmpty() ? 0 : 1;
    }

    if (command == QLatin1String("batch"))
        return runBatch(doc, path, parser);

    // ------------------------------------------- everything else, over the doc

    const cli::Outcome outcome = cli::applyCommand(doc, command, words, parser);
    const bool machineOutput = command == QLatin1String("info")
        || ((command == QLatin1String("palette") || command == QLatin1String("layer"))
            && (words.isEmpty() || words.first() == QLatin1String("list")));
    if (!outcome.output.isEmpty())
        out() << (command == QLatin1String("show") || machineOutput
                      ? outcome.output
                      : safeDiagnostic(outcome.output));
    if (!outcome.error.isEmpty())
        err() << safeDiagnostic(outcome.error) << "\n";
    if (outcome.changed && !Codec::writeFile(path, doc, &error)) {
        err() << safeDiagnostic(error) << "\n";
        return 1;
    }
    return outcome.code;
}
