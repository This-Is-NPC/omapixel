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
#include "Document.h"
#include "Ops.h"
#include "Strings.h"
#include "Render.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QSaveFile>
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
        *lines = in.readAll().split(QLatin1Char('\n'));
        return true;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = path + QStringLiteral(": ") + file.errorString();
        return false;
    }
    *lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
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
        err() << error << "\n";
        return 1;
    }

    int applied = 0;
    int changes = 0;
    for (int number = 1; number <= lines.size(); ++number) {
        const QString line = lines.at(number - 1).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        QStringList words = QProcess::splitCommand(line);
        if (words.isEmpty())
            continue;
        const QString command = words.takeFirst();

        if (!cli::isDocumentCommand(command)) {
            err() << "line " << number << ": " << command
                  << " cannot run in a batch — a batch works on the one document "
                     "it opened\n";
            return 2;
        }

        // A fresh parser per line: the flags belong to that line, and reusing
        // the outer one would let a --slot from line 3 leak into line 4.
        QCommandLineParser lineParser;
        cli::addOptions(lineParser);
        if (!lineParser.parse(QStringList{QStringLiteral("omapixel")} + words)) {
            err() << "line " << number << ": " << lineParser.errorText() << "\n";
            return 2;
        }

        const cli::Outcome outcome = cli::applyCommand(
            doc, command, lineParser.positionalArguments(), lineParser);
        if (!outcome.output.isEmpty())
            out() << outcome.output;
        if (outcome.code != 0) {
            err() << "line " << number << ": " << outcome.error << "\n";
            err() << "nothing was saved\n";
            return outcome.code;
        }
        applied += 1;
        if (outcome.changed)
            changes += 1;
    }

    if (changes > 0 && !Codec::writeFile(path, doc, &error)) {
        err() << error << "\n";
        return 1;
    }
    out() << path << ": " << applied << " command(s), " << changes << " change(s)\n";
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
    const auto read = [](const QString &path, QJsonObject *into) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        *into = QJsonDocument::fromJson(file.readAll()).object();
        return true;
    };

    QJsonObject english;
    if (!read(home + QStringLiteral("/en.json"), &english)) {
        err() << home << "/en.json: not there\n";
        return 1;
    }

    if (wanted.isEmpty()) {
        out() << "catalogues in " << home << ":\n";
        const QFileInfoList files =
            QDir(home).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo &file : files) {
            QJsonObject one;
            if (!read(file.absoluteFilePath(), &one))
                continue;
            out() << QStringLiteral("  %1 %2 strings")
                         .arg(file.completeBaseName(), -8)
                         .arg(one.size());
            if (file.completeBaseName() != QLatin1String("en"))
                out() << "  (" << one.size() * 100 / english.size() << "% of English)";
            out() << "\n";
        }
        out() << "\nto start one:  cp i18n/en.json i18n/pt.json"
              << "  &&  omapixel i18n pt\n";
        return 0;
    }

    QJsonObject them;
    if (!read(home + QLatin1Char('/') + wanted + QStringLiteral(".json"), &them)) {
        err() << "no i18n/" << wanted << ".json — copy i18n/en.json to start\n";
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

    out() << wanted << ": " << (english.size() - missing.size()) << " of "
          << english.size() << " translated\n";
    if (!missing.isEmpty()) {
        out() << "\n  " << missing.size() << " still to do:\n";
        for (int i = 0; i < missing.size() && i < 40; ++i) {
            out() << QStringLiteral("    %1 %2\n")
                         .arg(missing.at(i), -38)
                         .arg(english.value(missing.at(i)).toString());
        }
        if (missing.size() > 40)
            out() << "    ... and " << missing.size() - 40 << " more\n";
    }
    if (!stale.isEmpty()) {
        out() << "\n  " << stale.size()
              << " the program no longer asks for, safe to delete:\n";
        for (const QString &key : stale)
            out() << "    " << key << "\n";
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
            out() << path << ": not there — omapixel runs on its defaults\n";
            return 0;
        }
        if (problems.isEmpty()) {
            out() << path << ": good\n";
            return 0;
        }
        err() << path << ":\n";
        for (const QString &problem : problems)
            err() << "  " << problem << "\n";
        return 1;
    }

    if (what == QLatin1String("write")) {
        const QString text = Config::defaultText();
        if (text.isEmpty()) {
            err() << "the default config is not installed — looked in:\n";
            for (const QString &place : Config::defaultSearchPath())
                err() << "  " << place << "\n";
            return 1;
        }
        if (QFile::exists(path)) {
            err() << path << " is already there — delete it first, or edit it\n";
            return 1;
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            err() << "could not write " << path << "\n";
            return 1;
        }
        file.write(text.toUtf8());
        out() << "wrote " << path << "\n";
        return 0;
    }

    if (!what.isEmpty()) {
        err() << "config: say `check`, `write`, or nothing at all\n";
        return 2;
    }

    out() << "file      " << path;
    out() << (QFile::exists(path) ? "\n" : "   (not there — running on the defaults)\n");
    const QString speaking = Strings::shared().language();
    const QString wanted = Strings::preferredLanguage();
    out() << "language  " << speaking;
    // Asked for one and speaking another means there is no catalogue for it.
    // Saying only the one it settled on reads as the setting being ignored.
    if (!wanted.startsWith(speaking))
        out() << "   (asked for " << wanted << ", no catalogue — `omapixel i18n`)";
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
            out() << line << "\n";
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
        out() << text;
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
        "  render   write a PNG\n"
        "  resize   change the frame, keeping the drawing centred\n"
        "  clip     add | rm | rename | fps\n"
        "  frame    add | dup | rm | move\n"
        "  paint    one pixel\n"
        "  line     from one point to another\n"
        "  rect     an outline or a filled block\n"
        "  fill     flood the contiguous run under a point\n"
        "  edit     clear | shift | flip | swap\n"
        "  palette  list | set | rm\n"
        "  batch    many commands over one document, read once and written once\n"
        "  i18n     what a language catalogue is still missing\n"
        "  config   the settings file: check | write\n"
        "  diff     what differs between two documents\n"
        "  import   pull one sprite set out of a catalog\n"
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

    if (command == QLatin1String("config"))
        return inspectConfig(words.value(0));

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
                err() << "--size: " << text << " is not COLUMNSxROWS\n";
                return 2;
            }
        }
        const Document doc = Document::blank(columns, rows);
        QString error;
        if (!Codec::writeFile(words.first(), doc, &error)) {
            err() << error << "\n";
            return 1;
        }
        out() << words.first() << ": " << columns << "x" << rows << ", one clip\n";
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
        QFile file(words.first());
        if (!file.open(QIODevice::ReadOnly)) {
            err() << words.first() << ": " << file.errorString() << "\n";
            return 1;
        }
        const QJsonObject catalog = QJsonDocument::fromJson(file.readAll()).object();
        const QString species = parser.value(QStringLiteral("name"));
        const QString variant = parser.isSet(QStringLiteral("index"))
                                    ? parser.value(QStringLiteral("index"))
                                    : QStringLiteral("0");
        const Bridge::Result pulled = Bridge::importSpecies(catalog, species, variant);
        if (!pulled) {
            err() << pulled.error << "\n";
            return 1;
        }
        QString error;
        if (!Codec::writeFile(parser.value(QStringLiteral("out")), pulled.document,
                              &error)) {
            err() << error << "\n";
            return 1;
        }
        int frames = 0;
        for (const Clip &clip : pulled.document.clips())
            frames += clip.frames.size();
        out() << parser.value(QStringLiteral("out")) << ": "
              << pulled.document.clips().size() << " clip(s), " << frames << " frames\n";
        return 0;
    }

    // Everything below reads a document first.
    if (words.isEmpty()) {
        err() << command << ": say which document\n";
        return 2;
    }
    const QString path = words.takeFirst();
    Codec::Result loaded = Codec::readFile(path, warningLimits());
    if (!loaded) {
        err() << loaded.error << "\n";
        return 1;
    }
    for (const QString &warning : loaded.warnings)
        err() << "warning: " << warning << "\n";
    Document doc = loaded.document;
    QString error;

    // ------------------------------------------------ commands over two files

    if (command == QLatin1String("render")) {
        if (!parser.isSet(QStringLiteral("out"))) {
            err() << "render: say where to write, with -o\n";
            return 2;
        }
        if (doc.clips().isEmpty()) {
            err() << "the document has no clips\n";
            return 1;
        }
        const QString clipName = parser.value(QStringLiteral("clip")).isEmpty()
                                     ? doc.clips().first().name
                                     : parser.value(QStringLiteral("clip"));
        const QString frameText = parser.value(QStringLiteral("frame"));
        const int frame = frameText.isEmpty() ? 0 : frameText.toInt();
        render::Options options;
        options.scale = parser.isSet(QStringLiteral("scale"))
                            ? parser.value(QStringLiteral("scale")).toInt()
                            : 1;
        options.checker = parser.isSet(QStringLiteral("checker"));
        options.sheet = parser.isSet(QStringLiteral("sheet"));
        options.warningPixels = renderWarningPixels();
        QString warning;
        QString renderError;
        const QImage image =
            render::toImage(doc, clipName, frame, options, &warning, &renderError);
        if (!warning.isEmpty())
            err() << "warning: " << warning << "\n";
        if (image.isNull()) {
            err() << "render: " << renderError << "\n";
            return 1;
        }
        if (!image.save(parser.value(QStringLiteral("out")))) {
            err() << "render: could not write " << parser.value(QStringLiteral("out"))
                  << "\n";
            return 1;
        }
        out() << parser.value(QStringLiteral("out")) << ": " << image.width() << "x"
              << image.height() << "\n";
        return 0;
    }

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
                err() << "error: " << line << "\n";
            err() << "did not export: fix the document first\n";
            return 1;
        }
        const QString into = words.first();
        QFile file(into);
        if (!file.open(QIODevice::ReadOnly)) {
            err() << into << ": " << file.errorString() << "\n";
            return 1;
        }
        QJsonParseError parse;
        const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parse);
        file.close();
        if (parse.error != QJsonParseError::NoError) {
            err() << into << ": invalid JSON at offset " << parse.offset << ": "
                  << parse.errorString() << "\n";
            return 1;
        }
        if (!parsed.isObject()) {
            err() << into << ": the catalog has to be a JSON object\n";
            return 1;
        }
        const QJsonObject catalog = parsed.object();
        const QString species = parser.value(QStringLiteral("name"));
        const QString variant = parser.isSet(QStringLiteral("index"))
                                    ? parser.value(QStringLiteral("index"))
                                    : QStringLiteral("0");
        const Bridge::Result pushed = Bridge::exportInto(catalog, doc, species, variant);
        if (!pushed) {
            err() << pushed.error << "\n";
            return 1;
        }
        for (const QString &name : pushed.skipped) {
            err() << "warning: the catalog does not know the sequence " << name
                  << " — skipped\n";
        }
        QSaveFile writing(into);
        if (!writing.open(QIODevice::WriteOnly)) {
            err() << into << ": " << writing.errorString() << "\n";
            return 1;
        }
        const QByteArray serialized =
            QJsonDocument(pushed.catalog).toJson(QJsonDocument::Compact);
        if (writing.write(serialized) != serialized.size()) {
            err() << into << ": could not write the complete catalog: "
                  << writing.errorString() << "\n";
            writing.cancelWriting();
            return 1;
        }
        if (!writing.commit()) {
            err() << into << ": " << writing.errorString() << "\n";
            return 1;
        }
        out() << into << ": " << species << "/" << variant << " — "
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
            err() << other.error << "\n";
            return 1;
        }
        for (const QString &warning : other.warnings)
            err() << "warning: " << warning << "\n";
        const QStringList differences =
            cli::documentDifferences(doc, other.document, path, words.first());
        for (const QString &difference : differences)
            out() << difference << "\n";
        out() << differences.size() << " difference(s)\n";
        return differences.isEmpty() ? 0 : 1;
    }

    if (command == QLatin1String("batch"))
        return runBatch(doc, path, parser);

    // ------------------------------------------- everything else, over the doc

    const cli::Outcome outcome = cli::applyCommand(doc, command, words, parser);
    if (!outcome.output.isEmpty())
        out() << outcome.output;
    if (!outcome.error.isEmpty())
        err() << outcome.error << "\n";
    if (outcome.changed && !Codec::writeFile(path, doc, &error)) {
        err() << error << "\n";
        return 1;
    }
    return outcome.code;
}
