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
#include "Document.h"
#include "Ops.h"
#include "Render.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QTextStream>

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

} // namespace

// --------------------------------------------------------------- the commands

int main(int argc, char *argv[])
{
    // QGuiApplication and not QCoreApplication: `render` paints with QPainter,
    // which needs the GUI stack up. It never opens a window, and the offscreen
    // platform keeps it working with no display at all.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("omapixel"));
    QCoreApplication::setApplicationVersion(QStringLiteral("2.0"));

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
        "  diff     what differs between two documents\n"
        "  import   pull one sprite set out of a catalog\n"
        "  export   put the clips back into one\n"
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
    Codec::Result loaded = Codec::readFile(path);
    if (!loaded) {
        err() << loaded.error << "\n";
        return 1;
    }
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
        const QImage image = render::toImage(doc, clipName, frame, options);
        if (image.isNull() || !image.save(parser.value(QStringLiteral("out")))) {
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
        const QJsonObject catalog = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
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
        writing.write(QJsonDocument(pushed.catalog).toJson(QJsonDocument::Compact));
        if (!writing.commit()) {
            err() << into << ": " << writing.errorString() << "\n";
            return 1;
        }
        out() << into << ": " << species << "/" << variant << " — "
              << (doc.clips().size() - pushed.skipped.size()) << " sequence(s)\n";
        return 0;
    }

    if (command == QLatin1String("diff")) {
        if (words.isEmpty()) {
            err() << "diff: say which document to compare against\n";
            return 2;
        }
        Codec::Result other = Codec::readFile(words.first());
        if (!other) {
            err() << other.error << "\n";
            return 1;
        }
        int total = 0;
        for (const Clip &clip : doc.clips()) {
            const Clip *theirs = other.document.clip(clip.name);
            if (!theirs) {
                out() << "only in " << path << ": clip " << clip.name << "\n";
                continue;
            }
            const int frames = qMax(clip.frames.size(), theirs->frames.size());
            for (int i = 0; i < frames; ++i) {
                const Grid mine = doc.frame(clip.name, i);
                const Grid yours = other.document.frame(clip.name, i);
                const auto differences = ops::diff(mine, yours);
                if (!differences.isEmpty()) {
                    out() << clip.name << "[" << i << "]: " << differences.size()
                          << " pixel(s)\n";
                    total += differences.size();
                }
            }
        }
        for (const Clip &clip : other.document.clips()) {
            if (!doc.clip(clip.name))
                out() << "only in " << words.first() << ": clip " << clip.name << "\n";
        }
        out() << total << " pixel(s) differ\n";
        return total == 0 ? 0 : 1;
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
